#ifndef TASKKIT_TASKSYSTEMCONFIGURATION_H
#define TASKKIT_TASKSYSTEMCONFIGURATION_H

namespace TKit
{
	struct TaskSystemConfiguration
	{
		bool createDefaultScheduler = true;
	};

	class TaskSystemConfigurationBuilder
	{
	public:
		TaskSystemConfigurationBuilder() = default;

		TaskSystemConfigurationBuilder& WithDefaultScheduler(bool create)
		{
			config_.createDefaultScheduler = create;
			return *this;
		}

		TaskSystemConfiguration Build() const
		{
			return config_;
		}

	private:
		TaskSystemConfiguration config_;
	};
}

#endif //TASKKIT_TASKSYSTEMCONFIGURATION_H
