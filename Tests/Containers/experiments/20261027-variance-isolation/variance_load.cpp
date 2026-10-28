#define NOMINMAX
#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <intrin.h>

int main(int argc, char** argv)
{
    if (argc != 6) return 2;
    const unsigned cpu = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10));
    if (cpu >= 64) return 3;
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[3]);
    HANDLE stop = OpenEventA(SYNCHRONIZE, FALSE, argv[4]);
    const auto ttl = std::strtoull(argv[5], nullptr, 10);
    if (!ready || !stop || ttl > 300000 || !ttl) return 4;
    if (!SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << cpu)) return 5;
    const bool memory = std::strcmp(argv[1], "memory") == 0;
    const bool integer = std::strcmp(argv[1], "integer") == 0;
    if (!memory && !integer) return 6;
    // A Normal-process foreground-like boosted thread can outrank the original
    // measuring/guard threads (Normal + AboveNormal). It cannot outrank High.
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST)) return 7;
    std::vector<std::uint64_t> storage(memory ? 8 * 1024 * 1024 : 1, 1);
    std::uint64_t x = 23;
    volatile std::uint64_t sink = 0;
    const auto started = GetTickCount64();
    std::printf("load,mode,%s,cpu,%u,process_class,%lu,thread_priority,%d,burst_ms,350,period_ms,1000\n",
                argv[1], cpu, GetPriorityClass(GetCurrentProcess()), GetThreadPriority(GetCurrentThread()));
    std::fflush(stdout);
    SetEvent(ready);
    while (WaitForSingleObject(stop, 0) == WAIT_TIMEOUT && GetTickCount64() - started < ttl)
    {
        if ((GetTickCount64() - started) % 1000 >= 350) { Sleep(1); continue; }
        for (unsigned i = 0; i < 16384; ++i)
        {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            if (memory) storage[(x & ((1u << 20) - 1)) * 8] += x;
        }
        sink = x;
    }
    (void)sink;
    CloseHandle(stop); CloseHandle(ready);
    return 0;
}
