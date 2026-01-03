#ifndef TASKKIT_TASKKITTESTBASE_H
#define TASKKIT_TASKKITTESTBASE_H
#include <gtest/gtest.h>
#include "TaskKit.h"

namespace TKit::Tests
{
	using namespace std::chrono_literals;
	using TestClock = std::chrono::steady_clock;

	class TaskKitTestBase : public ::testing::Test
	{
	protected:
		void SetUp() override
		{
			auto& taskSystem = TaskSystem::GetInstance();
			schedulerId_ = taskSystem.CreateScheduler();
			registration_ = taskSystem.RegisterScheduler(schedulerId_);
		}

		void TearDown() override
		{
			auto& taskSystem = TaskSystem::GetInstance();
			auto& scheduler = taskSystem.GetScheduler(schedulerId_);

			EXPECT_EQ(scheduler.GetPendingTaskCount(), 0)
				<< "Test left " << scheduler.GetPendingTaskCount() << " pending tasks";

			registration_ = TaskSystem::SchedulerRegistration();
			taskSystem.DestroyScheduler(schedulerId_);
		}

		void RunScheduler(int frames = 1) const
		{
			auto& scheduler = TaskSystem::GetInstance().GetScheduler(schedulerId_);
			for (int i = 0; i < frames; ++i)
			{
				scheduler.Update();
			}
		}

		template<typename Clock, typename Rep, typename Period>
		void RunSchedulerFor(
			const std::chrono::time_point<Clock>& start,
			const std::chrono::duration<Rep, Period>& max,
			Clock::duration checkInterval = 10ms)const
		{
			auto elapsed = Clock::now() - start;
			while (elapsed < max)
			{
				std::this_thread::sleep_for(checkInterval);
				RunScheduler(1);
				elapsed = Clock::now() - start;
			}
		}

		template<typename Clock>
		void RunSchedulerUntil(
			const std::chrono::time_point<Clock>& target,
			Clock::duration checkInterval = 10ms) const
		{
			auto now = Clock::now();
			while (now < target)
			{
				std::this_thread::sleep_for(checkInterval);
				RunScheduler(1);
				now = Clock::now();
			}
		}

	private:
		std::size_t schedulerId_ = 0;
		TaskSystem::SchedulerRegistration registration_;
	};
}

#endif //TASKKIT_TASKKITTESTBASE_H