#ifndef TASKKIT_CUSTOMAWAITER_H
#define TASKKIT_CUSTOMAWAITER_H

namespace TKit
{
	template<typename T>
	class CustomAwaitTransformer;

	template<typename T>
	concept CustomAwaitable = requires()
	{
		{ CustomAwaitTransformer<T>::Transform(std::declval<T>()) };
	};
}

#endif //TASKKIT_CUSTOMAWAITER_H