#ifndef TASKKIT_TASKSCHEDULER_MANAGER_H
#define TASKKIT_TASKSCHEDULER_MANAGER_H
#include <stack>
#include <thread>
#include <unordered_map>
#include "Exceptions.h"
#include "TaskScheduler.h"
#include "TaskSchedulerId.h"

namespace TKit
{
	class TaskSchedulerManager final
	{
		struct ThreadContext final
		{
			std::stack<std::size_t> internalIdStack;
			std::vector<TaskScheduler> schedulers;
		};
	public:
		explicit TaskSchedulerManager(const std::unordered_map<std::thread::id, std::size_t>& threadIdToTaskSchedulerCount)
		{
			for (const auto& [threadId, count]: threadIdToTaskSchedulerCount)
			{
				threadContexts_.emplace(threadId, ThreadContext{});
				for (std::size_t i = 0; i < count; ++i)
				{
					CreateScheduler(threadId);
				}
			}
		}
		~TaskSchedulerManager() = default;

		void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
		{
			ThrowIfInvalidId(id);
			threadContexts_.at(id.GetThreadId()).schedulers.at(id.GetInternalId()).Schedule(handle);
		}

		void ActivateScheduler(const TaskSchedulerId& id)
		{
			ThrowIfDifferentThread(id);
			ThrowIfInvalidId(id);

			threadContexts_.at(id.GetThreadId()).internalIdStack.push(id.GetInternalId());
		}

		void DeactivateScheduler()
		{
			const auto threadId = std::this_thread::get_id();
			ThrowIfInvalidThread(threadId);
			ThrowIfNoActiveScheduler(threadId);

			auto& stack = threadContexts_.at(threadId).internalIdStack;
			stack.pop();
		}

		TaskSchedulerId GetActivatedSchedulerId() const
		{
			const auto threadId = std::this_thread::get_id();
			ThrowIfInvalidThread(threadId);
			ThrowIfNoActiveScheduler(threadId);

			const auto& stack = threadContexts_.at(threadId).internalIdStack;
			return { threadId, stack.top() };
		}

		void UpdateActivatedScheduler()
		{
			GetScheduler(GetActivatedSchedulerId()).Update();
		}

		std::vector<TaskSchedulerId> GetAllSchedulerIds() const
		{
			std::vector<TaskSchedulerId> ids;
			for (const auto& [threadId, context] : threadContexts_)
			{
				for (std::size_t i = 0; i < context.schedulers.size(); ++i)
				{
					ids.emplace_back(threadId, i);
				}
			}
			return ids;
		}

		std::vector<TaskSchedulerId> GetThreadSchedulerIds(std::thread::id threadId) const
		{
			ThrowIfInvalidThread(threadId);

			std::vector<TaskSchedulerId> ids;
			const auto& schedulers = threadContexts_.at(threadId).schedulers;
			ids.reserve(schedulers.size());
			for (std::size_t i = 0; i < schedulers.size(); ++i)
			{
				ids.emplace_back(threadId, i);
			}
			return ids;
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount(const TaskSchedulerId& id) const
		{
			return GetScheduler(id).GetPendingTaskCount();
		}

		TaskSchedulerManager(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager& operator=(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager(TaskSchedulerManager&&) = delete;
		TaskSchedulerManager& operator=(TaskSchedulerManager&&) = delete;

	private:
		TaskSchedulerId CreateScheduler(std::thread::id threadId)
		{
			auto& schedulers = threadContexts_.at(threadId).schedulers;
			schedulers.emplace_back(100);
			return {threadId, schedulers.size() - 1};
		}

		TaskScheduler& GetScheduler(const TaskSchedulerId& id)
		{
			ThrowIfInvalidId(id);
			return threadContexts_.at(id.GetThreadId()).schedulers.at(id.GetInternalId());
		}

		const TaskScheduler& GetScheduler(const TaskSchedulerId& id) const
		{
			ThrowIfInvalidId(id);
			return threadContexts_.at(id.GetThreadId()).schedulers.at(id.GetInternalId());
		}

		void ThrowIfDifferentThread(const TaskSchedulerId& id) const
		{
			const auto threadId = std::this_thread::get_id();
			if (threadId != id.GetThreadId())
			{
				throw std::runtime_error("TaskSchedulerManager: called from different thread");
			}
		}

		void ThrowIfInvalidThread(std::thread::id id) const
		{
			if (!threadContexts_.contains(id))
			{
				throw std::runtime_error("TaskSchedulerManager: called from unregistered thread");
			}
		}

		void ThrowIfInvalidId(const TaskSchedulerId& id) const
		{
			ThrowIfInvalidThread(id.GetThreadId());
			const auto& schedulers = threadContexts_.at(id.GetThreadId()).schedulers;
			if (id.GetInternalId() >= schedulers.size())
			{
				throw InvalidSchedulerIdError(id);
			}
		}

		void ThrowIfNoActiveScheduler(std::thread::id threadId) const
		{
			const auto& stack = threadContexts_.at(threadId).internalIdStack;
			if (stack.empty())
			{
				throw std::runtime_error("TaskSchedulerManager: no active scheduler in current context");
			}
		}

		std::unordered_map<std::thread::id, ThreadContext> threadContexts_;
	};
}

#endif //TASKKIT_TASKSCHEDULER_MANAGER_H