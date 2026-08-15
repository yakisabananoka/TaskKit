#ifndef TASKKIT_TASK_SYSTEM_CONFIGURATION_H
#define TASKKIT_TASK_SYSTEM_CONFIGURATION_H

#include <chrono>
#include <cstddef>
#include <optional>
#include "TaskAllocator.h"

namespace TKit
{
	struct TaskSystemConfiguration
	{
		class Builder;

		std::optional<TaskAllocator> allocator;
		std::size_t threadPoolSize = 0;
		std::size_t reservedTaskCount = 100;

		// How long a pool worker waits between updates while frame-waiting
		// coroutines (co_yield / WaitFor) are parked on it. Smaller values lower
		// latency, larger values lower CPU usage.
		std::chrono::nanoseconds workerFrameInterval = std::chrono::milliseconds(1);
	};

	class TaskSystemConfiguration::Builder
	{
	public:
		Builder() = default;

		Builder& WithCustomAllocator(const TaskAllocator& allocator)
		{
			configuration_.allocator = allocator;
			return *this;
		}

		Builder& WithThreadPoolSize(std::size_t size)
		{
			configuration_.threadPoolSize = size;
			return *this;
		}

		Builder& WithReservedTaskCount(std::size_t count)
		{
			configuration_.reservedTaskCount = count;
			return *this;
		}

		Builder& WithWorkerFrameInterval(std::chrono::nanoseconds interval)
		{
			configuration_.workerFrameInterval = interval;
			return *this;
		}

		[[nodiscard]]
		TaskSystemConfiguration Build() const
		{
			return configuration_;
		}

	private:
		TaskSystemConfiguration configuration_;
	};
}

#endif //TASKKIT_TASK_SYSTEM_CONFIGURATION_H
