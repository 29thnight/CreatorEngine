#pragma once

#if defined(CE_CONTRACT_STANDALONE)
#include <concurrent_queue.h>
namespace ce
{
    template<typename T>
    using concurrent_queue = ::Concurrency::concurrent_queue<T>;
}
#else
import Creator.Concurrency;
#endif
