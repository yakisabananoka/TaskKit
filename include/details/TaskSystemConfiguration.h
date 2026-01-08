#ifndef TASKKIT_TASKSYSTEMCONFIGURATION_H
#define TASKKIT_TASKSYSTEMCONFIGURATION_H

#include "TaskAllocator.h"

namespace TKit
{
	struct TaskSystemConfiguration
	{
		bool createDefaultScheduler;
		std::optional<TaskAllocator> allocator;
	};

	class TaskSystemConfigurationBuilder
	{
	public:
		TaskSystemConfigurationBuilder() = default;

		TaskSystemConfigurationBuilder& WithDefaultScheduler(bool createDefault)
		{
			configuration_.createDefaultScheduler = createDefault;
			return *this;
		}

		TaskSystemConfigurationBuilder& WithCustomAllocator(const TaskAllocator& allocator)
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

#endif //TASKKIT_TASKSYSTEMCONFIGURATION_H
