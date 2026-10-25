#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.LifetimeToken.h"
#else
import Creator.Core.LifetimeToken;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../RHIQueueContract.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Platform/WindowsSDK/Creator.GraphicsSDK.h"
#else
import "Creator.GraphicsSDK.h";
#endif

struct DX12QueueServiceState;

// Explicit queue service. Existing frame submissions retain their current path.
class DX12QueueService final : public IRHICommandQueueFactory
{
public:
    explicit DX12QueueService(ID3D12Device* device);
    ~DX12QueueService();
    RHIQueueCapabilities QueryQueueCapabilities() const override;
    bool CreateQueue(RHIQueueKind kind, own::shared_owner<IRHICommandQueue>& queue,
        std::string& error) override;
    // Reuse the presentation queue, with a service-owned retirement timeline.
    bool GetPrimaryGraphicsQueue(ID3D12CommandQueue* primary,
        own::shared_owner<IRHICommandQueue>& queue, std::string& error);
    // Revoke admission, then drain accepted primitive operations before releasing queues.
    bool Shutdown(std::string& error);
    // Publish only after the primary submission's CPU ticket has succeeded.
    bool ImportPrimaryCompletion(ID3D12Fence* fence, uint64_t value,
        RHITimelinePoint& point, std::string& error);
    bool JoinPrimaryQueue(ID3D12CommandQueue* queue, const RHITimelinePoint& point,
        std::string& error);
    // Borrowed backend handle; caller retains the endpoint and drains before use ends.
    static ID3D12CommandQueue* NativeQueue(const own::shared_owner<IRHICommandQueue>& queue);
    struct RecordingPoolStats
    {
        uint64_t created{0}, reused{0}, leased{0}, cached{0};
    };
    static RecordingPoolStats QueryRecordingPool(const own::shared_owner<IRHICommandQueue>& queue);
    // Backend recording adapter for the supported buffer-copy COMMON boundary.
    // Default-heap inputs must be in COMMON; upload/readback fixed states stay fixed.
    static bool RecordBufferCopy(const own::shared_owner<IRHICommandQueue>& queue,
        ID3D12Resource* source, ID3D12Resource* destination, uint64_t bytes,
        ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error,
        ce::lifetime_token lifetimeToken = {});
#if !CE_SHIPPING
    bool EnqueueTestGate(const own::shared_owner<IRHICommandQueue>& queue,
        ID3D12Fence* fence, uint64_t value, std::string& error);
    void RejectNextTestSubmission(bool afterExecute);
#endif
private:
    own::shared_owner<DX12QueueServiceState> state_;
};
