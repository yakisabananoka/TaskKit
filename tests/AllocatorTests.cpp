#include "TestBase.h"

namespace TKit::Tests
{
	class AllocatorTests : public ::testing::Test
	{
	protected:
		static void CheckPendingTasksAreZero(const TaskSchedulerId& schedulerId)
		{
			const auto pendingCount = TaskSystem::GetPendingTaskCount(schedulerId);
			EXPECT_EQ(pendingCount, 0)
				<< "Test left " << pendingCount << " pending tasks";
		}
	};

	TEST_F(AllocatorTests, CustomAllocatorIntegration)
	{
		int allocCount = 0;
		int deallocCount = 0;

		struct AllocatorContext
		{
			int* allocCount;
			int* deallocCount;
		};

		AllocatorContext allocatorContext{ &allocCount, &deallocCount };

		auto allocate = [](void* ctx, std::size_t size) -> void*
		{
			const auto* context = static_cast<AllocatorContext*>(ctx);
			++(*context->allocCount);
			return ::operator new(size);
		};
		auto deallocate = [](void* ctx, void* ptr, std::size_t size)
		{
			const auto* context = static_cast<AllocatorContext*>(ctx);
			++(*context->deallocCount);
			::operator delete(ptr, size);
		};

		const TaskAllocator customAllocator(&allocatorContext, allocate, deallocate);

		const auto config = TaskSystemConfiguration::Builder()
			.WithCustomAllocator(customAllocator)
			.Build();

		TaskSystem::Initialize(config);

		const auto ids = TaskSystem::GetMainThreadSchedulerIds();
		const auto schedulerId = ids[0];
		{
			auto registration = TaskSystem::ActivateScheduler(schedulerId);

			auto task = []() -> Task<>
			{
				co_return;
			};

			task().Forget();

			EXPECT_GT(allocCount, 0) << "Custom allocator should have been called for allocation";
			EXPECT_GT(deallocCount, 0) << "Custom allocator should have been called for deallocation";

			const int previousAllocCount = allocCount;
			const int previousDeallocCount = deallocCount;

			constexpr int additionalTasks = 10;
			for (int i = 0; i < additionalTasks; ++i)
			{
				auto multiTask = []() -> Task<>
				{
					co_return;
				};
				multiTask().Forget();
			}

			EXPECT_GE(allocCount - previousAllocCount, additionalTasks)
				<< "Should allocate at least once per additional task";
			EXPECT_GE(deallocCount - previousDeallocCount, additionalTasks)
				<< "Should deallocate at least once per additional task";
		}

		CheckPendingTasksAreZero(schedulerId);
		TaskSystem::Shutdown();
	}

	TEST_F(AllocatorTests, DifferentSizedTasks)
	{
		std::size_t minSize = SIZE_MAX;
		std::size_t maxSize = 0;

		struct SizeTracker
		{
			std::size_t* minSize;
			std::size_t* maxSize;
		};

		SizeTracker tracker{ &minSize, &maxSize };

		auto allocate = [](void* ctx, std::size_t size) -> void*
		{
			const auto* tracker = static_cast<SizeTracker*>(ctx);

			if (size < *tracker->minSize)
				*tracker->minSize = size;
			if (size > *tracker->maxSize)
				*tracker->maxSize = size;

			return ::operator new(size);
		};
		auto deallocate = [](void*, void* ptr, std::size_t size)
		{
			::operator delete(ptr, size);
		};

		const TaskAllocator sizeTrackingAllocator(&tracker, allocate, deallocate);

		const auto config = TaskSystemConfiguration::Builder()
			.WithCustomAllocator(sizeTrackingAllocator)
			.Build();

		TaskSystem::Initialize(config);

		const auto ids = TaskSystem::GetMainThreadSchedulerIds();
		const auto schedulerId = ids[0];
		{
			auto registration = TaskSystem::ActivateScheduler(schedulerId);

			auto smallTask = []() -> Task<>
			{
				co_return;
			};

			auto largeTask = []() -> Task<>
			{
				int a = 1, b = 2, c = 3, d = 4, e = 5;
				[[maybe_unused]] double x = 1.0, y = 2.0, z = 3.0;
				co_yield {};
				[[maybe_unused]] int result = a + b + c + d + e;
				co_return;
			};

			smallTask().Forget();
			largeTask().Forget();

			TaskSystem::UpdateActivatedScheduler();

			EXPECT_LT(minSize, SIZE_MAX) << "Should have tracked minimum size";
			EXPECT_GT(maxSize, 0) << "Should have tracked maximum size";
			EXPECT_LT(minSize, maxSize) << "Should have different sized allocations";
		}

		CheckPendingTasksAreZero(schedulerId);
		TaskSystem::Shutdown();
	}

	TEST_F(AllocatorTests, PoolAllocatorReuse)
	{
		int actualNewCalls = 0;
		int allocateCalls = 0;
		int deallocateCalls = 0;
		std::vector<void*> freeList;

		struct PoolContext
		{
			int* actualNewCalls;
			int* allocateCalls;
			int* deallocateCalls;
			std::vector<void*>* freeList;
		};

		PoolContext poolContext{ &actualNewCalls, &allocateCalls, &deallocateCalls, &freeList };

		auto allocate = [](void* ctx, std::size_t size) -> void*
		{
			auto* context = static_cast<PoolContext*>(ctx);
			++(*context->allocateCalls);

			if (!context->freeList->empty())
			{
				void* ptr = context->freeList->back();
				context->freeList->pop_back();
				return ptr;
			}

			++(*context->actualNewCalls);
			return ::operator new(size);
		};
		auto deallocate = [](void* ctx, void* ptr, std::size_t)
		{
			auto* context = static_cast<PoolContext*>(ctx);
			++(*context->deallocateCalls);

			context->freeList->push_back(ptr);
		};

		const TaskAllocator poolAllocator(&poolContext, allocate, deallocate);

		const auto config = TaskSystemConfiguration::Builder()
			.WithCustomAllocator(poolAllocator)
			.Build();

		TaskSystem::Initialize(config);

		const auto ids = TaskSystem::GetMainThreadSchedulerIds();
		const auto schedulerId = ids[0];
		{
			auto registration = TaskSystem::ActivateScheduler(schedulerId);

			constexpr int taskCount = 100;
			for (int i = 0; i < taskCount; ++i)
			{
				auto task = []() -> Task<>
				{
					co_return;
				};
				task().Forget();
			}

			EXPECT_EQ(allocateCalls, taskCount) << "Allocate should be called for each task";
			EXPECT_EQ(deallocateCalls, taskCount) << "Deallocate should be called for each task";
			EXPECT_LT(actualNewCalls, taskCount) << "Actual allocations should be less than task count due to reuse";
			EXPECT_GT(actualNewCalls, 0) << "Should have some actual allocations";
		}

		for (void* ptr : freeList)
		{
			::operator delete(ptr);
		}

		CheckPendingTasksAreZero(schedulerId);
		TaskSystem::Shutdown();
	}

	TEST_F(AllocatorTests, NestedTaskAllocations)
	{
		int allocCount = 0;

		auto allocate = [](void* ctx, std::size_t size) -> void*
		{
			auto* counter = static_cast<int*>(ctx);
			++(*counter);
			return ::operator new(size);
		};
		auto deallocate = [](void*, void* ptr, std::size_t size)
		{
			::operator delete(ptr, size);
		};

		const TaskAllocator trackingAllocator(&allocCount, allocate, deallocate);

		const auto config = TaskSystemConfiguration::Builder()
			.WithCustomAllocator(trackingAllocator)
			.Build();

		TaskSystem::Initialize(config);

		const auto ids = TaskSystem::GetMainThreadSchedulerIds();
		const auto schedulerId = ids[0];
		{
			auto registration = TaskSystem::ActivateScheduler(schedulerId);

			const int initialAllocCount = allocCount;

			auto innerTask = []() -> Task<int>
			{
				co_return 42;
			};

			auto outerTask = [&innerTask]() -> Task<>
			{
				int result = co_await innerTask();
				EXPECT_EQ(result, 42);
				co_return;
			};

			outerTask().Forget();

			TaskSystem::UpdateActivatedScheduler();

			EXPECT_GT(allocCount, initialAllocCount + 1)
				<< "Should allocate for nested tasks";
		}

		TaskSystem::Shutdown();
	}
}
