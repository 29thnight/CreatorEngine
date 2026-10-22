// Platform boundaries for CPU graph fixtures. No worker jobs are dispatched by
// these tests; an accidental worker wait is a failure, not a successful no-op.
#include "Engine/Utility_Framework/JobScheduler.h"
#include "ThirdParty/mimalloc/include/mimalloc.h"
#include <cstdlib>
#include <cstring>

extern "C" void* mi_malloc(size_t bytes) noexcept
{
    return std::malloc(bytes);
}
extern "C" void* mi_malloc_aligned(size_t bytes, size_t alignment) noexcept
{
    void* result{};
    alignment = (std::max)(alignment, sizeof(void*));
    return posix_memalign(&result, alignment, (std::max)(bytes, size_t{1})) == 0 ? result : nullptr;
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
uint64_t job_timestamp_ns() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
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
