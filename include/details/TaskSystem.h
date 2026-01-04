#ifndef TASKKIT_TASKSYSTEM_H
#define TASKKIT_TASKSYSTEM_H

#include <cstddef>
#include <stack>
#include <unordered_map>
#include "Exceptions.h"
#include "TaskScheduler.h"
#include "TaskSystemConfiguration.h"

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

			explicit SchedulerRegistration(const TaskScheduler::Id& id) : valid_(true)
			{
				GetInternalIdStack().push(id.internalId);
			}

			~SchedulerRegistration()
			{
				if (valid_)
				{
					GetInternalIdStack().pop();
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
						GetInternalIdStack().pop();
					}
					valid_ = other.valid_;
					other.valid_ = false;
				}
				return *this;
			}

		private:
			bool valid_;
		};

		static void Initialize(const TaskSystemConfiguration& config = TaskSystemConfiguration{})
		{
			ThrowIfInitialized();

			if (config.createDefaultScheduler)
			{
				std::size_t internalId = GetNextInternalId();
				GetSchedulers().emplace(internalId, TaskScheduler{});
				GetInternalIdStack().push(internalId);
			}

			GetThreadState().initialized = true;
		}

		static void Shutdown()
		{
			ThrowIfNotInitialized();

			auto& stack = GetInternalIdStack();
			while (!stack.empty())
			{
				stack.pop();
			}

			GetSchedulers().clear();
			GetThreadState().initialized = false;
		}

		[[nodiscard]]
		static bool IsInitialized()
		{
			return GetThreadState().initialized;
		}

		[[nodiscard]]
		static TaskScheduler::Id GetCurrentSchedulerId()
		{
			ThrowIfNotInitialized();
			auto& stack = GetInternalIdStack();
			if (stack.empty())
			{
				throw std::runtime_error("No scheduler registered in current context");
			}
			return CreateId(stack.top());
		}

		[[nodiscard]]
		static TaskScheduler& GetCurrentScheduler()
		{
			return GetScheduler(GetCurrentSchedulerId());
		}

		[[nodiscard]]
		static TaskScheduler::Id CreateScheduler()
		{
			ThrowIfNotInitialized();
			std::size_t internalId = GetNextInternalId();
			GetSchedulers().emplace(internalId, TaskScheduler{});
			return CreateId(internalId);
		}

		static void DestroyScheduler(const TaskScheduler::Id& id)
		{
			ThrowIfInvalidId(id);
			GetSchedulers().erase(id.internalId);
		}

		[[nodiscard]]
		static TaskScheduler& GetScheduler(const TaskScheduler::Id& id)
		{
			ThrowIfInvalidId(id);
			return GetSchedulers().at(id.internalId);
		}

		[[nodiscard]]
		static SchedulerRegistration RegisterScheduler(const TaskScheduler::Id& id)
		{
			ThrowIfInvalidId(id);
			return SchedulerRegistration{ id };
		}

	private:
		struct ThreadState
		{
			bool initialized = false;
		};

		static ThreadState& GetThreadState()
		{
			thread_local ThreadState state;
			return state;
		}

		static std::size_t GetNextInternalId()
		{
			thread_local std::size_t nextId = 1;
			return nextId++;
		}

		static std::stack<std::size_t>& GetInternalIdStack()
		{
			thread_local std::stack<std::size_t> internalIdStack;
			return internalIdStack;
		}

		static std::unordered_map<std::size_t, TaskScheduler>& GetSchedulers()
		{
			thread_local std::unordered_map<std::size_t, TaskScheduler> schedulers;
			return schedulers;
		}

		static TaskScheduler::Id CreateId(std::size_t internalId)
		{
			return TaskScheduler::Id
			{
				std::this_thread::get_id(),
				internalId
			};
		}

		static bool ValidateId(const TaskScheduler::Id& id)
		{
			return std::this_thread::get_id() == id.threadId &&
				GetSchedulers().contains(id.internalId);
		}

		static void ThrowIfInvalidId(const TaskScheduler::Id& id)
		{
			if (!ValidateId(id))
			{
				throw InvalidSchedulerIdError();
			}
		}

		static void ThrowIfNotInitialized()
		{
			if (!IsInitialized())
			{
				throw std::runtime_error("TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			}
		}

		static void ThrowIfInitialized()
		{
			if (IsInitialized())
			{
				throw std::runtime_error("TaskSystem already initialized for this thread.");
			}
		}

		TaskSystem() = default;
	};
}

#endif //TASKKIT_TASKSYSTEM_H