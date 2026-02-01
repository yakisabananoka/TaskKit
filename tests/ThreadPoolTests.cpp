#include <gtest/gtest.h>
#include <atomic>
#include <latch>
#include <thread>
#include <vector>
#include "details/ThreadPool.h"
#include "details/TaskSchedulerManager.h"

namespace TKit::Tests
{
	class ThreadPoolTests : public ::testing::Test
	{
	protected:
		struct Task
		{
			struct promise_type
			{
				Task get_return_object() { return Task{std::coroutine_handle<promise_type>::from_promise(*this)}; }
				std::suspend_always initial_suspend() noexcept { return {}; }
				std::suspend_never final_suspend() noexcept { return {}; }
				void return_void() {}
				void unhandled_exception() {}
			};

			std::coroutine_handle<promise_type> handle;
		};

		void SetUp() override
		{
			schedulerManager_ = std::make_unique<TaskSchedulerManager>();
		}

		void TearDown() override
		{
			schedulerManager_.reset();
		}

		std::unique_ptr<TaskSchedulerManager> schedulerManager_;
	};

	TEST_F(ThreadPoolTests, Construction)
	{
		ThreadPool pool(*schedulerManager_, 4);
		EXPECT_EQ(pool.GetWorkerCount(), 4);
	}

	TEST_F(ThreadPoolTests, ScheduleSimpleTask)
	{
		ThreadPool pool(*schedulerManager_, 2);

		std::latch latch{1};

		auto makeTask = [](std::latch* l) -> Task
		{
			l->count_down();
			co_return;
		};

		auto task = makeTask(&latch);
		pool.Schedule(task.handle);

		latch.wait();
	}

	TEST_F(ThreadPoolTests, ScheduleMultipleTasks)
	{
		ThreadPool pool(*schedulerManager_, 4);

		constexpr int taskCount = 100;
		std::latch latch{taskCount};

		auto makeTask = [](std::latch* l) -> Task
		{
			l->count_down();
			co_return;
		};

		std::vector<Task> tasks;
		tasks.reserve(taskCount);

		for (int i = 0; i < taskCount; ++i)
		{
			tasks.push_back(makeTask(&latch));
			pool.Schedule(tasks.back().handle);
		}

		latch.wait();
	}

	TEST_F(ThreadPoolTests, ScheduleToSpecificWorker)
	{
		ThreadPool pool(*schedulerManager_, 4);

		constexpr int tasksPerWorker = 10;
		constexpr int totalTasks = tasksPerWorker * 4;
		std::latch latch{totalTasks};

		std::atomic<int> successCount{0};

		auto makeTask = [](std::thread::id expectedId, std::atomic<int>* success, std::latch* l) -> Task
		{
			if (std::this_thread::get_id() == expectedId)
			{
				success->fetch_add(1, std::memory_order_relaxed);
			}
			l->count_down();
			co_return;
		};

		for (std::size_t worker = 0; worker < 4; ++worker)
		{
			auto expectedThreadId = pool.GetSchedulerId(worker).GetThreadId();

			for (int i = 0; i < tasksPerWorker; ++i)
			{
				auto task = makeTask(expectedThreadId, &successCount, &latch);
				pool.Schedule(worker, task.handle);
			}
		}

		latch.wait();

		EXPECT_EQ(successCount.load(), totalTasks);
	}

	TEST_F(ThreadPoolTests, GetSchedulerId)
	{
		ThreadPool pool(*schedulerManager_, 3);

		for (std::size_t i = 0; i < 3; ++i)
		{
			auto id = pool.GetSchedulerId(i);
			EXPECT_NE(id.GetThreadId(), std::thread::id{});
		}
	}

	TEST_F(ThreadPoolTests, RoundRobinDistribution)
	{
		ThreadPool pool(*schedulerManager_, 4);

		constexpr int tasksPerWorker = 25;
		constexpr int totalTasks = tasksPerWorker * 4;

		std::latch latch{totalTasks};

		auto makeTask = [](std::latch* l) -> Task
		{
			l->count_down();
			co_return;
		};

		std::vector<Task> tasks;
		tasks.reserve(totalTasks);

		for (int i = 0; i < totalTasks; ++i)
		{
			tasks.push_back(makeTask(&latch));
			pool.Schedule(tasks.back().handle);
		}

		latch.wait();
	}

	TEST_F(ThreadPoolTests, DestructorWaitsForTasks)
	{
		std::atomic<int> counter{0};

		{
			ThreadPool pool(*schedulerManager_, 2);

			constexpr int taskCount = 10;
			std::latch latch{taskCount};

			auto makeTask = [](std::atomic<int>* c, std::latch* l) -> Task
			{
				c->fetch_add(1, std::memory_order_relaxed);
				l->count_down();
				co_return;
			};

			for (int i = 0; i < taskCount; ++i)
			{
				auto task = makeTask(&counter, &latch);
				pool.Schedule(task.handle);
			}

			latch.wait();
		}

		EXPECT_EQ(counter.load(), 10);
	}

	TEST_F(ThreadPoolTests, StressTest)
	{
		ThreadPool pool(*schedulerManager_, 8);

		constexpr int taskCount = 10000;
		std::latch latch{taskCount};

		auto makeTask = [](std::latch* l) -> Task
		{
			l->count_down();
			co_return;
		};

		std::vector<Task> tasks;
		tasks.reserve(taskCount);

		for (int i = 0; i < taskCount; ++i)
		{
			tasks.push_back(makeTask(&latch));
			pool.Schedule(tasks.back().handle);
		}

		latch.wait();
	}

	TEST_F(ThreadPoolTests, ConcurrentScheduling)
	{
		ThreadPool pool(*schedulerManager_, 4);

		constexpr int tasksPerThread = 100;
		constexpr int numSchedulingThreads = 4;
		constexpr int totalTasks = tasksPerThread * numSchedulingThreads;

		std::latch latch{totalTasks};

		auto makeTask = [](std::latch* l) -> Task
		{
			l->count_down();
			co_return;
		};

		std::vector<std::thread> schedulingThreads;
		schedulingThreads.reserve(numSchedulingThreads);

		for (int t = 0; t < numSchedulingThreads; ++t)
		{
			schedulingThreads.emplace_back([&pool, &makeTask, &latch]()
			{
				std::vector<Task> tasks;
				tasks.reserve(tasksPerThread);

				for (int i = 0; i < tasksPerThread; ++i)
				{
					tasks.push_back(makeTask(&latch));
					pool.Schedule(tasks.back().handle);
				}
			});
		}

		for (auto& t : schedulingThreads)
		{
			t.join();
		}

		latch.wait();
	}
}
