#ifndef TASKKIT_CURRENT_SCHEDULER_H
#define TASKKIT_CURRENT_SCHEDULER_H

#include <cassert>
#include <cstdlib>
#include <utility>
#include "TaskScheduler.h"

namespace TKit
{
	class PromiseContext;
	class SchedulerHandle;

	namespace Details
	{
		// The single piece of mutable static state in TaskKit: the scheduler
		// currently driving this thread. A constinit raw pointer, so it needs no
		// dynamic initialization and no destruction — reading or writing it is
		// safe at any point of thread or program teardown (e.g. an activation
		// guard destroyed after its TaskSystem).
		//
		// The activation *stack* is not stored here: each SchedulerActivation
		// guard remembers the pointer it replaced, so nesting lives on the call
		// stack and only the innermost activation needs to be ambient.
		inline thread_local constinit TaskScheduler* CurrentSchedulerPointer = nullptr;

		[[nodiscard]]
		inline TaskScheduler* GetCurrentScheduler() noexcept
		{
			return CurrentSchedulerPointer;
		}

		inline TaskScheduler* ExchangeCurrentScheduler(TaskScheduler* scheduler) noexcept
		{
			return std::exchange(CurrentSchedulerPointer, scheduler);
		}

		// Precondition violations below guard memory safety, so they abort in
		// release builds too instead of compiling down to undefined behavior.

		[[nodiscard]]
		inline TaskScheduler& RequireCurrentScheduler() noexcept
		{
			TaskScheduler* scheduler = CurrentSchedulerPointer;
			assert(scheduler && "TaskKit: no scheduler is active on this thread (activate one before creating or resuming tasks)");
			if (scheduler == nullptr)
			{
				std::abort();
			}
			return *scheduler;
		}

		// The PromiseContext of the scheduler active on this thread. Frame
		// allocation and thread-pool switches resolve their services here;
		// aborts (memory safety) when no scheduler is active or the scheduler
		// was created without a context.
		[[nodiscard]]
		inline const PromiseContext& RequireCurrentPromiseContext() noexcept
		{
			const PromiseContext* context = RequireCurrentScheduler().GetPromiseContext();
			assert(context && "TaskKit: the active scheduler has no PromiseContext (create schedulers through TaskSystem)");
			if (context == nullptr)
			{
				std::abort();
			}
			return *context;
		}
	}

	// RAII guard that makes a scheduler current on this thread. The previous
	// activation is remembered inside the guard and restored on release, so
	// activations nest without any thread-local container; a non-LIFO release
	// is caught and aborts. Release only writes the thread-local pointer, so
	// destroying a guard after the TaskSystem itself is safe.
	//
	// Obtained from SchedulerHandle::Activate().
	class SchedulerActivation final
	{
	public:
		SchedulerActivation() noexcept :
			activated_(nullptr),
			previous_(nullptr)
		{
		}

		~SchedulerActivation()
		{
			Release();
		}

		SchedulerActivation(const SchedulerActivation&) = delete;
		SchedulerActivation& operator=(const SchedulerActivation&) = delete;

		SchedulerActivation(SchedulerActivation&& other) noexcept :
			activated_(std::exchange(other.activated_, nullptr)),
			previous_(std::exchange(other.previous_, nullptr))
		{
		}

		SchedulerActivation& operator=(SchedulerActivation&& other) noexcept
		{
			if (this != &other)
			{
				Release();
				activated_ = std::exchange(other.activated_, nullptr);
				previous_ = std::exchange(other.previous_, nullptr);
			}
			return *this;
		}

	private:
		friend class SchedulerHandle;

		explicit SchedulerActivation(TaskScheduler& scheduler) noexcept :
			activated_(&scheduler),
			previous_(Details::ExchangeCurrentScheduler(&scheduler))
		{
		}

		void Release() noexcept
		{
			if (activated_ == nullptr)
			{
				return;
			}

			// A non-LIFO release would silently deactivate someone else's
			// scheduler; abort instead — everything ambient resolves through
			// the current scheduler, so this guards memory safety.
			assert(Details::GetCurrentScheduler() == activated_ && "SchedulerActivation released out of LIFO order");
			if (Details::GetCurrentScheduler() != activated_)
			{
				std::abort();
			}

			Details::ExchangeCurrentScheduler(previous_);
			activated_ = nullptr;
			previous_ = nullptr;
		}

		TaskScheduler* activated_;
		TaskScheduler* previous_;
	};
}

#endif //TASKKIT_CURRENT_SCHEDULER_H
