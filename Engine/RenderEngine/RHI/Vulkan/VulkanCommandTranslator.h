#pragma once
#include "../IRHICommandTranslator.h"
#include "VulkanCommandBufferPool.h"
#include "VulkanDeviceResources.h"

class VulkanCommandTranslator final : public RHICommandTranslator
{
  public:
    VulkanCommandTranslator(VulkanCommandBufferPool& pool, VulkanDeviceResources& resources, RHIQueueIdentity queue)
        : RHICommandTranslator(pool, resources, queue)
    {
    }
};
