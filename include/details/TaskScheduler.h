#ifndef TASKKIT_TASK_SCHEDULER_H
#define TASKKIT_TASK_SCHEDULER_H

#include <atomic>
#include <cassert>
#include <chrono>
#include <coroutine>
#include <mutex>
#include <stop_token>
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
	// - Frame/time waits park a handle without a child coroutine: each Update
	//   sweeps them and resumes the ones that became due (or were stopped), so
	//   waiting costs no allocation at all.
	// - The pending count is a dedicated atomic so any thread can poll it in O(1).
	class TaskScheduler final
	{
		struct FrameWait
		{
			std::coroutine_handle<> handle;
			int remainingFrames;
			std::stop_token stopToken;
		};

		struct TimeWait
		{
			std::coroutine_handle<> handle;
			std::chrono::steady_clock::time_point due;
			std::stop_token stopToken;
		};

	public:
		explicit TaskScheduler(std::size_t reservedTaskCount, std::thread::id ownerId = std::this_thread::get_id()) :
			ownerId_(ownerId)
		{
			local_.reserve(reservedTaskCount);
			running_.reserve(reservedTaskCount);
			frameWaits_.reserve(reservedTaskCount);
			timeWaits_.reserve(reservedTaskCount);
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
			for (const auto& wait : frameWaits_)
			{
				wait.handle.destroy();
			}
			for (const auto& wait : timeWaits_)
			{
				wait.handle.destroy();
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

		// Parks `handle` for `frameCount` updates. Owner thread only (wait
		// awaiters always target the scheduler running on the current thread).
		void ScheduleFrameWait(std::coroutine_handle<> handle, int frameCount, std::stop_token stopToken)
		{
			assert(std::this_thread::get_id() == ownerId_ && "TaskScheduler::ScheduleFrameWait: called off the owner thread");
			assert(frameCount > 0 && "TaskScheduler::ScheduleFrameWait: frameCount must be positive");

			pendingCount_.fetch_add(1, std::memory_order_release);
			frameWaits_.push_back({handle, frameCount, std::move(stopToken)});
		}

		// Parks `handle` until `due` (checked once per Update). Owner thread only.
		void ScheduleTimeWait(std::coroutine_handle<> handle, std::chrono::steady_clock::time_point due, std::stop_token stopToken)
		{
			assert(std::this_thread::get_id() == ownerId_ && "TaskScheduler::ScheduleTimeWait: called off the owner thread");

			pendingCount_.fetch_add(1, std::memory_order_release);
			timeWaits_.push_back({handle, due, std::move(stopToken)});
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

			SweepFrameWaits();
			SweepTimeWaits();

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
		// Both sweeps append due handles to local_ so they run in this Update,
		// and compact the wait list in place (no allocation at steady state).
		void SweepFrameWaits()
		{
			std::size_t keep = 0;
			for (std::size_t i = 0; i < frameWaits_.size(); ++i)
			{
				auto& wait = frameWaits_[i];
				--wait.remainingFrames;
				if (wait.remainingFrames <= 0 || wait.stopToken.stop_requested())
				{
					local_.push_back(wait.handle);
				}
				else
				{
					if (keep != i)
					{
						frameWaits_[keep] = std::move(wait);
					}
					++keep;
				}
			}
			frameWaits_.resize(keep);
		}

		void SweepTimeWaits()
		{
			if (timeWaits_.empty())
			{
				return;
			}

			const auto now = std::chrono::steady_clock::now();
			std::size_t keep = 0;
			for (std::size_t i = 0; i < timeWaits_.size(); ++i)
			{
				auto& wait = timeWaits_[i];
				if (now >= wait.due || wait.stopToken.stop_requested())
				{
					local_.push_back(wait.handle);
				}
				else
				{
					if (keep != i)
					{
						timeWaits_[keep] = std::move(wait);
					}
					++keep;
				}
			}
			timeWaits_.resize(keep);
		}

		std::thread::id ownerId_;
		std::vector<std::coroutine_handle<>> local_;
		std::vector<std::coroutine_handle<>> running_;
		std::vector<FrameWait> frameWaits_;
		std::vector<TimeWait> timeWaits_;
		std::mutex remoteMutex_;
		std::vector<std::coroutine_handle<>> remote_;
		alignas(64) std::atomic<std::size_t> pendingCount_{0};
	};
}

#endif //TASKKIT_TASK_SCHEDULER_H
