#ifndef TASKKIT_TASK_SCHEDULER_H
#define TASKKIT_TASK_SCHEDULER_H

#include <algorithm>
#include <queue>
#include <coroutine>
#include <thread>

namespace TKit
{
	class TaskScheduler final
	{
	public:
		TaskScheduler() = default;
		~TaskScheduler()
		{
			while (!nextFrameHandles_.empty())
			{
				auto handle = nextFrameHandles_.front();
				nextFrameHandles_.pop();
				handle.destroy();
			}
		}

		void Update()
		{
			auto nextFrameHandles = std::move(nextFrameHandles_);

			while (!nextFrameHandles.empty())
			{
				auto handle = nextFrameHandles.front();
				nextFrameHandles.pop();
				handle.resume();
			}
		}

		void ScheduleNextFrame(std::coroutine_handle<> handle)
		{
			nextFrameHandles_.push(handle);
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount() const
		{
			return nextFrameHandles_.size();
		}

		TaskScheduler(const TaskScheduler&) = delete;
		TaskScheduler& operator=(const TaskScheduler&) = delete;
		TaskScheduler(TaskScheduler&& other) noexcept
		{
			nextFrameHandles_ = std::move(other.nextFrameHandles_);
		}
		TaskScheduler& operator=(TaskScheduler&& other) noexcept
		{
			if (this != &other)
			{
				nextFrameHandles_ = std::move(other.nextFrameHandles_);
			}
			return *this;
		}

	private:
		std::queue<std::coroutine_handle<>> nextFrameHandles_;
	};
}

#endif //TASKKIT_TASK_SCHEDULER_H