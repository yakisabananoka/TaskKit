#include <Windows.h>
#include "../include/TaskKit.h"

using namespace TKit;

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
    co_await WaitFor(std::chrono::seconds(2));
    std::printf("ExampleDelayForTask end after 2 seconds\n");
}

int main()
{
    auto& taskSystem = TaskSystem::GetInstance();
    const auto id = taskSystem.GetCurrentSchedulerId();
    auto& scheduler = taskSystem.GetScheduler(id);

    ExampleDelayFrameTask().Forget();
    ExampleDelayForTask().Forget();

    while (!GetAsyncKeyState(VK_ESCAPE))
    {
        scheduler.Update();
        Sleep(16);
    }

    return 0;
}