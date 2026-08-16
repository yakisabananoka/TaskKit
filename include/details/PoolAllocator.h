#ifndef TASKKIT_POOL_ALLOCATOR_H
#define TASKKIT_POOL_ALLOCATOR_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include "TaskAllocator.h"

namespace TKit
{
	// Size-class slab allocator with per-thread pools.
	//
	// - Allocations are served from the calling thread's pool without locking
	//   (the registry mutex is only hit on the first allocation per thread).
	// - Frees on the owning thread push straight onto the local free list (LIFO,
	//   so the block is immediately reusable).
	// - Frees from other threads go onto the owner's lock-free remote list and
	//   are collected by the owner the next time its local free list runs dry.
	class PoolAllocator
	{
	public:
		static constexpr std::array<std::size_t, 9> PoolSizes = {
			48, 64, 128, 256, 512, 1024, 2048, 4096, 8192
		};

	private:
		struct ThreadLocalPool;

		struct alignas(std::max_align_t) BlockMeta
		{
			ThreadLocalPool* ownerPool;
			std::uint8_t poolIndex;
		};

		static constexpr std::size_t AlignedMetaSize =
			Details::AlignUp(sizeof(BlockMeta), alignof(std::max_align_t));

		struct alignas(std::max_align_t) Slab
		{
			Slab* next;
		};

		struct FreeNode
		{
			FreeNode* next;
		};

		struct RemoteFreeNode
		{
			RemoteFreeNode* next;
			std::size_t poolIndex;
		};

		struct PoolState
		{
			FreeNode* freeList = nullptr;
			Slab* slabs = nullptr;
		};

		struct ThreadLocalPool
		{
			PoolAllocator* parent;
			std::uint64_t allocatorId;
			std::thread::id ownerId;
			std::array<PoolState, PoolSizes.size()> pools;

			// On its own cache line: written by other threads, read by the owner.
			alignas(64) std::atomic<RemoteFreeNode*> remoteFreeHead{nullptr};

			ThreadLocalPool(PoolAllocator* p, std::uint64_t id, std::thread::id tid)
				: parent(p), allocatorId(id), ownerId(tid)
			{
			}

			~ThreadLocalPool()
			{
				// Remote-freed blocks live inside the slabs, so releasing the
				// slabs releases them too.
				for (auto& pool : pools)
				{
					Slab* slab = pool.slabs;
					while (slab)
					{
						Slab* next = slab->next;
						::operator delete(slab);
						slab = next;
					}
				}
			}

			void CollectRemoteFree()
			{
				RemoteFreeNode* head = remoteFreeHead.exchange(nullptr, std::memory_order_acquire);

				while (head != nullptr)
				{
					RemoteFreeNode* current = head;
					head = current->next;

					const std::size_t poolIndex = current->poolIndex;

					auto* freeNode = new (current) FreeNode{};
					freeNode->next = pools[poolIndex].freeList;
					pools[poolIndex].freeList = freeNode;
				}
			}

			void PushRemoteFree(void* ptr, std::size_t poolIndex)
			{
				auto* node = new (ptr) RemoteFreeNode{};
				node->poolIndex = poolIndex;

				RemoteFreeNode* oldHead = remoteFreeHead.load(std::memory_order_relaxed);
				do
				{
					node->next = oldHead;
				} while (!remoteFreeHead.compare_exchange_weak(
					oldHead, node,
					std::memory_order_release,
					std::memory_order_relaxed));
			}

			void* AllocateFromPool(std::size_t poolIndex)
			{
				auto& pool = pools[poolIndex];

				if (pool.freeList)
				{
					FreeNode* node = pool.freeList;
					pool.freeList = node->next;
					return node;
				}

				CollectRemoteFree();
				if (pool.freeList)
				{
					FreeNode* node = pool.freeList;
					pool.freeList = node->next;
					return node;
				}

				return AllocateNewSlab(poolIndex);
			}

			void* AllocateNewSlab(std::size_t poolIndex)
			{
				constexpr std::size_t SlabBlockCount = 32;
				const std::size_t userSize = PoolSizes[poolIndex];
				const std::size_t blockSize = userSize + AlignedMetaSize;
				const std::size_t slabDataSize = blockSize * SlabBlockCount;
				const std::size_t slabTotalSize = sizeof(Slab) + slabDataSize;

				void* slabMemory = ::operator new(slabTotalSize);
				auto* slab = new (slabMemory) Slab{};
				slab->next = pools[poolIndex].slabs;
				pools[poolIndex].slabs = slab;

				char* blockStart = reinterpret_cast<char*>(slab) + sizeof(Slab);
				void* result = blockStart;

				for (std::size_t i = 1; i < SlabBlockCount; ++i)
				{
					void* blockAddr = blockStart + i * blockSize;
					auto* node = new (blockAddr) FreeNode{};
					node->next = pools[poolIndex].freeList;
					pools[poolIndex].freeList = node;
				}

				return result;
			}
		};

		struct TlsCacheEntry
		{
			ThreadLocalPool* pool;
			std::uint64_t allocatorId;
		};

		// Deliberately static: a process-wide monotonic id source. Ids are never
		// reused, which is what makes the thread-local cache below safe across
		// allocator instances. Constant-initialized atomic — no init-order or
		// teardown hazards.
		static std::uint64_t GetNextId()
		{
			static constinit std::atomic<std::uint64_t> counter{1};
			return counter.fetch_add(1, std::memory_order_relaxed);
		}

	public:
		PoolAllocator()
			: id_(GetNextId())
		{
		}

		~PoolAllocator()
		{
			std::lock_guard lock(poolsMutex_);
			for (const auto& [threadId, pool] : threadPools_)
			{
				delete pool;
			}
		}

		void* Allocate(std::size_t size)
		{
			ThreadLocalPool* pool = GetOrCreateThreadPool();

			const int poolIndex = FindPoolIndex(size);

			if (poolIndex >= 0)
			{
				void* rawPtr = pool->AllocateFromPool(static_cast<std::size_t>(poolIndex));

				auto* meta = new (rawPtr) BlockMeta{};
				meta->ownerPool = pool;
				meta->poolIndex = static_cast<std::uint8_t>(poolIndex);

				return static_cast<char*>(rawPtr) + AlignedMetaSize;
			}

			// Oversized: plain heap allocation, still prefixed with meta so
			// Deallocate can tell it apart.
			const std::size_t allocSize = size + AlignedMetaSize;
			void* rawPtr = ::operator new(allocSize);

			auto* meta = new (rawPtr) BlockMeta{};
			meta->ownerPool = pool;
			meta->poolIndex = static_cast<std::uint8_t>(PoolSizes.size());

			return static_cast<char*>(rawPtr) + AlignedMetaSize;
		}

		void Deallocate(void* ptr, [[maybe_unused]] std::size_t size)
		{
			if (!ptr)
			{
				return;
			}

			void* rawPtr = static_cast<char*>(ptr) - AlignedMetaSize;
			auto* meta = std::launder(static_cast<BlockMeta*>(rawPtr));

			ThreadLocalPool* ownerPool = meta->ownerPool;
			const std::size_t poolIndex = meta->poolIndex;

			if (poolIndex >= PoolSizes.size())
			{
				::operator delete(rawPtr);
				return;
			}

			if (ownerPool->ownerId == std::this_thread::get_id())
			{
				auto* node = new (rawPtr) FreeNode{};
				node->next = ownerPool->pools[poolIndex].freeList;
				ownerPool->pools[poolIndex].freeList = node;
			}
			else
			{
				ownerPool->PushRemoteFree(rawPtr, poolIndex);
			}
		}

		// Pre-populates the calling thread's pools so the first allocations do
		// not pay for slab creation. Covers size classes up to maxBlockSize.
		void Prewarm(std::size_t maxBlockSize = 1024)
		{
			ThreadLocalPool* pool = GetOrCreateThreadPool();

			for (std::size_t i = 0; i < PoolSizes.size(); ++i)
			{
				if (PoolSizes[i] > maxBlockSize)
				{
					break;
				}

				auto& state = pool->pools[i];
				if (state.freeList != nullptr || state.slabs != nullptr)
				{
					continue;
				}

				void* block = pool->AllocateNewSlab(i);
				auto* node = new (block) FreeNode{};
				node->next = state.freeList;
				state.freeList = node;
			}
		}

		TaskAllocator CreateTaskAllocator()
		{
			return TaskAllocator{
				this,
				[](void* context, std::size_t size) -> void* {
					auto* allocator = static_cast<PoolAllocator*>(context);
					return allocator->Allocate(size);
				},
				[](void* context, void* ptr, std::size_t size) {
					auto* allocator = static_cast<PoolAllocator*>(context);
					allocator->Deallocate(ptr, size);
				}
			};
		}

		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;

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
			return -1;
		}

		ThreadLocalPool* GetOrCreateThreadPool()
		{
			// Deliberately thread-local: this cache is what keeps the allocation
			// hot path lock-free (without it every Allocate would take the
			// registry mutex). Keyed by allocator id: ids are never reused, so a
			// stale entry from a destroyed allocator can never be mistaken for
			// this one. Trivially destructible, so safe at thread teardown.
			thread_local constinit TlsCacheEntry cache = {nullptr, 0};
			if (cache.pool && cache.allocatorId == id_)
			{
				return cache.pool;
			}

			const std::thread::id threadId = std::this_thread::get_id();
			ThreadLocalPool* pool = nullptr;
			{
				std::lock_guard lock(poolsMutex_);

				auto itr = threadPools_.find(threadId);
				if (itr != threadPools_.end())
				{
					pool = itr->second;
				}
				else
				{
					pool = new ThreadLocalPool(this, id_, threadId);
					threadPools_[threadId] = pool;
				}
			}

			cache.allocatorId = id_;
			cache.pool = pool;

			return pool;
		}

		std::uint64_t id_;
		std::unordered_map<std::thread::id, ThreadLocalPool*> threadPools_;
		std::mutex poolsMutex_;
	};
}

#endif //TASKKIT_POOL_ALLOCATOR_H
