#pragma once

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
        std::vector<VulkanLayoutSlot> m_parameters;
        VulkanDescriptorRequirements m_requirements;
    };
    uint64_t m_uploadRecordingId{};
    uint64_t m_descriptorVersion{};
    uint64_t m_bindingEpoch{};
    size_t m_bindingCount{};
    size_t m_samplerCount{};
    size_t m_layoutCount{};
    std::vector<VulkanPipelineCache::PipelineSlot> m_pipelines;
    std::vector<Layout> m_layouts;
    std::vector<VulkanResourceTable::Slot<VulkanImageEntry>> m_images;
    std::vector<VulkanResourceTable::Slot<VulkanBufferEntry>> m_buffers;
    std::vector<std::vector<RHIBindingDesc>> m_bindings;
    std::vector<std::vector<VkSampler>> m_samplers;
    std::vector<VulkanRenderTargetBinding> m_renderTargets;
    std::shared_ptr<const void> m_renderTargetLease;
    std::vector<std::shared_ptr<const void>> m_uploadLeases;
};
