#ifndef TASKKIT_UTILITY_H
#define TASKKIT_UTILITY_H

#include <functional>
#include <memory>
#include <optional>
#include <tuple>
#include "Task.h"
#include "TaskSchedulerId.h"
#include "ThreadPool.h"

namespace TKit
{
	namespace Details
	{
		template<typename T>
		struct TaskTraits
		{
			static constexpr bool IsTask = false;
		};

		template<typename T>
		struct TaskTraits<Task<T>>
		{
			using ResultType = T;
			static constexpr bool IsTask = true;
		};

		template<typename T>
		using TaskFuncTraits = TaskTraits<std::invoke_result_t<T>>;

		template<typename T, typename... Results>
		constexpr bool FulfillsAllType = (std::is_same_v<T, Results> && ...);

		template<typename... Results>
		constexpr bool FulfillsAllVoid = FulfillsAllType<void, Results...>;

		template<typename... Results>
		constexpr bool FulfillsSameType = (std::is_same_v<Results, std::tuple_element_t<0, std::tuple<Results...>>> && ...);

		template<typename... Results>
		constexpr bool HasAnyType = (sizeof...(Results) > 0);

		template<typename Result>
		inline auto ToMonostateIfVoid(Task<Result>&& task)
		{
			if constexpr (std::is_void_v<Result>)
			{
				return std::move(task).ToMonostateTask();
			}
			else
			{
				return std::move(task);
			}
		}

		template<std::size_t I, typename ResultVariant, typename TaskResult>
		inline Task<> WhenAnyHelperTask(std::shared_ptr<std::optional<ResultVariant>> result, Task<TaskResult> task)
		{
			auto taskResult = co_await std::move(task);
			if (!result->has_value())
			{
				*result = ResultVariant(std::in_place_index<I>, std::move(taskResult));
			}
		}

		template<std::size_t I, typename ResultVariant, typename First, typename... Others>
		inline void WhenAnyHelper(std::shared_ptr<std::optional<ResultVariant>> result, Task<First>&& first, Task<Others>&&... others)
		{
			WhenAnyHelperTask<I>(result, std::move(first)).Forget();

			if constexpr (sizeof...(others) > 0)
			{
				WhenAnyHelper<I + 1>(result, std::move(others)...);
			}
		}
	}

	inline void ThrowIfStopRequested(const std::stop_token& stopToken)
	{
		if (stopToken.stop_requested())
		{
			throw OperationStoppedError();
		}
	}

	struct SwitchToThreadPoolAwaiter
	{
		[[nodiscard]]
		bool await_ready() const noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) const
		{
			PromiseContext::GetCurrent().GetThreadPool().Schedule(handle);
		}

		void await_resume() const noexcept
		{
		}
	};

	inline SwitchToThreadPoolAwaiter SwitchToThreadPool()
	{
		return SwitchToThreadPoolAwaiter{};
	}

	template<>
	class AwaitTransformer<SwitchToThreadPoolAwaiter>
	{
	public:
		static SwitchToThreadPoolAwaiter Transform(SwitchToThreadPoolAwaiter awaiter) noexcept
		{
			return awaiter;
		}
	};

	struct SwitchToSelectedSchedulerAwaiter
	{
		TaskSchedulerId schedulerId;

		[[nodiscard]]
		bool await_ready() const noexcept
		{
			return false;
		}

		void await_suspend(std::coroutine_handle<> handle) const
		{
			PromiseContext::GetCurrent().GetSchedulerManager().Schedule(schedulerId, handle);
		}

		void await_resume() const noexcept
		{
		}
	};

	inline SwitchToSelectedSchedulerAwaiter SwitchToSelectedScheduler(TaskSchedulerId schedulerId)
	{
		return SwitchToSelectedSchedulerAwaiter{schedulerId};
	}

	template<>
	class AwaitTransformer<SwitchToSelectedSchedulerAwaiter>
	{
	public:
		static SwitchToSelectedSchedulerAwaiter Transform(SwitchToSelectedSchedulerAwaiter awaiter) noexcept
		{
			return awaiter;
		}
	};

	template<typename Func>
		requires std::is_invocable_v<Func> && (!Details::TaskFuncTraits<Func>::IsTask)
	inline Task<std::invoke_result_t<Func>> RunOnThreadPool(Func&& func)
	{
		auto originalSchedulerId = PromiseContext::GetCurrent().GetSchedulerManager().GetActivatedSchedulerId();
		co_await SwitchToThreadPool();

		if constexpr (std::is_void_v<std::invoke_result_t<Func>>)
		{
			std::forward<Func>(func)();
			co_await SwitchToSelectedScheduler(originalSchedulerId);
			co_return;
		}
		else
		{
			auto result = std::forward<Func>(func)();
			co_await SwitchToSelectedScheduler(originalSchedulerId);
			co_return std::move(result);
		}
	}

	template<typename Func>
	requires std::is_invocable_v<Func> && (Details::TaskFuncTraits<Func>::IsTask)
	inline Task<typename Details::TaskFuncTraits<Func>::ResultType> RunOnThreadPool(Func&& func)
	{
		auto originalSchedulerId = PromiseContext::GetCurrent().GetSchedulerManager().GetActivatedSchedulerId();
		co_await SwitchToThreadPool();

		if constexpr (std::is_void_v<typename Details::TaskFuncTraits<Func>::ResultType>)
		{
			co_await std::forward<Func>(func)();
			co_await SwitchToSelectedScheduler(originalSchedulerId);
			co_return;
		}
		else
		{
			auto result = co_await std::forward<Func>(func)();
			co_await SwitchToSelectedScheduler(originalSchedulerId);
			co_return std::move(result);
		}
	}

	inline Task<> GetCompletedTask()
	{
		co_return;
	}

	inline Task<> CreateTask(std::function<Task<>(std::stop_token)> func, std::stop_token stopToken = {})
	{
		ThrowIfStopRequested(stopToken);
		co_await func(stopToken);
	}

	inline void RunTask(const std::function<Task<>(std::stop_token)>& func, std::stop_token stopToken = {})
	{
		ThrowIfStopRequested(stopToken);
		func(stopToken).Forget();
	}

	inline Task<> DelayFrame(int frameCount, std::stop_token stopToken = {})
	{
		if (frameCount > 0)
		{
			while (frameCount-- > 0)
			{
				ThrowIfStopRequested(stopToken);
				co_yield {};
			}
		}

		ThrowIfStopRequested(stopToken);
		co_return;
	}

	template<typename Rep, typename Period>
	inline Task<> WaitFor(std::chrono::duration<Rep, Period> duration, std::stop_token stopToken = {})
	{
		const auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < duration)
		{
			ThrowIfStopRequested(stopToken);
			co_yield {};
		}

		ThrowIfStopRequested(stopToken);
		co_return;
	}

	template<typename Clock, typename Duration>
	inline Task<> WaitUntil(std::chrono::time_point<Clock, Duration> timePoint, std::stop_token stopToken = {})
	{
		while (Clock::now() < timePoint)
		{
			ThrowIfStopRequested(stopToken);
			co_yield {};
		}

		ThrowIfStopRequested(stopToken);
		co_return;
	}

	template<typename... Results>
	using WhenAllResultType = std::tuple<std::conditional_t<std::is_void_v<Results>, std::monostate, Results>...>;

	template<typename... Results>
		requires Details::HasAnyType<Results...> && (!Details::FulfillsAllVoid<void, Results...>)
	inline Task<WhenAllResultType<Results...>> WhenAll(Task<Results>&&... tasks)
	{
		co_return std::make_tuple(co_await Details::ToMonostateIfVoid(std::move(tasks))...);
	}

	template<typename... Results>
		requires Details::HasAnyType<Results...> && Details::FulfillsAllVoid<Results...>
	inline Task<> WhenAll(Task<Results>&&... tasks)
	{
		(co_await std::move(tasks), ...);
		co_return;
	}

	inline Task<> WhenAll(std::vector<Task<>> tasks)
	{
		if (tasks.empty())
		{
			co_return;
		}

		for (auto& task : tasks)
		{
			co_await std::move(task);
		}

		co_return;
	}

	template<typename... Results>
	using WhenAnyResultType = std::variant<std::conditional_t<std::is_void_v<Results>, std::monostate, Results>...>;

	template<typename... Results>
		requires Details::HasAnyType<Results...> && (!Details::FulfillsAllVoid<Results...>)
	inline Task<WhenAnyResultType<Results...>> WhenAny(Task<Results>&&... tasks)
	{
		auto result = std::make_shared<std::optional<WhenAnyResultType<Results...>>>();
		Details::WhenAnyHelper<0>(result, Details::ToMonostateIfVoid(std::move(tasks))...);

		while (!result->has_value())
		{
			co_yield {};
		}

		co_return std::move(result->value());
	}

	template<typename... Results>
		requires Details::HasAnyType<Results...> && Details::FulfillsAllVoid<Results...>
	inline Task<std::size_t> WhenAny(Task<Results>&&... tasks)
	{
		auto result = std::make_shared<std::optional<WhenAnyResultType<Results...>>>();
		Details::WhenAnyHelper<0>(result, Details::ToMonostateIfVoid(std::move(tasks))...);

		while (!result->has_value())
		{
			co_yield {};
		}

		co_return result->value().index();
	}

	template<typename Rep, typename Period>
	class AwaitTransformer<std::chrono::duration<Rep, Period>>
	{
	public:
		static auto Transform(const std::chrono::duration<Rep, Period>& duration) noexcept
		{
			return Task<>::Awaiter{ WaitFor(duration) };
		}
	};

}

#endif //TASKKIT_UTILITY_H