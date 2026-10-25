#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Concurrency.Jobs.h"
#else
import Creator.Concurrency.Jobs;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.LifetimeToken;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "RHIQueueContract.h"
#include "RHICommandProvenance.h"
#include "RHICommandRecordingJobs.h"
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

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
    // Software segments reset logical pass bindings. Backends may retain known
    // native state on the uninterrupted target; direct callbacks still reset it.
    virtual RHIEncoder& AcquireTranslationEncoder(uint32_t target) { return AcquireEncoder(target); }
    // Unknown for legacy native adapters. Software recordings publish the same
    // value identity before callbacks that their eventual sealed batch carries.
    virtual RHICommandProvenance GetCommandProvenance(uint32_t) const { return {}; }
    virtual bool Finish(ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error) = 0;
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
    virtual bool Record(const own::shared_owner<IRHICommandQueue>& queue,
        const std::function<void(RHIEncoder&)>& commands, ce::lifetime_token token,
        ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error) = 0;
    // Zero keeps compatibility with serial recording adapters. A positive count
    // opts into owned targets, including the one-target small-workload path.
    virtual uint32_t GetWorkerCount() const { return 0; }
    virtual bool BeginRecording(const own::shared_owner<IRHICommandQueue>&,
        uint32_t, ce::lifetime_token, ce::pooled_owner<IRHIQueueRecording>&,
        std::string& error)
    {
        error = "Queue recorder does not support independent command targets.";
        return false;
    }
    void RunParallel(const std::function<void(uint32_t)>& job, uint32_t targetCount)
    {
        RHIRunCommandRecordingJobs(m_scheduler, job, targetCount);
    }
    // Finish all CPU translation before entering the native submission owner.
    // This may allocate/record native targets but must not submit, wait, or signal
    // GPU queues. A graph prepares every batch before its first queue operation.
    virtual bool PrepareSubmissionBatch(const own::shared_owner<IRHICommandQueue>&,
                                        const ce::pooled_owner<IRHIQueueCommandBatch>&, std::string&)
    {
        return true;
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
