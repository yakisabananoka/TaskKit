#ifndef TASKKIT_TASK_H
#define TASKKIT_TASK_H

#include <coroutine>
#include <variant>
#include <cstdio>
#include "PromiseBase.h"
#include "TaskSystem.h"
#include "AwaitTransformer.h"

namespace TKit
{
	template<typename T = void>
	class [[nodiscard]] Task final
	{
	public:
		class Awaiter;
		class Promise;
		using Handle = std::coroutine_handle<Promise>;

		using promise_type = Promise;

		~Task()
		{
			if (handle_)
			{
				handle_.destroy();
			}
		}

		void Forget() &&
		{
			if (!handle_)
			{
				return;
			}

			if (handle_.promise().IsReady())
			{
				handle_.destroy();
				handle_ = nullptr;
				return;
			}

			handle_.promise().MarkAsForgotten();
			handle_ = nullptr;
		}

		[[nodiscard]]
		bool IsReady() const noexcept
		{
			return !handle_ || handle_.promise().IsReady();
		}

		[[nodiscard]]
		Task<std::monostate> ToMonostateTask() &&
		{
			co_await std::move(*this);
			co_return std::monostate{};
		}

		operator Task<>() &&
		{
			return Task(std::move(handle_));
		}

		Task(const Task&) = delete;
		Task& operator=(const Task&) = delete;
		Task(Task&& other) noexcept :
			handle_(other.handle_)
		{
			other.handle_ = nullptr;
		}
		Task& operator=(Task&& other) noexcept
		{
			if (this != &other)
			{
				if (handle_)
				{
					handle_.destroy();
				}
				handle_ = other.handle_;
				other.handle_ = nullptr;
			}
			return *this;
		}

	private:
		explicit Task(Handle handle) :
			handle_(std::move(handle))
		{
		}

		Handle handle_;
	};

	template<typename T>
	class Task<T>::Awaiter
	{
	public:
		explicit Awaiter(Task&& task) :
			task_(std::move(task))
		{
		}

		[[nodiscard]]
		bool await_ready() const noexcept
		{
			return GetPromise().IsReady();
		}

		void await_suspend(std::coroutine_handle<> awaitingHandle) noexcept
		{
			GetPromise().SetContinuation(awaitingHandle);
		}

		T await_resume()
		{
			if constexpr (std::is_void_v<T>)
			{
				GetPromise().Get();
				return;
			}
			else
			{
				return GetPromise().Get();
			}
		}

	private:
		Promise& GetPromise() const noexcept
		{
			return task_.handle_.promise();
		}

		Task task_;
	};

	template<typename T>
	class Task<T>::Promise final : public PromiseBase<T>
	{
	public:
		void* operator new(std::size_t size)
		{
			if (!TaskSystem::IsInitialized())
			{
				return ::operator new(size);
			}

			return TaskSystem::GetAllocator().Allocate(size);
		}

		void operator delete(void* ptr, std::size_t size) noexcept
		{
			if (!TaskSystem::IsInitialized())
			{
				::operator delete(ptr, size);
				return;
			}

			TaskSystem::GetAllocator().Deallocate(ptr, size);
		}

		[[nodiscard]]
		std::coroutine_handle<> GetContinuation() const noexcept
		{
			return continuation_ ? continuation_ : std::noop_coroutine();
		}

		void SetContinuation(std::coroutine_handle<> continuation) noexcept
		{
			continuation_ = continuation;
		}

		void MarkAsForgotten() noexcept
		{
			isForgotten_ = true;
		}

		template<Awaitable U>
		auto await_transform(U&& awaitable) noexcept
		{
			return AwaitTransformer<std::decay_t<U>>::Transform(std::forward<U>(awaitable));
		}

		auto get_return_object()
		{
			return Task{ Handle::from_promise(*this) };
		}

		std::suspend_always yield_value(std::monostate)
		{
			TaskSystem::Schedule(TaskSystem::GetActivatedSchedulerId(), Handle::from_promise(*this));
			return {};
		}

		std::suspend_never initial_suspend() noexcept
		{
			return {};
		}

		auto final_suspend() noexcept
		{
			struct FinalAwaiter
			{
				[[nodiscard]]
				bool await_ready() const noexcept
				{
					return false;
				}

				std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept
				{
					auto& promise = handle.promise();
					if (promise.isForgotten_)
					{
						handle.destroy();
						return std::noop_coroutine();
					}
					return promise.GetContinuation();
				}

				void await_resume() noexcept
				{
				}
			};

			return FinalAwaiter{};
		}

	private:
		std::coroutine_handle<> continuation_;
		bool isForgotten_ = false;
	};

	template<typename T>
	class AwaitTransformer<Task<T>>
	{
	public:
		static auto Transform(Task<T>&& task) noexcept
		{
			return typename Task<T>::Awaiter{ std::move(task) };
		}
	};
}

#endif //TASKKIT_TASK_H
