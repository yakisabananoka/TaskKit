#ifndef TASKKIT_THREAD_POOL_H
#define TASKKIT_THREAD_POOL_H

#include <cassert>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "TaskScheduler.h"
#include "TaskSchedulerId.h"
#include "TaskSchedulerManager.h"

namespace TKit
{
	class ThreadPool final
	{
		struct WorkerContext
		{
			TaskSchedulerId schedulerId;
			std::mutex mutex;
			std::condition_variable cv;
		};

	public:
		ThreadPool(
			TaskSchedulerManager& schedulerManager,
			std::size_t threadCount,
			std::size_t reservedTaskCount = 100
		) :
			schedulerManager_(&schedulerManager),
			running_(true)
		{
			workers_.reserve(threadCount);
			workerContexts_.reserve(threadCount);

			for (std::size_t i = 0; i < threadCount; ++i)
			{
				workerContexts_.push_back(std::make_unique<WorkerContext>());
			}

			std::atomic<std::size_t> waitingWorkers{0};
			std::mutex initMutex;
			std::condition_variable workerReadyCv;
			std::condition_variable startCv;
			bool ready = false;

			for (std::size_t i = 0; i < threadCount; ++i)
			{
				workers_.emplace_back([this, i, &waitingWorkers, &initMutex, &workerReadyCv, &startCv, &ready]()
				{
					{
						std::unique_lock lock(initMutex);
						waitingWorkers.fetch_add(1, std::memory_order_release);
						workerReadyCv.notify_one();
						startCv.wait(lock, [&ready]() { return ready; });
					}
					waitingWorkers.fetch_add(1, std::memory_order_release);

					WorkerMain(i);
				});
			}

			{
				std::unique_lock lock(initMutex);
				workerReadyCv.wait(lock, [&waitingWorkers, threadCount]()
				{
					return waitingWorkers.load(std::memory_order_acquire) == threadCount;
				});

				for (std::size_t i = 0; i < threadCount; ++i)
				{
					auto workerId = workers_[i].get_id();
					workerContexts_[i]->schedulerId = schedulerManager_->CreateScheduler(workerId, reservedTaskCount);
				}

				waitingWorkers.store(0, std::memory_order_release);
				ready = true;
			}
			startCv.notify_all();

			while (waitingWorkers.load(std::memory_order_acquire) < threadCount)
			{
				std::this_thread::yield();
			}
		}

		~ThreadPool()
		{
			running_.store(false, std::memory_order_release);

			for (auto& context : workerContexts_)
			{
				std::lock_guard lock(context->mutex);
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

		void Schedule(std::coroutine_handle<> handle)
		{
			const std::size_t index = nextScheduler_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
			schedulerManager_->Schedule(workerContexts_[index]->schedulerId, handle);

			std::lock_guard lock(workerContexts_[index]->mutex);
			workerContexts_[index]->cv.notify_one();
		}

		void Schedule(std::size_t workerIndex, std::coroutine_handle<> handle)
		{
			assert(workerIndex < workers_.size() && "ThreadPool: invalid worker index");
			schedulerManager_->Schedule(workerContexts_[workerIndex]->schedulerId, handle);

			std::lock_guard lock(workerContexts_[workerIndex]->mutex);
			workerContexts_[workerIndex]->cv.notify_one();
		}

		[[nodiscard]]
		std::size_t GetWorkerCount() const
		{
			return workers_.size();
		}

		[[nodiscard]]
		TaskSchedulerId GetSchedulerId(std::size_t workerIndex) const
		{
			assert(workerIndex < workers_.size() && "ThreadPool: invalid worker index");
			return workerContexts_[workerIndex]->schedulerId;
		}

		ThreadPool(const ThreadPool&) = delete;
		ThreadPool& operator=(const ThreadPool&) = delete;
		ThreadPool(ThreadPool&&) = delete;
		ThreadPool& operator=(ThreadPool&&) = delete;

	private:
		void WorkerMain(std::size_t workerIndex)
		{
			auto& context = *workerContexts_[workerIndex];
			auto schedulerId = context.schedulerId;

			while (true)
			{
				{
					std::unique_lock lock(context.mutex);
					context.cv.wait(lock, [this, &schedulerId]()
					{
						return !running_.load(std::memory_order_acquire) ||
						       schedulerManager_->GetPendingTaskCount(schedulerId) > 0;
					});

					if (!running_.load(std::memory_order_acquire) &&
					    schedulerManager_->GetPendingTaskCount(schedulerId) == 0)
					{
						break;
					}
				}

				schedulerManager_->ActivateScheduler(schedulerId);
				schedulerManager_->UpdateActivatedScheduler();
				schedulerManager_->DeactivateScheduler();
			}
		}

		TaskSchedulerManager* schedulerManager_;
		std::vector<std::thread> workers_;
		std::vector<std::unique_ptr<WorkerContext>> workerContexts_;
		std::atomic<std::size_t> nextScheduler_{0};
		std::atomic<bool> running_;
	};
}

#endif //TASKKIT_THREAD_POOL_H
