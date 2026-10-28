#include "../../../Containers/ContainerAPI.h"
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

#include "VulkanBindingTable.h"
#include "VulkanPipelineCache.h"
#include "VulkanRenderTargetTable.h"
#include "VulkanResourceTable.h"

class VulkanDeviceResources;

// A frozen pool exclusively owns this retained storage until both CPU replay and
// GPU retirement release it. Handles are resolved against this recording only.
class VulkanRecordingResources final
{
  public:
    bool Capture(VulkanDeviceResources& resources, std::string& outError);
    void Reset();
    uint64_t GetUploadRecordingId() const { return m_uploadRecordingId; }
    uint64_t GetDescriptorVersion() const { return m_descriptorVersion; }

    VulkanPipelineEntry Resolve(RHIPipelineHandle handle) const;
    VulkanLayoutSlot ResolveParam(RHIPipelineLayoutHandle handle, uint32_t parameter) const;
    VulkanDescriptorRequirements ResolveDescriptorRequirements(RHIPipelineLayoutHandle handle) const;
    VulkanImageEntry Resolve(RHITextureHandle handle) const;
    VulkanBufferEntry Resolve(RHIBufferHandle handle) const;
    VulkanRenderTargetBinding ResolveTargets(uint64_t slot) const;
    std::span<const RHIBindingDesc> Resolve(const RHIBindingTable& table) const;
    std::span<const VkSampler> Resolve(const RHISamplerTable& table) const;
    VkImageView ResolveCubeView(RHITextureHandle handle, RHIFormat format, uint32_t baseMip, uint32_t mipLevels,
                                uint32_t firstSlice) const;

  private:
    struct Layout
    {
        ce::dynamic_array<VulkanLayoutSlot> m_parameters;
        VulkanDescriptorRequirements m_requirements;
    };
    uint64_t m_uploadRecordingId{};
    uint64_t m_descriptorVersion{};
    uint64_t m_bindingEpoch{};
    size_t m_bindingCount{};
    size_t m_samplerCount{};
    size_t m_layoutCount{};
    ce::dynamic_array<VulkanPipelineCache::PipelineSlot> m_pipelines;
    ce::dynamic_array<Layout> m_layouts;
    ce::dynamic_array<VulkanResourceTable::Slot<VulkanImageEntry>> m_images;
    ce::dynamic_array<VulkanResourceTable::Slot<VulkanBufferEntry>> m_buffers;
    ce::dynamic_array<ce::dynamic_array<RHIBindingDesc>> m_bindings;
    ce::dynamic_array<ce::dynamic_array<VkSampler>> m_samplers;
    ce::dynamic_array<VulkanRenderTargetBinding> m_renderTargets;
    ce::lifetime_token m_renderTargetLease;
    ce::dynamic_array<ce::lifetime_token> m_uploadLeases;
};
