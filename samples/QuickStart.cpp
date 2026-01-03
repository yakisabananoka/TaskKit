#include "TaskKit.h"

using namespace TKit;
using namespace std::literals::chrono_literals;

Task<> ExampleDelayFrameTask()
{
    for (int i = 0; i < 5; ++i)
    {
        std::printf("ExampleTask iteration %d\n", i);
        co_await DelayFrame(1);
    }

    std::printf("ExampleTask end\n");
}

Task<> ExampleDelayForTask()
{
    std::printf("ExampleDelayForTask start\n");
    co_await WaitFor(2s);
    std::printf("ExampleDelayForTask end after 2 seconds\n");
}

Task<> ExampleWhenAllTask()
{
    std::printf("ExampleWhenAll start\n");

    auto task1 = []() -> Task<>
    {
        std::printf("  Task1 start\n");
        co_await WaitFor(5000ms);
        std::printf("  Task1 end\n");
    };

    auto task2 = []() -> Task<>
    {
        std::printf("  Task2 start\n");
        co_await WaitFor(3000ms);
        std::printf("  Task2 end\n");
    };

    co_await WhenAll(task1(), task2());
    std::printf("ExampleWhenAll end - all tasks completed\n");
}

int main()
{
    auto& taskSystem = TaskSystem::GetInstance();
    const auto id = taskSystem.GetCurrentSchedulerId();
    auto& scheduler = taskSystem.GetScheduler(id);

    // Run different examples (uncomment to try)
    //ExampleDelayFrameTask().Forget();
    //ExampleDelayForTask().Forget();
    ExampleWhenAllTask().Forget();

    while (scheduler.GetPendingTaskCount() > 0)
    {
        scheduler.Update();
        std::this_thread::sleep_for(16ms);
    }

    std::printf("All tasks completed.\n");
    return 0;
}