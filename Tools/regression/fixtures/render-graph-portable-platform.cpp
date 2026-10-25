// Platform boundaries for CPU graph fixtures. No worker jobs are dispatched by
// these tests; an accidental worker wait is a failure, not a successful no-op.
#if defined(CE_CONTRACT_STANDALONE)
#include "../../build/ModuleAdapters/Creator.Concurrency.Jobs.h"
#else
import Creator.Concurrency.Jobs;
#endif
#include "ThirdParty/mimalloc/include/mimalloc.h"
#include "render-graph-allocation-counters.h"
#include <cstdlib>
#include <cstring>
#include <new>

namespace ce::regression
{
    thread_local AllocationCounts* activeAllocationCounts{};
}

namespace
{
    void* AllocateStandard(std::size_t bytes) noexcept
    {
        void* result = std::malloc((std::max)(bytes, std::size_t{1}));
        if (result)
        {
            ce::regression::CountStandardAllocation(bytes);
        }
        return result;
    }

    void* AllocateStandardAligned(std::size_t bytes, std::align_val_t requestedAlignment) noexcept
    {
        void* result{};
        const auto alignment = (std::max)(static_cast<std::size_t>(requestedAlignment), sizeof(void*));
        if (posix_memalign(&result, alignment, (std::max)(bytes, std::size_t{1})) != 0)
        {
            return nullptr;
        }
        ce::regression::CountStandardAllocation(bytes);
        return result;
    }
} // namespace

void* operator new(std::size_t bytes)
{
    if (void* result = AllocateStandard(bytes))
    {
        return result;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
    return AllocateStandard(bytes);
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept
{
    return AllocateStandard(bytes);
}
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    if (void* result = AllocateStandardAligned(bytes, alignment))
    {
        return result;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return ::operator new(bytes, alignment);
}
void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return AllocateStandardAligned(bytes, alignment);
}
void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return AllocateStandardAligned(bytes, alignment);
}
void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}

extern "C" void* mi_malloc(size_t bytes) noexcept
{
    void* result = std::malloc((std::max)(bytes, size_t{1}));
    if (result)
    {
        ce::regression::CountMimallocAllocation(bytes);
    }
    return result;
}
extern "C" void* mi_malloc_aligned(size_t bytes, size_t alignment) noexcept
{
    void* result{};
    alignment = (std::max)(alignment, sizeof(void*));
    if (posix_memalign(&result, alignment, (std::max)(bytes, size_t{1})) != 0)
    {
        return nullptr;
    }
    ce::regression::CountMimallocAllocation(bytes);
    return result;
}
extern "C" void mi_free(void* pointer) noexcept
{
    std::free(pointer);
}
extern "C" int mi_dupenv_s(char** buffer, size_t* length, const char* name) noexcept
{
    *buffer = nullptr;
    *length = 0;
    const char* value = std::getenv(name);
    if (!value)
    {
        return 0;
    }
    *length = std::strlen(value) + 1;
    *buffer = static_cast<char*>(std::malloc(*length));
    if (!*buffer)
    {
        return 12;
    }
    ce::regression::CountMimallocAllocation(*length);
    std::memcpy(*buffer, value, *length);
    return 0;
}
void job_handle::wait() const
{
    if (valid())
    {
        throw std::logic_error("Portable graph fixture does not execute worker jobs");
    }
}
thread_pool::thread_pool() = default;
thread_pool::~thread_pool() = default;
bool thread_pool::is_worker_thread() noexcept
{
    return false;
}
job_scheduler::job_scheduler(thread_pool&) {}
job_scheduler::~job_scheduler() = default;
bool job_scheduler::is_running() const
{
    return false;
}
std::size_t job_scheduler::worker_count() const
{
    return 0;
}
job_handle job_scheduler::submit_indexed(std::size_t, std::function<void(std::size_t)>)
{
    throw std::logic_error("Portable graph fixture does not submit worker jobs");
}
namespace ce
{
    job_scheduler& get_job_scheduler()
    {
        static thread_pool pool;
        static job_scheduler scheduler{pool};
        return scheduler;
    }
} // namespace ce
