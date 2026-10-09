#pragma once
#include "../RHIQueueRecorder.h"
#include <algorithm>
class DX12DeviceResources;

class DX12QueueRecorder final : public IRHIQueueRecorder
{
public:
    explicit DX12QueueRecorder(DX12DeviceResources& resources, uint32_t workers = 4,
        job_scheduler& scheduler = ce::get_job_scheduler())
        : IRHIQueueRecorder(scheduler), resources_(resources), workers_((std::max)(1u, (std::min)(8u, workers))) {}
    IRenderDeviceServices& DeviceServices() const override;
    uint32_t GetWorkerCount() const override { return workers_; }
    bool BeginRecording(const std::shared_ptr<IRHICommandQueue>& queue,
        uint32_t targetCount, std::shared_ptr<const void> token,
        std::shared_ptr<IRHIQueueRecording>& recording, std::string& error) override;
    bool ExecuteSubmission(const std::function<bool(std::string&)>& submit,
        std::string& error) override;
    bool Record(const std::shared_ptr<IRHICommandQueue>& queue,
        const std::function<void(RHIEncoder&)>& commands, std::shared_ptr<const void> token,
        std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error) override;
private:
    DX12DeviceResources& resources_;
    uint32_t workers_;
};
