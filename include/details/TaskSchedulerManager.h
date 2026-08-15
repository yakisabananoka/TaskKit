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
	// Owns all TaskSchedulers. Creation is mutex-guarded and may happen from any
	// thread; schedulers get stable addresses so a TaskSchedulerId can point at
	// them directly and the hot scheduling paths need no lookups or locks.
	//
	// The activation stack is a per-thread notion, kept in thread-local storage:
	// activating a scheduler never touches shared state.
	class TaskSchedulerManager final
	{
	public:
		TaskSchedulerManager() = default;
		~TaskSchedulerManager() = default;

		TaskSchedulerId CreateScheduler(std::thread::id threadId, std::size_t reservedTaskCount)
		{
			auto scheduler = std::make_unique<TaskScheduler>(reservedTaskCount, threadId);
			const TaskSchedulerId id{scheduler.get()};

			std::lock_guard lock(mutex_);
			schedulers_.push_back(std::move(scheduler));
			return id;
		}

		// Precondition violations below guard memory safety, so they abort in
		// release builds too instead of compiling down to undefined behavior.

		void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
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
		// activation guard destroyed after TaskSystem::Shutdown).
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
		std::size_t GetPendingTaskCount(const TaskSchedulerId& id) const
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
			assert(scheduler && "TaskSchedulerManager: no scheduler is active on this thread");
			if (scheduler == nullptr)
			{
				std::abort();
			}
			return *scheduler;
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
		std::vector<std::unique_ptr<TaskScheduler>> schedulers_;
	};
}

#endif //TASKKIT_TASKSCHEDULER_MANAGER_H
