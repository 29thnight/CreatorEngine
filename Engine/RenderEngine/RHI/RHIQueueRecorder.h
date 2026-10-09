#pragma once
#include "RHIQueueContract.h"
#include "RHICommandRecordingJobs.h"
#include <functional>

class RHIEncoder;
class IRenderDeviceServices;

// Owner opens independent command targets before jobs start and seals them only
// after every job joins. Target order is submission order; each target has one
// recording job at a time. AcquireEncoder starts fresh pass/slice encoder state.
class IRHIQueueRecording
{
public:
    virtual ~IRHIQueueRecording() = default;
    virtual RHIEncoder& AcquireEncoder(uint32_t target) = 0;
    virtual bool Finish(std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error) = 0;
};

// Each recording exclusively leases allocator storage until batch retirement.
// The token pins callback resources/descriptors; no reset before completion.
class IRHIQueueRecorder
{
public:
    explicit IRHIQueueRecorder(job_scheduler& scheduler = ce::get_job_scheduler())
        : m_scheduler(scheduler) {}
    virtual ~IRHIQueueRecorder() = default;
    virtual IRenderDeviceServices& DeviceServices() const = 0;
    virtual bool Record(const std::shared_ptr<IRHICommandQueue>& queue,
        const std::function<void(RHIEncoder&)>& commands, std::shared_ptr<const void> token,
        std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error) = 0;
    // Zero keeps compatibility with serial recording adapters. A positive count
    // opts into owned targets, including the one-target small-workload path.
    virtual uint32_t GetWorkerCount() const { return 0; }
    virtual bool BeginRecording(const std::shared_ptr<IRHICommandQueue>&,
        uint32_t, std::shared_ptr<const void>, std::shared_ptr<IRHIQueueRecording>&,
        std::string& error)
    {
        error = "Queue recorder does not support independent command targets.";
        return false;
    }
    void RunParallel(const std::function<void(uint32_t)>& job, uint32_t targetCount)
    {
        RHIRunCommandRecordingJobs(m_scheduler, job, targetCount);
    }
    // Backend may marshal the entire transaction to its submission owner once.
    // Return after CPU admission, never wait for GPU completion here.
    virtual bool ExecuteSubmission(const std::function<bool(std::string&)>& submit,
        std::string& error)
    {
        return submit(error);
    }
private:
    job_scheduler& m_scheduler;
};
