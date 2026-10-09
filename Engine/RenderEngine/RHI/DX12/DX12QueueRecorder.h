#pragma once
#include "../RHIQueueRecorder.h"
class DX12DeviceResources;

class DX12QueueRecorder final : public IRHIQueueRecorder
{
public:
    explicit DX12QueueRecorder(DX12DeviceResources& resources) : resources_(resources) {}
    IRenderDeviceServices& DeviceServices() const override;
    bool Record(const std::shared_ptr<IRHICommandQueue>& queue,
        const std::function<void(RHIEncoder&)>& commands, std::shared_ptr<const void> token,
        std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error) override;
private:
    DX12DeviceResources& resources_;
};
