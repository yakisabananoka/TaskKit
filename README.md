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
* **Task composition** - Combine multiple tasks with `WhenAll`
* **Thread-local by design** - Automatic thread safety through thread-local storage
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
  - [Cancellable Tasks](#cancellable-tasks)
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
    // Initialize TaskSystem (once per thread)
    TaskSystem::Initialize();

    {
        // Create and register a scheduler
        auto id = TaskSystem::CreateScheduler();
        auto registration = TaskSystem::RegisterScheduler(id);

        // Start the task (fire-and-forget)
        ExampleTask().Forget();

        // Update scheduler each frame
        auto& scheduler = TaskSystem::GetScheduler(id);
        while (scheduler.GetPendingTaskCount() > 0)
        {
            scheduler.Update();
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

**TaskSystem** uses thread-local storage for automatic thread isolation. Each thread maintains its own independent set of schedulers.

**Key features:**
- Must call `TaskSystem::Initialize()` once per thread before use
- Schedulers are identified by unique IDs containing thread ID
- `SchedulerRegistration` RAII guard manages current scheduler context
- Thread-safe by design - no cross-thread scheduler access

**Typical pattern:**

```cpp
// Initialize thread-local TaskSystem
TaskSystem::Initialize();

// Create scheduler
auto id = TaskSystem::CreateScheduler();

// Register as current (RAII guard)
{
    auto reg = TaskSystem::RegisterScheduler(id);

    // Tasks created here use this scheduler
    MyTask().Forget();

} // Automatically unregistered

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
Task<> ConcurrentExample()
{
    auto task1 = []() -> Task<> {
        co_await WaitFor(2s);
        std::printf("Task 1 done\n");
    };

    auto task2 = []() -> Task<> {
        co_await WaitFor(3s);
        std::printf("Task 2 done\n");
    };

    // Wait for all tasks to complete
    co_await WhenAll(task1(), task2());
    std::printf("All tasks completed\n");
}
```

### Cancellable Tasks

```cpp
Task<> CancellableTask(std::stop_token stopToken)
{
    for (int i = 0; i < 10; ++i)
    {
        // Check for cancellation
        TASKKIT_STOP_TOKEN_PROCESS(stopToken);

        co_await DelayFrame(1);
        std::printf("Iteration %d\n", i);
    }
}
```

---

## API Reference

### Core Types

#### `Task<T>`

The main coroutine type representing an asynchronous operation.

- **Template parameter**: `T` - Return value type (use `Task<>` for void)
- **Move-only**: Cannot be copied, only moved
- **Eager execution**: Starts immediately upon creation

#### `TaskScheduler`

Frame-based scheduler managing coroutine execution.

- `Update()` - Process all pending tasks for current frame
- `GetPendingTaskCount()` - Returns number of tasks waiting to execute

#### `TaskSystem`

Static class managing multiple schedulers with thread-local storage.

- `Initialize(config)` - Initialize thread-local state (required before use)
- `Shutdown()` - Cleanup thread-local state
- `CreateScheduler()` - Create new scheduler, returns ID
- `DestroyScheduler(id)` - Remove scheduler
- `RegisterScheduler(id)` - Returns RAII guard setting scheduler as current
- `GetScheduler(id)` - Get scheduler by ID
- `GetCurrentScheduler()` - Get currently active scheduler

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
