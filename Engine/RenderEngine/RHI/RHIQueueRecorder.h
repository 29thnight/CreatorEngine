#pragma once
#include "RHIQueueContract.h"
#include <functional>

class RHIEncoder;
class IRenderDeviceServices;

// Each recording exclusively leases allocator storage until batch retirement.
// The token pins callback resources/descriptors; no reset before completion.
class IRHIQueueRecorder
{
public:
    virtual ~IRHIQueueRecorder() = default;
    virtual IRenderDeviceServices& DeviceServices() const = 0;
    virtual bool Record(const std::shared_ptr<IRHICommandQueue>& queue,
        const std::function<void(RHIEncoder&)>& commands, std::shared_ptr<const void> token,
        std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error) = 0;
};
