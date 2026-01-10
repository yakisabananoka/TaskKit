#ifndef TASKKIT_POOL_ALLOCATOR_H
#define TASKKIT_POOL_ALLOCATOR_H

#include <cstddef>
#include <vector>
#include <array>
#include <new>
#include "TaskAllocator.h"

namespace TKit
{
	class PoolAllocator
	{
	public:
		static constexpr std::array<std::size_t, 8> PoolSizes = {
			64, 128, 256, 512, 1024, 2048, 4096, 8192
		};

		PoolAllocator() = default;
		~PoolAllocator()
		{
			for (std::size_t i = 0; i < pools_.size(); ++i)
			{
				auto& pool = pools_[i];
				const std::size_t size = PoolSizes[i];
				for (void* ptr : pool)
				{
					::operator delete(ptr, size);
				}
			}
		}

		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;

		void* Allocate(std::size_t size)
		{
			const int poolIndex = FindPoolIndex(size);

			if (poolIndex >= 0)
			{
				auto& pool = pools_[poolIndex];
				if (!pool.empty())
				{
					void* ptr = pool.back();
					pool.pop_back();
					return ptr;
				}

				const std::size_t allocSize = PoolSizes[poolIndex];
				return ::operator new(allocSize);
			}

			return ::operator new(size);
		}

		void Deallocate(void* ptr, std::size_t size)
		{
			if (!ptr)
			{
				return;
			}

			if (size == 0)
			{
				::operator delete(ptr);
				return;
			}

			const int poolIndex = FindPoolIndex(size);
			if (poolIndex >= 0)
			{
				pools_[poolIndex].push_back(ptr);
			}
			else
			{
				::operator delete(ptr, size);
			}
		}

		TaskAllocator CreateTaskAllocator()
		{
			return TaskAllocator{
				this,
				[](void* context, std::size_t size) -> void* {
					auto* pool = static_cast<PoolAllocator*>(context);
					return pool->Allocate(size);
				},
				[](void* context, void* ptr, std::size_t size) {
					auto* pool = static_cast<PoolAllocator*>(context);
					pool->Deallocate(ptr, size);
				}
			};
		}

	private:
		[[nodiscard]]
		static int FindPoolIndex(std::size_t size)
		{
			for (int i = 0; i < static_cast<int>(PoolSizes.size()); ++i)
			{
				if (size <= PoolSizes[i])
				{
					return i;
				}
			}
			return -1; // Size too large
		}

		std::array<std::vector<void*>, PoolSizes.size()> pools_;
	};
}

#endif //TASKKIT_POOL_ALLOCATOR_H
