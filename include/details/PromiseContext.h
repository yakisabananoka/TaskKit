#ifndef TASKKIT_PROMISE_CONTEXT_H
#define TASKKIT_PROMISE_CONTEXT_H

#include <cassert>
#include "TaskAllocator.h"
#include "TaskSchedulerManager.h"

namespace TKit
{
	class PromiseContext final
	{
	public:
		PromiseContext(TaskAllocator& allocator, TaskSchedulerManager& schedulerManager) noexcept :
			allocator_(&allocator),
			schedulerManager_(&schedulerManager)
		{
		}

		void static SetCurrent(PromiseContext* context) noexcept
		{
			currentContext_ = context;
		}

		[[nodiscard]]
		static const PromiseContext& GetCurrent() noexcept
		{
			assert(currentContext_ && "TaskContext::GetCurrent: No current PromiseContext set");
			return *currentContext_;
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

	private:
		TaskAllocator* allocator_;
		TaskSchedulerManager* schedulerManager_;

		static inline PromiseContext* currentContext_ = nullptr;
	};
}

#endif //TASKKIT_PROMISE_CONTEXT_H
