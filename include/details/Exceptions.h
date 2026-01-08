#ifndef TASKKIT_EXCEPTIONS_H
#define TASKKIT_EXCEPTIONS_H
#include <format>
#include <utility>
#include "TaskScheduler.h"

namespace TKit
{
	class TaskKitError : public std::exception
	{
	public:
		explicit TaskKitError(std::string message) :
			message_(std::move(message))
		{
		}

		[[nodiscard]]
		const char* what() const noexcept override
		{
			return message_.c_str();
		}

	private:
		std::string message_;
	};

	class InvalidSchedulerIdError final : public TaskKitError
	{
	public:
		explicit InvalidSchedulerIdError(const TaskScheduler::Id& id) :
			TaskKitError(std::format("Invalid TaskScheduler Id: internalId={}", id.internalId))
		{
		}
	};
}

#endif //TASKKIT_EXCEPTIONS_H
