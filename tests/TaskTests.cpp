#include <chrono>
#include <memory>
#include <thread>
#include "TaskKitTestBase.h"

namespace TKit::Tests
{
	class TaskKitTest : public TaskKitTestBase
	{
	};

	TEST_F(TaskKitTest, SimpleTaskCompletion)
	{
		bool executed = false;

		auto task = [&]() -> Task<>
		{
			executed = true;
			co_return;
		};

		[[maybe_unused]] auto t = task();
		EXPECT_TRUE(executed);
	}

	TEST_F(TaskKitTest, TaskWithReturnValue)
	{
		auto valueTask = []() -> Task<int>
		{
			co_return 42;
		};

		int result = 0;

		auto task = [&]() -> Task<>
		{
			result = co_await valueTask();
			co_return;
		};

		task().Forget();

		EXPECT_EQ(result, 42);
	}

	TEST_F(TaskKitTest, ForgetTask)
	{
		int counter = 0;

		auto task = [&]() -> Task<>
		{
			counter++;
			co_yield {};
			counter++;
			co_yield {};
			counter++;
			co_return;
		};

		task().Forget();

		EXPECT_EQ(counter, 1);

		RunScheduler(1);
		EXPECT_EQ(counter, 2);

		RunScheduler(1);
		EXPECT_EQ(counter, 3);
	}

	TEST_F(TaskKitTest, ForgetTaskAutoDestroy)
	{
		static int destructorCalled = 0;

		struct TestObject
		{
			~TestObject() { destructorCalled++; }
		};

		auto task = []() -> Task<>
		{
			TestObject obj;
			co_yield {};
			co_return;
		};

		task().Forget();
		EXPECT_EQ(destructorCalled, 0);

		RunScheduler(1);
		EXPECT_EQ(destructorCalled, 1);
	}

	TEST_F(TaskKitTest, DelayFrameZero)
	{
		int counter = 0;

		auto task = [&]() -> Task<>
		{
			counter++;
			co_await DelayFrame(0);
			counter++;
			co_return;
		};

		task().Forget();
		EXPECT_EQ(counter, 2);
	}

	TEST_F(TaskKitTest, DelayFrameOne)
	{
		int counter = 0;

		auto task = [&]() -> Task<>
		{
			counter++;
			co_await DelayFrame(1);
			counter++;
			co_return;
		};

		task().Forget();
		EXPECT_EQ(counter, 1);

		RunScheduler(1);
		EXPECT_EQ(counter, 2);
	}

	TEST_F(TaskKitTest, DelayFrameMultiple)
	{
		int counter = 0;

		auto task = [&]() -> Task<>
		{
			counter++;
			co_await DelayFrame(3);
			counter++;
			co_return;
		};

		task().Forget();
		EXPECT_EQ(counter, 1);

		RunScheduler(1);
		EXPECT_EQ(counter, 1);

		RunScheduler(1);
		EXPECT_EQ(counter, 1);

		RunScheduler(1);
		EXPECT_EQ(counter, 2);
	}

	TEST_F(TaskKitTest, WaitForCompletion)
	{
		int counter = 0;
		auto task = [&]() -> Task<>
		{
			counter++;
			co_await WaitFor(100ms);
			counter++;
			co_return;
		};

		auto start = TestClock::now();

		task().Forget();
		EXPECT_EQ(counter, 1);

		RunSchedulerFor(start, 100ms);

		EXPECT_EQ(counter, 2);
	}

	TEST_F(TaskKitTest, WaitForMultipleTasks)
	{
		int counter1 = 0;
		int counter2 = 0;

		auto task1 = [&]() -> Task<>
		{
			counter1++;
			co_await WaitFor(50ms);
			counter1++;
			co_return;
		};

		auto task2 = [&]() -> Task<>
		{
			counter2++;
			co_await WaitFor(100ms);
			counter2++;
			co_return;
		};

		auto start = TestClock::now();

		task1().Forget();
		task2().Forget();

		EXPECT_EQ(counter1, 1);
		EXPECT_EQ(counter2, 1);

		RunSchedulerFor(start, 75ms);

		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 1);

		RunSchedulerFor(start, 100ms);

		EXPECT_EQ(counter1, 2);
		EXPECT_EQ(counter2, 2);
	}

	TEST_F(TaskKitTest, TaskChaining)
	{
		int counter = 0;

		auto innerTask = [&]() -> Task<>
		{
			counter++;
			co_yield {};
			counter++;
			co_return;
		};

		auto outerTask = [&, innerTask]() -> Task<>
		{
			counter++;
			co_await innerTask();
			counter++;
			co_return;
		};

		outerTask().Forget();
		EXPECT_EQ(counter, 2);

		RunScheduler(1);
		EXPECT_EQ(counter, 4);
	}

	TEST_F(TaskKitTest, TaskChainingWithReturnValue)
	{
		auto innerTask = []() -> Task<int>
		{
			co_return 42;
		};

		int result = 0;
		auto outerTask = [&, innerTask]() -> Task<>
		{
			result = co_await innerTask();
			co_return;
		};

		outerTask().Forget();
		EXPECT_EQ(result, 42);
	}

	TEST_F(TaskKitTest, TaskWithException)
	{
		auto throwTask = []() -> Task<>
		{
			co_yield {};
			throw std::runtime_error("Test exception");
		};

		auto task = [&]() -> Task<>
		{
			EXPECT_THROW(co_await throwTask(), std::runtime_error);
		};

		EXPECT_NO_THROW(throwTask().Forget());
		EXPECT_NO_THROW(RunScheduler(1));

		task().Forget();
		EXPECT_NO_THROW(RunScheduler(1));
	}

	TEST_F(TaskKitTest, MultipleYields)
	{
		int counter = 0;

		auto task = [&]() -> Task<>
		{
			for (int i = 0; i < 5; ++i)
			{
				counter++;
				co_yield {};
			}
			co_return;
		};

		task().Forget();
		EXPECT_EQ(counter, 1);

		for (int i = 1; i < 5; ++i)
		{
			RunScheduler(1);
			EXPECT_EQ(counter, i + 1);
		}

		RunScheduler(1);
	}

	TEST_F(TaskKitTest, WaitUntilFutureTime)
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

	TEST_F(TaskKitTest, WaitUntilPastTime)
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

	TEST_F(TaskKitTest, WhenAllBasic)
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

	TEST_F(TaskKitTest, WhenAllSingleTask)
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

	TEST_F(TaskKitTest, WhenAllWithReturnValues)
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

	TEST_F(TaskKitTest, WhenAllMixedDurations)
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

	TEST_F(TaskKitTest, WhenAllVectorEmpty)
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

	TEST_F(TaskKitTest, WhenAllVectorSingle)
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

	TEST_F(TaskKitTest, WhenAllVectorMultiple)
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

	TEST_F(TaskKitTest, WhenAllAllImmediateCompletion)
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

	TEST_F(TaskKitTest, WhenAnyBasic)
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

	TEST_F(TaskKitTest, WhenAnyWithDifferentDelays)
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

	TEST_F(TaskKitTest, WhenAnyAllVoid)
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

	TEST_F(TaskKitTest, WhenAnyMixedTypes)
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

	TEST_F(TaskKitTest, WhenAnyRaceCondition)
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
