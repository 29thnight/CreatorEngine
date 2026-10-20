#pragma once
#include "../IRHICommandTranslator.h"
#include "DX12CommandListPool.h"
#include "DX12DeviceResources.h"

class DX12CommandTranslator final : public RHICommandTranslator
{
  public:
    DX12CommandTranslator(DX12CommandListPool& pool, DX12DeviceResources& resources, RHIQueueIdentity queue)
        : RHICommandTranslator(pool, resources, queue)
    {
    }
};
