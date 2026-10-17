module;
// Preserve the SDK queue implementation, but keep its textual STL/PPL headers
// behind a module boundary so consumers never redeclare imported library types.
#include <concurrent_queue.h>

export module Creator.Concurrency;

export namespace ce
{
    template<typename T>
    using concurrent_queue = ::Concurrency::concurrent_queue<T>;
}
