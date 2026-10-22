#pragma once
#include "../../../Utility_Framework/LifetimeToken.h"
#include "../../../Utility_Framework/Ownership.h"
#include "../RHIQueueContract.h"
#include <d3d12.h>
#include "../../../Utility_Framework/StandardLibrary.h"

// Backend-only bridge for existing encoders/pools. Lists must already be closed.
// The mandatory token pins their non-resettable allocator/resource storage through
// completion; callers must not reset that storage while the batch is retained.
bool DX12SealQueueRecording(const own::shared_owner<IRHICommandQueue>& queue,
    std::span<ID3D12CommandList* const> lists, ce::lifetime_token lifetimeToken,
    ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error);
