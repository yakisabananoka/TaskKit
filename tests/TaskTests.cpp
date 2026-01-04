#include "TestBase.h"

namespace TKit::Tests
{
	class TaskTests : public TestBase
	{
	};

	TEST_F(TaskTests, SimpleTaskCompletion)
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

	TEST_F(TaskTests, TaskWithReturnValue)
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

	TEST_F(TaskTests, ForgetTask)
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

	TEST_F(TaskTests, ForgetTaskAutoDestroy)
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

	TEST_F(TaskTests, DelayFrameZero)
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

	TEST_F(TaskTests, DelayFrameOne)
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

	TEST_F(TaskTests, DelayFrameMultiple)
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

	TEST_F(TaskTests, WaitForCompletion)
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

	TEST_F(TaskTests, WaitForMultipleTasks)
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

	TEST_F(TaskTests, TaskChaining)
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

	TEST_F(TaskTests, TaskChainingWithReturnValue)
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

	TEST_F(TaskTests, TaskWithException)
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

	TEST_F(TaskTests, MultipleYields)
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
}
