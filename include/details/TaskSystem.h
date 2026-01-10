#ifndef TASKKIT_TASK_SYSTEM_H
#define TASKKIT_TASK_SYSTEM_H

#include <cstddef>
#include <stack>
#include <unordered_map>
#include "Exceptions.h"
#include "TaskScheduler.h"
#include "TaskSystemConfiguration.h"
#include "PoolAllocator.h"
#include "TaskSchedulerId.h"

namespace TKit
{
	template<typename T>
	class Task;

	class TaskSystem final
	{
	public:
		struct SchedulerRegistration final
		{
			SchedulerRegistration() : valid_(false)
			{
			}

			explicit SchedulerRegistration(const TaskSchedulerId& id) : valid_(true)
			{
				GetInternalIdStack().push(id.GetInternalId());
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

			auto& state = GetThreadState();

			state.useDefaultAllocator = !config.allocator.has_value();
			if (state.useDefaultAllocator)
			{
				auto* poolAllocator = new PoolAllocator();
				state.allocator = poolAllocator->CreateTaskAllocator();
			}
			else
			{
				state.allocator = config.allocator.value();
			}

			if (config.createDefaultScheduler)
			{
				const auto id = TaskSchedulerId::Create();
				state.schedulers.emplace(id.GetInternalId(), TaskScheduler{});
				state.internalIdStack.push(id.GetInternalId());
			}

			state.initialized = true;
		}

		static void Shutdown()
		{
			ThrowIfNotInitialized();

			auto& state = GetThreadState();
			if (state.useDefaultAllocator)
			{
				auto* poolAllocator = static_cast<PoolAllocator*>(GetAllocator().GetContext());
				delete poolAllocator;
			}

			state = ThreadState{};
		}

		[[nodiscard]]
		static bool IsInitialized()
		{
			return GetThreadState().initialized;
		}

		[[nodiscard]]
		static TaskSchedulerId GetCurrentSchedulerId()
		{
			ThrowIfNotInitialized();
			auto& stack = GetInternalIdStack();
			if (stack.empty())
			{
				throw std::runtime_error("No scheduler registered in current context");
			}
			return TaskSchedulerId{
				std::this_thread::get_id(),
				stack.top()
			};
		}

		[[nodiscard]]
		static TaskScheduler& GetCurrentScheduler()
		{
			return GetScheduler(GetCurrentSchedulerId());
		}

		[[nodiscard]]
		static TaskScheduler& GetScheduler(const TaskSchedulerId& id)
		{
			ThrowIfInvalidId(id);
			return GetSchedulers().at(id.GetInternalId());
		}

		[[nodiscard]]
		static TaskSchedulerId CreateScheduler()
		{
			ThrowIfNotInitialized();
			auto id = TaskSchedulerId::Create();
			GetSchedulers().emplace(id.GetInternalId(), TaskScheduler{});
			return id;
		}

		static void DestroyScheduler(const TaskSchedulerId& id)
		{
			ThrowIfInvalidId(id);
			GetSchedulers().erase(id.GetInternalId());
		}

		[[nodiscard]]
		static SchedulerRegistration RegisterScheduler(const TaskSchedulerId& id)
		{
			ThrowIfInvalidId(id);
			return SchedulerRegistration{ id };
		}

	private:
		struct ThreadState
		{
			TaskAllocator allocator;
			std::stack<std::size_t> internalIdStack;
			std::unordered_map<std::size_t, TaskScheduler> schedulers;
			bool initialized = false;
			bool useDefaultAllocator = true;
		};

		[[nodiscard]]
		static ThreadState& GetThreadState()
		{
			thread_local ThreadState state;
			return state;
		}

		[[nodiscard]]
		static std::stack<std::size_t>& GetInternalIdStack()
		{
			return GetThreadState().internalIdStack;
		}

		[[nodiscard]]
		static std::unordered_map<std::size_t, TaskScheduler>& GetSchedulers()
		{
			return GetThreadState().schedulers;
		}

		[[nodiscard]]
		static TaskAllocator& GetAllocator()
		{
			ThrowIfNotInitialized();
			return GetThreadState().allocator;
		}

		static bool ValidateId(const TaskSchedulerId& id)
		{
			return std::this_thread::get_id() == id.GetThreadId() &&
				GetSchedulers().contains(id.GetInternalId());
		}

		static void ThrowIfInvalidId(const TaskSchedulerId& id)
		{
			if (!ValidateId(id))
			{
				throw InvalidSchedulerIdError(id);
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

		template <typename T>
		friend class Task;
	};
}

#endif //TASKKIT_TASK_SYSTEM_H