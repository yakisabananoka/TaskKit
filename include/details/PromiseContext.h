#ifndef TASKKIT_PROMISE_CONTEXT_H
#define TASKKIT_PROMISE_CONTEXT_H

#include <atomic>
#include <cassert>
#include <cstdlib>

namespace TKit
{
	class TaskAllocator;
	class TaskSchedulerManager;
	class ThreadPool;

	// Ambient services (allocator / scheduler manager / thread pool) that task
	// coroutines reach without threading them through every call. Set once by
	// TaskSystem::Initialize and cleared by TaskSystem::Shutdown; read-only in
	// between, so a single atomic pointer is all the synchronization needed.
	class PromiseContext final
	{
	public:
		PromiseContext(TaskAllocator& allocator, TaskSchedulerManager& schedulerManager, ThreadPool& threadPool) noexcept :
			allocator_(&allocator),
			schedulerManager_(&schedulerManager),
			threadPool_(&threadPool)
		{
		}

		static void SetCurrent(PromiseContext* context) noexcept
		{
			GetCurrentStorage().store(context, std::memory_order_release);
		}

		[[nodiscard]]
		static const PromiseContext& GetCurrent() noexcept
		{
			const PromiseContext* context = GetCurrentStorage().load(std::memory_order_acquire);
			assert(context && "PromiseContext::GetCurrent: no context set. Call TaskSystem::Initialize() first.");
			if (context == nullptr)
			{
				// Creating tasks before Initialize / after Shutdown would
				// otherwise dereference null inside the frame allocator.
				std::abort();
			}
			return *context;
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
			return *threadPool_;
		}

	private:
		static std::atomic<PromiseContext*>& GetCurrentStorage() noexcept
		{
			static std::atomic<PromiseContext*> current{nullptr};
			return current;
		}

		TaskAllocator* allocator_;
		TaskSchedulerManager* schedulerManager_;
		ThreadPool* threadPool_;
	};
}

#endif //TASKKIT_PROMISE_CONTEXT_H
