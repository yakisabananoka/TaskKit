#ifndef TASKKIT_TEST_BASE_H
#define TASKKIT_TEST_BASE_H
#include <gtest/gtest.h>
#include <optional>
#include "TaskKit.h"

namespace TKit::Tests
{
	using namespace std::chrono_literals;
	using TestClock = std::chrono::steady_clock;

	class TestBase : public ::testing::Test
	{
	protected:

		void SetUp() override
		{
			taskSystem_.emplace();
			scheduler_ = taskSystem_->CreateScheduler();
			registration_ = scheduler_.Activate();
		}

		void TearDown() override
		{
			const auto pendingCount = scheduler_.GetPendingTaskCount();

			EXPECT_EQ(pendingCount, 0)
				<< "Test left " << pendingCount << " pending tasks";

			registration_ = SchedulerActivation{};
			taskSystem_.reset();
		}

		void RunScheduler(int frames = 1) const
		{
			for (int i = 0; i < frames; ++i)
			{
				scheduler_.Update();
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

		[[nodiscard]]
		SchedulerHandle GetScheduler() const
		{
			return scheduler_;
		}

	private:
		std::optional<TaskSystem> taskSystem_;
		SchedulerHandle scheduler_;
		SchedulerActivation registration_;
	};
}

#endif //TASKKIT_TEST_BASE_H