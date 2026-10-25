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
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"

// Backend-only bridge for existing encoders/pools. Lists must already be closed.
// The mandatory token pins their non-resettable allocator/resource storage through
// completion; callers must not reset that storage while the batch is retained.
bool DX12SealQueueRecording(const own::shared_owner<IRHICommandQueue>& queue,
    std::span<ID3D12CommandList* const> lists, ce::lifetime_token lifetimeToken,
    ce::pooled_owner<IRHIQueueCommandBatch>& batch, std::string& error);
