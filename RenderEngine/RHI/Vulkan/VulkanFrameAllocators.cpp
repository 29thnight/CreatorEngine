#ifndef DYNAMICCPP_EXPORTS
#include "VulkanFrameAllocators.h"

#include <algorithm>
#include <iterator>

using namespace VulkanApi;

namespace
{
    constexpr uint32_t kVkMaxSetsPerFrame = 256;

    const VkDescriptorPoolSize kVkPoolBudget[] = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  512 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  256 },
        { VK_DESCRIPTOR_TYPE_SAMPLER,        128 },
    };

    uint64_t VulkanUploadAlignUp(uint64_t value, uint64_t alignment)
    {
        alignment = (std::max)(alignment, 1ull);
        return ((value + alignment - 1) / alignment) * alignment;
    }
}

// ─────────────────────────────────────────── 완료점 기반 업로드 세그먼트

bool VulkanUploadSegmentAllocator::Initialize(VkDevice device,
    VkPhysicalDevice physicalDevice, VulkanResourceTable& table,
    uint64_t regularSegmentBytes, uint64_t largeThreshold,
    uint32_t standbyRegularCount, std::string& outError)
{
    if (VK_NULL_HANDLE == device || VK_NULL_HANDLE == physicalDevice ||
        0 == regularSegmentBytes || 0 == largeThreshold)
    {
        outError = "Vulkan 업로드 세그먼트 인자가 잘못됐다";
        return false;
    }

    m_device = device;
    m_table = &table;
    m_regularSegmentBytes = VulkanUploadAlignUp(regularSegmentBytes, 4096);
    m_largeThreshold = (std::min)(largeThreshold, m_regularSegmentBytes);
    m_creationThread = std::this_thread::get_id();

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    m_uniformAlignment = (std::max<uint64_t>)(1,
        properties.limits.minUniformBufferOffsetAlignment);
    m_storageAlignment = (std::max<uint64_t>)(1,
        properties.limits.minStorageBufferOffsetAlignment);
    m_textureCopyAlignment = (std::max<uint64_t>)(1,
        properties.limits.optimalBufferCopyOffsetAlignment);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_memoryProperties);

    for (uint32_t i = 0; i < standbyRegularCount; ++i)
    {
        if (nullptr == CreateSegment(m_regularSegmentBytes, false, outError))
        {
            Shutdown(device);
            return false;
        }
    }
    return true;
}

VulkanUploadSegmentAllocator::Segment* VulkanUploadSegmentAllocator::CreateSegment(
    uint64_t bytes, bool large, std::string& outError)
{
    bytes = VulkanUploadAlignUp(bytes, 4096);
    auto segment = std::make_unique<Segment>();

    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateBuffer(m_device, &info, nullptr, &segment->buffer);
    if (VK_SUCCESS != result)
    {
        outError = "Vulkan 업로드 버퍼 생성 실패 — " + ResultToString(result);
        ++m_oomFailures;
        return nullptr;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, segment->buffer, &requirements);

    const VkMemoryPropertyFlags wanted =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t memoryType = UINT32_MAX;
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; ++i)
    {
        if (0 != (requirements.memoryTypeBits & (1u << i)) &&
            wanted == (m_memoryProperties.memoryTypes[i].propertyFlags & wanted))
        {
            memoryType = i;
            break;
        }
    }
    if (UINT32_MAX == memoryType)
    {
        vkDestroyBuffer(m_device, segment->buffer, nullptr);
        segment->buffer = VK_NULL_HANDLE;
        outError = "Vulkan 호스트 가시·일관 메모리 타입을 찾지 못했다";
        ++m_oomFailures;
        return nullptr;
    }

    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(m_device, &allocate, nullptr, &segment->memory);
    if (VK_SUCCESS != result)
    {
        vkDestroyBuffer(m_device, segment->buffer, nullptr);
        segment->buffer = VK_NULL_HANDLE;
        outError = "Vulkan 업로드 메모리 할당 실패 — " + ResultToString(result);
        ++m_oomFailures;
        return nullptr;
    }

    result = vkBindBufferMemory(m_device, segment->buffer, segment->memory, 0);
    if (VK_SUCCESS == result)
        result = vkMapMemory(m_device, segment->memory, 0, VK_WHOLE_SIZE, 0,
            &segment->mapped);
    if (VK_SUCCESS != result)
    {
        vkDestroyBuffer(m_device, segment->buffer, nullptr);
        vkFreeMemory(m_device, segment->memory, nullptr);
        outError = "Vulkan 업로드 메모리 바인드/매핑 실패 — " + ResultToString(result);
        ++m_oomFailures;
        return nullptr;
    }

    segment->capacity = bytes;
    segment->large = large;
    VulkanBufferEntry entry{};
    entry.buffer = segment->buffer;
    entry.memory = segment->memory;
    entry.bytes = bytes;
    entry.mapped = segment->mapped;
    segment->handle = m_table->AddExternalBuffer(entry);
    if (!segment->handle.IsValid())
    {
        vkUnmapMemory(m_device, segment->memory);
        vkDestroyBuffer(m_device, segment->buffer, nullptr);
        vkFreeMemory(m_device, segment->memory, nullptr);
        outError = "Vulkan 업로드 세그먼트를 RHI 표에 등록하지 못했다";
        ++m_oomFailures;
        return nullptr;
    }

    Segment* raw = segment.get();
    m_segments.push_back(std::move(segment));
    ++m_slowPathCreates;
    return raw;
}

void VulkanUploadSegmentAllocator::Shutdown(VkDevice device)
{
    std::lock_guard lock(m_mutex);
    if (VK_NULL_HANDLE != device)
    {
        for (const auto& segment : m_segments)
        {
            if (m_table && segment->handle.IsValid())
                m_table->Release(device, segment->handle);
            if (segment->mapped && segment->memory)
                vkUnmapMemory(device, segment->memory);
            if (segment->buffer) vkDestroyBuffer(device, segment->buffer, nullptr);
            if (segment->memory) vkFreeMemory(device, segment->memory, nullptr);
        }
    }
    m_segments.clear();
    m_device = VK_NULL_HANDLE;
    m_table = nullptr;
    m_regularSegmentBytes = 0;
    m_largeThreshold = 0;
    m_currentRecordingId = 0;
    m_recordingBytes = 0;
    m_peakRecordingBytes = 0;
    m_slowPathCreates = 0;
    m_reuses = 0;
    m_tailWasteBytes = 0;
    m_reclaimLag = 0;
    m_batchRollbacks = 0;
    m_oomFailures = 0;
    m_allocations = 0;
    m_bytesAllocated = 0;
}

uint64_t VulkanUploadSegmentAllocator::RequiredAlignment(
    const RHIUploadRequest& request) const
{
    uint64_t required = 1;
    switch (request.usage)
    {
    case RHIUploadUsage::ConstantBuffer: required = m_uniformAlignment; break;
    case RHIUploadUsage::TextureCopy: required = m_textureCopyAlignment; break;
    case RHIUploadUsage::BufferCopy:
    case RHIUploadUsage::ShaderTable: required = m_storageAlignment; break;
    case RHIUploadUsage::VertexData:
    case RHIUploadUsage::IndexData: required = 4; break;
    default: break;
    }
    return (std::max)(required, (std::max)(request.minimumAlignment, 1ull));
}

bool VulkanUploadSegmentAllocator::TryPack(const Segment& segment,
    std::span<const RHIUploadRequest> requests, std::vector<uint64_t>& offsets,
    uint64_t& outEnd) const
{
    offsets.resize(requests.size());
    uint64_t cursor = segment.cursor;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        if (0 == requests[i].bytes) return false;
        cursor = VulkanUploadAlignUp(cursor, RequiredAlignment(requests[i]));
        if (cursor > segment.capacity || requests[i].bytes > segment.capacity - cursor)
            return false;
        offsets[i] = cursor;
        cursor += requests[i].bytes;
    }
    outEnd = cursor;
    return true;
}

VulkanUploadSegmentAllocator::Segment* VulkanUploadSegmentAllocator::FindSegment(
    bool large, uint64_t recordingId, std::span<const RHIUploadRequest> requests,
    std::vector<uint64_t>& offsets, uint64_t& outEnd)
{
    Segment* available = nullptr;
    for (const auto& candidate : m_segments)
    {
        if (candidate->large != large) continue;
        if (candidate->state == RHIUploadSegmentState::Active &&
            candidate->recordingId == recordingId &&
            TryPack(*candidate, requests, offsets, outEnd))
            return candidate.get();

        if (candidate->state == RHIUploadSegmentState::Available &&
            TryPack(*candidate, requests, offsets, outEnd) &&
            (nullptr == available || candidate->capacity < available->capacity))
            available = candidate.get();
    }
    if (available)
    {
        available->state = RHIUploadSegmentState::Active;
        available->recordingId = recordingId;
        ++m_reuses;
        TryPack(*available, requests, offsets, outEnd);
    }
    return available;
}

void VulkanUploadSegmentAllocator::Collect(uint64_t completedValue)
{
    std::lock_guard lock(m_mutex);
    for (const auto& segment : m_segments)
    {
        if (segment->state == RHIUploadSegmentState::Pending &&
            segment->completionValue <= completedValue)
        {
            m_reclaimLag = (std::max)(m_reclaimLag,
                completedValue - segment->completionValue);
            segment->state = RHIUploadSegmentState::Available;
            segment->cursor = 0;
            segment->recordingId = 0;
            segment->completionValue = 0;
        }
    }
}

void VulkanUploadSegmentAllocator::BeginRecording(uint64_t recordingId)
{
    std::lock_guard lock(m_mutex);
    m_currentRecordingId = recordingId;
    m_recordingBytes = 0;
}

bool VulkanUploadSegmentAllocator::ReserveBatch(uint64_t recordingId,
    std::span<const RHIUploadRequest> requests,
    std::span<RHIBufferSlice> outSlices, std::string& outError)
{
    if (requests.empty() || requests.size() != outSlices.size() || 0 == recordingId)
    {
        outError = "Vulkan 업로드 배치의 요청/출력 또는 recording id가 잘못됐다";
        return false;
    }

    std::lock_guard lock(m_mutex);
    if (recordingId != m_currentRecordingId)
    {
        outError = "Vulkan 업로드 배치가 현재 recording과 일치하지 않는다";
        return false;
    }

    uint64_t minimumPacked = 0;
    for (const auto& request : requests)
    {
        if (0 == request.bytes)
        {
            outError = "0바이트 Vulkan 업로드 요청은 예약할 수 없다";
            ++m_batchRollbacks;
            return false;
        }
        minimumPacked = VulkanUploadAlignUp(minimumPacked, RequiredAlignment(request));
        minimumPacked += request.bytes;
    }

    const bool large = minimumPacked > m_largeThreshold;
    std::vector<uint64_t> offsets;
    uint64_t end = 0;
    Segment* segment = FindSegment(large, recordingId, requests, offsets, end);
    if (nullptr == segment)
    {
        if (std::this_thread::get_id() != m_creationThread)
        {
            outError = "Vulkan worker 기록 중 업로드 세그먼트 증가가 필요하다";
            ++m_batchRollbacks;
            ++m_oomFailures;
            return false;
        }
        const uint64_t bytes = large
            ? VulkanUploadAlignUp(minimumPacked, 4ull * 1024 * 1024)
            : m_regularSegmentBytes;
        segment = CreateSegment(bytes, large, outError);
        if (nullptr == segment)
        {
            ++m_batchRollbacks;
            return false;
        }
        segment->state = RHIUploadSegmentState::Active;
        segment->recordingId = recordingId;
        if (!TryPack(*segment, requests, offsets, end))
        {
            segment->state = RHIUploadSegmentState::Available;
            outError = "새 Vulkan 업로드 세그먼트에 배치를 배치하지 못했다";
            ++m_batchRollbacks;
            return false;
        }
    }

    std::vector<RHIBufferSlice> slices(requests.size());
    for (size_t i = 0; i < requests.size(); ++i)
    {
        slices[i].buffer = segment->handle;
        slices[i].offset = offsets[i];
        slices[i].size = requests[i].bytes;
        slices[i].cpuAddress = static_cast<uint8_t*>(segment->mapped) + offsets[i];
    }

    const uint64_t oldCursor = segment->cursor;
    segment->cursor = end;
    m_recordingBytes += end - oldCursor;
    m_allocations += requests.size();
    for (const auto& request : requests) m_bytesAllocated += request.bytes;
    m_peakRecordingBytes = (std::max)(m_peakRecordingBytes, m_recordingBytes);
    std::copy(slices.begin(), slices.end(), outSlices.begin());
    return true;
}

void VulkanUploadSegmentAllocator::OnSubmitted(uint64_t recordingId,
    RHICompletionPoint completion)
{
    std::lock_guard lock(m_mutex);
    for (const auto& segment : m_segments)
    {
        if (segment->state != RHIUploadSegmentState::Active ||
            segment->recordingId != recordingId) continue;
        m_tailWasteBytes += segment->capacity - segment->cursor;
        segment->completionValue = completion.value;
        segment->state = completion.IsValid()
            ? RHIUploadSegmentState::Pending
            : RHIUploadSegmentState::Quarantined;
    }
}

void VulkanUploadSegmentAllocator::AbortRecording(uint64_t recordingId)
{
    std::lock_guard lock(m_mutex);
    for (const auto& segment : m_segments)
    {
        if (segment->state == RHIUploadSegmentState::Active &&
            segment->recordingId == recordingId)
        {
            segment->state = RHIUploadSegmentState::Available;
            segment->cursor = 0;
            segment->recordingId = 0;
        }
    }
    if (recordingId == m_currentRecordingId) m_recordingBytes = 0;
}

RHIUploadStats VulkanUploadSegmentAllocator::GetStats() const
{
    std::lock_guard lock(m_mutex);
    RHIUploadStats stats{};
    stats.allocations = m_allocations;
    stats.bytesAllocated = m_bytesAllocated;
    stats.peakFrameBytes = m_peakRecordingBytes;
    stats.segmentCount = static_cast<uint32_t>(m_segments.size());
    stats.peakRecordingBytes = m_peakRecordingBytes;
    stats.slowPathCreates = m_slowPathCreates;
    stats.reuses = m_reuses;
    stats.tailWasteBytes = m_tailWasteBytes;
    stats.batchRollbacks = m_batchRollbacks;
    stats.oomFailures = m_oomFailures;
    stats.reclaimLag = m_reclaimLag;
    for (const auto& segment : m_segments)
    {
        stats.segmentBytes += segment->capacity;
        if (segment->large)
        {
            ++stats.largeSegments;
            stats.largeBytes += segment->capacity;
        }
        switch (segment->state)
        {
        case RHIUploadSegmentState::Active:
            ++stats.activeSegments; stats.activeBytes += segment->capacity; break;
        case RHIUploadSegmentState::Pending:
            ++stats.pendingSegments; stats.pendingBytes += segment->capacity;
            if (0 == stats.oldestPendingValue ||
                segment->completionValue < stats.oldestPendingValue)
                stats.oldestPendingValue = segment->completionValue;
            break;
        case RHIUploadSegmentState::Available:
            ++stats.availableSegments; stats.availableBytes += segment->capacity; break;
        default: break;
        }
    }
    return stats;
}

uint64_t VulkanUploadSegmentAllocator::GetRecordingUsedBytes() const
{
    std::lock_guard lock(m_mutex);
    return m_recordingBytes;
}

// ────────────────────────────────────────────────────────────── 디스크립터 풀

bool VulkanDescriptorPool::Initialize(VkDevice device, uint32_t frameCount,
    std::string& outError)
{
    if (VK_NULL_HANDLE == device || 0 == frameCount)
    {
        outError = "디스크립터 풀 인자가 잘못됐다";
        return false;
    }

    m_pools.resize(frameCount, VK_NULL_HANDLE);
    for (VkDescriptorPool& pool : m_pools)
    {
        VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        info.maxSets = kVkMaxSetsPerFrame;
        info.poolSizeCount = static_cast<uint32_t>(std::size(kVkPoolBudget));
        info.pPoolSizes = kVkPoolBudget;
        const VkResult made = vkCreateDescriptorPool(device, &info, nullptr, &pool);
        if (VK_SUCCESS != made)
        {
            outError = "디스크립터 풀 생성 실패 — " + ResultToString(made);
            return false;
        }
    }
    return true;
}

void VulkanDescriptorPool::Shutdown(VkDevice device)
{
    if (VK_NULL_HANDLE != device)
    {
        for (VkDescriptorPool pool : m_pools)
            if (VK_NULL_HANDLE != pool) vkDestroyDescriptorPool(device, pool, nullptr);
    }
    m_pools.clear();
}

bool VulkanDescriptorPool::Reset(VkDevice device, uint32_t frameIndex)
{
    m_frameIndex = frameIndex;
    if (VK_NULL_HANDLE == device || frameIndex >= m_pools.size()) return false;
    return VK_SUCCESS == vkResetDescriptorPool(device, m_pools[frameIndex], 0);
}

VkDescriptorSet VulkanDescriptorPool::Allocate(VkDevice device,
    VkDescriptorSetLayout setLayout)
{
    if (VK_NULL_HANDLE == device || VK_NULL_HANDLE == setLayout) return VK_NULL_HANDLE;
    if (m_frameIndex >= m_pools.size()) return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    info.descriptorPool = m_pools[m_frameIndex];
    info.descriptorSetCount = 1;
    info.pSetLayouts = &setLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (VK_SUCCESS != vkAllocateDescriptorSets(device, &info, &set)) return VK_NULL_HANDLE;
    return set;
}

#endif
