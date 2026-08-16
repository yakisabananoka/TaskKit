#ifndef TASKKIT_THREAD_POOL_H
#define TASKKIT_THREAD_POOL_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <latch>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "SchedulerHandle.h"
#include "TaskScheduler.h"
#include "TaskSchedulerManager.h"

namespace TKit
{
	// A fixed set of worker threads, each driving its own TaskScheduler.
	//
	// Workers sleep on a condition variable while idle. When an update leaves
	// frame-waiting coroutines behind (co_yield / WaitFor running on a worker),
	// the worker paces itself with `frameInterval` between updates instead of
	// spinning at 100% CPU.
	//
	// Destruction stops the workers promptly: tasks still queued or parked on a
	// worker scheduler are NOT run to completion — they are destroyed with the
	// scheduler. Drain the pool first if completion matters.
	class ThreadPool final
	{
		struct WorkerContext
		{
			SchedulerHandle scheduler;
			std::mutex mutex;
			std::condition_variable cv;
		};

	public:
		ThreadPool(
			TaskSchedulerManager& schedulerManager,
			std::size_t threadCount,
			std::size_t reservedTaskCount = DefaultReservedTaskCount,
			std::chrono::nanoseconds frameInterval = std::chrono::milliseconds(1)
		) :
			schedulerManager_(&schedulerManager),
			frameInterval_(frameInterval),
			running_(true)
		{
			assert(threadCount > 0 && "ThreadPool: threadCount must be at least 1");

			workerContexts_.reserve(threadCount);
			for (std::size_t i = 0; i < threadCount; ++i)
			{
				workerContexts_.push_back(std::make_unique<WorkerContext>());
			}

			// Each worker registers its own scheduler (CreateScheduler is
			// thread-safe); the constructor only returns once every worker did,
			// so GetScheduler()/Schedule() are usable immediately.
			std::latch schedulersReady(static_cast<std::ptrdiff_t>(threadCount));
			std::mutex initExceptionMutex;
			std::exception_ptr initException;

			try
			{
				workers_.reserve(threadCount);
				for (std::size_t i = 0; i < threadCount; ++i)
				{
					workers_.emplace_back([this, i, reservedTaskCount, &schedulersReady, &initExceptionMutex, &initException]()
					{
						auto& context = *workerContexts_[i];
						try
						{
							context.scheduler = schedulerManager_->CreateScheduler(std::this_thread::get_id(), reservedTaskCount);
						}
						catch (...)
						{
							{
								std::lock_guard lock(initExceptionMutex);
								if (!initException)
								{
									initException = std::current_exception();
								}
							}
							schedulersReady.count_down();
							return;
						}
						schedulersReady.count_down();
						WorkerMain(context);
					});
				}
			}
			catch (...)
			{
				// Thread creation failed part-way: stop and join the workers that
				// did start before the latch and contexts leave scope.
				StopWorkers();
				throw;
			}

			schedulersReady.wait();

			// The latch orders every count_down before this read.
			if (initException)
			{
				StopWorkers();
				std::rethrow_exception(initException);
			}
		}

		~ThreadPool()
		{
			StopWorkers();
		}

		void Schedule(std::coroutine_handle<> handle)
		{
			const std::size_t index = nextWorker_.fetch_add(1, std::memory_order_relaxed) % workerContexts_.size();
			Schedule(index, handle);
		}

		void Schedule(std::size_t workerIndex, std::coroutine_handle<> handle)
		{
			assert(workerIndex < workerContexts_.size() && "ThreadPool: invalid worker index");
			auto& context = *workerContexts_[workerIndex];

			context.scheduler.Schedule(handle);

			// The empty critical section pairs with the predicate check the worker
			// performs under the same mutex, so the wakeup cannot be lost;
			// notifying after the unlock avoids waking the worker straight into a
			// held mutex.
			{
				std::lock_guard lock(context.mutex);
			}
			context.cv.notify_one();
		}

		[[nodiscard]]
		std::size_t GetWorkerCount() const noexcept
		{
			return workers_.size();
		}

		[[nodiscard]]
		SchedulerHandle GetScheduler(std::size_t workerIndex) const
		{
			assert(workerIndex < workerContexts_.size() && "ThreadPool: invalid worker index");
			return workerContexts_[workerIndex]->scheduler;
		}

		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = delete;
		ThreadPool& operator=(ThreadPool&&) = delete;

	private:
		void StopWorkers() noexcept
		{
			running_.store(false, std::memory_order_release);

			for (const auto& context : workerContexts_)
			{
				{
					std::lock_guard lock(context->mutex);
				}
				context->cv.notify_one();
			}

			for (auto& worker : workers_)
			{
				if (worker.joinable())
				{
					worker.join();
				}
			}
		}

		void WorkerMain(WorkerContext& context)
		{
			TaskScheduler* scheduler = context.scheduler.GetScheduler();

			const auto hasWork = [this, scheduler]()
			{
				return !running_.load(std::memory_order_acquire) || scheduler->GetPendingTaskCount() > 0;
			};

			while (true)
			{
				{
					std::unique_lock lock(context.mutex);
					// The timeout doubles as a safety net for handles that reach
					// this scheduler without going through ThreadPool::Schedule
					// (and therefore without a notify).
					if (!context.cv.wait_for(lock, IdleRecheckInterval, hasWork))
					{
						continue;
					}

					// Leave promptly on shutdown: waiting until the queue drains
					// would busy-loop on frame/time-waiting coroutines (and never
					// return for a yield-forever task). Whatever remains is
					// destroyed with the scheduler.
					if (!running_.load(std::memory_order_acquire))
					{
						break;
					}
				}

				// Update makes the scheduler current for its duration, so tasks
				// created by resumed coroutines bind to this worker.
				context.scheduler.Update();

				// Anything still pending yielded to the "next frame" — pace the
				// next update instead of spinning. Shutdown interrupts the wait.
				if (scheduler->GetPendingTaskCount() > 0)
				{
					std::unique_lock lock(context.mutex);
					context.cv.wait_for(lock, frameInterval_, [this]()
					{
						return !running_.load(std::memory_order_acquire);
					});
				}
			}
		}

		static constexpr std::chrono::milliseconds IdleRecheckInterval{10};

		TaskSchedulerManager* schedulerManager_;
		std::chrono::nanoseconds frameInterval_;
		std::vector<std::thread> workers_;
		std::vector<std::unique_ptr<WorkerContext>> workerContexts_;
		std::atomic<std::size_t> nextWorker_{0};
		std::atomic<bool> running_;
	};
}

#endif //TASKKIT_THREAD_POOL_H
