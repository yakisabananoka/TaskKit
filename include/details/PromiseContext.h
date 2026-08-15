#ifndef TASKKIT_PROMISE_CONTEXT_H
#define TASKKIT_PROMISE_CONTEXT_H

#include <cassert>

namespace TKit
{
	class TaskAllocator;
	class TaskSchedulerManager;
	class ThreadPool;

	// Per-TaskSystem services (allocator / scheduler manager / thread pool) that
	// task coroutines reach without threading them through every call. Owned by
	// its TaskSystem and stamped into every TaskScheduler that system creates;
	// code running under an activated scheduler resolves it through
	// TaskSchedulerManager::RequireCurrentPromiseContext().
	class PromiseContext final
	{
	public:
		PromiseContext(TaskAllocator& allocator, TaskSchedulerManager& schedulerManager) noexcept :
			allocator_(&allocator),
			schedulerManager_(&schedulerManager)
		{
		}

		// The thread pool is constructed after this context (its workers need
		// the context already wired into the scheduler manager when they
		// register their schedulers), so TaskSystem sets it afterwards.
		void SetThreadPool(ThreadPool& threadPool) noexcept
		{
			threadPool_ = &threadPool;
		}

		[[nodiscard]]
		TaskAllocator& GetAllocator() const noexcept
		{
			return *allocator_;
		}

		[[nodiscard]]
		TaskSchedulerManager& GetSchedulerManager() const noexcept
		{
			return *schedulerManager_;
		}

		[[nodiscard]]
		ThreadPool& GetThreadPool() const noexcept
		{
			assert(threadPool_ && "PromiseContext::GetThreadPool: thread pool not wired yet");
			return *threadPool_;
		}

	private:
		TaskAllocator* allocator_;
		TaskSchedulerManager* schedulerManager_;
		ThreadPool* threadPool_ = nullptr;
	};
}

#endif //TASKKIT_PROMISE_CONTEXT_H
