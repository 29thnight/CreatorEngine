#pragma once
#include "../../../Utility_Framework/LifetimeToken.h"
#include "../../../Utility_Framework/Ownership.h"
#include "../RHIQueueRecorder.h"
#include "../../../Utility_Framework/StandardLibrary.h"
class DX12DeviceResources;

class DX12QueueRecorder final : public IRHIQueueRecorder
{
public:
    explicit DX12QueueRecorder(DX12DeviceResources& resources, uint32_t workers = 4,
        job_scheduler& scheduler = ce::get_job_scheduler())
        : IRHIQueueRecorder(scheduler), resources_(resources), workers_((std::max)(1u, (std::min)(8u, workers))) {}
    IRenderDeviceServices& DeviceServices() const override;
    uint32_t GetWorkerCount() const override { return workers_; }
    bool BeginRecording(const own::shared_owner<IRHICommandQueue>& queue,
        uint32_t targetCount, ce::lifetime_token token,
        ce::pooled_owner<IRHIQueueRecording>& recording, std::string& error) override;
    bool ExecuteSubmission(const std::function<bool(std::string&)>& submit,
        std::string& error) override;
    bool Record(const own::shared_owner<IRHICommandQueue>& queue,
        const std::function<void(RHIEncoder&)>& commands, ce::lifetime_token token,
        ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error) override;
private:
    DX12DeviceResources& resources_;
    uint32_t workers_;
};
