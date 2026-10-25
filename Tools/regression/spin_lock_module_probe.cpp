#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.SpinLock.h"
#else
import std;
import Creator.Core.SpinLock;
#endif

int main()
{
    std::atomic_flag flag{};
    int count{};
    const auto increment = [&]
    {
        for (int i = 0; i < 10000; ++i)
        {
            spin_lock guard(flag);
            ++count;
        }
    };
    std::thread first(increment);
    std::thread second(increment);
    first.join();
    second.join();
    if (count != 20000 || flag.test(std::memory_order_relaxed))
    {
        return 1;
    }
    std::puts("SPIN_LOCK_OK increments=20000 released=true");
}
