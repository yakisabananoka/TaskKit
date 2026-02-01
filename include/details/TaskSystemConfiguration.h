#ifndef TASKKIT_TASK_SYSTEM_CONFIGURATION_H
#define TASKKIT_TASK_SYSTEM_CONFIGURATION_H

#include "TaskAllocator.h"

namespace TKit
{
	struct TaskSystemConfiguration
	{
		class Builder;

		std::optional<TaskAllocator> allocator;
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
