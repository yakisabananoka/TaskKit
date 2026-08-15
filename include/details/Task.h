#ifndef TASKKIT_TASK_H
#define TASKKIT_TASK_H

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <new>
#include <utility>
#include <variant>
#include "AwaitTransformer.h"
#include "PromiseBase.h"
#include "PromiseContext.h"
#include "TaskAllocator.h"
#include "TaskSchedulerManager.h"

namespace TKit
{
	namespace Details
	{
		// Every coroutine frame is prefixed with this header so the frame knows
		// how to free itself even after the ambient PromiseContext changed or was
		// cleared (e.g. frames destroyed during TaskSystem::Shutdown).
		struct FrameHeader
		{
			TaskAllocator::DeallocateFunc deallocate;
			void* context;
		};

		inline constexpr std::size_t FrameHeaderSize =
			(sizeof(FrameHeader) + alignof(std::max_align_t) - 1) & ~(alignof(std::max_align_t) - 1);

		[[nodiscard]]
		inline void* AllocateFrame(std::size_t size)
		{
			const TaskAllocator& allocator = PromiseContext::GetCurrent().GetAllocator();
			void* raw = allocator.Allocate(size + FrameHeaderSize);
			new (raw) FrameHeader{allocator.GetDeallocateFunc(), allocator.GetContext()};
			return static_cast<char*>(raw) + FrameHeaderSize;
		}

		inline void DeallocateFrame(void* ptr, std::size_t size) noexcept
		{
			void* raw = static_cast<char*>(ptr) - FrameHeaderSize;
			const FrameHeader header = *std::launder(static_cast<FrameHeader*>(raw));
			header.deallocate(header.context, raw, size + FrameHeaderSize);
		}
	}

	// An eagerly-started coroutine task.
	//
	// Ownership model: the frame has at most two interested parties — the task
	// object (or the awaiter that consumed it) and the running coroutine itself.
	// PromiseStateCore arbitrates atomically which of the two destroys the frame:
	// destroying/forgetting an unfinished task detaches it (it keeps running on
	// its scheduler and frees itself at completion), destroying a finished task
	// frees the frame immediately. Scheduler queues therefore never hold a handle
	// to a destroyed frame.
	template<typename = void>
	class [[nodiscard]] Task final
	{
	public:
		class Awaiter;
		class Promise;
		using Handle = std::coroutine_handle<Promise>;

		using promise_type = Promise;

		~Task()
		{
			Release();
		}

		// Fire-and-forget: equivalent to dropping the task object, made explicit.
		void Forget() &&
		{
			Release();
			handle_ = nullptr;
		}

		[[nodiscard]]
		bool IsReady() const noexcept
		{
			return !handle_ || handle_.promise().IsReady();
		}

		[[nodiscard]]
		bool IsDone() const noexcept
		{
			return IsReady();
		}

		[[nodiscard]]
		Task<std::monostate> ToMonostateTask() &&
		{
			co_await std::move(*this);
			co_return std::monostate{};
		}

		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;

		Task(Task&& other) noexcept :
			handle_(std::exchange(other.handle_, nullptr))
		{
		}

		Task& operator=(Task&& other) noexcept
		{
			if (this != &other)
			{
				Release();
				handle_ = std::exchange(other.handle_, nullptr);
			}
			return *this;
		}

	private:
		explicit Task(Handle handle) noexcept :
			handle_(handle)
		{
		}

		void Release() noexcept
		{
			if (handle_ && handle_.promise().Detach())
			{
				handle_.destroy();
			}
		}

		Handle handle_;
	};

	template<typename T>
	class Task<T>::Awaiter
	{
	public:
		explicit Awaiter(Task&& task) noexcept :
			task_(std::move(task))
		{
		}

		[[nodiscard]]
		bool await_ready() const noexcept
		{
			return GetPromise().IsReady();
		}

		// Returns false when the task finished between await_ready and here
		// (possible when it runs on another thread); the awaiting coroutine then
		// resumes immediately instead of suspending.
		bool await_suspend(std::coroutine_handle<> awaitingHandle) noexcept
		{
			return GetPromise().TryInstallContinuation(awaitingHandle);
		}

		T await_resume()
		{
			return GetPromise().TakeResult();
		}

	private:
		[[nodiscard]]
		Promise& GetPromise() const noexcept
		{
			return task_.handle_.promise();
		}

		Task task_;
	};

	template<typename T>
	class Task<T>::Promise final : public PromiseBase<T>
	{
	public:
		void* operator new(std::size_t size)
		{
			return Details::AllocateFrame(size);
		}

		void operator delete(void* ptr, std::size_t size) noexcept
		{
			Details::DeallocateFrame(ptr, size);
		}

		Task get_return_object() noexcept
		{
			return Task{Handle::from_promise(*this)};
		}

		std::suspend_never initial_suspend() noexcept
		{
			return {};
		}

		auto final_suspend() noexcept
		{
			struct FinalAwaiter
			{
				[[nodiscard]]
				bool await_ready() const noexcept
				{
					return false;
				}

				std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept
				{
					const auto action = handle.promise().Complete();
					if (action.destroyFrame)
					{
						handle.destroy();
						return std::noop_coroutine();
					}
					if (action.continuation)
					{
						return action.continuation;
					}
					return std::noop_coroutine();
				}

				void await_resume() const noexcept
				{
				}
			};

			return FinalAwaiter{};
		}

		// co_yield {} parks the coroutine on the scheduler currently running on
		// this thread; it resumes on that scheduler's next Update().
		auto yield_value(std::monostate) noexcept
		{
			struct YieldAwaiter
			{
				TaskScheduler* scheduler;

				[[nodiscard]]
				bool await_ready() const noexcept
				{
					return false;
				}

				void await_suspend(std::coroutine_handle<> handle) const
				{
					scheduler->Schedule(handle);
				}

				void await_resume() const noexcept
				{
				}
			};

			TaskScheduler* scheduler = TaskSchedulerManager::CurrentActiveScheduler();
			assert(scheduler && "co_yield: no scheduler is active on this thread");
			return YieldAwaiter{scheduler};
		}

		template<Awaitable U>
		auto await_transform(U&& awaitable) noexcept
		{
			return AwaitTransformer<std::decay_t<U>>::Transform(std::forward<U>(awaitable));
		}
	};

	template<typename T>
	class AwaitTransformer<Task<T>>
	{
	public:
		static auto Transform(Task<T>&& task) noexcept
		{
			return typename Task<T>::Awaiter{std::move(task)};
		}
	};
}

#endif //TASKKIT_TASK_H
