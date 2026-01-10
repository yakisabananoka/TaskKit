#ifndef TASKKIT_TASK_ALLOCATOR_H
#define TASKKIT_TASK_ALLOCATOR_H

#include <cstddef>
#include <new>

namespace TKit
{
	class TaskAllocator
	{
	public:
		using AllocateFunc = void* (*)(void* context, std::size_t size);
		using DeallocateFunc = void (*)(void* context, void* ptr, std::size_t size);

		TaskAllocator() : TaskAllocator(nullptr, nullptr, nullptr)
		{
		}

		TaskAllocator(void* context, AllocateFunc allocate, DeallocateFunc deallocate) :
			context_(context),
			allocate_(allocate),
			deallocate_(deallocate)
		{
			if (!allocate_)
			{
				allocate_ = [](void*, std::size_t size) -> void*
				{
					return ::operator new(size);
				};
			}

			if (!deallocate_)
			{
				deallocate_ = [](void*, void* ptr, std::size_t size)
				{
					::operator delete(ptr, size);
				};
			}
		}

		[[nodiscard]]
		void* Allocate(std::size_t size) const
		{
			return allocate_(context_, size);
		}

		void Deallocate(void* ptr, std::size_t size) const
		{
			deallocate_(context_, ptr, size);
		}

		[[nodiscard]]
		void* GetContext() const noexcept
		{
			return context_;
		}

	private:
		void* context_;
		AllocateFunc allocate_;
		DeallocateFunc deallocate_;
	};
}

#endif //TASKKIT_TASK_ALLOCATOR_H
