#ifndef TASKKIT_UTILITY_H
#define TASKKIT_UTILITY_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <cstdlib>
#include <exception>
#include <new>
#include <optional>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include "AwaitTransformer.h"
#include "Exceptions.h"
#include "PromiseContext.h"
#include "Task.h"
#include "TaskSchedulerId.h"
#include "TaskSchedulerManager.h"
#include "ThreadPool.h"

namespace TKit
{
	namespace Details
	{
		template<typename T>
		struct TaskTraits
		{
			static constexpr bool IsTask = false;
		};

		template<typename T>
		struct TaskTraits<Task<T>>
		{
			using ResultType = T;
			static constexpr bool IsTask = true;
		};

		template<typename T>
		using TaskFuncTraits = TaskTraits<std::invoke_result_t<T>>;

		// Result type of RunOnThreadPool: the task's result when `Func` returns a
		// Task, the plain return type otherwise.
		template<typename Func, bool IsTaskFunc = TaskFuncTraits<Func>::IsTask>
		struct RunOnThreadPoolResult
		{
			using Type = typename TaskFuncTraits<Func>::ResultType;
		};

		template<typename Func>
		struct RunOnThreadPoolResult<Func, false>
		{
			using Type = std::invoke_result_t<Func&>;
		};

		template<typename T, typename... Results>
		constexpr bool FulfillsAllType = (std::is_same_v<T, Results> && ...);

		template<typename... Results>
		constexpr bool FulfillsAllVoid = FulfillsAllType<void, Results...>;

		template<typename... Results>
		constexpr bool HasAnyType = (sizeof...(Results) > 0);
	}

	inline void ThrowIfStopRequested(const std::stop_token& stopToken)
	{
		if (stopToken.stop_requested())
		{
			throw OperationStoppedError();
		}
	}

	struct SwitchToThreadPoolAwaiter
	{
		[[nodiscard]]
		bool await_ready() const noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) const
		{
			TaskSchedulerManager::RequireCurrentPromiseContext().GetThreadPool().Schedule(handle);
		}

		void await_resume() const noexcept
		{
		}
	};

	inline SwitchToThreadPoolAwaiter SwitchToThreadPool()
	{
		return SwitchToThreadPoolAwaiter{};
	}

	struct SwitchToSelectedSchedulerAwaiter
	{
		TaskSchedulerId schedulerId;

		[[nodiscard]]
		bool await_ready() const noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) const
		{
			TaskScheduler* scheduler = schedulerId.GetScheduler();
			assert(scheduler && "SwitchToSelectedScheduler: invalid scheduler id");
			if (scheduler == nullptr)
			{
				std::abort();
			}
			scheduler->Schedule(handle);
		}

		void await_resume() const noexcept
		{
		}
	};

	inline SwitchToSelectedSchedulerAwaiter SwitchToSelectedScheduler(TaskSchedulerId schedulerId)
	{
		return SwitchToSelectedSchedulerAwaiter{schedulerId};
	}

	// Runs `func` on the thread pool and always returns to the scheduler that was
	// active at the call, even when `func` throws (the exception is rethrown on
	// the original scheduler). `func` may return a value, void, or a Task of
	// either; it is taken by value because the caller's temporaries may die
	// while the pool still runs.
	template<typename Func>
		requires std::is_invocable_v<Func&>
	inline Task<typename Details::RunOnThreadPoolResult<Func>::Type> RunOnThreadPool(Func func)
	{
		using Result = typename Details::RunOnThreadPoolResult<Func>::Type;
		constexpr bool ReturnsTask = Details::TaskFuncTraits<Func>::IsTask;

		const TaskSchedulerId originalSchedulerId{&TaskSchedulerManager::RequireActiveScheduler()};

		co_await SwitchToThreadPool();

		std::exception_ptr exception;
		if constexpr (std::is_void_v<Result>)
		{
			try
			{
				if constexpr (ReturnsTask)
				{
					co_await func();
				}
				else
				{
					func();
				}
			}
			catch (...)
			{
				exception = std::current_exception();
			}

			co_await SwitchToSelectedScheduler(originalSchedulerId);
			if (exception)
			{
				std::rethrow_exception(exception);
			}
			co_return;
		}
		else
		{
			std::optional<Result> result;
			try
			{
				if constexpr (ReturnsTask)
				{
					result.emplace(co_await func());
				}
				else
				{
					result.emplace(func());
				}
			}
			catch (...)
			{
				exception = std::current_exception();
			}

			co_await SwitchToSelectedScheduler(originalSchedulerId);
			if (exception)
			{
				std::rethrow_exception(exception);
			}
			co_return std::move(*result);
		}
	}

	inline Task<> GetCompletedTask()
	{
		co_return;
	}

	// `func` is anything invocable as Task<>(std::stop_token); taking it as a
	// template (by value) avoids the std::function type-erasure allocation.
	template<typename Func>
		requires std::is_invocable_r_v<Task<>, Func&, std::stop_token>
	inline Task<> CreateTask(Func func, std::stop_token stopToken = {})
	{
		ThrowIfStopRequested(stopToken);
		co_await func(stopToken);
	}

	template<typename Func>
		requires std::is_invocable_r_v<Task<>, Func&, std::stop_token>
	inline void RunTask(Func&& func, std::stop_token stopToken = {})
	{
		ThrowIfStopRequested(stopToken);
		func(stopToken).Forget();
	}

	// Allocation-free frame wait: parks the awaiting coroutine directly on the
	// current scheduler's frame-wait list — no child coroutine, no frame.
	// A stop request wakes it on the next Update and rethrows from await_resume.
	struct FrameWaitAwaiter
	{
		int remainingFrames = 0;
		std::stop_token stopToken;

		[[nodiscard]]
		bool await_ready() const
		{
			ThrowIfStopRequested(stopToken);
			return remainingFrames <= 0;
		}

		void await_suspend(std::coroutine_handle<> handle) const
		{
			TaskSchedulerManager::RequireActiveScheduler().ScheduleFrameWait(handle, remainingFrames, stopToken);
		}

		void await_resume() const
		{
			ThrowIfStopRequested(stopToken);
		}
	};

	// Allocation-free time wait; the due time is checked once per Update of the
	// scheduler the coroutine suspended on.
	struct TimeWaitAwaiter
	{
		std::chrono::steady_clock::time_point due{};
		std::stop_token stopToken;

		[[nodiscard]]
		bool await_ready() const
		{
			ThrowIfStopRequested(stopToken);
			return std::chrono::steady_clock::now() >= due;
		}

		void await_suspend(std::coroutine_handle<> handle) const
		{
			TaskSchedulerManager::RequireActiveScheduler().ScheduleTimeWait(handle, due, stopToken);
		}

		void await_resume() const
		{
			ThrowIfStopRequested(stopToken);
		}
	};

	[[nodiscard]]
	inline FrameWaitAwaiter DelayFrame(int frameCount, std::stop_token stopToken = {})
	{
		return FrameWaitAwaiter{frameCount, std::move(stopToken)};
	}

	template<typename Rep, typename Period>
	[[nodiscard]]
	inline TimeWaitAwaiter WaitFor(std::chrono::duration<Rep, Period> duration, std::stop_token stopToken = {})
	{
		const auto due = std::chrono::steady_clock::now()
			+ std::chrono::ceil<std::chrono::steady_clock::duration>(duration);
		return TimeWaitAwaiter{due, std::move(stopToken)};
	}

	template<typename Clock, typename Duration>
	[[nodiscard]]
	inline TimeWaitAwaiter WaitUntil(std::chrono::time_point<Clock, Duration> timePoint, std::stop_token stopToken = {})
	{
		if constexpr (std::is_same_v<Clock, std::chrono::steady_clock>)
		{
			return TimeWaitAwaiter{
				std::chrono::ceil<std::chrono::steady_clock::duration>(timePoint),
				std::move(stopToken)
			};
		}
		else
		{
			// Non-steady clocks are converted once at the call; later clock
			// adjustments do not shift the wait.
			const auto remaining = timePoint - Clock::now();
			const auto due = std::chrono::steady_clock::now()
				+ std::chrono::ceil<std::chrono::steady_clock::duration>(remaining);
			return TimeWaitAwaiter{due, std::move(stopToken)};
		}
	}

	// Task<> wrappers around the allocation-free wait awaiters, for when a wait
	// has to live as a task — stored in a variable, put into a
	// std::vector<Task<>> for WhenAll, raced in WhenAny, and so on. Each wrapper
	// costs the usual one pooled frame allocation.
	[[nodiscard]]
	inline Task<> DelayFrameTask(int frameCount, std::stop_token stopToken = {})
	{
		co_await DelayFrame(frameCount, std::move(stopToken));
	}

	template<typename Rep, typename Period>
	[[nodiscard]]
	inline Task<> WaitForTask(std::chrono::duration<Rep, Period> duration, std::stop_token stopToken = {})
	{
		co_await WaitFor(duration, std::move(stopToken));
	}

	template<typename Clock, typename Duration>
	[[nodiscard]]
	inline Task<> WaitUntilTask(std::chrono::time_point<Clock, Duration> timePoint, std::stop_token stopToken = {})
	{
		co_await WaitUntil(timePoint, std::move(stopToken));
	}

	template<typename... Results>
	using WhenAllResultType = std::tuple<std::conditional_t<std::is_void_v<Results>, std::monostate, Results>...>;

	namespace Details
	{
		// Awaits one WhenAll element without letting its exception escape: the
		// first failure is parked in `firstException` so the remaining tasks are
		// still awaited (nothing keeps running unobserved), and rethrown once
		// every task finished. Void results surface as monostate; a failed
		// element yields nullopt.
		template<typename Result>
		struct WhenAllElementAwaiter
		{
			using Element = std::conditional_t<std::is_void_v<Result>, std::monostate, Result>;

			typename Task<Result>::Awaiter inner;
			std::exception_ptr* firstException;

			[[nodiscard]]
			bool await_ready() const noexcept
			{
				return inner.await_ready();
			}

			bool await_suspend(std::coroutine_handle<> handle) noexcept
			{
				return inner.await_suspend(handle);
			}

			std::optional<Element> await_resume() noexcept
			{
				try
				{
					if constexpr (std::is_void_v<Result>)
					{
						inner.await_resume();
						return std::monostate{};
					}
					else
					{
						return inner.await_resume();
					}
				}
				catch (...)
				{
					if (!*firstException)
					{
						*firstException = std::current_exception();
					}
					return std::nullopt;
				}
			}
		};

		template<typename Result>
		[[nodiscard]]
		inline WhenAllElementAwaiter<Result> AwaitWhenAllElement(Task<Result>&& task, std::exception_ptr& firstException)
		{
			return WhenAllElementAwaiter<Result>{typename Task<Result>::Awaiter{std::move(task)}, &firstException};
		}
	}

	// Tasks are taken by value: they are eager, so they all already run; the
	// coroutine owns them for its whole lifetime (no dangling if the returned
	// task is stored and awaited later). If any task fails, the rest are still
	// awaited and the first exception is rethrown once all of them finished.
	template<typename... Results>
		requires Details::HasAnyType<Results...> && (!Details::FulfillsAllVoid<Results...>)
	inline Task<WhenAllResultType<Results...>> WhenAll(Task<Results>... tasks)
	{
		std::exception_ptr firstException;

		// Braced initialization guarantees left-to-right evaluation of the awaits.
		auto results = std::tuple{co_await Details::AwaitWhenAllElement(std::move(tasks), firstException)...};

		if (firstException)
		{
			std::rethrow_exception(firstException);
		}

		co_return std::apply([](auto&&... values)
		{
			return WhenAllResultType<Results...>{std::move(*values)...};
		}, std::move(results));
	}

	template<typename... Results>
		requires Details::HasAnyType<Results...> && Details::FulfillsAllVoid<Results...>
	inline Task<> WhenAll(Task<Results>... tasks)
	{
		std::exception_ptr firstException;

		(static_cast<void>(co_await Details::AwaitWhenAllElement(std::move(tasks), firstException)), ...);

		if (firstException)
		{
			std::rethrow_exception(firstException);
		}
		co_return;
	}

	inline Task<> WhenAll(std::vector<Task<>> tasks)
	{
		std::exception_ptr firstException;

		for (auto& task : tasks)
		{
			static_cast<void>(co_await Details::AwaitWhenAllElement(std::move(task), firstException));
		}

		if (firstException)
		{
			std::rethrow_exception(firstException);
		}
		co_return;
	}

	template<typename... Results>
	using WhenAnyResultType = std::variant<std::conditional_t<std::is_void_v<Results>, std::monostate, Results>...>;

	namespace Details
	{
		// One-shot completion channel for WhenAny. `winnerClaimed` arbitrates
		// which competitor writes the result; `waiter` hands the parent's
		// continuation over with the same atomic protocol PromiseStateCore uses,
		// so completions from other threads cannot lose the wakeup.
		//
		// Allocated through the TaskAllocator (pooled by default) with an
		// intrusive reference count instead of shared_ptr, so a WhenAny call
		// performs no heap allocation. Like a coroutine frame's header, the
		// deallocate function is captured at creation.
		template<typename ResultVariant>
		struct WhenAnyState
		{
			std::atomic<int> refCount{1};
			std::atomic<bool> winnerClaimed{false};
			std::atomic<void*> waiter{nullptr}; // nullptr -> handle -> SignaledTag
			TaskScheduler* resumeScheduler = nullptr;
			std::optional<ResultVariant> result;
			std::exception_ptr exception;
			TaskAllocator::DeallocateFunc deallocate = nullptr;
			void* deallocateContext = nullptr;

			// Returns a state with one reference owned by the caller.
			[[nodiscard]]
			static WhenAnyState* Create()
			{
				static_assert(alignof(WhenAnyState) <= alignof(std::max_align_t));

				const TaskAllocator& allocator = TaskSchedulerManager::RequireCurrentPromiseContext().GetAllocator();
				void* memory = allocator.Allocate(sizeof(WhenAnyState));
				auto* state = new (memory) WhenAnyState();
				state->deallocate = allocator.GetDeallocateFunc();
				state->deallocateContext = allocator.GetContext();
				return state;
			}

			void AddRef() noexcept
			{
				refCount.fetch_add(1, std::memory_order_relaxed);
			}

			void Release() noexcept
			{
				if (refCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
				{
					const auto deallocateFunc = deallocate;
					void* context = deallocateContext;
					this->~WhenAnyState();
					deallocateFunc(context, this, sizeof(WhenAnyState));
				}
			}

			bool TryClaim() noexcept
			{
				return !winnerClaimed.exchange(true, std::memory_order_acq_rel);
			}

			void Signal()
			{
				void* previous = waiter.exchange(SignaledTag(), std::memory_order_acq_rel);
				if (previous == nullptr || previous == SignaledTag())
				{
					return;
				}

				const auto handle = std::coroutine_handle<>::from_address(previous);

				// Resume inline when the parent lives on the scheduler currently
				// running on this very thread (preserves same-frame completion);
				// otherwise hand it to its own scheduler thread-safely.
				if (resumeScheduler != nullptr && TaskSchedulerManager::CurrentActiveScheduler() != resumeScheduler)
				{
					resumeScheduler->Schedule(handle);
				}
				else
				{
					handle.resume();
				}
			}

			[[nodiscard]]
			bool IsSignaled() const noexcept
			{
				return waiter.load(std::memory_order_acquire) == SignaledTag();
			}

			static void* SignaledTag() noexcept
			{
				static constinit char tag = 0;
				return &tag;
			}
		};

		// Minimal intrusive smart pointer for WhenAnyState. The constructor
		// adopts the caller's reference; copies add one.
		template<typename ResultVariant>
		class WhenAnyStateRef
		{
		public:
			explicit WhenAnyStateRef(WhenAnyState<ResultVariant>* state) noexcept :
				state_(state)
			{
			}

			WhenAnyStateRef(const WhenAnyStateRef& other) noexcept :
				state_(other.state_)
			{
				state_->AddRef();
			}

			WhenAnyStateRef(WhenAnyStateRef&& other) noexcept :
				state_(std::exchange(other.state_, nullptr))
			{
			}

			WhenAnyStateRef& operator=(const WhenAnyStateRef&) = delete;
			WhenAnyStateRef& operator=(WhenAnyStateRef&&) = delete;

			~WhenAnyStateRef()
			{
				if (state_)
				{
					state_->Release();
				}
			}

			WhenAnyState<ResultVariant>* operator->() const noexcept
			{
				return state_;
			}

			[[nodiscard]]
			WhenAnyState<ResultVariant>* Get() const noexcept
			{
				return state_;
			}

		private:
			WhenAnyState<ResultVariant>* state_;
		};

		template<typename ResultVariant>
		struct WhenAnySignalAwaiter
		{
			WhenAnyState<ResultVariant>* state;

			[[nodiscard]]
			bool await_ready() const noexcept
			{
				return state->IsSignaled();
			}

			bool await_suspend(std::coroutine_handle<> handle) noexcept
			{
				state->resumeScheduler = TaskSchedulerManager::CurrentActiveScheduler();

				void* expected = nullptr;
				return state->waiter.compare_exchange_strong(
					expected, handle.address(),
					std::memory_order_release,
					std::memory_order_acquire);
			}

			void await_resume() const noexcept
			{
			}
		};

		template<std::size_t I, typename ResultVariant, typename TaskResult>
		inline Task<> WhenAnyCompetitor(WhenAnyStateRef<ResultVariant> state, Task<TaskResult> task)
		{
			try
			{
				if constexpr (std::is_void_v<TaskResult>)
				{
					co_await std::move(task);
					if (state->TryClaim())
					{
						state->result.emplace(std::in_place_index<I>);
						state->Signal();
					}
				}
				else
				{
					auto value = co_await std::move(task);
					if (state->TryClaim())
					{
						// Once claimed, Signal() must always follow — a throwing
						// result move would otherwise leave the parent suspended
						// forever. Publish the exception instead.
						try
						{
							state->result.emplace(std::in_place_index<I>, std::move(value));
						}
						catch (...)
						{
							state->exception = std::current_exception();
						}
						state->Signal();
					}
				}
			}
			catch (...)
			{
				if (state->TryClaim())
				{
					state->exception = std::current_exception();
					state->Signal();
				}
			}
		}

		template<std::size_t I, typename ResultVariant, typename First, typename... Others>
		inline void StartWhenAnyCompetitors(const WhenAnyStateRef<ResultVariant>& state, Task<First> first, Task<Others>... others)
		{
			WhenAnyCompetitor<I, ResultVariant>(state, std::move(first)).Forget();

			if constexpr (sizeof...(others) > 0)
			{
				StartWhenAnyCompetitors<I + 1>(state, std::move(others)...);
			}
		}
	}

	// Completes when the first task completes. The remaining tasks keep running
	// to completion in the background (no cancellation is imposed on them). If
	// the first task to finish failed, its exception is rethrown here.
	template<typename... Results>
		requires Details::HasAnyType<Results...> && (!Details::FulfillsAllVoid<Results...>)
	inline Task<WhenAnyResultType<Results...>> WhenAny(Task<Results>... tasks)
	{
		using Variant = WhenAnyResultType<Results...>;

		const Details::WhenAnyStateRef<Variant> state{Details::WhenAnyState<Variant>::Create()};
		Details::StartWhenAnyCompetitors<0>(state, std::move(tasks)...);

		co_await Details::WhenAnySignalAwaiter<Variant>{state.Get()};

		if (state->exception)
		{
			std::rethrow_exception(state->exception);
		}
		co_return std::move(*state->result);
	}

	template<typename... Results>
		requires Details::HasAnyType<Results...> && Details::FulfillsAllVoid<Results...>
	inline Task<std::size_t> WhenAny(Task<Results>... tasks)
	{
		using Variant = WhenAnyResultType<Results...>;

		const Details::WhenAnyStateRef<Variant> state{Details::WhenAnyState<Variant>::Create()};
		Details::StartWhenAnyCompetitors<0>(state, std::move(tasks)...);

		co_await Details::WhenAnySignalAwaiter<Variant>{state.Get()};

		if (state->exception)
		{
			std::rethrow_exception(state->exception);
		}
		co_return state->result->index();
	}

	template<typename Rep, typename Period>
	class AwaitTransformer<std::chrono::duration<Rep, Period>>
	{
	public:
		static TimeWaitAwaiter Transform(const std::chrono::duration<Rep, Period>& duration) noexcept
		{
			return WaitFor(duration);
		}
	};
}

#endif //TASKKIT_UTILITY_H
