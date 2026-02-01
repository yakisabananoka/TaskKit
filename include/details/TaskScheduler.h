#ifndef TASKKIT_TASK_SCHEDULER_H
#define TASKKIT_TASK_SCHEDULER_H

#include <atomic>
#include <coroutine>
#include <vector>
#include <thread>

namespace TKit
{
	class TaskScheduler final
	{
		struct RemoteNode
		{
			RemoteNode* next;
			std::coroutine_handle<> handle;
		};

	public:
		explicit TaskScheduler(std::size_t reservedTaskCount, std::thread::id ownerId = std::thread::id{}) :
			ownerId_(ownerId == std::thread::id{} ? std::this_thread::get_id() : ownerId)
		{
			handles_.reserve(reservedTaskCount);
			updateHandles_.reserve(reservedTaskCount);
		}

		~TaskScheduler()
		{
			for (const auto& handle : handles_)
			{
				handle.destroy();
			}
			for (const auto& handle : updateHandles_)
			{
				handle.destroy();
			}

			RemoteNode* head = remoteHead_.load(std::memory_order_acquire);
			while (head)
			{
				RemoteNode* current = head;
				head = head->next;
				current->handle.destroy();
				delete current;
			}
		}

		void Update()
		{
			CollectRemote();
			std::swap(updateHandles_, handles_);

			for (const auto& handle : updateHandles_)
			{
				handle.resume();
			}
			updateHandles_.clear();
		}

		void Schedule(std::coroutine_handle<> handle)
		{
			if (std::this_thread::get_id() == ownerId_)
			{
				handles_.emplace_back(handle);
			}
			else
			{
				PushRemote(handle);
			}
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount() const
		{
			std::size_t count = handles_.size();

			RemoteNode* node = remoteHead_.load(std::memory_order_acquire);
			while (node)
			{
				++count;
				node = node->next;
			}

			return count;
		}

		TaskScheduler(const TaskScheduler&) = delete;
		TaskScheduler& operator=(const TaskScheduler&) = delete;

		TaskScheduler(TaskScheduler&& other) noexcept :
			ownerId_(other.ownerId_),
			handles_(std::move(other.handles_)),
			updateHandles_(std::move(other.updateHandles_)),
			remoteHead_(other.remoteHead_.exchange(nullptr, std::memory_order_acquire))
		{
		}

		TaskScheduler& operator=(TaskScheduler&& other) noexcept
		{
			if (this != &other)
			{
				ownerId_ = other.ownerId_;
				handles_ = std::move(other.handles_);
				updateHandles_ = std::move(other.updateHandles_);

				RemoteNode* oldHead = remoteHead_.exchange(nullptr, std::memory_order_acquire);
				while (oldHead)
				{
					RemoteNode* next = oldHead->next;
					delete oldHead;
					oldHead = next;
				}
				remoteHead_.store(
					other.remoteHead_.exchange(nullptr, std::memory_order_acquire),
					std::memory_order_release
				);
			}
			return *this;
		}

	private:
		void CollectRemote()
		{
			RemoteNode* head = remoteHead_.exchange(nullptr, std::memory_order_acquire);
			while (head)
			{
				RemoteNode* current = head;
				head = head->next;
				handles_.emplace_back(current->handle);
				delete current;
			}
		}

		void PushRemote(std::coroutine_handle<> handle)
		{
			auto* node = new RemoteNode{nullptr, handle};

			RemoteNode* oldHead = remoteHead_.load(std::memory_order_relaxed);
			do
			{
				node->next = oldHead;
			} while (!remoteHead_.compare_exchange_weak(
				oldHead, node,
				std::memory_order_release,
				std::memory_order_relaxed));
		}

		std::thread::id ownerId_;
		std::vector<std::coroutine_handle<>> handles_;
		std::vector<std::coroutine_handle<>> updateHandles_;
		std::atomic<RemoteNode*> remoteHead_{nullptr};
	};
}

#endif //TASKKIT_TASK_SCHEDULER_H
