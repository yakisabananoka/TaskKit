# TaskKit

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Provides an intuitive and lightweight task system with frame-based scheduling for C++20 coroutines.

<!-- LANG_LINKS_START -->
**Languages: [English](README.md) | [日本語](README_ja.md)**
<!-- LANG_LINKS_END -->

* **Header-only library** - Easy integration, just include and use
* **C++20 coroutines** - Modern async/await syntax with `co_await`
* **Frame-based scheduling** - Perfect for game loops and real-time applications
* **Time-based delays** - Support for both frame delays (`DelayFrame`) and duration-based waits (`WaitFor`)
* **Task composition** - Combine multiple tasks with `WhenAll` and `WhenAny`
* **Thread pool support** - Built-in thread pool with `SwitchToThreadPool` and `RunOnThreadPool`
* **Efficient memory allocation** - Pool allocator reduces heap overhead for coroutine frames
* **Customizable allocators** - Bring your own allocator for fine-grained control
* **Zero dependencies** - Only requires C++20 standard library

---

- [Getting Started](#getting-started)
  - [Requirements](#requirements)
  - [Installation](#installation)
- [Quick Start](#quick-start)
- [Core Concepts](#core-concepts)
  - [Task Lifecycle](#task-lifecycle)
  - [TaskSystem and Schedulers](#tasksystem-and-schedulers)
- [Usage Examples](#usage-examples)
  - [Delayed Execution](#delayed-execution)
  - [Task with Return Value](#task-with-return-value)
  - [Concurrent Tasks](#concurrent-tasks)
  - [Thread Pool Operations](#thread-pool-operations)
  - [Cancellable Tasks](#cancellable-tasks)
  - [Custom Allocators](#custom-allocators)
- [Advanced Features](#advanced-features)
  - [Custom Awaitable Types](#custom-awaitable-types)
- [API Reference](#api-reference)
  - [Core Types](#core-types)
  - [Utility Functions](#utility-functions)
  - [Task Methods](#task-methods)
- [Building and Testing](#building-and-testing)
  - [Build Options](#build-options)
  - [Running Tests](#running-tests)
- [License](#license)

---

## Getting Started

### Requirements

C++20 compatible compiler with coroutines support:

- **GCC 11+** (recommended) - Coroutines enabled by default with `-std=c++20`
  - GCC 10 has experimental support but requires `-fcoroutines` flag and has known bugs
- **Clang 14+** (recommended) - Full C++20 coroutines support
  - Clang 8-13 have partial support but not recommended for production
- **MSVC 19.28+** (Visual Studio 2019 16.8+) - Feature-complete C++20 coroutines
  - Use `/std:c++20` or `/std:c++latest`
- **AppleClang 12+** - Full coroutines support

CMake 3.15 or later

### Installation

#### As Header-Only Library

1. Copy the `include` directory to your project
2. Add the include path to your compiler settings
3. Include `TaskKit.h` in your code

#### Using CMake

```cmake
add_subdirectory(TaskKit)
target_link_libraries(your_target PRIVATE TaskKit)
```

---

## Quick Start

```cpp
#include <TaskKit.h>
using namespace TKit;

Task<> ExampleTask()
{
    std::printf("Task started\n");

    // Wait for 3 frames
    co_await DelayFrame(3);

    std::printf("After 3 frames\n");

    // Wait for 2 seconds
    co_await WaitFor(2s);

    std::printf("Task completed\n");
}

int main()
{
    // Initialize TaskSystem (creates thread pool automatically)
    TaskSystem::Initialize();

    {
        // Create and activate a scheduler
        auto id = TaskSystem::CreateScheduler();
        auto activation = TaskSystem::ActivateScheduler(id);

        // Start the task (fire-and-forget)
        ExampleTask().Forget();

        // Update scheduler each frame
        while (TaskSystem::GetPendingTaskCount(id) > 0)
        {
            TaskSystem::UpdateActivatedScheduler();
            std::this_thread::sleep_for(16ms); // ~60 FPS
        }
    }

    // Cleanup
    TaskSystem::Shutdown();
    return 0;
}
```

---

## Core Concepts

### Task Lifecycle

Tasks in TaskKit follow a simple lifecycle:

1. **Creation** - Task coroutine starts immediately (not suspended at entry)
2. **Suspension** - `co_yield {}` or awaiting utilities schedules resumption on next frame
3. **Completion** - `co_return` sets the result and resumes continuation
4. **Forgotten Tasks** - `.Forget()` marks task for auto-destruction on completion (fire-and-forget)

> **Note**: Tasks are move-only and marked with `[[nodiscard]]` to prevent accidental resource leaks.

### TaskSystem and Schedulers

**TaskSystem** manages schedulers and a built-in thread pool. It must be initialized before use.

**Key features:**
- Call `TaskSystem::Initialize()` once at application startup
- Schedulers are identified by unique IDs containing thread ID
- `SchedulerActivation` RAII guard manages current scheduler context
- Built-in thread pool for offloading heavy computations

**Typical pattern:**

```cpp
// Initialize TaskSystem (creates thread pool)
TaskSystem::Initialize();

// Create scheduler for main thread
auto id = TaskSystem::CreateScheduler();

// Activate as current (RAII guard)
{
    auto activation = TaskSystem::ActivateScheduler(id);

    // Tasks created here use this scheduler
    MyTask().Forget();

    // Update loop
    while (TaskSystem::GetPendingTaskCount(id) > 0)
    {
        TaskSystem::UpdateActivatedScheduler();
    }

} // Automatically deactivated

// Cleanup
TaskSystem::Shutdown();
```

---

## Usage Examples

### Delayed Execution

```cpp
Task<> DelayExample()
{
    // Delay by frame count
    co_await DelayFrame(5);  // Wait 5 frames

    // Delay by duration
    co_await WaitFor(1s);    // Wait 1 second

    // Delay until specific time
    auto targetTime = std::chrono::steady_clock::now() + 5s;
    co_await WaitUntil(targetTime);
}
```

### Task with Return Value

```cpp
Task<int> CalculateAsync()
{
    co_await DelayFrame(1);
    co_return 42;
}

Task<> UseResult()
{
    int result = co_await CalculateAsync();
    std::printf("Result: %d\n", result);
}
```

### Concurrent Tasks

```cpp
Task<> WhenAllExample()
{
    auto task1 = []() -> Task<int> {
        co_await WaitFor(2s);
        co_return 1;
    };

    auto task2 = []() -> Task<int> {
        co_await WaitFor(3s);
        co_return 2;
    };

    // Wait for all tasks to complete
    auto [result1, result2] = co_await WhenAll(task1(), task2());
    std::printf("Results: %d, %d\n", result1, result2);
}

Task<> WhenAnyExample()
{
    auto task1 = []() -> Task<int> {
        co_await WaitFor(2s);
        co_return 1;
    };

    auto task2 = []() -> Task<int> {
        co_await WaitFor(1s);
        co_return 2;
    };

    // Wait for the first task to complete
    auto result = co_await WhenAny(task1(), task2());
    // result is std::variant<int, int>
    std::printf("First completed index: %zu\n", result.index());
}
```

### Thread Pool Operations

```cpp
Task<> ThreadPoolExample(TaskSchedulerId mainSchedulerId)
{
    // Switch to thread pool for heavy computation
    co_await SwitchToThreadPool();

    // Now running on a worker thread
    auto result = HeavyComputation();

    // Switch back to main scheduler
    co_await SwitchToSelectedScheduler(mainSchedulerId);

    // Back on main thread
    UpdateUI(result);
}

Task<> RunOnThreadPoolExample()
{
    // RunOnThreadPool automatically returns to original scheduler
    int result = co_await RunOnThreadPool([]() {
        return HeavyComputation();
    });

    // Automatically back on original thread
    UpdateUI(result);
}

Task<> RunOnThreadPoolWithTaskExample()
{
    // Also works with functions returning Task<T>
    auto data = co_await RunOnThreadPool([]() -> Task<std::vector<int>> {
        co_await SomeAsyncOperation();
        co_return ProcessData();
    });
}
```

### Cancellable Tasks

```cpp
Task<> CancellableTask(std::stop_token stopToken)
{
    for (int i = 0; i < 10; ++i)
    {
        // Check for cancellation
        ThrowIfStopRequested(stopToken);

        co_await DelayFrame(1);
        std::printf("Iteration %d\n", i);
    }
}
```

### Custom Allocators

```cpp
// Define custom allocator for coroutine frames
struct MyAllocatorContext {
    // Your allocator state
};

MyAllocatorContext myContext;

TaskAllocator myAllocator{
    &myContext,
    [](void* ctx, std::size_t size) -> void* {
        // Custom allocation
        auto* context = static_cast<MyAllocatorContext*>(ctx);
        return myCustomAlloc(context, size);
    },
    [](void* ctx, void* ptr, std::size_t size) {
        // Custom deallocation
        auto* context = static_cast<MyAllocatorContext*>(ctx);
        myCustomFree(context, ptr, size);
    }
};

// Initialize TaskSystem with custom configuration
auto config = TaskSystemConfiguration::Builder()
    .WithCustomAllocator(myAllocator)
    .WithThreadPoolSize(4)        // Number of worker threads
    .WithReservedTaskCount(200)   // Reserved task slots per scheduler
    .Build();

TaskSystem::Initialize(config);
```

> **Note**: By default, TaskKit uses an efficient pool allocator that reduces heap allocation overhead. Thread pool size defaults to `std::thread::hardware_concurrency()`.

---

## Advanced Features

### Custom Awaitable Types

TaskKit provides an extensibility mechanism to make custom types awaitable within Task coroutines using `AwaitTransformer`.

**How it works:**

The `AwaitTransformer` template allows you to specialize transformation logic for your custom types. When a Task encounters `co_await yourCustomType`, it will call `AwaitTransformer<YourType>::Transform()` to convert it into an awaitable object.

**Example:**

```cpp
// Your custom type
struct MyCustomEvent
{
    int eventId;
    std::string eventData;
};

// Specialize AwaitTransformer for your type
namespace TKit
{
    template<>
    class AwaitTransformer<MyCustomEvent>
    {
    public:
        static auto Transform(MyCustomEvent&& event)
        {
            // Return an awaiter that handles your custom type
            struct Awaiter
            {
                MyCustomEvent event;

                bool await_ready() const noexcept { return false; }

                void await_suspend(std::coroutine_handle<> handle)
                {
                    // Custom suspension logic
                    // e.g., register event handler, schedule callback, etc.
                }

                MyCustomEvent await_resume()
                {
                    return std::move(event);
                }
            };

            return Awaiter{ std::move(event) };
        }
    };
}

// Now you can use it in Tasks
Task<> ProcessEvent()
{
    MyCustomEvent event{ 42, "data" };

    // Your custom type is now awaitable!
    auto result = co_await event;

    std::printf("Processed event: %d\n", result.eventId);
}
```

**Requirements:**

1. Specialize `AwaitTransformer<T>` in the `TKit` namespace
2. Provide a static `Transform()` method that accepts your type
3. Return an awaiter object with standard awaiter interface:
   - `await_ready()` - Returns true if result is immediately available
   - `await_suspend(handle)` - Called when coroutine suspends
   - `await_resume()` - Returns the result when coroutine resumes

**Use cases:**

- Integrating with event systems (UI events, network events)
- Wrapping callback-based APIs to work with coroutines
- Custom synchronization primitives
- Domain-specific async operations

> **Note**: The `Awaitable` concept automatically detects types that have a valid `AwaitTransformer` specialization.

---

## API Reference

### Core Types

#### `Task<T>`

The main coroutine type representing an asynchronous operation.

- **Template parameter**: `T` - Return value type (use `Task<>` for void)
- **Move-only**: Cannot be copied, only moved
- **Eager execution**: Starts immediately upon creation

#### `TaskSystem`

Static class managing schedulers and thread pool.

- `Initialize(config)` - Initialize TaskSystem with optional configuration
- `Shutdown()` - Cleanup and destroy thread pool
- `IsInitialized()` - Check if TaskSystem is initialized
- `CreateScheduler(threadId, reservedCount)` - Create new scheduler, returns ID
- `ActivateScheduler(id)` - Returns RAII guard that activates scheduler
- `UpdateActivatedScheduler()` - Process pending tasks on activated scheduler
- `GetPendingTaskCount(id)` - Get number of pending tasks
- `GetActivatedSchedulerId()` - Get currently activated scheduler ID
- `Schedule(id, handle)` - Schedule coroutine handle to specific scheduler

#### `TaskSystemConfiguration::Builder`

Builder for TaskSystem configuration.

- `WithCustomAllocator(allocator)` - Set custom memory allocator
- `WithThreadPoolSize(size)` - Set number of worker threads (0 = hardware_concurrency)
- `WithReservedTaskCount(count)` - Set reserved task slots per scheduler
- `Build()` - Create configuration object

### Utility Functions

#### `DelayFrame(int frames)`

Suspends execution for specified number of frames.

```cpp
co_await DelayFrame(5);  // Wait 5 frames
```

#### `WaitFor(duration)`

Suspends execution until duration elapsed.

```cpp
co_await WaitFor(2s);         // Wait 2 seconds
co_await WaitFor(500ms);      // Wait 500 milliseconds
```

#### `WaitUntil(timepoint)`

Suspends execution until specific time point reached.

```cpp
auto target = std::chrono::steady_clock::now() + 5s;
co_await WaitUntil(target);
```

#### `WhenAll(tasks...)`

Waits for multiple tasks to complete, returns tuple of results.

```cpp
auto [result1, result2] = co_await WhenAll(Task1(), Task2());
```

#### `WhenAny(tasks...)`

Waits for the first task to complete, returns variant of results.

```cpp
auto result = co_await WhenAny(Task1(), Task2());
// result.index() indicates which task completed first
```

#### `SwitchToThreadPool()`

Switches coroutine execution to thread pool.

```cpp
co_await SwitchToThreadPool();
// Now running on worker thread
```

#### `SwitchToSelectedScheduler(id)`

Switches coroutine execution to specified scheduler.

```cpp
co_await SwitchToSelectedScheduler(mainSchedulerId);
// Now running on main thread
```

#### `RunOnThreadPool(func)`

Executes function on thread pool and automatically returns to original scheduler.

```cpp
// With regular function
int result = co_await RunOnThreadPool([]() { return compute(); });

// With Task-returning function
auto data = co_await RunOnThreadPool([]() -> Task<Data> {
    co_return co_await AsyncCompute();
});
```

#### `GetCompletedTask()`

Returns an immediately completed task (useful for conditional logic).

```cpp
co_await (condition ? ActualTask() : GetCompletedTask());
```

### Task Methods

#### `.Forget()`

Marks task for fire-and-forget execution with automatic cleanup.

```cpp
MyBackgroundTask().Forget();
```

> **Important**: Without `.Forget()`, the task must be stored or awaited to prevent premature destruction.

#### `.IsDone()`

Checks if task has completed execution.

```cpp
auto task = MyTask();
if (task.IsDone()) { /* ... */ }
```

#### `.IsReady()`

Checks if task result is ready to retrieve.

```cpp
if (task.IsReady()) {
    auto result = co_await task;
}
```

---

## Building and Testing

### Build Options

```bash
# Configure with default options
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Customize build
cmake -B build \
  -DBUILD_SAMPLES=OFF \  # Don't build samples
  -DBUILD_TESTS=OFF \    # Don't build tests
  -DUSE_GTEST=ON         # Use Google Test (default)
```

### Running Tests

```bash
# Run all tests with CTest
ctest --test-dir build

# Run test executable directly
./build/bin/tests/TaskKit_tests

# Run specific test with Google Test filter
./build/bin/tests/TaskKit_tests --gtest_filter=TaskKitTest.DelayFrame*
```

### Running Samples

```bash
./build/bin/samples/QuickStart
```

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
