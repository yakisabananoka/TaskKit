#ifndef TASKKIT_AWAIT_TRANSFORMER_H
#define TASKKIT_AWAIT_TRANSFORMER_H

#include <concepts>
#include <coroutine>
#include <type_traits>
#include <utility>

namespace TKit
{
	template<typename T>
	class AwaitTransformer;

	// The standard awaiter interface. Anything satisfying it passes through
	// AwaitTransformer untouched, so plain awaiter types need no specialization
	// of their own.
	template<typename T>
	concept DirectAwaiter = requires(T& awaiter, std::coroutine_handle<> handle)
	{
		{ awaiter.await_ready() } -> std::convertible_to<bool>;
		awaiter.await_suspend(handle);
		awaiter.await_resume();
	};

	template<DirectAwaiter T>
	class AwaitTransformer<T>
	{
	public:
		static T Transform(T awaiter)
		{
			return awaiter;
		}
	};

	// A type can be co_awaited inside a Task if an AwaitTransformer turns it
	// into an awaiter. The transformer is looked up on the decayed type, so
	// lvalues of copyable awaiters work too.
	template<typename T>
	concept Awaitable = requires()
	{
		{ AwaitTransformer<std::decay_t<T>>::Transform(std::declval<T>()) };
	};
}

#endif //TASKKIT_AWAIT_TRANSFORMER_H
