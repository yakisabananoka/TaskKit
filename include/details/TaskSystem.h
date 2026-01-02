#ifndef TASKKIT_TASKSYSTEM_H
#define TASKKIT_TASKSYSTEM_H

#include <cstddef>
#include <stack>
#include <unordered_map>
#include "TaskScheduler.h"

namespace TKit
{
	class TaskSystem final
	{
	public:
		struct SchedulerRegistration final
		{
			SchedulerRegistration() : valid_(false)
			{
			}

			SchedulerRegistration(size_t id) : valid_(true)
			{
				GetInstance().schedulerIdStack_.push(id);
			}

			~SchedulerRegistration()
			{
				if (valid_)
				{
					GetInstance().schedulerIdStack_.pop();
				}
			}

			SchedulerRegistration(const SchedulerRegistration&) = delete;
			SchedulerRegistration& operator=(const SchedulerRegistration&) = delete;

			SchedulerRegistration(SchedulerRegistration&& other) noexcept : valid_(other.valid_)
			{
				other.valid_ = false;
			}

			SchedulerRegistration& operator=(SchedulerRegistration&& other) noexcept
			{
				if (this != &other)
				{
					if (valid_)
					{
						GetInstance().schedulerIdStack_.pop();
					}
					valid_ = other.valid_;
					other.valid_ = false;
				}
				return *this;
			}

		private:
			bool valid_;
		};

		[[nodiscard]]
		static TaskSystem& GetInstance()
		{
			static TaskSystem instance;
			return instance;
		}

		~TaskSystem() = default;

		[[nodiscard]]
		std::size_t GetCurrentSchedulerId() const
		{
			return schedulerIdStack_.top();
		}

		[[nodiscard]]
		std::size_t CreateScheduler()
		{
			std::size_t newId = schedulerCounter_++;
			schedulers_.emplace(newId, TaskScheduler{});
			return newId;
		}

		void DestroyScheduler(std::size_t id)
		{
			schedulers_.erase(id);
		}

		[[nodiscard]]
		TaskScheduler& GetScheduler(std::size_t id)
		{
			return schedulers_.at(id);
		}

		[[nodiscard]]
		SchedulerRegistration RegisterScheduler(std::size_t id)
		{
			return SchedulerRegistration{ id };
		}

		TaskSystem(const TaskSystem&) = delete;
		TaskSystem& operator=(const TaskSystem&) = delete;
		TaskSystem(TaskSystem&&) = delete;
		TaskSystem& operator=(TaskSystem&&) = delete;

	private:
		TaskSystem()
		{
			auto defaultId = CreateScheduler();
			schedulerIdStack_.push(defaultId);
		}

		std::stack<std::size_t> schedulerIdStack_{};
		std::unordered_map<std::size_t, TaskScheduler> schedulers_{};
		std::size_t schedulerCounter_ = 0;
	};
}

#endif //TASKKIT_TASKSYSTEM_H