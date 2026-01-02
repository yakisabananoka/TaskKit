#include <gtest/gtest.h>
#include "../include/TaskKit.h"
#include <chrono>
#include <memory>
#include <thread>

using namespace TKit;

class TaskKitTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		auto& taskSystem = TaskSystem::GetInstance();
		schedulerId_ = taskSystem.CreateScheduler();
		// Register scheduler so tasks created in this test use it
		registration_ = taskSystem.RegisterScheduler(schedulerId_);
	}

	void TearDown() override
	{
		auto& taskSystem = TaskSystem::GetInstance();
		auto& scheduler = taskSystem.GetScheduler(schedulerId_);

		// Run scheduler to clean up any remaining tasks
		for (int i = 0; i < 100; ++i)
		{
			scheduler.Update();
			if (scheduler.GetPendingTaskCount() == 0)
			{
				break;
			}
		}

		// Verify no tasks are left pending
		EXPECT_EQ(scheduler.GetPendingTaskCount(), 0)
			<< "Test left " << scheduler.GetPendingTaskCount() << " pending tasks";

		registration_ = TaskSystem::SchedulerRegistration();
		taskSystem.DestroyScheduler(schedulerId_);
	}

	void RunScheduler(int frames = 1)
	{
		auto& scheduler = TaskSystem::GetInstance().GetScheduler(schedulerId_);
		for (int i = 0; i < frames; ++i)
		{
			scheduler.Update();
		}
	}

private:
	std::size_t schedulerId_ = 0;
	TaskSystem::SchedulerRegistration registration_;
};

// Basic Task Tests
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
	auto task = []() -> Task<int>
	{
		co_return 42;
	};

	[[maybe_unused]] auto t = task();
	// Task is executed immediately due to initial_suspend returning suspend_never
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

	EXPECT_EQ(counter, 1); // initial_suspend is suspend_never, so starts immediately

	RunScheduler(1);
	EXPECT_EQ(counter, 2);

	RunScheduler(1);
	EXPECT_EQ(counter, 3);
}

TEST_F(TaskKitTest, ForgetTaskAutoDestroy)
{
	static int destructorCalled = 0;
	destructorCalled = 0;

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
	// After task completes, it should auto-destroy and call destructor
	EXPECT_EQ(destructorCalled, 1);
}

// DelayFrame Tests
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
	EXPECT_EQ(counter, 2); // DelayFrame(0) should complete immediately
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

// WaitFor Tests
TEST_F(TaskKitTest, WaitForCompletion)
{
	using namespace std::chrono;

	int counter = 0;
	auto start = steady_clock::now();

	auto task = [&]() -> Task<>
	{
		counter++;
		co_await WaitFor(milliseconds(100));
		counter++;
		co_return;
	};

	task().Forget();
	EXPECT_EQ(counter, 1);

	// Run for a duration longer than wait time
	auto elapsed = steady_clock::now() - start;
	while (elapsed < milliseconds(150))
	{
		RunScheduler(1);
		std::this_thread::sleep_for(milliseconds(10));
		elapsed = steady_clock::now() - start;
	}

	EXPECT_EQ(counter, 2);
}

TEST_F(TaskKitTest, WaitForMultipleTasks)
{
	using namespace std::chrono;

	int counter1 = 0;
	int counter2 = 0;

	auto task1 = [&]() -> Task<>
	{
		counter1++;
		co_await WaitFor(milliseconds(50));
		counter1++;
		co_return;
	};

	auto task2 = [&]() -> Task<>
	{
		counter2++;
		co_await WaitFor(milliseconds(100));
		counter2++;
		co_return;
	};

	task1().Forget();
	task2().Forget();

	EXPECT_EQ(counter1, 1);
	EXPECT_EQ(counter2, 1);

	auto start = steady_clock::now();
	auto elapsed = steady_clock::now() - start;

	// Wait for first task to complete
	while (elapsed < milliseconds(75))
	{
		RunScheduler(1);
		std::this_thread::sleep_for(milliseconds(10));
		elapsed = steady_clock::now() - start;
	}

	EXPECT_EQ(counter1, 2);
	EXPECT_EQ(counter2, 1);

	// Wait for second task to complete
	while (elapsed < milliseconds(125))
	{
		RunScheduler(1);
		std::this_thread::sleep_for(milliseconds(10));
		elapsed = steady_clock::now() - start;
	}

	EXPECT_EQ(counter1, 2);
	EXPECT_EQ(counter2, 2);
}

// Task Chaining Tests
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
	EXPECT_EQ(counter, 2); // outer starts, inner starts

	RunScheduler(1);
	EXPECT_EQ(counter, 4); // inner continues and completes, outer continues immediately
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

// Exception Handling Tests
TEST_F(TaskKitTest, TaskWithException)
{
	auto task = []() -> Task<>
	{
		co_yield {};
		throw std::runtime_error("Test exception");
	};

	EXPECT_NO_THROW(task().Forget());

	// Exception should be caught by unhandled_exception
	EXPECT_NO_THROW(RunScheduler(1));
}

// Multiple Sequential Yields
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
}

// WaitUntil Tests
TEST_F(TaskKitTest, WaitUntilFutureTime)
{
	using namespace std::chrono;

	int counter = 0;
	auto targetTime = steady_clock::now() + milliseconds(100);

	auto task = [&, targetTime]() -> Task<>
	{
		counter++;
		co_await WaitUntil(targetTime);
		counter++;
		co_return;
	};

	task().Forget();
	EXPECT_EQ(counter, 1);

	// Run until target time is reached
	while (steady_clock::now() < targetTime + milliseconds(50))
	{
		RunScheduler(1);
		std::this_thread::sleep_for(milliseconds(10));
	}

	EXPECT_EQ(counter, 2);
}

TEST_F(TaskKitTest, WaitUntilPastTime)
{
	using namespace std::chrono;

	int counter = 0;
	auto targetTime = steady_clock::now() - milliseconds(100); // Past time

	auto task = [&, targetTime]() -> Task<>
	{
		counter++;
		co_await WaitUntil(targetTime);
		counter++;
		co_return;
	};

	task().Forget();
	EXPECT_EQ(counter, 2); // Should complete immediately
}

// WhenAll Tests
TEST_F(TaskKitTest, WhenAllBasic)
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
		co_return;
	};

	allTask().Forget();
	EXPECT_EQ(counter1, 1);
	EXPECT_EQ(counter2, 1);
	EXPECT_EQ(counter3, 1);

	RunScheduler(1);
	EXPECT_EQ(counter1, 2);
	EXPECT_EQ(counter2, 1);
	EXPECT_EQ(counter3, 1);

	RunScheduler(1);
	EXPECT_EQ(counter1, 2);
	EXPECT_EQ(counter2, 2);
	EXPECT_EQ(counter3, 1);
}

TEST_F(TaskKitTest, WhenAllEmpty)
{
	bool completed = false;

	auto allTask = [&]() -> Task<>
	{
		co_await WhenAll();
		completed = true;
		co_return;
	};

	allTask().Forget();
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

	auto task1 = [&]() -> Task<>
	{
		counter1++;
		co_return; // Completes immediately
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

	bool completed = false;

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
	EXPECT_EQ(counter2, 2);
	EXPECT_EQ(counter3, 1);
	EXPECT_FALSE(completed);

	RunScheduler(1);
	EXPECT_EQ(counter3, 1);
	EXPECT_FALSE(completed);

	RunScheduler(1);
	EXPECT_EQ(counter3, 2);
	EXPECT_TRUE(completed); // All tasks completed
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

	auto allTask = [&]() -> Task<>
	{
		std::vector<Task<>> tasks;
		tasks.push_back([&]() -> Task<>
		{
			counter++;
			co_yield {};
			counter++;
			co_return;
		}());

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

	auto allTask = [&]() -> Task<>
	{
		std::vector<Task<>> tasks;

		tasks.push_back([&]() -> Task<>
		{
			counter1++;
			co_yield {};
			counter1++;
			co_return;
		}());

		tasks.push_back([&]() -> Task<>
		{
			counter2++;
			co_yield {};
			co_yield {};
			counter2++;
			co_return;
		}());

		tasks.push_back([&]() -> Task<>
		{
			counter3++;
			co_return;
		}());

		co_await WhenAll(std::move(tasks));
		co_return;
	};

	allTask().Forget();
	EXPECT_EQ(counter1, 1);
	EXPECT_EQ(counter2, 1);
	EXPECT_EQ(counter3, 1);

	RunScheduler(1);
	EXPECT_EQ(counter1, 2);
	EXPECT_EQ(counter2, 1);

	RunScheduler(1);
	EXPECT_EQ(counter2, 2);
}

TEST_F(TaskKitTest, WhenAllAllImmediateCompletion)
{
	int counter1 = 0;
	int counter2 = 0;
	int counter3 = 0;

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

	bool completed = false;

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
	EXPECT_TRUE(completed); // All tasks completed immediately
}

