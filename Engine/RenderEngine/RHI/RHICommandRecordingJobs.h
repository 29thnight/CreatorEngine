#pragma once
#include "JobScheduler.h"
#include "../../Utility_Framework/StandardLibrary.h"

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
    // Share one indexed callback instead of allocating a task vector and one
    // function wrapper per command target. Completion still releases all
    // callback captures before wait returns, including on failure.
    scheduler.submit_indexed(targetCount, [&job](std::size_t target)
    {
        job(static_cast<uint32_t>(target));
    }).wait();
}
