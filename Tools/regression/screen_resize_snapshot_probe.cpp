#include "ScreenSizedResource.h"
#include <atomic>
#include <barrier>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    ScreenResizeBus bus;
    bus.SetSize(1, 2);
    const auto initial = bus.GetSizeSnapshot();
    bus.SetSize(1, 2);
    if (initial.generation != bus.GetSizeSnapshot().generation) return 1;

    bool callbackObserved = false;
    const auto subscription = bus.Subscribe(nullptr, [&](uint32_t width, uint32_t height)
    {
        const auto size = bus.GetSizeSnapshot();
        callbackObserved = size.width == width && size.height == height;
        const auto nested = bus.Subscribe(nullptr, nullptr);
        bus.Unsubscribe(nested);
    });
    bus.BroadcastResize(2, 3);
    bus.Unsubscribe(subscription);
    if (!callbackObserved) return 2;

    std::atomic_bool failed{ false };
    std::barrier start(5);
    std::vector<std::jthread> threads;
    for (uint32_t writer = 0; writer < 2; ++writer)
    {
        threads.emplace_back([&, writer]
        {
            start.arrive_and_wait();
            for (uint32_t i = 1; i <= 200000; ++i)
            {
                const uint32_t width = i * 2 + writer;
                bus.SetSize(width, width + 1);
            }
        });
    }
    for (uint32_t reader = 0; reader < 3; ++reader)
    {
        threads.emplace_back([&]
        {
            start.arrive_and_wait();
            auto previous = bus.GetSizeSnapshot();
            for (uint32_t i = 0; i < 200000; ++i)
            {
                const auto size = bus.GetSizeSnapshot();
                if (size.height != size.width + 1 || size.generation < previous.generation ||
                    (size.generation == previous.generation &&
                        (size.width != previous.width || size.height != previous.height)))
                    failed = true;
                previous = size;
            }
        });
    }
    threads.clear();
    if (failed) return 3;
    bus.SetSize(0, 0);
    if (bus.GetAspectRatio() != 1.f) return 4;
    std::puts("Screen resize snapshot PASS: 400000 writes, 600000 reads, duplicate/zero/callback contracts");
    return 0;
}
