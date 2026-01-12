#ifndef TASKKIT_TASK_SCHEDULER_H
#define TASKKIT_TASK_SCHEDULER_H

#include <algorithm>
#include <coroutine>
#include <queue>
#include <thread>

namespace TKit
{
	class TaskScheduler final
	{
	public:
		explicit TaskScheduler([[maybe_unused]] std::size_t reservedTaskCount)
		{
			handles_.reserve(reservedTaskCount);
			updateHandles_.reserve(reservedTaskCount);
		}
		~TaskScheduler()
		{
			LockHandles();
			LockUpdateHandles();
			for (const auto& handle: handles_)
			{
				handle.destroy();
			}
			for (const auto& handle: updateHandles_)
			{
				handle.destroy();
			}
			UnlockUpdateHandles();
			UnlockHandles();
		}

		void Update()
		{
			LockUpdateHandles();

			LockHandles();
			std::swap(updateHandles_, handles_);
			UnlockHandles();

			for (const auto& handle : updateHandles_)
			{
				handle.resume();
			}
			updateHandles_.clear();

			UnlockUpdateHandles();
		}

		void Schedule(std::coroutine_handle<> handle)
		{
			LockHandles();
			handles_.emplace_back(handle);
			UnlockHandles();
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount() const
		{
			LockHandles();
			std::size_t count = handles_.size();
			UnlockHandles();

			return count;
		}

		TaskScheduler(const TaskScheduler&) = delete;
		TaskScheduler& operator=(const TaskScheduler&) = delete;
		TaskScheduler(TaskScheduler&& other) noexcept
		{
			operator=(std::move(other));
		}
		TaskScheduler& operator=(TaskScheduler&& other) noexcept
		{
			if (this != &other)
			{
				LockHandles();
				LockUpdateHandles();

				other.LockHandles();
				other.LockUpdateHandles();

				handles_ = std::move(other.handles_);
				updateHandles_ = std::move(other.updateHandles_);

				other.UnlockUpdateHandles();
				other.UnlockHandles();

				UnlockUpdateHandles();
				UnlockHandles();
			}
			return *this;
		}

	private:
		void LockHandles() const
		{
			while (handlesLock_.test_and_set(std::memory_order_acquire))
			{
				std::this_thread::yield();
			}
		}

		void UnlockHandles() const
		{
			handlesLock_.clear(std::memory_order_release);
		}

		void LockUpdateHandles() const
		{
			while (updateHandlesLock_.test_and_set(std::memory_order_acquire))
			{
				std::this_thread::yield();
			}
		}

		void UnlockUpdateHandles() const
		{
			updateHandlesLock_.clear(std::memory_order_release);
		}

		std::vector<std::coroutine_handle<>> handles_;
		std::vector<std::coroutine_handle<>> updateHandles_;
		mutable std::atomic_flag handlesLock_ = ATOMIC_FLAG_INIT;
		mutable std::atomic_flag updateHandlesLock_ = ATOMIC_FLAG_INIT;
	};
}

#endif //TASKKIT_TASK_SCHEDULER_H