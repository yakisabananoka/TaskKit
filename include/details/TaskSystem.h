#ifndef TASKKIT_TASK_SYSTEM_H
#define TASKKIT_TASK_SYSTEM_H

#include <cassert>
#include <cstddef>
#include <unordered_map>
#include "TaskSystemConfiguration.h"
#include "PoolAllocator.h"
#include "TaskSchedulerId.h"
#include "TaskSchedulerManager.h"
#include "PromiseContext.h"

namespace TKit
{
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
			assert(!IsInitialized() && "TaskSystem already initialized for this thread.");
			auto& sharedState = GetSharedState();
			sharedState.mainThreadId = std::this_thread::get_id();
			sharedState.schedulerManager.emplace(std::unordered_map<std::thread::id, std::size_t>
				{
					{std::this_thread::get_id(), config.mainThreadSchedulerCount}
				}
			);

			sharedState.useDefaultAllocator = !config.allocator.has_value();
			if (sharedState.useDefaultAllocator)
			{
				auto* poolAllocator = new PoolAllocator();
				sharedState.allocator = poolAllocator->CreateTaskAllocator();
			}
			else
			{
				sharedState.allocator = config.allocator.value();
			}

			sharedState.promiseContext.emplace(sharedState.allocator, *sharedState.schedulerManager);
			PromiseContext::SetCurrent(&sharedState.promiseContext.value());

			sharedState.isInitialized = true;
		}

		static void Shutdown()
		{
			assert(IsInitialized() && "TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			auto& sharedState = GetSharedState();
			assert(std::this_thread::get_id() == sharedState.mainThreadId && "TaskSystem main thread mismatch.");

			PromiseContext::SetCurrent(nullptr);
			sharedState.promiseContext.reset();

			if (sharedState.useDefaultAllocator)
			{
				auto* poolAllocator = static_cast<PoolAllocator*>(GetAllocator().GetContext());
				delete poolAllocator;
			}

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
			assert(IsInitialized() && "TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			return GetSchedulerManager().GetActivatedSchedulerId();
		}

		[[nodiscard]]
		static SchedulerActivation ActivateScheduler(const TaskSchedulerId& id)
		{
			return SchedulerActivation{id};
		}

		static void UpdateActivatedScheduler()
		{
			assert(IsInitialized() && "TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			GetSchedulerManager().UpdateActivatedScheduler();
		}

		static std::vector<TaskSchedulerId> GetMainThreadSchedulerIds()
		{
			assert(IsInitialized() && "TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			return GetSchedulerManager().GetThreadSchedulerIds(GetSharedState().mainThreadId);
		}

		static std::size_t GetPendingTaskCount(const TaskSchedulerId& id)
		{
			assert(IsInitialized() && "TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			return GetSchedulerManager().GetPendingTaskCount(id);
		}

		static void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
		{
			assert(IsInitialized() && "TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			GetSchedulerManager().Schedule(id, handle);
		}

	private:
		struct SharedState
		{
			std::thread::id mainThreadId;
			TaskAllocator allocator;
			std::optional<TaskSchedulerManager> schedulerManager;
			std::optional<PromiseContext> promiseContext;
			bool useDefaultAllocator = true;
			bool isInitialized = false;
		};

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
			assert(IsInitialized() && "TaskSystem not initialized for this thread. Call TaskSystem::Initialize() first.");
			return GetSharedState().allocator;
		}

		TaskSystem() = default;
	};
}

#endif //TASKKIT_TASK_SYSTEM_H
