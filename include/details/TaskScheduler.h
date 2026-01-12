#ifndef TASKKIT_TASK_SCHEDULER_H
#define TASKKIT_TASK_SCHEDULER_H

#include <algorithm>
#include <vector>
#include <coroutine>
#include <thread>

namespace TKit
{
	class TaskScheduler final
	{
	public:
		explicit TaskScheduler(std::size_t reservedTaskCount)
		{
			handles_.reserve(reservedTaskCount);
			processingHandles_.reserve(reservedTaskCount);
			isRunning_.clear();
		}
		~TaskScheduler()
		{
			Lock();
			while (!handles_.empty())
			{
				auto handle = handles_.front();
				handles_.pop_back();
				handle.destroy();
			}
			while (!processingHandles_.empty())
			{
				auto handle = processingHandles_.front();
				processingHandles_.pop_back();
				handle.destroy();
			}
			Unlock();
		}

		void Update()
		{
			Lock();
			std::swap(processingHandles_, handles_);
			Unlock();

			while (!processingHandles_.empty())
			{
				auto handle = processingHandles_.front();
				processingHandles_.pop_back();
				handle.resume();
			}
		}

		void Schedule(std::coroutine_handle<> handle)
		{
			Lock();
			handles_.emplace_back(handle);
			Unlock();
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount() const
		{
			Lock();
			std::size_t count = handles_.size();
			Unlock();

			return count;
		}

		TaskScheduler(const TaskScheduler&) = delete;
		TaskScheduler& operator=(const TaskScheduler&) = delete;
		TaskScheduler(TaskScheduler&& other) noexcept
		{
			Lock();
			other.Lock();
			handles_ = std::move(other.handles_);
			other.Unlock();
			Unlock();
		}
		TaskScheduler& operator=(TaskScheduler&& other) noexcept
		{
			if (this != &other)
			{
				Lock();
				other.Lock();
				handles_ = std::move(other.handles_);
				other.Unlock();
				Unlock();
			}
			return *this;
		}

	private:
		void Lock() const
		{
			while (isRunning_.test_and_set(std::memory_order_acquire))
			{
				std::this_thread::yield();
			}
		}

		void Unlock() const
		{
			isRunning_.clear(std::memory_order_release);
		}

		std::vector<std::coroutine_handle<>> handles_;
		std::vector<std::coroutine_handle<>> processingHandles_;
		mutable std::atomic_flag isRunning_;
	};
}

#endif //TASKKIT_TASK_SCHEDULER_H