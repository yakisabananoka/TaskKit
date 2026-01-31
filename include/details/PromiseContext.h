#ifndef TASKKIT_PROMISE_CONTEXT_H
#define TASKKIT_PROMISE_CONTEXT_H

#include <coroutine>
#include <cstddef>
#include <cassert>
#include "TaskSchedulerId.h"
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

		[[nodiscard]]
		void* Allocate(std::size_t size) const
		{
			return allocator_->Allocate(size);
		}

		void Deallocate(void* ptr, std::size_t size) const noexcept
		{
			allocator_->Deallocate(ptr, size);
		}

		[[nodiscard]]
		TaskSchedulerId GetActivatedSchedulerId() const
		{
			return schedulerManager_->GetActivatedSchedulerId();
		}

		void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle) const
		{
			schedulerManager_->Schedule(id, handle);
		}

		void static SetCurrent(PromiseContext* context) noexcept
		{
			currentContext_ = context;
		}

		[[nodiscard]]
		static const PromiseContext& GetCurrent() noexcept
		{
			assert(currentContext_ && "PromiseContext::GetCurrent: No current PromiseContext set");
			return *currentContext_;
		}

	private:
		TaskAllocator* allocator_;
		TaskSchedulerManager* schedulerManager_;

		static inline PromiseContext* currentContext_ = nullptr;
	};
}

#endif //TASKKIT_PROMISE_CONTEXT_H
