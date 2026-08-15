#ifndef TASKKIT_TASK_SYSTEM_H
#define TASKKIT_TASK_SYSTEM_H

#include <algorithm>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <optional>
#include <thread>
#include "PoolAllocator.h"
#include "PromiseContext.h"
#include "TaskSchedulerId.h"
#include "TaskSchedulerManager.h"
#include "TaskSystemConfiguration.h"
#include "ThreadPool.h"

namespace TKit
{
	// Process-wide facade owning the scheduler manager, the thread pool and the
	// frame allocator. Initialize() must complete on the main thread before any
	// Task is created; Shutdown() must run on the same thread after all tasks
	// finished (drain your schedulers first).
	class TaskSystem final
	{
	public:
		// RAII guard for the thread-local scheduler activation stack.
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
			assert(!IsInitialized() && "TaskSystem already initialized.");
			auto& state = GetSharedState();
			state.mainThreadId = std::this_thread::get_id();
			state.schedulerManager.emplace();

			if (config.allocator.has_value())
			{
				state.allocator = config.allocator.value();
			}
			else
			{
				state.poolAllocator = std::make_unique<PoolAllocator>();
				state.allocator = state.poolAllocator->CreateTaskAllocator();
				state.poolAllocator->Prewarm();
			}

			const std::size_t threadCount = config.threadPoolSize > 0
				? config.threadPoolSize
				: std::max<std::size_t>(1, std::thread::hardware_concurrency());
			state.threadPool = std::make_unique<ThreadPool>(
				*state.schedulerManager,
				threadCount,
				config.reservedTaskCount,
				config.workerFrameInterval
			);

			state.promiseContext.emplace(state.allocator, *state.schedulerManager, *state.threadPool);
			PromiseContext::SetCurrent(&state.promiseContext.value());

			state.isInitialized = true;
		}

		static void Shutdown()
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			auto& state = GetSharedState();
			assert(std::this_thread::get_id() == state.mainThreadId && "TaskSystem::Shutdown: main thread mismatch.");

			// Order matters: join the workers, then destroy the schedulers (any
			// leftover queued frame frees itself through its frame header), and
			// only then drop the context and the allocator behind those frames.
			state.threadPool.reset();
			state.schedulerManager.reset();

			PromiseContext::SetCurrent(nullptr);
			state.promiseContext.reset();
			state.poolAllocator.reset();

			state.mainThreadId = {};
			state.isInitialized = false;
		}

		[[nodiscard]]
		static bool IsInitialized()
		{
			return GetSharedState().isInitialized;
		}

		[[nodiscard]]
		static TaskSchedulerId GetActivatedSchedulerId()
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			return GetSchedulerManager().GetActivatedSchedulerId();
		}

		[[nodiscard]]
		static SchedulerActivation ActivateScheduler(const TaskSchedulerId& id)
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			assert(id.GetThreadId() == std::this_thread::get_id() && "Cannot activate scheduler for different thread.");
			return SchedulerActivation{id};
		}

		static void UpdateActivatedScheduler()
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			GetSchedulerManager().UpdateActivatedScheduler();
		}

		[[nodiscard]]
		static std::size_t GetPendingTaskCount(const TaskSchedulerId& id)
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			return GetSchedulerManager().GetPendingTaskCount(id);
		}

		static void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			GetSchedulerManager().Schedule(id, handle);
		}

		[[nodiscard]]
		static TaskSchedulerId CreateScheduler(std::optional<std::thread::id> threadId = std::nullopt, std::size_t reservedTaskCount = 100)
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			return GetSchedulerManager().CreateScheduler(threadId.value_or(std::this_thread::get_id()), reservedTaskCount);
		}

	private:
		struct SharedState
		{
			std::thread::id mainThreadId;
			TaskAllocator allocator;
			std::unique_ptr<PoolAllocator> poolAllocator;
			std::optional<TaskSchedulerManager> schedulerManager;
			std::unique_ptr<ThreadPool> threadPool;
			std::optional<PromiseContext> promiseContext;
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

		TaskSystem() = default;
	};
}

#endif //TASKKIT_TASK_SYSTEM_H
