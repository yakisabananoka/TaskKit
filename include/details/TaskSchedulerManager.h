#ifndef TASKKIT_TASKSCHEDULER_MANAGER_H
#define TASKKIT_TASKSCHEDULER_MANAGER_H

#include <cassert>
#include <coroutine>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "TaskScheduler.h"
#include "TaskSchedulerId.h"

namespace TKit
{
	class PromiseContext;

	// Owns all TaskSchedulers of one TaskSystem. Creation is mutex-guarded and
	// may happen from any thread; schedulers get stable addresses so a
	// TaskSchedulerId can point at them directly and the hot scheduling paths
	// need no lookups or locks.
	//
	// The activation stack is a per-thread notion, kept in thread-local storage:
	// activating a scheduler never touches shared state.
	class TaskSchedulerManager final
	{
	public:
		TaskSchedulerManager() = default;
		~TaskSchedulerManager() = default;

		// Wired by TaskSystem before any scheduler exists; every scheduler
		// created afterwards carries this pointer, which is how coroutines
		// running under an activation find their system's services.
		void SetPromiseContext(const PromiseContext* context)
		{
			std::lock_guard lock(mutex_);
			promiseContext_ = context;
		}

		TaskSchedulerId CreateScheduler(std::thread::id threadId, std::size_t reservedTaskCount)
		{
			std::lock_guard lock(mutex_);
			auto scheduler = std::make_unique<TaskScheduler>(reservedTaskCount, threadId, promiseContext_);
			const TaskSchedulerId id{scheduler.get()};
			schedulers_.push_back(std::move(scheduler));
			return id;
		}

		// Precondition violations below guard memory safety, so they abort in
		// release builds too instead of compiling down to undefined behavior.

		static void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
		{
			TaskScheduler* scheduler = id.GetScheduler();
			assert(scheduler && "TaskSchedulerManager::Schedule: invalid scheduler id");
			if (scheduler == nullptr)
			{
				std::abort();
			}
			scheduler->Schedule(handle);
		}

		// The activation stack is thread-local, so activation management is
		// static: it works even while no manager instance is alive (e.g. an
		// activation guard destroyed after its TaskSystem).
		static void ActivateScheduler(const TaskSchedulerId& id)
		{
			TaskScheduler* scheduler = id.GetScheduler();
			assert(scheduler && "TaskSchedulerManager::ActivateScheduler: invalid scheduler id");
			if (scheduler == nullptr || scheduler->GetOwnerThreadId() != std::this_thread::get_id())
			{
				assert(false && "TaskSchedulerManager: cannot activate a scheduler owned by another thread");
				std::abort();
			}
			ActivationStack().push_back(scheduler);
		}

		static void DeactivateScheduler()
		{
			auto& stack = ActivationStack();
			assert(!stack.empty() && "TaskSchedulerManager: no active scheduler in current context");
			if (stack.empty())
			{
				std::abort();
			}
			stack.pop_back();
		}

		// Deactivation that verifies LIFO order: aborts if `expected` is not the
		// innermost activation, instead of silently popping someone else's.
		static void DeactivateScheduler(TaskScheduler* expected)
		{
			auto& stack = ActivationStack();
			assert(!stack.empty() && stack.back() == expected && "SchedulerActivation destroyed out of LIFO order");
			if (stack.empty() || stack.back() != expected)
			{
				std::abort();
			}
			stack.pop_back();
		}

		[[nodiscard]]
		static TaskSchedulerId GetActivatedSchedulerId()
		{
			return TaskSchedulerId{&RequireActiveScheduler()};
		}

		static void UpdateActivatedScheduler()
		{
			RequireActiveScheduler().Update();
		}

		[[nodiscard]]
		static std::size_t GetPendingTaskCount(const TaskSchedulerId& id)
		{
			TaskScheduler* scheduler = id.GetScheduler();
			assert(scheduler && "TaskSchedulerManager::GetPendingTaskCount: invalid scheduler id");
			if (scheduler == nullptr)
			{
				std::abort();
			}
			return scheduler->GetPendingTaskCount();
		}

		// The scheduler currently running on this thread, or nullptr outside of
		// any activation.
		[[nodiscard]]
		static TaskScheduler* CurrentActiveScheduler() noexcept
		{
			auto& stack = ActivationStack();
			return stack.empty() ? nullptr : stack.back();
		}

		// Like CurrentActiveScheduler, but an activation is required: aborts
		// instead of returning null, so callers can dereference unconditionally.
		[[nodiscard]]
		static TaskScheduler& RequireActiveScheduler() noexcept
		{
			TaskScheduler* scheduler = CurrentActiveScheduler();
			assert(scheduler && "TaskSchedulerManager: no scheduler is active on this thread (activate one before creating or resuming tasks)");
			if (scheduler == nullptr)
			{
				std::abort();
			}
			return *scheduler;
		}

		// The PromiseContext of the scheduler active on this thread. Frame
		// allocation and thread-pool switches resolve their services here;
		// aborts (memory safety) when no scheduler is active or the scheduler
		// was created without a context.
		[[nodiscard]]
		static const PromiseContext& RequireCurrentPromiseContext() noexcept
		{
			const PromiseContext* context = RequireActiveScheduler().GetPromiseContext();
			assert(context && "TaskSchedulerManager: the active scheduler has no PromiseContext (create schedulers through TaskSystem)");
			if (context == nullptr)
			{
				std::abort();
			}
			return *context;
		}

		TaskSchedulerManager(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager& operator=(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager(TaskSchedulerManager&&) = delete;
		TaskSchedulerManager& operator=(TaskSchedulerManager&&) = delete;

	private:
		static std::vector<TaskScheduler*>& ActivationStack() noexcept
		{
			static thread_local std::vector<TaskScheduler*> stack;
			return stack;
		}

		mutable std::mutex mutex_;
		const PromiseContext* promiseContext_ = nullptr;
		std::vector<std::unique_ptr<TaskScheduler>> schedulers_;
	};
}

#endif //TASKKIT_TASKSCHEDULER_MANAGER_H
