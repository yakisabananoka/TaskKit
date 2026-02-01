#ifndef TASKKIT_TASKSCHEDULER_MANAGER_H
#define TASKKIT_TASKSCHEDULER_MANAGER_H
#include <cassert>
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
		TaskSchedulerManager() = default;
		~TaskSchedulerManager() = default;

		TaskSchedulerId CreateScheduler(std::thread::id threadId, std::size_t reservedTaskCount)
		{
			auto& context = threadContexts_[threadId];
			context.schedulers.emplace_back(reservedTaskCount, threadId);
			return {threadId, context.schedulers.size() - 1};
		}

		void Schedule(const TaskSchedulerId& id, std::coroutine_handle<> handle)
		{
			assert(threadContexts_.contains(id.GetThreadId()) && "TaskSchedulerManager: called from unregistered thread");
			auto& schedulers = threadContexts_.at(id.GetThreadId()).schedulers;
			assert(id.GetInternalId() < schedulers.size() && "TaskSchedulerManager: invalid scheduler id");

			schedulers.at(id.GetInternalId()).Schedule(handle);
		}

		void ActivateScheduler(const TaskSchedulerId& id)
		{
			assert(std::this_thread::get_id() == id.GetThreadId() && "TaskSchedulerManager: called from different thread");
			assert(threadContexts_.contains(id.GetThreadId()) && "TaskSchedulerManager: called from unregistered thread");
			const auto& schedulers = threadContexts_.at(id.GetThreadId()).schedulers;
			assert(id.GetInternalId() < schedulers.size() && "TaskSchedulerManager: invalid scheduler id");

			threadContexts_.at(id.GetThreadId()).internalIdStack.push(id.GetInternalId());
		}

		void DeactivateScheduler()
		{
			const auto threadId = std::this_thread::get_id();
			assert(threadContexts_.contains(threadId) && "TaskSchedulerManager: called from unregistered thread");
			const auto& stack = threadContexts_.at(threadId).internalIdStack;
			assert(!stack.empty() && "TaskSchedulerManager: no active scheduler in current context");

			threadContexts_.at(threadId).internalIdStack.pop();
		}

		TaskSchedulerId GetActivatedSchedulerId() const
		{
			const auto threadId = std::this_thread::get_id();
			assert(threadContexts_.contains(threadId) && "TaskSchedulerManager: called from unregistered thread");
			const auto& stack = threadContexts_.at(threadId).internalIdStack;
			assert(!stack.empty() && "TaskSchedulerManager: no active scheduler in current context");

			return { threadId, stack.top() };
		}

		void UpdateActivatedScheduler()
		{
			GetScheduler(GetActivatedSchedulerId()).Update();
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount(const TaskSchedulerId& id) const
		{
			return GetScheduler(id).GetPendingTaskCount();
		}

		[[nodiscard]]
		bool HasSchedulers(std::thread::id threadId) const
		{
			auto it = threadContexts_.find(threadId);
			return it != threadContexts_.end() && !it->second.schedulers.empty();
		}

		TaskSchedulerManager(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager& operator=(const TaskSchedulerManager&) = delete;
		TaskSchedulerManager(TaskSchedulerManager&&) = delete;
		TaskSchedulerManager& operator=(TaskSchedulerManager&&) = delete;

	private:
		TaskScheduler& GetScheduler(const TaskSchedulerId& id)
		{
			assert(threadContexts_.contains(id.GetThreadId()) && "TaskSchedulerManager: called from unregistered thread");
			auto& schedulers = threadContexts_.at(id.GetThreadId()).schedulers;
			assert(id.GetInternalId() < schedulers.size() && "TaskSchedulerManager: invalid scheduler id");
			return schedulers.at(id.GetInternalId());
		}

		const TaskScheduler& GetScheduler(const TaskSchedulerId& id) const
		{
			assert(threadContexts_.contains(id.GetThreadId()) && "TaskSchedulerManager: called from unregistered thread");
			const auto& schedulers = threadContexts_.at(id.GetThreadId()).schedulers;
			assert(id.GetInternalId() < schedulers.size() && "TaskSchedulerManager: invalid scheduler id");
			return schedulers.at(id.GetInternalId());
		}

		std::unordered_map<std::thread::id, ThreadContext> threadContexts_;
	};
}

#endif //TASKKIT_TASKSCHEDULER_MANAGER_H
