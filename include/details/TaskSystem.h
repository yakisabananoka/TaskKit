#ifndef TASKKIT_TASK_SYSTEM_H
#define TASKKIT_TASK_SYSTEM_H

#include <cstddef>
#include <unordered_map>
#include "Exceptions.h"
#include "TaskSystemConfiguration.h"
#include "PoolAllocator.h"
#include "TaskSchedulerId.h"
#include "TaskSchedulerManager.h"

namespace TKit
{
	template<typename T>
	class Task;

	class TaskSystem final
	{
	public:
		struct SchedulerActivation final
		{
			SchedulerActivation() : valid_(false)
			{
			}

			explicit SchedulerActivation(const TaskSchedulerId& id) : valid_(true)
			{
				GetSchedulerManager().ActivateScheduler(id);
			}

			~SchedulerActivation()
			{
				if (valid_)
				{
					GetSchedulerManager().DeactivateScheduler();
				}
			}

			SchedulerActivation(const SchedulerActivation&) = delete;
			SchedulerActivation& operator=(const SchedulerActivation&) = delete;

			SchedulerActivation(SchedulerActivation&& other) noexcept : valid_(other.valid_)
			{
				other.valid_ = false;
			}

			SchedulerActivation& operator=(SchedulerActivation&& other) noexcept
			{
				if (this != &other)
				{
					if (valid_)
					{
						GetSchedulerManager().DeactivateScheduler();
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
			auto& sharedState = GetSharedState();
			sharedState.mainThreadId = std::this_thread::get_id();
			sharedState.schedulerManager.emplace(std::unordered_map<std::thread::id, std::size_t>
				{
					{std::this_thread::get_id(), config.mainThreadSchedulerCount}
				}
			);

			auto& threadState = GetThreadState();

			threadState.useDefaultAllocator = !config.allocator.has_value();
			if (threadState.useDefaultAllocator)
			{
				auto* poolAllocator = new PoolAllocator();
				threadState.allocator = poolAllocator->CreateTaskAllocator();
			}
			else
			{
				threadState.allocator = config.allocator.value();
			}

			sharedState.isInitialized = true;
		}

		static void Shutdown()
		{
			ThrowIfNotInitialized();
			ThrowIfMainThreadMismatch();

			auto& threadState = GetThreadState();
			if (threadState.useDefaultAllocator)
			{
				auto* poolAllocator = static_cast<PoolAllocator*>(GetAllocator().GetContext());
				delete poolAllocator;
			}

			threadState = ThreadState{};

			auto& sharedState = GetSharedState();
			sharedState.schedulerManager.reset();
			sharedState.mainThreadId = {};
			sharedState.isInitialized = false;
		}

		[[nodiscard]]
		static bool IsInitialized()
		{
			return GetSharedState().isInitialized;
		}

		[[nodiscard]]
		static TaskSchedulerId GetActivatedSchedulerId()
		{
			ThrowIfNotInitialized();
			return GetSchedulerManager().GetActivatedSchedulerId();
		}

		[[nodiscard]]
		static SchedulerActivation ActivateScheduler(const TaskSchedulerId& id)
		{
			return SchedulerActivation{id};
		}

		static void UpdateActivatedScheduler()
		{
			ThrowIfNotInitialized();
			GetSchedulerManager().UpdateActivatedScheduler();
		}

		static std::vector<TaskSchedulerId> GetMainThreadSchedulerIds()
		{
			ThrowIfNotInitialized();
			return GetSchedulerManager().GetThreadSchedulerIds(GetSharedState().mainThreadId);
		}

		static std::size_t GetPendingTaskCount(const TaskSchedulerId& id)
		{
			ThrowIfNotInitialized();
			return GetSchedulerManager().GetPendingTaskCount(id);
		}

		static void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
		{
			ThrowIfNotInitialized();
			GetSchedulerManager().Schedule(id, handle);
		}

	private:
		struct ThreadState
		{
			TaskAllocator allocator;
			bool useDefaultAllocator = true;
		};

		struct SharedState
		{
			std::thread::id mainThreadId;
			std::optional<TaskSchedulerManager> schedulerManager;
			bool isInitialized = false;
		};

		[[nodiscard]]
		static ThreadState& GetThreadState()
		{
			thread_local ThreadState state;
			return state;
		}

		[[nodiscard]]
		static SharedState& GetSharedState()
		{
			static SharedState state;
			return state;
		}

		[[nodiscard]]
		static TaskSchedulerManager& GetSchedulerManager()
		{
			return GetSharedState().schedulerManager.value();
		}

		[[nodiscard]]
		static TaskAllocator& GetAllocator()
		{
			ThrowIfNotInitialized();
			return GetThreadState().allocator;
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

		static void ThrowIfMainThreadMismatch()
		{
			auto& sharedState = GetSharedState();
			if (std::this_thread::get_id() != sharedState.mainThreadId)
			{
				throw std::runtime_error("TaskSystem main thread mismatch.");
			}
		}

		TaskSystem() = default;

		template <typename T>
		friend class Task;
	};
}

#endif //TASKKIT_TASK_SYSTEM_H