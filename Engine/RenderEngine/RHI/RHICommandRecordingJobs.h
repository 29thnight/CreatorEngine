#pragma once
#include "JobScheduler.h"
#include <cstdint>
#include <functional>
#include <stdexcept>

// RG4 and owned queue recording share the same engine jobs and join contract.
// A completed handle has released every callback, including on job failure.
inline void RHIRunCommandRecordingJobs(job_scheduler& scheduler,
    const std::function<void(uint32_t)>& job, uint32_t targetCount)
{
    if (targetCount == 0)
    {
        return;
    }
    if (thread_pool::is_worker_thread())
    {
        throw std::logic_error("command recording cannot wait from a job worker");
    }
    job_group group;
    for (uint32_t target = 0; target < targetCount; ++target)
    {
        group.add([&job, target] { job(target); });
    }
    scheduler.submit(std::move(group)).wait();
}
