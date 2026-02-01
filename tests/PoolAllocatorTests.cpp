#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <set>
#include "details/PoolAllocator.h"

namespace TKit::Tests
{
	class PoolAllocatorTests : public ::testing::Test
	{
	protected:
		static constexpr std::size_t SlabBlockCount = 32;
		PoolAllocator allocator_;
	};

	TEST_F(PoolAllocatorTests, BasicAllocateDeallocate)
	{
		void* ptr1 = allocator_.Allocate(64);
		ASSERT_NE(ptr1, nullptr);

		void* ptr2 = allocator_.Allocate(128);
		ASSERT_NE(ptr2, nullptr);

		void* ptr3 = allocator_.Allocate(256);
		ASSERT_NE(ptr3, nullptr);

		allocator_.Deallocate(ptr1, 64);
		allocator_.Deallocate(ptr2, 128);
		allocator_.Deallocate(ptr3, 256);

		void* ptr4 = allocator_.Allocate(64);
		EXPECT_EQ(ptr4, ptr1) << "Local free should be immediately reusable (LIFO)";

		allocator_.Deallocate(ptr4, 64);
	}

	TEST_F(PoolAllocatorTests, LargeAllocation)
	{
		constexpr std::size_t largeSize = 16384;
		void* ptr = allocator_.Allocate(largeSize);
		ASSERT_NE(ptr, nullptr);

		allocator_.Deallocate(ptr, largeSize);
	}

	TEST_F(PoolAllocatorTests, SlabAllocation)
	{
		std::vector<void*> pointers;
		pointers.reserve(SlabBlockCount);

		for (std::size_t i = 0; i < SlabBlockCount; ++i)
		{
			void* ptr = allocator_.Allocate(64);
			ASSERT_NE(ptr, nullptr);
			pointers.push_back(ptr);
		}

		std::set<void*> uniquePointers(pointers.begin(), pointers.end());
		EXPECT_EQ(uniquePointers.size(), SlabBlockCount)
			<< "All pointers should be unique";

		for (void* ptr : pointers)
		{
			allocator_.Deallocate(ptr, 64);
		}
	}

	TEST_F(PoolAllocatorTests, RemoteFreeFromSingleThread)
	{
		void* ptr = allocator_.Allocate(64);
		ASSERT_NE(ptr, nullptr);

		std::atomic<bool> deallocated{false};

		std::thread remoteThread([&]()
		{
			allocator_.Deallocate(ptr, 64);
			deallocated.store(true, std::memory_order_release);
		});

		remoteThread.join();
		EXPECT_TRUE(deallocated.load(std::memory_order_acquire));

		std::vector<void*> allocated;
		bool foundPtr = false;

		for (int i = 0; i < 100 && !foundPtr; ++i)
		{
			void* p = allocator_.Allocate(64);
			if (p == ptr)
			{
				foundPtr = true;
			}
			allocated.push_back(p);
		}

		EXPECT_TRUE(foundPtr) << "RemoteFree memory should eventually be reused";

		for (void* p : allocated)
		{
			allocator_.Deallocate(p, 64);
		}
	}

	TEST_F(PoolAllocatorTests, RemoteFreeFromMultipleThreads)
	{
		constexpr int numPointers = 100;
		std::vector<void*> pointers(numPointers);

		for (int i = 0; i < numPointers; ++i)
		{
			pointers[i] = allocator_.Allocate(64);
			ASSERT_NE(pointers[i], nullptr);
		}

		std::set<void*> originalPointers(pointers.begin(), pointers.end());

		constexpr int numThreads = 4;
		const int pointersPerThread = numPointers / numThreads;

		std::vector<std::thread> threads;
		threads.reserve(numThreads);

		for (int t = 0; t < numThreads; ++t)
		{
			const int start = t * pointersPerThread;
			const int end = (t == numThreads - 1) ? numPointers : start + pointersPerThread;

			threads.emplace_back([&, start, end]()
			{
				for (int i = start; i < end; ++i)
				{
					allocator_.Deallocate(pointers[i], 64);
				}
			});
		}

		for (auto& thread : threads)
		{
			thread.join();
		}

		std::vector<void*> reusedPointers;
		reusedPointers.reserve(numPointers * 2);

		for (int i = 0; i < numPointers * 2; ++i)
		{
			reusedPointers.push_back(allocator_.Allocate(64));
		}

		int reusedCount = 0;
		for (void* ptr : reusedPointers)
		{
			if (originalPointers.contains(ptr))
			{
				++reusedCount;
			}
		}

		EXPECT_EQ(reusedCount, numPointers)
			<< "All RemoteFree memory should eventually be reused";

		for (void* ptr : reusedPointers)
		{
			allocator_.Deallocate(ptr, 64);
		}
	}

	TEST_F(PoolAllocatorTests, ConcurrentAllocateAndRemoteFree)
	{
		constexpr int numIterations = 1000;
		std::atomic<int> allocCount{0};
		std::atomic<int> deallocCount{0};

		std::atomic<void*> sharedPtr{nullptr};
		std::atomic<bool> done{false};

		std::thread producerThread([&]()
		{
			for (int i = 0; i < numIterations; ++i)
			{
				void* ptr = allocator_.Allocate(128);
				allocCount.fetch_add(1, std::memory_order_relaxed);

				while (sharedPtr.load(std::memory_order_acquire) != nullptr)
				{
					std::this_thread::yield();
				}
				sharedPtr.store(ptr, std::memory_order_release);
			}
			done.store(true, std::memory_order_release);
		});

		std::thread consumerThread([&]()
		{
			while (!done.load(std::memory_order_acquire) ||
			       sharedPtr.load(std::memory_order_acquire) != nullptr)
			{
				void* ptr = sharedPtr.exchange(nullptr, std::memory_order_acq_rel);
				if (ptr != nullptr)
				{
					allocator_.Deallocate(ptr, 128);
					deallocCount.fetch_add(1, std::memory_order_relaxed);
				}
				else
				{
					std::this_thread::yield();
				}
			}
		});

		producerThread.join();
		consumerThread.join();

		EXPECT_EQ(allocCount.load(), numIterations);
		EXPECT_EQ(deallocCount.load(), numIterations);
	}

	TEST_F(PoolAllocatorTests, MultiplePoolSizes)
	{
		std::vector<std::pair<void*, std::size_t>> allocations;

		for (std::size_t size : PoolAllocator::PoolSizes)
		{
			void* ptr = allocator_.Allocate(size);
			ASSERT_NE(ptr, nullptr);
			allocations.emplace_back(ptr, size);
		}

		std::set<void*> originalPointers;
		for (const auto& [ptr, size] : allocations)
		{
			originalPointers.insert(ptr);
		}

		std::thread remoteThread([&]()
		{
			for (const auto& [ptr, size] : allocations)
			{
				allocator_.Deallocate(ptr, size);
			}
		});

		remoteThread.join();

		for (std::size_t i = 0; i < PoolAllocator::PoolSizes.size(); ++i)
		{
			const std::size_t size = PoolAllocator::PoolSizes[i];
			void* targetPtr = allocations[i].first;

			std::vector<void*> allocated;
			bool found = false;

			for (int j = 0; j < 64 && !found; ++j)
			{
				void* ptr = allocator_.Allocate(size);
				if (ptr == targetPtr)
				{
					found = true;
				}
				allocated.push_back(ptr);
			}

			EXPECT_TRUE(found)
				<< "RemoteFree memory should be reused for size " << size;

			for (void* p : allocated)
			{
				allocator_.Deallocate(p, size);
			}
		}
	}

	TEST_F(PoolAllocatorTests, ThreadLocalPoolIsolation)
	{
		std::atomic<void*> thread1Ptr{nullptr};
		std::atomic<void*> thread2Ptr{nullptr};

		std::thread thread1([&]()
		{
			void* ptr = allocator_.Allocate(64);
			thread1Ptr.store(ptr, std::memory_order_release);

			while (thread2Ptr.load(std::memory_order_acquire) == nullptr)
			{
				std::this_thread::yield();
			}

			allocator_.Deallocate(ptr, 64);

			void* ptr2 = allocator_.Allocate(64);
			EXPECT_EQ(ptr2, ptr) << "Local free should be immediately reusable";
			allocator_.Deallocate(ptr2, 64);
		});

		std::thread thread2([&]()
		{
			void* ptr = allocator_.Allocate(64);
			thread2Ptr.store(ptr, std::memory_order_release);

			while (thread1Ptr.load(std::memory_order_acquire) == nullptr)
			{
				std::this_thread::yield();
			}

			allocator_.Deallocate(ptr, 64);

			void* ptr2 = allocator_.Allocate(64);
			EXPECT_EQ(ptr2, ptr) << "Local free should be immediately reusable";
			allocator_.Deallocate(ptr2, 64);
		});

		thread1.join();
		thread2.join();

		EXPECT_NE(thread1Ptr.load(), thread2Ptr.load())
			<< "Different threads should allocate from different Slabs";
	}

	TEST_F(PoolAllocatorTests, MemoryReuseEfficiency)
	{
		constexpr int iterations = 1000;
		std::set<void*> allPointers;

		for (int i = 0; i < iterations; ++i)
		{
			void* ptr = allocator_.Allocate(64);
			ASSERT_NE(ptr, nullptr);
			allPointers.insert(ptr);
			allocator_.Deallocate(ptr, 64);
		}

		EXPECT_EQ(allPointers.size(), 1u)
			<< "Memory should be reused immediately after local free";
	}

	TEST_F(PoolAllocatorTests, DeallocateIgnoresSize)
	{
		void* ptr = allocator_.Allocate(64);
		ASSERT_NE(ptr, nullptr);

		allocator_.Deallocate(ptr, 999);

		void* ptr2 = allocator_.Allocate(64);
		EXPECT_EQ(ptr2, ptr) << "Memory should be correctly freed even with wrong size";

		allocator_.Deallocate(ptr2, 64);
	}

	TEST_F(PoolAllocatorTests, StressTest)
	{
		constexpr int numThreads = 8;
		constexpr int operationsPerThread = 10000;

		std::vector<std::thread> threads;
		std::atomic<int> totalAllocs{0};
		std::atomic<int> totalDeallocs{0};

		for (int t = 0; t < numThreads; ++t)
		{
			threads.emplace_back([&]()
			{
				std::vector<std::pair<void*, std::size_t>> localPtrs;
				localPtrs.reserve(100);

				for (int i = 0; i < operationsPerThread; ++i)
				{
					if (localPtrs.empty() || (i % 3 != 0))
					{
						std::size_t size = 64 + (i % 8) * 64;
						void* ptr = allocator_.Allocate(size);
						localPtrs.emplace_back(ptr, size);
						totalAllocs.fetch_add(1, std::memory_order_relaxed);
					}
					else
					{
						auto [ptr, size] = localPtrs.back();
						localPtrs.pop_back();
						allocator_.Deallocate(ptr, size);
						totalDeallocs.fetch_add(1, std::memory_order_relaxed);
					}
				}

				for (auto [ptr, size] : localPtrs)
				{
					allocator_.Deallocate(ptr, size);
					totalDeallocs.fetch_add(1, std::memory_order_relaxed);
				}
			});
		}

		for (auto& thread : threads)
		{
			thread.join();
		}

		EXPECT_EQ(totalAllocs.load(), totalDeallocs.load())
			<< "All allocations should be deallocated";
	}

	TEST_F(PoolAllocatorTests, StressTestWithMixedSizes)
	{
		constexpr int numThreads = 4;
		constexpr int operationsPerThread = 5000;

		std::vector<std::thread> threads;

		for (int t = 0; t < numThreads; ++t)
		{
			threads.emplace_back([&, t]()
			{
				std::vector<std::pair<void*, std::size_t>> localPtrs;

				for (int i = 0; i < operationsPerThread; ++i)
				{
					std::size_t size = PoolAllocator::PoolSizes[(t + i) % PoolAllocator::PoolSizes.size()];

					void* ptr = allocator_.Allocate(size);
					ASSERT_NE(ptr, nullptr);
					localPtrs.emplace_back(ptr, size);

					if (localPtrs.size() > 50)
					{
						for (int j = 0; j < 25; ++j)
						{
							auto [p, s] = localPtrs.back();
							localPtrs.pop_back();
							allocator_.Deallocate(p, s);
						}
					}
				}

				for (auto [ptr, size] : localPtrs)
				{
					allocator_.Deallocate(ptr, size);
				}
			});
		}

		for (auto& thread : threads)
		{
			thread.join();
		}
	}
}
