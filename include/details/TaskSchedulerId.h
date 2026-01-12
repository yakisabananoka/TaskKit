#ifndef TASKKIT_TASKSCHEDULER_ID_H
#define TASKKIT_TASKSCHEDULER_ID_H
#include <thread>

namespace TKit
{
	class TaskSchedulerId final
	{
	public:
		TaskSchedulerId() noexcept = default;
		TaskSchedulerId(std::thread::id threadId, std::size_t internalId) noexcept :
			threadId_(threadId),
			internalId_(internalId)
		{
		}

		[[nodiscard]]
		std::size_t GetInternalId() const noexcept
		{
			return internalId_;
		}

		[[nodiscard]]
		std::thread::id GetThreadId() const noexcept
		{
			return threadId_;
		}

		auto operator<=>(const TaskSchedulerId& other) const noexcept = default;

	private:
		std::thread::id threadId_;
		std::size_t internalId_ = 0;
	};
}

#endif //TASKKIT_TASKSCHEDULER_ID_H