// Micro-benchmark for TaskKit allocation behavior and throughput.
//
// Tracks two counters per scenario:
//  - heap allocs:  every global operator new (slabs, shared states, containers)
//  - frame allocs: every allocation requested through the TaskAllocator
// Each scenario runs once as warm-up, then once measured, so the numbers show
// steady-state behavior.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include "TaskKit.h"

namespace
{
	std::atomic<std::size_t> g_heapAllocCount{0};
	std::atomic<std::size_t> g_heapAllocBytes{0};
	std::atomic<std::size_t> g_frameAllocCount{0};
}

void* operator new(std::size_t size)
{
	g_heapAllocCount.fetch_add(1, std::memory_order_relaxed);
	g_heapAllocBytes.fetch_add(size, std::memory_order_relaxed);
	if (void* ptr = std::malloc(size))
	{
		return ptr;
	}
	throw std::bad_alloc{};
}

void* operator new[](std::size_t size)
{
	g_heapAllocCount.fetch_add(1, std::memory_order_relaxed);
	g_heapAllocBytes.fetch_add(size, std::memory_order_relaxed);
	if (void* ptr = std::malloc(size))
	{
		return ptr;
	}
	throw std::bad_alloc{};
}

void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

namespace
{
	using namespace std::chrono_literals;
	using Clock = std::chrono::steady_clock;

	struct Snapshot
	{
		std::size_t heapAllocs;
		std::size_t heapBytes;
		std::size_t frameAllocs;
		Clock::time_point time;
	};

	Snapshot Take()
	{
		return {
			g_heapAllocCount.load(std::memory_order_relaxed),
			g_heapAllocBytes.load(std::memory_order_relaxed),
			g_frameAllocCount.load(std::memory_order_relaxed),
			Clock::now()
		};
	}

	void Report(const char* name, std::size_t operations, const Snapshot& before, const Snapshot& after)
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(after.time - before.time);
		const double nsPerOp = static_cast<double>(elapsed.count()) / static_cast<double>(operations);
		std::printf("%-28s ops=%-8zu total=%7.2fms  ns/op=%8.1f  frameAllocs=%-8zu heapAllocs=%-6zu heapKB=%zu\n",
			name,
			operations,
			static_cast<double>(elapsed.count()) / 1'000'000.0,
			nsPerOp,
			after.frameAllocs - before.frameAllocs,
			after.heapAllocs - before.heapAllocs,
			(after.heapBytes - before.heapBytes) / 1024);
	}

	TKit::Task<> EmptyTask()
	{
		co_return;
	}

	TKit::Task<int> IntTask(int value)
	{
		co_return value;
	}

	TKit::Task<> YieldTask(int count)
	{
		for (int i = 0; i < count; ++i)
		{
			co_yield {};
		}
	}

	TKit::Task<> DelayFrameTask(int count)
	{
		for (int i = 0; i < count; ++i)
		{
			co_await TKit::DelayFrame(1);
		}
	}

	TKit::Task<> WaitForTask(std::chrono::milliseconds duration)
	{
		co_await TKit::WaitFor(duration);
	}

	TKit::Task<> WhenAnyTask()
	{
		(void)co_await TKit::WhenAny(IntTask(1), IntTask(2));
	}

	void Drain(const TKit::SchedulerHandle& scheduler)
	{
		while (scheduler.GetPendingTaskCount() > 0)
		{
			scheduler.Update();
		}
	}
}

int main()
{
	TKit::PoolAllocator pool;
	const TKit::TaskAllocator countingAllocator{
		&pool,
		[](void* context, std::size_t size) -> void* {
			g_frameAllocCount.fetch_add(1, std::memory_order_relaxed);
			return static_cast<TKit::PoolAllocator*>(context)->Allocate(size);
		},
		[](void* context, void* ptr, std::size_t size) {
			static_cast<TKit::PoolAllocator*>(context)->Deallocate(ptr, size);
		}
	};

	TKit::TaskSystem taskSystem{TKit::TaskSystemConfiguration::Builder()
		.WithCustomAllocator(countingAllocator)
		.WithThreadPoolSize(2)
		.Build()};

	const auto scheduler = taskSystem.CreateScheduler(std::nullopt, 30000);
	{
		auto activation = scheduler.Activate();

		// S1: fire-and-forget churn (task completes synchronously)
		for (int i = 0; i < 50'000; ++i) { EmptyTask().Forget(); }
		auto before = Take();
		for (int i = 0; i < 200'000; ++i) { EmptyTask().Forget(); }
		Report("S1 create/forget churn", 200'000, before, Take());

		// S2: co_yield pump (1000 tasks x 100 yields)
		const auto runYield = [&] {
			for (int i = 0; i < 1000; ++i) { YieldTask(100).Forget(); }
			Drain(scheduler);
		};
		runYield();
		before = Take();
		runYield();
		Report("S2 co_yield pump 1000x100", 100'000, before, Take());

		// S3: DelayFrame pump (1000 tasks x 100 one-frame delays)
		const auto runDelay = [&] {
			for (int i = 0; i < 1000; ++i) { DelayFrameTask(100).Forget(); }
			Drain(scheduler);
		};
		runDelay();
		before = Take();
		runDelay();
		Report("S3 DelayFrame pump 1000x100", 100'000, before, Take());

		// S4: WaitFor(5ms) x 10000 tasks
		const auto runWait = [&] {
			for (int i = 0; i < 10'000; ++i) { WaitForTask(5ms).Forget(); }
			Drain(scheduler);
		};
		runWait();
		before = Take();
		runWait();
		Report("S4 WaitFor(5ms) x10000", 10'000, before, Take());

		// S5: WhenAny churn (both competitors complete synchronously)
		for (int i = 0; i < 10'000; ++i) { WhenAnyTask().Forget(); }
		before = Take();
		for (int i = 0; i < 50'000; ++i) { WhenAnyTask().Forget(); }
		Report("S5 WhenAny churn", 50'000, before, Take());
	}

	// The TaskSystem destructor shuts everything down.
	return 0;
}
