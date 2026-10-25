#pragma once
// Generated from Engine/Contracts/Concurrency/Creator.Concurrency.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

// Preserve the SDK queue implementation, but keep its textual STL/PPL headers
// behind a module boundary so consumers never redeclare imported library types.
#include <concurrent_queue.h>


namespace ce
{
    template<typename T>
    using concurrent_queue = ::Concurrency::concurrent_queue<T>;
}
