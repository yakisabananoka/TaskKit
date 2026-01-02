#ifndef TASKKIT_TASKSCHEDULER_H
#define TASKKIT_TASKSCHEDULER_H

#include <algorithm>
#include <queue>
#include <coroutine>

namespace TKit
{
	class TaskScheduler final
	{
	public:
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

	private:
		std::queue<std::coroutine_handle<>> nextFrameHandles_;
	};
}

#endif //TASKKIT_TASKSCHEDULER_H