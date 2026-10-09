#pragma once
#include "../RHIQueueContract.h"
#include <d3d12.h>

struct DX12QueueServiceState;

// Explicit queue service. Existing frame submissions retain their current path.
class DX12QueueService final : public IRHICommandQueueFactory
{
public:
    explicit DX12QueueService(ID3D12Device* device);
    ~DX12QueueService();
    RHIQueueCapabilities QueryQueueCapabilities() const override;
    bool CreateQueue(RHIQueueKind kind, std::shared_ptr<IRHICommandQueue>& queue,
        std::string& error) override;
    // Reuse the presentation queue, with a service-owned retirement timeline.
    bool GetPrimaryGraphicsQueue(ID3D12CommandQueue* primary,
        std::shared_ptr<IRHICommandQueue>& queue, std::string& error);
    // Revoke admission, then drain accepted primitive operations before releasing queues.
    bool Shutdown(std::string& error);
    // Publish only after the primary submission's CPU ticket has succeeded.
    bool ImportPrimaryCompletion(ID3D12Fence* fence, uint64_t value,
        RHITimelinePoint& point, std::string& error);
    bool JoinPrimaryQueue(ID3D12CommandQueue* queue, const RHITimelinePoint& point,
        std::string& error);
    // Borrowed backend handle; caller retains the endpoint and drains before use ends.
    static ID3D12CommandQueue* NativeQueue(const std::shared_ptr<IRHICommandQueue>& queue);
    struct RecordingPoolStats
    {
        uint64_t created{0}, reused{0}, leased{0}, cached{0};
    };
    static RecordingPoolStats QueryRecordingPool(const std::shared_ptr<IRHICommandQueue>& queue);
    // Backend recording adapter for the supported buffer-copy COMMON boundary.
    // Default-heap inputs must be in COMMON; upload/readback fixed states stay fixed.
    static bool RecordBufferCopy(const std::shared_ptr<IRHICommandQueue>& queue,
        ID3D12Resource* source, ID3D12Resource* destination, uint64_t bytes,
        std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error,
        std::shared_ptr<const void> lifetimeToken = {});
#if !CE_SHIPPING
    bool EnqueueTestGate(const std::shared_ptr<IRHICommandQueue>& queue,
        ID3D12Fence* fence, uint64_t value, std::string& error);
    void RejectNextTestSubmission(bool afterExecute);
#endif
private:
    std::shared_ptr<DX12QueueServiceState> state_;
};
