#ifndef TASKKIT_PROMISE_BASE_H
#define TASKKIT_PROMISE_BASE_H

#include <atomic>
#include <coroutine>
#include <exception>
#include <utility>
#include <variant>

namespace TKit
{
	namespace Details
	{
		// Unique addresses used as sentinel values inside PromiseStateCore.
		inline void* ReadyTag() noexcept
		{
			static constinit char tag = 0;
			return &tag;
		}

		inline void* DetachedTag() noexcept
		{
			static constinit char tag = 0;
			return &tag;
		}
	}

	// Lock-free lifecycle state shared by every task promise. A single atomic
	// resolves every race between completion, awaiting and detaching:
	//
	//   nullptr       running; task object attached; no awaiter yet
	//   <address>     an awaiter installed its continuation handle
	//   ReadyTag      the coroutine finished (result/exception stored before)
	//   DetachedTag   the task object was dropped; the frame destroys itself
	//                 when it completes
	class PromiseStateCore
	{
	public:
		[[nodiscard]]
		bool IsReady() const noexcept
		{
			return state_.load(std::memory_order_acquire) == Details::ReadyTag();
		}

		// Called by the awaiter after IsReady() returned false. Returns false if
		// the coroutine finished in the meantime (possibly on another thread), in
		// which case the awaiter must not suspend.
		bool TryInstallContinuation(std::coroutine_handle<> continuation) noexcept
		{
			void* expected = nullptr;
			return state_.compare_exchange_strong(
				expected, continuation.address(),
				std::memory_order_release,
				std::memory_order_acquire);
		}

		// Called by the task object when it lets go of the frame. Returns true if
		// the coroutine already finished and the caller must destroy the frame.
		bool Detach() noexcept
		{
			void* previous = state_.exchange(Details::DetachedTag(), std::memory_order_acq_rel);
			return previous == Details::ReadyTag();
		}

		struct CompletionAction
		{
			std::coroutine_handle<> continuation;
			bool destroyFrame;
		};

		// Called exactly once, from final_suspend.
		[[nodiscard]]
		CompletionAction Complete() noexcept
		{
			void* previous = state_.exchange(Details::ReadyTag(), std::memory_order_acq_rel);
			if (previous == Details::DetachedTag())
			{
				return {nullptr, true};
			}
			if (previous == nullptr)
			{
				return {nullptr, false};
			}
			return {std::coroutine_handle<>::from_address(previous), false};
		}

	private:
		std::atomic<void*> state_{nullptr};
	};

	template<typename T>
	class PromiseBase : public PromiseStateCore
	{
	public:
		void return_value(T value)
		{
			result_.template emplace<1>(std::move(value));
		}

		void unhandled_exception() noexcept
		{
			result_.template emplace<2>(std::current_exception());
		}

		// Only valid once IsReady() is true; moves the result out, so a task can
		// be awaited at most once.
		T TakeResult()
		{
			if (result_.index() == 2)
			{
				std::rethrow_exception(std::get<2>(std::move(result_)));
			}
			return std::get<1>(std::move(result_));
		}

	protected:
		PromiseBase() = default;
		~PromiseBase() = default;

	private:
		std::variant<std::monostate, T, std::exception_ptr> result_;
	};

	template<>
	class PromiseBase<void> : public PromiseStateCore
	{
	public:
		void return_void() noexcept
		{
		}

		void unhandled_exception() noexcept
		{
			exception_ = std::current_exception();
		}

		void TakeResult()
		{
			if (exception_)
			{
				std::rethrow_exception(std::exchange(exception_, nullptr));
			}
		}

	protected:
		PromiseBase() = default;
		~PromiseBase() = default;

	private:
		std::exception_ptr exception_;
	};
}

#endif //TASKKIT_PROMISE_BASE_H
