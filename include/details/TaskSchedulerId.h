#ifndef TASKKIT_TASKSCHEDULER_ID_H
#define TASKKIT_TASKSCHEDULER_ID_H

#include <thread>
#include "TaskScheduler.h"

namespace TKit
{
	// Opaque handle to a TaskScheduler owned by a TaskSchedulerManager.
	// Internally a direct pointer, so scheduling through an id costs no lookup.
	// Valid until the owning manager is destroyed.
	class TaskSchedulerId final
	{
	public:
		TaskSchedulerId() noexcept = default;

		explicit TaskSchedulerId(TaskScheduler* scheduler) noexcept :
			scheduler_(scheduler)
		{
		}

		[[nodiscard]]
		std::thread::id GetThreadId() const noexcept
		{
			return scheduler_ ? scheduler_->GetOwnerThreadId() : std::thread::id{};
		}

		[[nodiscard]]
		TaskScheduler* GetScheduler() const noexcept
		{
			return scheduler_;
		}

		[[nodiscard]]
		bool IsValid() const noexcept
		{
			return scheduler_ != nullptr;
		}

		auto operator<=>(const TaskSchedulerId&) const noexcept = default;

	private:
		TaskScheduler* scheduler_ = nullptr;
	};
}

#endif //TASKKIT_TASKSCHEDULER_ID_H
