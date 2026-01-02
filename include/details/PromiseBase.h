#ifndef TASKKIT_PROMISE_H
#define TASKKIT_PROMISE_H
#include <future>

namespace TKit
{
	template<typename T>
	class PromiseBase
	{
	public:
		virtual ~PromiseBase() = default;

		void return_value(T value)
		{
			promise_.set_value(std::move(value));
		}

		void unhandled_exception()
		{
			promise_.set_exception(std::current_exception());
		}

		[[nodiscard]]
		bool IsReady() const
		{
			using namespace std::literals::chrono_literals;
			return future_.wait_for(0s) == std::future_status::ready;
		}

		T Get()
		{
			return future_.get();
		}

	protected:
		PromiseBase() :
			future_(promise_.get_future())
		{
		}

	private:
		std::promise<T> promise_;
		std::future<T> future_;
	};

	template <>
	class PromiseBase<void>
	{
	public:
		virtual ~PromiseBase() = default;

		void return_void()
		{
			promise_.set_value();
		}

		void unhandled_exception()
		{
			promise_.set_exception(std::current_exception());
		}

		[[nodiscard]]
		bool IsReady() const
		{
			using namespace std::literals::chrono_literals;
			return future_.wait_for(0s) == std::future_status::ready;
		}

		void Get()
		{
			future_.get();
		}

	protected:
		PromiseBase() :
			future_(promise_.get_future())
		{
		}

	private:
		std::promise<void> promise_;
		std::future<void> future_;
	};
}

#endif //TASKKIT_PROMISE_H