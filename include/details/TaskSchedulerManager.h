#ifndef TASKKIT_TASKSCHEDULER_MANAGER_H
#define TASKKIT_TASKSCHEDULER_MANAGER_H

#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "SchedulerHandle.h"
#include "TaskScheduler.h"

namespace TKit
{
	class PromiseContext;

	// Owns all TaskSchedulers of one TaskSystem — a plain instance registry
	// with no static state. Creation is mutex-guarded and may happen from any
	// thread; schedulers get stable addresses so a SchedulerHandle can point at
	// them directly and the hot scheduling paths need no lookups or locks.
	//
	// Which scheduler is *current* on a thread is not managed here: see
	// Details::CurrentSchedulerPointer and SchedulerActivation
	// (CurrentScheduler.h).
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

		SchedulerHandle CreateScheduler(std::thread::id threadId, std::size_t reservedTaskCount)
		{
			std::lock_guard lock(mutex_);
			auto scheduler = std::make_unique<TaskScheduler>(reservedTaskCount, threadId, promiseContext_);
			const SchedulerHandle handle{scheduler.get()};
			schedulers_.push_back(std::move(scheduler));
			return handle;
		}

		TaskSchedulerManager(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager& operator=(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager(TaskSchedulerManager&&) = delete;
		TaskSchedulerManager& operator=(TaskSchedulerManager&&) = delete;

	private:
		mutable std::mutex mutex_;
		const PromiseContext* promiseContext_ = nullptr;
		std::vector<std::unique_ptr<TaskScheduler>> schedulers_;
	};
}

#endif //TASKKIT_TASKSCHEDULER_MANAGER_H
