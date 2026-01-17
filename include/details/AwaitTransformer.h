#ifndef TASKKIT_AWAIT_TRANSFORMER_H
#define TASKKIT_AWAIT_TRANSFORMER_H
#include <utility>

namespace TKit
{
	template<typename T>
	class AwaitTransformer;

	template<typename T>
	concept Awaitable = requires()
	{
		{ AwaitTransformer<T>::Transform(std::declval<T>()) };
	};
}

#endif //TASKKIT_AWAIT_TRANSFORMER_H