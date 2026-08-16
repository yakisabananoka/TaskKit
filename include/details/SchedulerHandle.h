#ifndef TASKKIT_SCHEDULER_HANDLE_H
#define TASKKIT_SCHEDULER_HANDLE_H

#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <thread>
#include "CurrentScheduler.h"
#include "TaskScheduler.h"

namespace TKit
{
	// Non-owning handle to a TaskScheduler owned by a TaskSchedulerManager —
	// all scheduler operations go through it, in the spirit of
	// std::coroutine_handle. Internally a direct pointer, so every operation
	// costs no lookup. Valid until the owning manager (i.e. the TaskSystem) is
	// destroyed.
	class SchedulerHandle final
	{
	public:
		SchedulerHandle() noexcept = default;

		explicit SchedulerHandle(TaskScheduler* scheduler) noexcept :
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

		// Makes this scheduler current on the calling thread (which must be the
		// scheduler's owner thread) until the returned guard is released.
		// Activate before creating tasks outside of Update().
		[[nodiscard]]
		SchedulerActivation Activate() const
		{
			TaskScheduler& scheduler = Require();
			assert(scheduler.GetOwnerThreadId() == std::this_thread::get_id() && "SchedulerHandle::Activate: cannot activate a scheduler owned by another thread");
			if (scheduler.GetOwnerThreadId() != std::this_thread::get_id())
			{
				std::abort();
			}
			return SchedulerActivation{scheduler};
		}

		// Runs one frame of this scheduler (owner thread only). The scheduler is
		// current for the duration, so tasks created by resumed coroutines bind
		// to it — no separate activation needed for the update itself.
		void Update() const
		{
			const SchedulerActivation activation = Activate();
			scheduler_->Update();
		}

		// Thread-safe; may be called from any thread.
		void Schedule(std::coroutine_handle<> handle) const
		{
			Require().Schedule(handle);
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount() const
		{
			return Require().GetPendingTaskCount();
		}

		auto operator<=>(const SchedulerHandle&) const noexcept = default;

	private:
		// An invalid handle here means scheduling into nowhere — a memory-safety
		// precondition, so it aborts in release builds too.
		[[nodiscard]]
		TaskScheduler& Require() const noexcept
		{
			assert(scheduler_ && "SchedulerHandle: invalid (default-constructed or stale) scheduler handle");
			if (scheduler_ == nullptr)
			{
				std::abort();
			}
			return *scheduler_;
		}

		TaskScheduler* scheduler_ = nullptr;
	};

	// Handle to the scheduler currently active on this thread; aborts when no
	// scheduler is active.
	[[nodiscard]]
	inline SchedulerHandle GetActivatedScheduler()
	{
		return SchedulerHandle{&Details::RequireCurrentScheduler()};
	}
}

#endif //TASKKIT_SCHEDULER_HANDLE_H
