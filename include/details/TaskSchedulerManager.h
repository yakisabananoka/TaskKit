#ifndef TASKKIT_TASKSCHEDULER_MANAGER_H
#define TASKKIT_TASKSCHEDULER_MANAGER_H

#include <cassert>
#include <coroutine>
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

		void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
		{
			assert(id.IsValid() && "TaskSchedulerManager::Schedule: invalid scheduler id");
			id.GetScheduler()->Schedule(handle);
		}

		void ActivateScheduler(const TaskSchedulerId& id)
		{
			assert(id.IsValid() && "TaskSchedulerManager::ActivateScheduler: invalid scheduler id");
			assert(std::this_thread::get_id() == id.GetThreadId() && "TaskSchedulerManager: cannot activate a scheduler owned by another thread");
			ActivationStack().push_back(id.GetScheduler());
		}

		void DeactivateScheduler()
		{
			auto& stack = ActivationStack();
			assert(!stack.empty() && "TaskSchedulerManager: no active scheduler in current context");
			stack.pop_back();
		}

		[[nodiscard]]
		TaskSchedulerId GetActivatedSchedulerId() const
		{
			auto& stack = ActivationStack();
			assert(!stack.empty() && "TaskSchedulerManager: no active scheduler in current context");
			return TaskSchedulerId{stack.back()};
		}

		void UpdateActivatedScheduler()
		{
			auto& stack = ActivationStack();
			assert(!stack.empty() && "TaskSchedulerManager: no active scheduler in current context");
			stack.back()->Update();
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount(const TaskSchedulerId& id) const
		{
			assert(id.IsValid() && "TaskSchedulerManager::GetPendingTaskCount: invalid scheduler id");
			return id.GetScheduler()->GetPendingTaskCount();
		}

		// The scheduler currently running on this thread, or nullptr outside of
		// any activation.
		[[nodiscard]]
		static TaskScheduler* CurrentActiveScheduler() noexcept
		{
			auto& stack = ActivationStack();
			return stack.empty() ? nullptr : stack.back();
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
