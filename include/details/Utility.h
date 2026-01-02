#ifndef TASKKIT_UTILITY_H
#define TASKKIT_UTILITY_H

#include <functional>
#include <tuple>
#include <variant>
#include "Task.h"

#if !defined(TASKKIT_STOP_TOKEN_PROCESS)
#define TASKKIT_STOP_TOKEN_PROCESS(STOP_TOKEN) TKit::Details::ThrowIfStopRequested(STOP_TOKEN)
#endif

namespace TKit
{
	namespace Details
	{
		inline void ThrowIfStopRequested(const std::stop_token& stopToken)
		{
			if (stopToken.stop_requested())
			{
				throw std::runtime_error("Task cancelled via stop_token");
			}
		}
	}

	inline Task<> GetCompletedTask()
	{
		co_return;
	}

	inline Task<> CreateTask(std::function<Task<>(std::stop_token)> func, std::stop_token stopToken = {})
	{
		TASKKIT_STOP_TOKEN_PROCESS(stopToken);
		co_await func(stopToken);
	}

	inline void RunTask(std::function<Task<>(std::stop_token)> func, std::stop_token stopToken = {})
	{
		TASKKIT_STOP_TOKEN_PROCESS(stopToken);
		func(stopToken).Forget();
	}

	inline Task<> DelayFrame(int frameCount, std::stop_token stopToken = {})
	{
		if (frameCount > 0)
		{
			while (frameCount-- > 0)
			{
				TASKKIT_STOP_TOKEN_PROCESS(stopToken);
				co_yield {};
			}
		}

		TASKKIT_STOP_TOKEN_PROCESS(stopToken);
		co_return;
	}

	template<typename Rep, typename Period>
	inline Task<> WaitFor(std::chrono::duration<Rep, Period> duration, std::stop_token stopToken = {})
	{
		const auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < duration)
		{
			TASKKIT_STOP_TOKEN_PROCESS(stopToken);
			co_yield {};
		}

		TASKKIT_STOP_TOKEN_PROCESS(stopToken);
		co_return;
	}

	template<typename Clock, typename Duration>
	inline Task<> WaitUntil(std::chrono::time_point<Clock, Duration> timePoint, std::stop_token stopToken = {})
	{
		while (Clock::now() < timePoint)
		{
			TASKKIT_STOP_TOKEN_PROCESS(stopToken);
			co_yield {};
		}

		TASKKIT_STOP_TOKEN_PROCESS(stopToken);
		co_return;
	}

	template<typename... Results>
	inline Task<std::tuple<Results...>> WhenAll(Task<Results>&&... tasks)
	{
		if constexpr (sizeof...(Results) == 0)
		{
			co_return std::tuple{};
		}
		else
		{
			co_return std::make_tuple(co_await std::move(tasks)...);
		}
	}

	template<typename... Results>
		requires (std::same_as<Results, void>&&...)
	inline Task<> WhenAll(Task<Results>&&... tasks)
	{
		if constexpr (sizeof...(tasks) == 0)
		{
			co_return;
		}
		else
		{
			(co_await std::move(tasks), ...);
			co_return;
		}
	}

	inline Task<> WhenAll(std::vector<Task<>> tasks)
	{
		if (tasks.empty())
		{
			co_return;
		}

		// Await each task sequentially, matching the behavior of the variadic template version
		// Tasks that are already ready will complete immediately via await_ready()
		for (auto& task : tasks)
		{
			co_await std::move(task);
		}

		co_return;
	}
}

#endif //TASKKIT_UTILITY_H