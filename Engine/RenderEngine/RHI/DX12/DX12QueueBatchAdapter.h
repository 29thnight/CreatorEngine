#pragma once
#include "../RHIQueueContract.h"
#include <d3d12.h>
#include <span>

// Backend-only bridge for existing encoders/pools. Lists must already be closed.
// The mandatory token pins their non-resettable allocator/resource storage through
// completion; callers must not reset that storage while the batch is retained.
bool DX12SealQueueRecording(const std::shared_ptr<IRHICommandQueue>& queue,
    std::span<ID3D12CommandList* const> lists, std::shared_ptr<const void> lifetimeToken,
    std::shared_ptr<IRHIQueueCommandBatch>& batch, std::string& error);
