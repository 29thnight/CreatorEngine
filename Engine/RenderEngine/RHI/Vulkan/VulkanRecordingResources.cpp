#include "VulkanRecordingResources.h"
#include "VulkanDeviceResources.h"

void VulkanRecordingResources::Reset()
{
    m_uploadRecordingId = 0;
    m_descriptorVersion = 0;
    m_bindingEpoch = 0;
    m_bindingCount = 0;
    m_samplerCount = 0;
    m_layoutCount = 0;
    m_pipelines.clear();
    m_images.clear();
    m_buffers.clear();
    m_renderTargets.clear();
    m_renderTargetLease.reset();
    m_uploadLeases.clear();
}

bool VulkanRecordingResources::Capture(VulkanDeviceResources& resources, std::string& outError)
{
    Reset();
    if (!resources.m_pipelineCache || !resources.m_currentRecordingId)
    {
        outError = "Vulkan recording snapshot requires a pipeline cache and active recording.";
        return false;
    }
    auto& bindings = resources.m_bindingTable;
    {
        const std::lock_guard lock(bindings.m_mutex);
        m_bindingEpoch = bindings.m_epoch;
        m_bindingCount = bindings.m_activeRequests;
        if (m_bindings.size() < m_bindingCount)
        {
            m_bindings.resize(m_bindingCount);
        }
        for (size_t index = 0; index < m_bindingCount; ++index)
        {
            const auto& source = bindings.m_requests[index]->descs;
            m_bindings[index].assign(source.begin(), source.end());
            // Materialize all view ranges before freezing metadata. Native
            // translation only reads this recording's immutable view generation.
            for (const auto& view : source)
            {
                if (view.dim != RHIBindingDesc::Dim::Buffer && view.resource.IsValid() &&
                    !resources.m_resourceTable.GetOrCreateImageView(resources.m_device, view))
                {
                    outError = "Vulkan recording snapshot cannot resolve an image view.";
                    return false;
                }
            }
        }
    }
    auto& samplers = resources.m_samplerTable;
    m_samplerCount = samplers.m_requests.size();
    if (m_samplers.size() < m_samplerCount)
    {
        m_samplers.resize(m_samplerCount);
    }
    for (size_t index = 0; index < m_samplerCount; ++index)
    {
        const auto& source = samplers.m_requests[index].samplers;
        m_samplers[index].assign(source.begin(), source.end());
    }
    auto& targets = resources.m_renderTargetTables[resources.m_frameIndex];
    {
        const std::lock_guard lock(targets.m_mutex);
        m_renderTargets.assign(targets.m_bindings.begin(), targets.m_bindings.end());
        m_renderTargetLease = targets.m_ownedViews;
    }
    const auto& pipelines = *resources.m_pipelineCache;
    m_pipelines.assign(pipelines.m_pipelines.begin(), pipelines.m_pipelines.end());
    m_layoutCount = pipelines.m_layouts.size();
    if (m_layouts.size() < m_layoutCount)
    {
        m_layouts.resize(m_layoutCount);
    }
    for (size_t index = 0; index < m_layoutCount; ++index)
    {
        const auto& source = pipelines.m_layouts[index];
        m_layouts[index].m_parameters.assign(source.paramSlots.begin(), source.paramSlots.end());
        m_layouts[index].m_requirements = source.descriptorRequirements;
    }
    auto& table = resources.m_resourceTable;
    table.CollectRecordingLeases(resources.m_device);
    {
        const std::scoped_lock lock(table.m_imageRegistryMutex, table.m_bufferRegistryMutex);
        m_images.assign(table.m_images.begin(), table.m_images.end());
        const uint32_t bufferCount = table.m_bufferHighWater.load(std::memory_order_acquire);
        m_buffers.assign(table.m_buffers.begin(), table.m_buffers.begin() + bufferCount);
    }
    if (!resources.m_uploadAllocator.PinRecording(resources.m_currentRecordingId, m_uploadLeases, outError))
    {
        Reset();
        return false;
    }
    m_uploadRecordingId = resources.m_currentRecordingId;
    m_descriptorVersion = resources.GetDescriptorVersionToken();
    return true;
}

VulkanPipelineEntry VulkanRecordingResources::Resolve(RHIPipelineHandle handle) const
{
    const uint32_t slot = RHIHandleBits::SlotOf(handle.id);
    if (!handle.IsValid() || slot >= m_pipelines.size())
    {
        return {};
    }
    const auto& value = m_pipelines[slot];
    return value.alive && value.generation == RHIHandleBits::GenerationOf(handle.id) ? value.entry
                                                                                     : VulkanPipelineEntry{};
}

VulkanLayoutSlot VulkanRecordingResources::ResolveParam(RHIPipelineLayoutHandle handle, uint32_t parameter) const
{
    const uint32_t slot = RHIHandleBits::SlotOf(handle.id);
    if (!handle.IsValid() || slot >= m_layoutCount || parameter >= m_layouts[slot].m_parameters.size())
    {
        return {};
    }
    return m_layouts[slot].m_parameters[parameter];
}

VulkanDescriptorRequirements VulkanRecordingResources::ResolveDescriptorRequirements(
    RHIPipelineLayoutHandle handle) const
{
    const uint32_t slot = RHIHandleBits::SlotOf(handle.id);
    return handle.IsValid() && slot < m_layoutCount ? m_layouts[slot].m_requirements : VulkanDescriptorRequirements{};
}

VulkanImageEntry VulkanRecordingResources::Resolve(RHITextureHandle handle) const
{
    return VulkanResourceTable::ResolveIn(m_images, handle.id);
}

VulkanBufferEntry VulkanRecordingResources::Resolve(RHIBufferHandle handle) const
{
    return VulkanResourceTable::ResolveIn(m_buffers, handle.id);
}

VulkanRenderTargetBinding VulkanRecordingResources::ResolveTargets(uint64_t slot) const
{
    return slot < m_renderTargets.size() ? m_renderTargets[static_cast<size_t>(slot)] : VulkanRenderTargetBinding{};
}

std::span<const RHIBindingDesc> VulkanRecordingResources::Resolve(const RHIBindingTable& table) const
{
    if (!table.IsValid() || table.version != m_bindingEpoch || !table.backend || table.backend > m_bindingCount)
    {
        return {};
    }
    const auto& request = m_bindings[static_cast<size_t>(table.backend - 1)];
    return request.size() == table.count ? std::span<const RHIBindingDesc>(request) : std::span<const RHIBindingDesc>{};
}

std::span<const VkSampler> VulkanRecordingResources::Resolve(const RHISamplerTable& table) const
{
    return table.IsValid() && table.backend <= m_samplerCount
               ? std::span<const VkSampler>(m_samplers[static_cast<size_t>(table.backend - 1)])
               : std::span<const VkSampler>{};
}

VkImageView VulkanRecordingResources::ResolveCubeView(RHITextureHandle handle, RHIFormat format, uint32_t baseMip,
                                                      uint32_t mipLevels, uint32_t firstSlice) const
{
    const auto entry = Resolve(handle);
    const auto resolvedFormat = format == RHIFormat::Unknown ? entry.format : format;
    return entry.IsValid() && entry.cubeViewFormat == resolvedFormat && entry.cubeViewBaseMip == baseMip &&
                   entry.cubeViewMipLevels == mipLevels && entry.cubeViewFirstSlice == firstSlice
               ? entry.cubeView
               : VK_NULL_HANDLE;
}
