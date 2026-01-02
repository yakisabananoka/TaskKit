# TaskKit

[English](README.md) | [日本語](README.ja.md)

## Overview

TaskKit is a lightweight, header-only C++20 coroutine library for managing asynchronous tasks with frame-based scheduling. It provides an intuitive API for writing asynchronous code using modern C++ coroutines.

## Features

- **Header-only library** - Easy integration, just include and use
- **C++20 coroutines** - Modern async/await syntax
- **Frame-based scheduling** - Perfect for game loops and real-time applications
- **Time-based delays** - Support for both frame delays and duration-based waits
- **Task composition** - Combine multiple tasks with `WhenAll`
- **Stop token support** - Cancellable tasks via `std::stop_token`
- **Zero dependencies** - Only requires C++20 standard library
- **Lightweight** - Minimal overhead design

## Requirements

- C++20 compatible compiler with coroutines support:
  - **GCC 11+** (recommended) - Coroutines enabled by default with `-std=c++20`
    - GCC 10 has experimental support but requires `-fcoroutines` flag and has known bugs
  - **Clang 14+** (recommended) - Full C++20 coroutines support
    - Clang 8-13 have partial support but not recommended for production
  - **MSVC 19.28+** (Visual Studio 2019 16.8+) - Feature-complete C++20 coroutines
    - Use `/std:c++20` or `/std:c++latest`
  - **AppleClang 12+** - Full coroutines support
- CMake 3.15 or later

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
    auto& taskSystem = TaskSystem::GetInstance();
    auto& scheduler = taskSystem.GetScheduler(
        taskSystem.GetCurrentSchedulerId()
    );

    // Start the task (fire-and-forget)
    ExampleTask().Forget();

    // Update scheduler each frame
    while (running)
    {
        scheduler.Update();
        std::this_thread::sleep_for(16ms); // ~60 FPS
    }

    return 0;
}
```

## Installation

### As Header-Only Library

1. Copy the `include` directory to your project
2. Add the include path to your compiler settings
3. Include `TaskKit.h` in your code

### Using CMake

```cmake
add_subdirectory(TaskKit)
target_link_libraries(your_target PRIVATE TaskKit)
```

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

## Building

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Build without samples
cmake -B build -DBUILD_SAMPLES=OFF

# Build without tests
cmake -B build -DBUILD_TESTS=OFF
```

## Running Tests

```bash
# Run all tests
ctest --test-dir build

# Run specific test
./build/bin/tests/TaskKit_tests --gtest_filter=TaskKitTest.DelayFrame*
```

## API Reference

### Core Types

- `Task<T>` - Coroutine task type with optional return value
- `TaskScheduler` - Frame-based scheduler for task execution
- `TaskSystem` - Singleton managing multiple schedulers

### Utility Functions

- `DelayFrame(int frames)` - Delay execution by frame count
- `WaitFor(duration)` - Delay execution by time duration
- `WaitUntil(timepoint)` - Delay until specific time point
- `WhenAll(tasks...)` - Wait for multiple tasks concurrently
- `GetCompletedTask()` - Get an immediately completed task

### Task Methods

- `.Forget()` - Fire-and-forget execution (auto-cleanup)
- `.IsDone()` - Check if task completed
- `.IsReady()` - Check if task result is ready

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
