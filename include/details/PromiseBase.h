#ifndef TASKKIT_PROMISE_BASE_H
#define TASKKIT_PROMISE_BASE_H
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
			result_ = value;
		}

		void unhandled_exception()
		{
			result_ = std::current_exception();
		}

		[[nodiscard]]
		bool IsReady() const
		{
			return result_.has_value();
		}

		T Get()
		{
			while (!IsReady())
			{
				// wait
			}

			if (std::holds_alternative<std::exception_ptr>(*result_))
			{
				std::rethrow_exception(std::get<std::exception_ptr>(*result_));
			}

			return std::get<T>(std::move(*result_));
		}

	protected:
		PromiseBase() = default;

	private:
		std::optional<std::variant<T, std::exception_ptr>> result_;
	};

	template <>
	class PromiseBase<void>
	{
	public:
		virtual ~PromiseBase() = default;

		void return_void()
		{
			result_ = 0;
		}

		void unhandled_exception()
		{
			result_ = std::current_exception();
		}

		[[nodiscard]]
		bool IsReady() const
		{
			return result_.has_value();
		}

		void Get()
		{
			while (!IsReady())
			{
				// wait
			}

			if (std::holds_alternative<std::exception_ptr>(*result_))
			{
				std::rethrow_exception(std::get<std::exception_ptr>(*result_));
			}
		}

	protected:
		PromiseBase() = default;

	private:
		std::optional<std::variant<int, std::exception_ptr>> result_;
	};
}

#endif //TASKKIT_PROMISE_BASE_H