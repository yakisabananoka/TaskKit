#ifndef TASKKIT_UTILITY_H
#define TASKKIT_UTILITY_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
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

		template<typename T, typename... Results>
		constexpr bool FulfillsAllType = (std::is_same_v<T, Results> && ...);

		template<typename... Results>
		constexpr bool FulfillsAllVoid = FulfillsAllType<void, Results...>;

		template<typename... Results>
		constexpr bool FulfillsSameType = (std::is_same_v<Results, std::tuple_element_t<0, std::tuple<Results...>>> && ...);

		template<typename... Results>
		constexpr bool HasAnyType = (sizeof...(Results) > 0);

		template<typename Result>
		inline auto ToMonostateIfVoid(Task<Result>&& task)
		{
			if constexpr (std::is_void_v<Result>)
			{
				return std::move(task).ToMonostateTask();
			}
			else
			{
				return std::move(task);
			}
		}
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
			PromiseContext::GetCurrent().GetThreadPool().Schedule(handle);
		}

		void await_resume() const noexcept
		{
		}
	};

	inline SwitchToThreadPoolAwaiter SwitchToThreadPool()
	{
		return SwitchToThreadPoolAwaiter{};
	}

	template<>
	class AwaitTransformer<SwitchToThreadPoolAwaiter>
	{
	public:
		static SwitchToThreadPoolAwaiter Transform(SwitchToThreadPoolAwaiter awaiter) noexcept
		{
			return awaiter;
		}
	};

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
			assert(schedulerId.IsValid() && "SwitchToSelectedScheduler: invalid scheduler id");
			schedulerId.GetScheduler()->Schedule(handle);
		}

		void await_resume() const noexcept
		{
		}
	};

	inline SwitchToSelectedSchedulerAwaiter SwitchToSelectedScheduler(TaskSchedulerId schedulerId)
	{
		return SwitchToSelectedSchedulerAwaiter{schedulerId};
	}

	template<>
	class AwaitTransformer<SwitchToSelectedSchedulerAwaiter>
	{
	public:
		static SwitchToSelectedSchedulerAwaiter Transform(SwitchToSelectedSchedulerAwaiter awaiter) noexcept
		{
			return awaiter;
		}
	};

	// Runs `func` on the thread pool and always returns to the scheduler that was
	// active at the call, even when `func` throws (the exception is rethrown on
	// the original scheduler). `func` is taken by value: the coroutine must own
	// it, since the caller's temporaries may die while the pool still runs.
	template<typename Func>
		requires std::is_invocable_v<Func> && (!Details::TaskFuncTraits<Func>::IsTask)
	inline Task<std::invoke_result_t<Func>> RunOnThreadPool(Func func)
	{
		using Result = std::invoke_result_t<Func>;

		TaskScheduler* origin = TaskSchedulerManager::CurrentActiveScheduler();
		assert(origin && "RunOnThreadPool: no scheduler is active on this thread");
		const TaskSchedulerId originalSchedulerId{origin};

		co_await SwitchToThreadPool();

		std::exception_ptr exception;
		if constexpr (std::is_void_v<Result>)
		{
			try
			{
				func();
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
				result.emplace(func());
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

	template<typename Func>
		requires std::is_invocable_v<Func> && (Details::TaskFuncTraits<Func>::IsTask)
	inline Task<typename Details::TaskFuncTraits<Func>::ResultType> RunOnThreadPool(Func func)
	{
		using Result = typename Details::TaskFuncTraits<Func>::ResultType;

		TaskScheduler* origin = TaskSchedulerManager::CurrentActiveScheduler();
		assert(origin && "RunOnThreadPool: no scheduler is active on this thread");
		const TaskSchedulerId originalSchedulerId{origin};

		co_await SwitchToThreadPool();

		std::exception_ptr exception;
		if constexpr (std::is_void_v<Result>)
		{
			try
			{
				co_await func();
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
				result.emplace(co_await func());
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

	inline Task<> CreateTask(std::function<Task<>(std::stop_token)> func, std::stop_token stopToken = {})
	{
		ThrowIfStopRequested(stopToken);
		co_await func(stopToken);
	}

	inline void RunTask(const std::function<Task<>(std::stop_token)>& func, std::stop_token stopToken = {})
	{
		ThrowIfStopRequested(stopToken);
		func(stopToken).Forget();
	}

	inline Task<> DelayFrame(int frameCount, std::stop_token stopToken = {})
	{
		while (frameCount-- > 0)
		{
			ThrowIfStopRequested(stopToken);
			co_yield {};
		}

		ThrowIfStopRequested(stopToken);
		co_return;
	}

	template<typename Rep, typename Period>
	inline Task<> WaitFor(std::chrono::duration<Rep, Period> duration, std::stop_token stopToken = {})
	{
		const auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < duration)
		{
			ThrowIfStopRequested(stopToken);
			co_yield {};
		}

		ThrowIfStopRequested(stopToken);
		co_return;
	}

	template<typename Clock, typename Duration>
	inline Task<> WaitUntil(std::chrono::time_point<Clock, Duration> timePoint, std::stop_token stopToken = {})
	{
		while (Clock::now() < timePoint)
		{
			ThrowIfStopRequested(stopToken);
			co_yield {};
		}

		ThrowIfStopRequested(stopToken);
		co_return;
	}

	template<typename... Results>
	using WhenAllResultType = std::tuple<std::conditional_t<std::is_void_v<Results>, std::monostate, Results>...>;

	// Tasks are taken by value: they are eager, so they all already run; the
	// coroutine owns them for its whole lifetime (no dangling if the returned
	// task is stored and awaited later).
	template<typename... Results>
		requires Details::HasAnyType<Results...> && (!Details::FulfillsAllVoid<Results...>)
	inline Task<WhenAllResultType<Results...>> WhenAll(Task<Results>... tasks)
	{
		// Braced initialization guarantees left-to-right evaluation of the awaits.
		co_return WhenAllResultType<Results...>{co_await Details::ToMonostateIfVoid(std::move(tasks))...};
	}

	template<typename... Results>
		requires Details::HasAnyType<Results...> && Details::FulfillsAllVoid<Results...>
	inline Task<> WhenAll(Task<Results>... tasks)
	{
		(co_await std::move(tasks), ...);
		co_return;
	}

	inline Task<> WhenAll(std::vector<Task<>> tasks)
	{
		for (auto& task : tasks)
		{
			co_await std::move(task);
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
		template<typename ResultVariant>
		struct WhenAnyState
		{
			std::atomic<bool> winnerClaimed{false};
			std::atomic<void*> waiter{nullptr}; // nullptr -> handle -> SignaledTag
			TaskScheduler* resumeScheduler = nullptr;
			std::optional<ResultVariant> result;
			std::exception_ptr exception;

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
		inline Task<> WhenAnyCompetitor(std::shared_ptr<WhenAnyState<ResultVariant>> state, Task<TaskResult> task)
		{
			try
			{
				auto value = co_await std::move(task);
				if (state->TryClaim())
				{
					state->result.emplace(std::in_place_index<I>, std::move(value));
					state->Signal();
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
		inline void StartWhenAnyCompetitors(const std::shared_ptr<WhenAnyState<ResultVariant>>& state, Task<First> first, Task<Others>... others)
		{
			WhenAnyCompetitor<I, ResultVariant>(state, std::move(first)).Forget();

			if constexpr (sizeof...(others) > 0)
			{
				StartWhenAnyCompetitors<I + 1>(state, std::move(others)...);
			}
		}
	}

	template<typename ResultVariant>
	class AwaitTransformer<Details::WhenAnySignalAwaiter<ResultVariant>>
	{
	public:
		static auto Transform(Details::WhenAnySignalAwaiter<ResultVariant> awaiter) noexcept
		{
			return awaiter;
		}
	};

	// Completes when the first task completes. The remaining tasks keep running
	// to completion in the background (no cancellation is imposed on them). If
	// the first task to finish failed, its exception is rethrown here.
	template<typename... Results>
		requires Details::HasAnyType<Results...> && (!Details::FulfillsAllVoid<Results...>)
	inline Task<WhenAnyResultType<Results...>> WhenAny(Task<Results>... tasks)
	{
		using Variant = WhenAnyResultType<Results...>;

		auto state = std::make_shared<Details::WhenAnyState<Variant>>();
		Details::StartWhenAnyCompetitors<0>(state, Details::ToMonostateIfVoid(std::move(tasks))...);

		co_await Details::WhenAnySignalAwaiter<Variant>{state.get()};

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

		auto state = std::make_shared<Details::WhenAnyState<Variant>>();
		Details::StartWhenAnyCompetitors<0>(state, Details::ToMonostateIfVoid(std::move(tasks))...);

		co_await Details::WhenAnySignalAwaiter<Variant>{state.get()};

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
		static auto Transform(const std::chrono::duration<Rep, Period>& duration) noexcept
		{
			return Task<>::Awaiter{WaitFor(duration)};
		}
	};
}

#endif //TASKKIT_UTILITY_H
