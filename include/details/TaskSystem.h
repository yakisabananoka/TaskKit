#ifndef TASKKIT_TASK_SYSTEM_H
#define TASKKIT_TASK_SYSTEM_H

#include <algorithm>
#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
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
		// RAII guard for the thread-local scheduler activation stack. Remembers
		// which scheduler it activated so a non-LIFO release is caught, and only
		// touches thread-local state on release — destroying a guard after
		// Shutdown() is safe.
		struct SchedulerActivation final
		{
			SchedulerActivation() : scheduler_(nullptr)
			{
			}

			explicit SchedulerActivation(const TaskSchedulerId& id) :
				scheduler_(id.GetScheduler())
			{
				TaskSchedulerManager::ActivateScheduler(id);
			}

			~SchedulerActivation()
			{
				if (scheduler_)
				{
					TaskSchedulerManager::DeactivateScheduler(scheduler_);
				}
			}

			SchedulerActivation(const SchedulerActivation&) = delete;
			SchedulerActivation& operator=(const SchedulerActivation&) = delete;

			SchedulerActivation(SchedulerActivation&& other) noexcept :
				scheduler_(std::exchange(other.scheduler_, nullptr))
			{
			}

			SchedulerActivation& operator=(SchedulerActivation&& other) noexcept
			{
				if (this != &other)
				{
					if (scheduler_)
					{
						TaskSchedulerManager::DeactivateScheduler(scheduler_);
					}
					scheduler_ = std::exchange(other.scheduler_, nullptr);
				}
				return *this;
			}

		private:
			TaskScheduler* scheduler_;
		};

		static void Initialize(const TaskSystemConfiguration& config = TaskSystemConfiguration{})
		{
			// Initializing twice would re-create the manager under live workers;
			// abort in release builds too rather than corrupt memory.
			assert(!IsInitialized() && "TaskSystem already initialized.");
			if (IsInitialized())
			{
				std::abort();
			}

			auto& state = GetSharedState();
			state.mainThreadId = std::this_thread::get_id();
			state.reservedTaskCount = config.reservedTaskCount;
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

			state.isInitialized.store(true, std::memory_order_release);
		}

		// Stops the thread pool promptly (pool tasks still queued or parked are
		// destroyed, not completed) and tears everything down. Every Task object
		// for a not-yet-finished task must be gone before this call: frames the
		// schedulers destroy here must not be touched by a Task destructor
		// afterwards. Drain your schedulers first if completion matters.
		static void Shutdown()
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			if (!IsInitialized())
			{
				std::abort();
			}

			auto& state = GetSharedState();
			assert(std::this_thread::get_id() == state.mainThreadId && "TaskSystem::Shutdown: main thread mismatch.");
			if (std::this_thread::get_id() != state.mainThreadId)
			{
				std::abort();
			}

			// Order matters: join the workers, then destroy the schedulers (any
			// leftover queued frame frees itself through its frame header), and
			// only then drop the context and the allocator behind those frames.
			state.threadPool.reset();
			state.schedulerManager.reset();

			PromiseContext::SetCurrent(nullptr);
			state.promiseContext.reset();
			state.poolAllocator.reset();

			state.mainThreadId = {};
			state.isInitialized.store(false, std::memory_order_release);
		}

		[[nodiscard]]
		static bool IsInitialized()
		{
			return GetSharedState().isInitialized.load(std::memory_order_acquire);
		}

		[[nodiscard]]
		static TaskSchedulerId GetActivatedSchedulerId()
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			return TaskSchedulerManager::GetActivatedSchedulerId();
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
			TaskSchedulerManager::UpdateActivatedScheduler();
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

		// reservedTaskCount defaults to the value configured at Initialize
		// (TaskSystemConfiguration::reservedTaskCount).
		[[nodiscard]]
		static TaskSchedulerId CreateScheduler(std::optional<std::thread::id> threadId = std::nullopt, std::optional<std::size_t> reservedTaskCount = std::nullopt)
		{
			assert(IsInitialized() && "TaskSystem not initialized. Call TaskSystem::Initialize() first.");
			return GetSchedulerManager().CreateScheduler(
				threadId.value_or(std::this_thread::get_id()),
				reservedTaskCount.value_or(GetSharedState().reservedTaskCount));
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
			std::size_t reservedTaskCount = DefaultReservedTaskCount;
			std::atomic<bool> isInitialized{false};
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
