#ifndef TASKKIT_EXCEPTIONS_H
#define TASKKIT_EXCEPTIONS_H
#include <format>
#include <stdexcept>

namespace TKit
{
	class InvalidSchedulerIdError final : public std::invalid_argument
	{
	public:
		InvalidSchedulerIdError() : std::invalid_argument("Invalid TaskScheduler Id provided")
		{
		}
	};
}

#endif //TASKKIT_EXCEPTIONS_H
