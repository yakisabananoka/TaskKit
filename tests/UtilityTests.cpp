#include "TestBase.h"

namespace TKit::Tests
{
	class UtilityTests : public TestBase
	{
	};

	TEST_F(UtilityTests, WaitUntilFutureTime)
	{
		int counter = 0;
		auto targetTime = TestClock::now() + 100ms;

		auto task = [&, targetTime]() -> Task<>
		{
			counter++;
			co_await WaitUntil(targetTime);
			counter++;
			co_return;
		};

		task().Forget();
		EXPECT_EQ(counter, 1);

		RunSchedulerUntil(targetTime);

		EXPECT_EQ(counter, 2);
	}

	TEST_F(UtilityTests, WaitUntilPastTime)
	{
		int counter = 0;
		auto targetTime = TestClock::now() - 100ms;

		auto task = [&, targetTime]() -> Task<>
		{
			counter++;
			co_await WaitUntil(targetTime);
			counter++;
			co_return;
		};

		task().Forget();
		EXPECT_EQ(counter, 2);
	}

	TEST_F(UtilityTests, WhenAllBasic)
	{
		int counter1 = 0;
		int counter2 = 0;
		int counter3 = 0;
		bool completed = false;

		auto task1 = [&]() -> Task<>
		{
			counter1++;
			co_yield {};
			counter1++;
			co_return;
		};

		auto task2 = [&]() -> Task<>
		{
			counter2++;
			co_yield {};
			co_yield {};
			counter2++;
			co_return;
		};

		auto task3 = [&]() -> Task<>
		{
			counter3++;
			co_return;
		};

		auto allTask = [&]() -> Task<>
		{
			co_await WhenAll(task1(), task2(), task3());
			completed = true;
			co_return;
		};

		allTask().Forget();
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);
		EXPECT_FALSE(completed);

		RunScheduler(1);
		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);
		EXPECT_FALSE(completed);

		RunScheduler(1);
		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 2);
		EXPECT_EQ(counter3, 1);
		EXPECT_TRUE(completed);
	}

	TEST_F(UtilityTests, WhenAllSingleTask)
	{
		int counter = 0;

		auto task = [&]() -> Task<>
		{
			counter++;
			co_yield {};
			counter++;
			co_return;
		};

		auto allTask = [&]() -> Task<>
		{
			co_await WhenAll(task());
			counter++;
			co_return;
		};

		allTask().Forget();
		EXPECT_EQ(counter, 1);

		RunScheduler(1);
		EXPECT_EQ(counter, 3);
	}

	TEST_F(UtilityTests, WhenAllWithReturnValues)
	{
		auto task1 = []() -> Task<int>
		{
			co_yield {};
			co_return 42;
		};

		auto task2 = []() -> Task<std::string>
		{
			co_yield {};
			co_yield {};
			co_return std::string("Hello");
		};

		auto task3 = []() -> Task<double>
		{
			co_return 3.14;
		};

		std::tuple<int, std::string, double> result;

		auto allTask = [&]() -> Task<>
		{
			result = co_await WhenAll(task1(), task2(), task3());
			co_return;
		};

		allTask().Forget();

		RunScheduler(2);
		EXPECT_EQ(std::get<0>(result), 42);
		EXPECT_EQ(std::get<1>(result), "Hello");
		EXPECT_DOUBLE_EQ(std::get<2>(result), 3.14);
	}

	TEST_F(UtilityTests, WhenAllMixedDurations)
	{
		int counter1 = 0;
		int counter2 = 0;
		int counter3 = 0;
		bool completed = false;

		auto task1 = [&]() -> Task<>
		{
			counter1++;
			co_return;
		};

		auto task2 = [&]() -> Task<>
		{
			counter2++;
			co_yield {};
			counter2++;
			co_return;
		};

		auto task3 = [&]() -> Task<>
		{
			counter3++;
			co_yield {};
			co_yield {};
			co_yield {};
			counter3++;
			co_return;
		};

		auto allTask = [&]() -> Task<>
		{
			co_await WhenAll(task1(), task2(), task3());
			completed = true;
			co_return;
		};

		allTask().Forget();
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);
		EXPECT_FALSE(completed);

		RunScheduler(1);
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 2);
		EXPECT_EQ(counter3, 1);
		EXPECT_FALSE(completed);

		RunScheduler(1);
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 2);
		EXPECT_EQ(counter3, 1);
		EXPECT_FALSE(completed);

		RunScheduler(1);
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 2);
		EXPECT_EQ(counter3, 2);
		EXPECT_TRUE(completed);
	}

	TEST_F(UtilityTests, WhenAllVectorEmpty)
	{
		bool completed = false;

		auto allTask = [&]() -> Task<>
		{
			std::vector<Task<>> tasks;
			co_await WhenAll(std::move(tasks));
			completed = true;
			co_return;
		};

		allTask().Forget();
		EXPECT_TRUE(completed);
	}

	TEST_F(UtilityTests, WhenAllVectorSingle)
	{
		int counter = 0;

		auto task = [&]() -> Task<>
		{
			counter++;
			co_yield {};
			counter++;
			co_return;
		};

		auto allTask = [&]() -> Task<>
		{
			std::vector<Task<>> tasks;
			tasks.push_back(task());

			co_await WhenAll(std::move(tasks));
			counter++;
			co_return;
		};

		allTask().Forget();
		EXPECT_EQ(counter, 1);

		RunScheduler(1);
		EXPECT_EQ(counter, 3);
	}

	TEST_F(UtilityTests, WhenAllVectorMultiple)
	{
		int counter1 = 0;
		int counter2 = 0;
		int counter3 = 0;
		bool completed = false;

		auto task1 = [&]() -> Task<>
		{
			counter1++;
			co_yield {};
			counter1++;
			co_return;
		};

		auto task2 = [&]() -> Task<>
		{
			counter2++;
			co_yield {};
			co_yield {};
			counter2++;
			co_return;
		};

		auto task3 = [&]() -> Task<>
		{
			counter3++;
			co_return;
		};

		auto allTask = [&]() -> Task<>
		{
			std::vector<Task<>> tasks;
			tasks.push_back(task1());
			tasks.push_back(task2());
			tasks.push_back(task3());

			co_await WhenAll(std::move(tasks));
			completed = true;
			co_return;
		};

		allTask().Forget();
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);
		EXPECT_FALSE(completed);

		RunScheduler(1);
		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);
		EXPECT_FALSE(completed);

		RunScheduler(1);
		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 2);
		EXPECT_EQ(counter3, 1);
		EXPECT_TRUE(completed);
	}

	TEST_F(UtilityTests, WhenAllAllImmediateCompletion)
	{
		int counter1 = 0;
		int counter2 = 0;
		int counter3 = 0;
		bool completed = false;

		auto task1 = [&]() -> Task<>
		{
			counter1++;
			co_return;
		};

		auto task2 = [&]() -> Task<>
		{
			counter2++;
			co_return;
		};

		auto task3 = [&]() -> Task<>
		{
			counter3++;
			co_return;
		};

		auto allTask = [&]() -> Task<>
		{
			co_await WhenAll(task1(), task2(), task3());
			completed = true;
			co_return;
		};

		allTask().Forget();
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);
		EXPECT_TRUE(completed);
	}

	TEST_F(UtilityTests, WhenAnyBasic)
	{
		int counter1 = 0;
		int counter2 = 0;
		int counter3 = 0;

		auto task1 = [&]() -> Task<int>
		{
			counter1++;
			co_return 10;
		};

		auto task2 = [&]() -> Task<int>
		{
			counter2++;
			co_yield {};
			counter2++;
			co_return 20;
		};

		auto task3 = [&]() -> Task<int>
		{
			counter3++;
			co_yield {};
			co_yield {};
			counter3++;
			co_return 30;
		};

		std::variant<int, int, int> result;

		auto anyTask = [&]() -> Task<>
		{
			result = co_await WhenAny(task1(), task2(), task3());
			co_return;
		};

		anyTask().Forget();

		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);

		RunScheduler(1);
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 2);
		EXPECT_EQ(counter3, 1);

		EXPECT_EQ(result.index(), 0);
		EXPECT_EQ(std::get<0>(result), 10);

		RunScheduler(1);
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 2);
		EXPECT_EQ(counter3, 2);
	}

	TEST_F(UtilityTests, WhenAnyWithDifferentDelays)
	{
		int counter1 = 0;
		int counter2 = 0;

		auto task1 = [&]() -> Task<std::string>
		{
			counter1++;
			co_await DelayFrame(3);
			counter1++;
			co_return std::string("First");
		};

		auto task2 = [&]() -> Task<std::string>
		{
			counter2++;
			co_await DelayFrame(1);
			counter2++;
			co_return std::string("Second");
		};

		std::variant<std::string, std::string> result;

		auto anyTask = [&]() -> Task<>
		{
			result = co_await WhenAny(task1(), task2());
			co_return;
		};

		anyTask().Forget();
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);

		RunScheduler(1);
		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 2);

		EXPECT_EQ(result.index(), 1);
		EXPECT_EQ(std::get<1>(result), "Second");

		RunScheduler(2);
		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 2);
	}

	TEST_F(UtilityTests, WhenAnyAllVoid)
	{
		int counter1 = 0;
		int counter2 = 0;
		int counter3 = 0;

		auto task1 = [&]() -> Task<>
		{
			counter1++;
			co_yield {};
			counter1++;
			co_return;
		};

		auto task2 = [&]() -> Task<>
		{
			counter2++;
			co_return;
		};

		auto task3 = [&]() -> Task<>
		{
			counter3++;
			co_yield {};
			co_yield {};
			counter3++;
			co_return;
		};

		std::size_t resultIndex = 999;

		auto anyTask = [&]() -> Task<>
		{
			resultIndex = co_await WhenAny(task1(), task2(), task3());
			co_return;
		};

		anyTask().Forget();

		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);

		RunScheduler(1);
		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);

		EXPECT_EQ(resultIndex, 1);

		RunScheduler(1);
		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 2);
	}

	TEST_F(UtilityTests, WhenAnyMixedTypes)
	{
		auto task1 = [&]() -> Task<int>
		{
			co_yield {};
			co_return 42;
		};

		auto task2 = [&]() -> Task<std::string>
		{
			co_return std::string("Hello");
		};

		auto task3 = [&]() -> Task<double>
		{
			co_yield {};
			co_yield {};
			co_return 3.14;
		};

		std::variant<int, std::string, double> result;

		auto anyTask = [&]() -> Task<>
		{
			result = co_await WhenAny(task1(), task2(), task3());
			co_return;
		};

		anyTask().Forget();

		EXPECT_EQ(result.index(), 1);
		EXPECT_EQ(std::get<1>(result), "Hello");

		RunScheduler(2);
	}

	TEST_F(UtilityTests, WhenAnyRaceCondition)
	{
		int counter1 = 0;
		int counter2 = 0;
		int counter3 = 0;

		auto task1 = [&]() -> Task<int>
		{
			counter1++;
			co_yield {};
			co_return 1;
		};

		auto task2 = [&]() -> Task<int>
		{
			counter2++;
			co_yield {};
			co_return 2;
		};

		auto task3 = [&]() -> Task<int>
		{
			counter3++;
			co_yield {};
			co_return 3;
		};

		std::variant<int, int, int> result;

		auto anyTask = [&]() -> Task<>
		{
			result = co_await WhenAny(task1(), task2(), task3());
			co_return;
		};

		anyTask().Forget();

		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);
		EXPECT_EQ(counter3, 1);

		RunScheduler(1);

		EXPECT_EQ(result.index(), 0);
		EXPECT_EQ(std::get<0>(result), 1);
	}
}
