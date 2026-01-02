#ifndef TASKKIT_UTILITY_H
#define TASKKIT_UTILITY_H

#include "Task.h"

namespace TKit
{
	inline Task<> GetCompletedTask()
	{
		co_return;
	}

	inline Task<> DelayFrame(int frameCount)
	{
		if (frameCount > 0)
		{
			while (frameCount-- > 0)
			{
				co_yield {};
			}
		}

		co_return;
	}

	template<typename Rep, typename Period>
	inline Task<> WaitFor(std::chrono::duration<Rep, Period> duration)
	{
		const auto start = std::chrono::steady_clock::now();
		while (std::chrono::steady_clock::now() - start < duration)
		{
			co_yield {};
		}
		co_return;
	}

	template<typename Clock, typename Duration>
	inline Task<> WaitUntil(std::chrono::time_point<Clock, Duration> timePoint)
	{
		while (Clock::now() < timePoint)
		{
			co_yield {};
		}
		co_return;
	}

}

#endif //TASKKIT_UTILITY_H