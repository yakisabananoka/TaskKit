#ifndef TASKKIT_TASK_SYSTEM_H
#define TASKKIT_TASK_SYSTEM_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include "PoolAllocator.h"
#include "PromiseContext.h"
#include "SchedulerHandle.h"
#include "TaskSchedulerManager.h"
#include "TaskSystemConfiguration.h"
#include "ThreadPool.h"

namespace TKit
{
	// One complete task runtime: owns the scheduler manager, the thread pool
	// and the frame allocator. Construct an instance before creating any Task
	// and destroy it (on the constructing thread) after all tasks finished —
	// drain your schedulers first. Instances are independent; several may
	// coexist, e.g. one per test. A Task binds to the system whose scheduler
	// is active on the creating thread, so activate a scheduler (see
	// SchedulerHandle::Activate) before creating tasks.
	class TaskSystem final
	{
	public:
		explicit TaskSystem(const TaskSystemConfiguration& config = TaskSystemConfiguration{}) :
			ownerThreadId_(std::this_thread::get_id()),
			reservedTaskCount_(config.reservedTaskCount),
			poolAllocator_(config.allocator.has_value() ? nullptr : std::make_unique<PoolAllocator>()),
			allocator_(config.allocator.has_value() ? config.allocator.value() : poolAllocator_->CreateTaskAllocator()),
			promiseContext_(allocator_, schedulerManager_)
		{
			if (poolAllocator_)
			{
				poolAllocator_->Prewarm();
			}

			// The context must be wired into the manager before the pool starts:
			// worker threads register their schedulers during ThreadPool
			// construction and each scheduler captures the context pointer then.
			schedulerManager_.SetPromiseContext(&promiseContext_);

			const std::size_t threadCount = config.threadPoolSize > 0
				? config.threadPoolSize
				: std::max<std::size_t>(1, std::thread::hardware_concurrency());
			threadPool_ = std::make_unique<ThreadPool>(
				schedulerManager_,
				threadCount,
				config.reservedTaskCount,
				config.workerFrameInterval
			);

			// Safe to publish late: coroutines can only reach a worker through a
			// mutex-guarded scheduler queue, and only after this constructor
			// returned, so every reader is ordered after this write.
			promiseContext_.SetThreadPool(*threadPool_);
		}

		// Stops the thread pool promptly (pool tasks still queued or parked are
		// destroyed, not completed) and tears everything down. Every Task object
		// for a not-yet-finished task must be gone before this point: frames the
		// schedulers destroy here must not be touched by a Task destructor
		// afterwards. Drain your schedulers first if completion matters.
		~TaskSystem()
		{
			// Destroying from another thread would race with anything still
			// using the constructing thread's schedulers; abort in release
			// builds too rather than corrupt memory.
			assert(std::this_thread::get_id() == ownerThreadId_ && "TaskSystem must be destroyed on the thread that constructed it.");
			if (std::this_thread::get_id() != ownerThreadId_)
			{
				std::abort();
			}

			// Members tear down in reverse declaration order: the thread pool
			// joins its workers, then the manager destroys the schedulers (any
			// leftover queued frame frees itself through its frame header), and
			// only then the pool allocator behind those frames goes away.
		}

		TaskSystem(const TaskSystem&) = delete;
		TaskSystem& operator=(const TaskSystem&) = delete;
		TaskSystem(TaskSystem&&) = delete;
		TaskSystem& operator=(TaskSystem&&) = delete;

		// reservedTaskCount defaults to the value configured at construction
		// (TaskSystemConfiguration::reservedTaskCount).
		[[nodiscard]]
		SchedulerHandle CreateScheduler(std::optional<std::thread::id> threadId = std::nullopt, std::optional<std::size_t> reservedTaskCount = std::nullopt)
		{
			return schedulerManager_.CreateScheduler(
				threadId.value_or(std::this_thread::get_id()),
				reservedTaskCount.value_or(reservedTaskCount_));
		}

	private:
		std::thread::id ownerThreadId_;
		std::size_t reservedTaskCount_;
		std::unique_ptr<PoolAllocator> poolAllocator_;
		TaskAllocator allocator_;
		TaskSchedulerManager schedulerManager_;
		PromiseContext promiseContext_;
		std::unique_ptr<ThreadPool> threadPool_;
	};
}

#endif //TASKKIT_TASK_SYSTEM_H
