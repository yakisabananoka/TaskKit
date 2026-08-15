#ifndef TASKKIT_TASK_SCHEDULER_H
#define TASKKIT_TASK_SCHEDULER_H

#include <atomic>
#include <cassert>
#include <coroutine>
#include <mutex>
#include <thread>
#include <vector>

namespace TKit
{
	// A frame-based coroutine queue owned by a single thread.
	//
	// - Schedule() may be called from any thread. The owner thread appends to a
	//   plain vector; other threads go through a mutex-guarded inbox.
	// - Update() may only be called on the owner thread. It resumes everything
	//   scheduled before the call; handles scheduled during Update() (e.g. by a
	//   coroutine yielding again) run on the next Update().
	// - The pending count is a dedicated atomic so any thread can poll it in O(1).
	class TaskScheduler final
	{
	public:
		explicit TaskScheduler(std::size_t reservedTaskCount, std::thread::id ownerId = std::this_thread::get_id()) :
			ownerId_(ownerId)
		{
			local_.reserve(reservedTaskCount);
			running_.reserve(reservedTaskCount);
		}

		~TaskScheduler()
		{
			for (const auto handle : local_)
			{
				handle.destroy();
			}
			for (const auto handle : remote_)
			{
				handle.destroy();
			}
		}

		void Schedule(std::coroutine_handle<> handle)
		{
			// Increment before publishing the handle: it can only be resumed (and
			// the count decremented) after it became visible, so the counter can
			// never underflow.
			pendingCount_.fetch_add(1, std::memory_order_release);

			if (std::this_thread::get_id() == ownerId_)
			{
				local_.push_back(handle);
			}
			else
			{
				std::lock_guard lock(remoteMutex_);
				remote_.push_back(handle);
			}
		}

		void Update()
		{
			assert(std::this_thread::get_id() == ownerId_ && "TaskScheduler::Update: called off the owner thread");
			assert(running_.empty() && "TaskScheduler::Update: reentrant update is not supported");

			{
				std::lock_guard lock(remoteMutex_);
				local_.insert(local_.end(), remote_.begin(), remote_.end());
				remote_.clear();
			}

			running_.swap(local_);

			for (const auto handle : running_)
			{
				handle.resume();
				pendingCount_.fetch_sub(1, std::memory_order_release);
			}
			running_.clear();
		}

		[[nodiscard]]
		std::size_t GetPendingTaskCount() const noexcept
		{
			return pendingCount_.load(std::memory_order_acquire);
		}

		[[nodiscard]]
		std::thread::id GetOwnerThreadId() const noexcept
		{
			return ownerId_;
		}

		TaskScheduler(const TaskScheduler&) = delete;
		TaskScheduler& operator=(const TaskScheduler&) = delete;
		TaskScheduler(TaskScheduler&&) = delete;
		TaskScheduler& operator=(TaskScheduler&&) = delete;

	private:
		std::thread::id ownerId_;
		std::vector<std::coroutine_handle<>> local_;
		std::vector<std::coroutine_handle<>> running_;
		std::mutex remoteMutex_;
		std::vector<std::coroutine_handle<>> remote_;
		alignas(64) std::atomic<std::size_t> pendingCount_{0};
	};
}

#endif //TASKKIT_TASK_SCHEDULER_H
