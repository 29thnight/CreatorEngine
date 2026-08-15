#ifndef DYNAMICCPP_EXPORTS
#include "VulkanFrameAllocators.h"

#include <algorithm>
#include <iterator>
#include <limits>

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
        if (value > (std::numeric_limits<uint64_t>::max)() - (alignment - 1))
            return (std::numeric_limits<uint64_t>::max)();
        return ((value + alignment - 1) / alignment) * alignment;
    }
}

// ─────────────────────────────────────────── 완료점 기반 업로드 세그먼트

bool VulkanUploadSegmentAllocator::Initialize(VkDevice device,
    VkPhysicalDevice physicalDevice, VulkanResourceTable& table,
    const RHIUploadSegmentPolicy& policy, std::string& outError)
{
    if (VK_NULL_HANDLE == device || VK_NULL_HANDLE == physicalDevice ||
        0 == policy.regularSegmentBytes || 0 == policy.largeThreshold)
    {
        outError = "Vulkan 업로드 세그먼트 인자가 잘못됐다";
        return false;
    }

    m_device = device;
    m_table = &table;
    m_regularSegmentBytes = VulkanUploadAlignUp(policy.regularSegmentBytes, 4096);
    m_largeThreshold = (std::min)(policy.largeThreshold, m_regularSegmentBytes);
    m_standbyRegularSegments = policy.standbyRegularSegments;
    m_trimDelayCollects = policy.trimDelayCollects;
    m_defaultLargeCacheBudgetBytes = policy.largeCacheBudgetBytes;
    m_softBudgetBytes.store(policy.softBudgetBytes, std::memory_order_release);
    m_largeCacheBudgetBytes.store(policy.largeCacheBudgetBytes, std::memory_order_release);
    m_ownerThread = std::this_thread::get_id();

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    m_uniformAlignment = (std::max<uint64_t>)(1,
        properties.limits.minUniformBufferOffsetAlignment);
    m_storageAlignment = (std::max<uint64_t>)(1,
        properties.limits.minStorageBufferOffsetAlignment);
    m_textureCopyAlignment = (std::max<uint64_t>)(1,
        properties.limits.optimalBufferCopyOffsetAlignment);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &m_memoryProperties);

    for (uint32_t i = 0; i < m_standbyRegularSegments; ++i)
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
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
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
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    if (UINT32_MAX == m_memoryHeapIndex)
        m_memoryHeapIndex = m_memoryProperties.memoryTypes[memoryType].heapIndex;

    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = memoryType;
    result = vkAllocateMemory(m_device, &allocate, nullptr, &segment->memory);
    if (VK_SUCCESS != result)
    {
        vkDestroyBuffer(m_device, segment->buffer, nullptr);
        segment->buffer = VK_NULL_HANDLE;
        outError = "Vulkan 업로드 메모리 할당 실패 — " + ResultToString(result);
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
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
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
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
        outError = "Vulkan 업로드 세그먼트를 stable RHI 표에 등록하지 못했다";
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    Segment* raw = segment.get();
    m_segments.push_back(std::move(segment));
    m_slowPathCreates.fetch_add(1, std::memory_order_relaxed);
    if (std::this_thread::get_id() != m_ownerThread)
        m_workerSegmentCreates.fetch_add(1, std::memory_order_relaxed);
    return raw;
}

void VulkanUploadSegmentAllocator::Shutdown(VkDevice device)
{
    m_fastRegular.store(nullptr, std::memory_order_release);
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
    m_standbyRegularSegments = 0;
    m_trimDelayCollects = 0;
    m_defaultLargeCacheBudgetBytes = 0;
    m_memoryHeapIndex = UINT32_MAX;
    m_collectEpoch = 0;
    m_currentRecordingId.store(0, std::memory_order_release);
    m_recordingBytes.store(0, std::memory_order_release);
    m_peakRecordingBytes.store(0, std::memory_order_release);
    m_slowPathCreates.store(0, std::memory_order_release);
    m_reuses.store(0, std::memory_order_release);
    m_fastPathReservations.store(0, std::memory_order_release);
    m_slowPathReservations.store(0, std::memory_order_release);
    m_casRetries.store(0, std::memory_order_release);
    m_workerSegmentCreates.store(0, std::memory_order_release);
    m_tailWasteBytes.store(0, std::memory_order_release);
    m_reclaimLag.store(0, std::memory_order_release);
    m_batchRollbacks.store(0, std::memory_order_release);
    m_oomFailures.store(0, std::memory_order_release);
    m_allocations.store(0, std::memory_order_release);
    m_bytesAllocated.store(0, std::memory_order_release);
    m_softBudgetBytes.store(0, std::memory_order_release);
    m_largeCacheBudgetBytes.store(0, std::memory_order_release);
    m_memoryPressure.store(false, std::memory_order_release);
    m_budgetOverrideForTesting.store(false, std::memory_order_release);
    m_trimmedSegments.store(0, std::memory_order_release);
    m_trimmedBytes.store(0, std::memory_order_release);
    m_budgetPressureEvents.store(0, std::memory_order_release);
    m_budgetRetries.store(0, std::memory_order_release);
    m_budgetOvercommits.store(0, std::memory_order_release);
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

bool VulkanUploadSegmentAllocator::TryPack(uint64_t start, uint64_t capacity,
    std::span<const RHIUploadRequest> requests, uint64_t& outEnd) const
{
    uint64_t cursor = start;
    for (const RHIUploadRequest& request : requests)
    {
        if (0 == request.bytes) return false;
        cursor = VulkanUploadAlignUp(cursor, RequiredAlignment(request));
        if (cursor > capacity || request.bytes > capacity - cursor) return false;
        cursor += request.bytes;
    }
    outEnd = cursor;
    return true;
}

void VulkanUploadSegmentAllocator::UpdatePeakRecordingBytes(uint64_t value)
{
    uint64_t peak = m_peakRecordingBytes.load(std::memory_order_relaxed);
    while (peak < value && !m_peakRecordingBytes.compare_exchange_weak(
        peak, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

bool VulkanUploadSegmentAllocator::TryReserveAtomic(Segment& segment,
    std::span<const RHIUploadRequest> requests,
    std::span<RHIBufferSlice> outSlices, bool fastPath)
{
    uint64_t expected = segment.cursor.load(std::memory_order_relaxed);
    uint64_t end = 0;
    for (;;)
    {
        if (!TryPack(expected, segment.capacity, requests, end)) return false;
        if (segment.cursor.compare_exchange_weak(expected, end,
            std::memory_order_acq_rel, std::memory_order_relaxed)) break;
        m_casRetries.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t cursor = expected;
    uint64_t requestedBytes = 0;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        cursor = VulkanUploadAlignUp(cursor, RequiredAlignment(requests[i]));
        outSlices[i].buffer = segment.handle;
        outSlices[i].offset = cursor;
        outSlices[i].size = requests[i].bytes;
        outSlices[i].cpuAddress = static_cast<uint8_t*>(segment.mapped) + cursor;
        cursor += requests[i].bytes;
        requestedBytes += requests[i].bytes;
    }

    const uint64_t used = end - expected;
    const uint64_t recordingBytes = m_recordingBytes.fetch_add(
        used, std::memory_order_relaxed) + used;
    UpdatePeakRecordingBytes(recordingBytes);
    m_allocations.fetch_add(requests.size(), std::memory_order_relaxed);
    m_bytesAllocated.fetch_add(requestedBytes, std::memory_order_relaxed);
    (fastPath ? m_fastPathReservations : m_slowPathReservations)
        .fetch_add(1, std::memory_order_relaxed);
    return true;
}

VulkanUploadSegmentAllocator::Segment*
VulkanUploadSegmentAllocator::FindAvailableSegmentLocked(bool large,
    uint64_t minimumBytes)
{
    Segment* best = nullptr;
    for (const auto& candidate : m_segments)
    {
        if (candidate->large != large ||
            candidate->state != RHIUploadSegmentState::Available ||
            candidate->capacity < minimumBytes) continue;
        if (nullptr == best || candidate->capacity < best->capacity)
            best = candidate.get();
    }
    return best;
}

uint64_t VulkanUploadSegmentAllocator::SegmentBytesLocked() const
{
    uint64_t bytes = 0;
    for (const auto& segment : m_segments) bytes += segment->capacity;
    return bytes;
}

void VulkanUploadSegmentAllocator::ReleaseSegmentLocked(Segment& segment)
{
    if (m_table && segment.handle.IsValid()) m_table->Release(m_device, segment.handle);
    segment.handle = {};
    if (segment.mapped && segment.memory) vkUnmapMemory(m_device, segment.memory);
    if (segment.buffer) vkDestroyBuffer(m_device, segment.buffer, nullptr);
    if (segment.memory) vkFreeMemory(m_device, segment.memory, nullptr);
    segment.mapped = nullptr;
    segment.buffer = VK_NULL_HANDLE;
    segment.memory = VK_NULL_HANDLE;
}

void VulkanUploadSegmentAllocator::TrimAvailableLocked(bool memoryPressure,
    uint64_t bytesNeeded)
{
    uint64_t totalBytes = SegmentBytesLocked();
    uint32_t availableRegular = 0;
    uint64_t availableLargeBytes = 0;
    for (const auto& segment : m_segments)
    {
        if (segment->state != RHIUploadSegmentState::Available) continue;
        if (segment->large) availableLargeBytes += segment->capacity;
        else ++availableRegular;
    }

    const uint32_t keepRegular = memoryPressure ? 0 : m_standbyRegularSegments;
    const uint64_t keepLarge = memoryPressure ? 0 :
        m_largeCacheBudgetBytes.load(std::memory_order_relaxed);
    const uint64_t softBudget = m_softBudgetBytes.load(std::memory_order_relaxed);

    for (;;)
    {
        const bool budgetExcess = 0 != softBudget &&
            (bytesNeeded > softBudget || totalBytes > softBudget - bytesNeeded);
        const bool immediate = memoryPressure || budgetExcess;
        const auto eligible = [&](const Segment& segment) {
            return immediate || 0 == m_trimDelayCollects ||
                (m_collectEpoch >= segment.lastCollectedEpoch &&
                    m_collectEpoch - segment.lastCollectedEpoch >= m_trimDelayCollects);
        };
        auto candidate = m_segments.end();

        if (availableLargeBytes > keepLarge || budgetExcess)
        {
            for (auto it = m_segments.begin(); it != m_segments.end(); ++it)
            {
                if ((*it)->state != RHIUploadSegmentState::Available ||
                    !(*it)->large || !eligible(**it))
                    continue;
                if (candidate == m_segments.end() || (*it)->capacity > (*candidate)->capacity)
                    candidate = it;
            }
        }
        if (candidate == m_segments.end() &&
            (availableRegular > keepRegular || budgetExcess))
        {
            for (auto it = m_segments.begin(); it != m_segments.end(); ++it)
            {
                if ((*it)->state == RHIUploadSegmentState::Available &&
                    !(*it)->large && eligible(**it))
                {
                    candidate = it;
                    break;
                }
            }
        }
        if (candidate == m_segments.end()) break;

        const uint64_t releasedBytes = (*candidate)->capacity;
        const bool releasedLarge = (*candidate)->large;
        ReleaseSegmentLocked(**candidate);
        m_segments.erase(candidate);
        totalBytes -= releasedBytes;
        if (releasedLarge) availableLargeBytes -= releasedBytes;
        else --availableRegular;
        m_trimmedSegments.fetch_add(1, std::memory_order_relaxed);
        m_trimmedBytes.fetch_add(releasedBytes, std::memory_order_relaxed);
    }
}

void VulkanUploadSegmentAllocator::EnsureBudgetForCreateLocked(uint64_t bytesNeeded)
{
    const uint64_t softBudget = m_softBudgetBytes.load(std::memory_order_relaxed);
    const uint64_t totalBytes = SegmentBytesLocked();
    const bool memoryPressure = m_memoryPressure.load(std::memory_order_relaxed);
    const bool budgetExcess = 0 != softBudget &&
        (bytesNeeded > softBudget || totalBytes > softBudget - bytesNeeded);
    if (!memoryPressure && !budgetExcess) return;

    m_budgetPressureEvents.fetch_add(1, std::memory_order_relaxed);
    m_budgetRetries.fetch_add(1, std::memory_order_relaxed);
    TrimAvailableLocked(memoryPressure, bytesNeeded);

    const uint64_t afterTrim = SegmentBytesLocked();
    if (0 != softBudget &&
        (bytesNeeded > softBudget || afterTrim > softBudget - bytesNeeded))
        m_budgetOvercommits.fetch_add(1, std::memory_order_relaxed);
}

void VulkanUploadSegmentAllocator::UpdateBudget(uint64_t softBudgetBytes,
    bool memoryPressure)
{
    if (m_budgetOverrideForTesting.load(std::memory_order_acquire)) return;
    m_softBudgetBytes.store(softBudgetBytes, std::memory_order_release);
    m_memoryPressure.store(memoryPressure, std::memory_order_release);
}

void VulkanUploadSegmentAllocator::SetBudgetForTesting(uint64_t softBudgetBytes,
    uint64_t largeCacheBudgetBytes, bool memoryPressure)
{
    m_budgetOverrideForTesting.store(true, std::memory_order_release);
    m_softBudgetBytes.store(softBudgetBytes, std::memory_order_release);
    m_largeCacheBudgetBytes.store(largeCacheBudgetBytes, std::memory_order_release);
    m_memoryPressure.store(memoryPressure, std::memory_order_release);
}

void VulkanUploadSegmentAllocator::ClearBudgetOverrideForTesting()
{
    m_largeCacheBudgetBytes.store(m_defaultLargeCacheBudgetBytes,
        std::memory_order_release);
    m_memoryPressure.store(false, std::memory_order_release);
    m_budgetOverrideForTesting.store(false, std::memory_order_release);
}

void VulkanUploadSegmentAllocator::Collect(uint64_t completedValue)
{
    std::lock_guard lock(m_mutex);
    ++m_collectEpoch;
    for (const auto& segment : m_segments)
    {
        if (segment->state == RHIUploadSegmentState::Pending &&
            segment->completionValue <= completedValue)
        {
            const uint64_t lag = completedValue - segment->completionValue;
            uint64_t previous = m_reclaimLag.load(std::memory_order_relaxed);
            while (previous < lag && !m_reclaimLag.compare_exchange_weak(
                previous, lag, std::memory_order_relaxed, std::memory_order_relaxed)) {}
            segment->state = RHIUploadSegmentState::Available;
            segment->cursor.store(0, std::memory_order_relaxed);
            segment->recordingId = 0;
            segment->completionValue = 0;
            segment->lastCollectedEpoch = m_collectEpoch;
        }
    }
    const bool memoryPressure = m_memoryPressure.load(std::memory_order_relaxed);
    if (memoryPressure)
        m_budgetPressureEvents.fetch_add(1, std::memory_order_relaxed);
    TrimAvailableLocked(memoryPressure, 0);
}

void VulkanUploadSegmentAllocator::BeginRecording(uint64_t recordingId)
{
    std::lock_guard lock(m_mutex);
    m_fastRegular.store(nullptr, std::memory_order_release);
    m_currentRecordingId.store(recordingId, std::memory_order_release);
    m_recordingBytes.store(0, std::memory_order_release);
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
    if (recordingId != m_currentRecordingId.load(std::memory_order_acquire))
    {
        outError = "Vulkan 업로드 배치가 현재 recording과 일치하지 않는다";
        return false;
    }

    uint64_t minimumPacked = 0;
    if (!TryPack(0, (std::numeric_limits<uint64_t>::max)(), requests, minimumPacked))
    {
        outError = "Vulkan 업로드 배치가 0바이트이거나 크기 계산이 넘쳤다";
        m_batchRollbacks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const bool large = minimumPacked > m_largeThreshold;
    if (!large)
    {
        Segment* const active = m_fastRegular.load(std::memory_order_acquire);
        if (nullptr != active && TryReserveAtomic(*active, requests, outSlices, true))
            return true;
    }

    std::lock_guard lock(m_mutex);
    if (recordingId != m_currentRecordingId.load(std::memory_order_acquire))
    {
        outError = "Vulkan 업로드 배치의 recording이 느린 경로 진입 중 끝났다";
        m_batchRollbacks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (!large)
    {
        Segment* const active = m_fastRegular.load(std::memory_order_acquire);
        if (nullptr != active && TryReserveAtomic(*active, requests, outSlices, false))
            return true;
    }
    else
    {
        for (const auto& candidate : m_segments)
        {
            if (!candidate->large ||
                candidate->state != RHIUploadSegmentState::Active ||
                candidate->recordingId != recordingId) continue;
            if (TryReserveAtomic(*candidate, requests, outSlices, false)) return true;
        }
    }

    Segment* segment = FindAvailableSegmentLocked(large, minimumPacked);
    if (nullptr != segment)
    {
        m_reuses.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        const uint64_t bytes = large
            ? VulkanUploadAlignUp(minimumPacked, 4ull * 1024 * 1024)
            : m_regularSegmentBytes;
        EnsureBudgetForCreateLocked(bytes);
        segment = CreateSegment(bytes, large, outError);
        if (nullptr == segment)
        {
            m_batchRollbacks.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    segment->state = RHIUploadSegmentState::Active;
    segment->recordingId = recordingId;
    segment->cursor.store(0, std::memory_order_relaxed);
    if (!TryReserveAtomic(*segment, requests, outSlices, false))
    {
        segment->state = RHIUploadSegmentState::Available;
        segment->recordingId = 0;
        segment->lastCollectedEpoch = m_collectEpoch;
        outError = "새 Vulkan 업로드 세그먼트에 배치를 배치하지 못했다";
        m_batchRollbacks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (!large) m_fastRegular.store(segment, std::memory_order_release);
    return true;
}

void VulkanUploadSegmentAllocator::OnSubmitted(uint64_t recordingId,
    RHICompletionPoint completion)
{
    m_fastRegular.store(nullptr, std::memory_order_release);
    std::lock_guard lock(m_mutex);
    for (const auto& segment : m_segments)
    {
        if (segment->state != RHIUploadSegmentState::Active ||
            segment->recordingId != recordingId) continue;
        m_tailWasteBytes.fetch_add(segment->capacity -
            segment->cursor.load(std::memory_order_relaxed), std::memory_order_relaxed);
        segment->completionValue = completion.value;
        segment->state = completion.IsValid()
            ? RHIUploadSegmentState::Pending
            : RHIUploadSegmentState::Quarantined;
    }
    if (recordingId == m_currentRecordingId.load(std::memory_order_relaxed))
        m_currentRecordingId.store(0, std::memory_order_release);
}

void VulkanUploadSegmentAllocator::AbortRecording(uint64_t recordingId)
{
    m_fastRegular.store(nullptr, std::memory_order_release);
    std::lock_guard lock(m_mutex);
    for (const auto& segment : m_segments)
    {
        if (segment->state == RHIUploadSegmentState::Active &&
            segment->recordingId == recordingId)
        {
            segment->state = RHIUploadSegmentState::Available;
            segment->cursor.store(0, std::memory_order_relaxed);
            segment->recordingId = 0;
            segment->lastCollectedEpoch = m_collectEpoch;
        }
    }
    if (recordingId == m_currentRecordingId.load(std::memory_order_relaxed))
    {
        m_recordingBytes.store(0, std::memory_order_release);
        m_currentRecordingId.store(0, std::memory_order_release);
    }
}

RHIUploadStats VulkanUploadSegmentAllocator::GetStats() const
{
    std::lock_guard lock(m_mutex);
    RHIUploadStats stats{};
    stats.allocations = m_allocations.load(std::memory_order_relaxed);
    stats.bytesAllocated = m_bytesAllocated.load(std::memory_order_relaxed);
    stats.peakFrameBytes = m_peakRecordingBytes.load(std::memory_order_relaxed);
    stats.segmentCount = static_cast<uint32_t>(m_segments.size());
    stats.peakRecordingBytes = m_peakRecordingBytes.load(std::memory_order_relaxed);
    stats.slowPathCreates = m_slowPathCreates.load(std::memory_order_relaxed);
    stats.reuses = m_reuses.load(std::memory_order_relaxed);
    stats.fastPathReservations = m_fastPathReservations.load(std::memory_order_relaxed);
    stats.slowPathReservations = m_slowPathReservations.load(std::memory_order_relaxed);
    stats.casRetries = m_casRetries.load(std::memory_order_relaxed);
    stats.workerSegmentCreates = m_workerSegmentCreates.load(std::memory_order_relaxed);
    stats.tailWasteBytes = m_tailWasteBytes.load(std::memory_order_relaxed);
    stats.batchRollbacks = m_batchRollbacks.load(std::memory_order_relaxed);
    stats.oomFailures = m_oomFailures.load(std::memory_order_relaxed);
    stats.reclaimLag = m_reclaimLag.load(std::memory_order_relaxed);
    stats.softBudgetBytes = m_softBudgetBytes.load(std::memory_order_relaxed);
    stats.largeCacheBudgetBytes = m_largeCacheBudgetBytes.load(std::memory_order_relaxed);
    stats.trimmedSegments = m_trimmedSegments.load(std::memory_order_relaxed);
    stats.trimmedBytes = m_trimmedBytes.load(std::memory_order_relaxed);
    stats.budgetPressureEvents = m_budgetPressureEvents.load(std::memory_order_relaxed);
    stats.budgetRetries = m_budgetRetries.load(std::memory_order_relaxed);
    stats.budgetOvercommits = m_budgetOvercommits.load(std::memory_order_relaxed);
    if (m_table)
    {
        stats.registrySlotReuses = m_table->BufferSlotReuseCount();
        stats.registryHighWater = m_table->BufferSlotHighWater();
    }
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
    return m_recordingBytes.load(std::memory_order_acquire);
}

// ────────────────────────────────────────────────────────────── 디스크립터 풀

bool VulkanDescriptorPoolRecycler::Initialize(VkDevice device,
    uint32_t initialVersions,
    std::string& outError)
{
    if (VK_NULL_HANDLE == device || 0 == initialVersions)
    {
        outError = "디스크립터 pool recycler 인자가 잘못됐다";
        return false;
    }

    Shutdown(device);
    m_versions.Reset(initialVersions);
    m_pools.resize(initialVersions, VK_NULL_HANDLE);
    for (uint32_t slot = 0; slot < initialVersions; ++slot)
    {
        if (!EnsurePool(device, slot, outError))
        {
            Shutdown(device);
            return false;
        }
    }
    m_allocations = 0;
    m_allocationFailures = 0;
    m_peakRecordingSets = 0;
    return true;
}

bool VulkanDescriptorPoolRecycler::EnsurePool(VkDevice device, uint32_t slot,
    std::string& outError)
{
    if (VK_NULL_HANDLE == device) return false;
    if (slot >= m_pools.size())
        m_pools.resize(static_cast<size_t>(slot) + 1, VK_NULL_HANDLE);
    if (VK_NULL_HANDLE != m_pools[slot]) return true;

    VkDescriptorPoolCreateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    info.maxSets = kVkMaxSetsPerFrame;
    info.poolSizeCount = static_cast<uint32_t>(std::size(kVkPoolBudget));
    info.pPoolSizes = kVkPoolBudget;
    const VkResult made = vkCreateDescriptorPool(
        device, &info, nullptr, &m_pools[slot]);
    if (VK_SUCCESS != made)
    {
        outError = "디스크립터 pool version 생성 실패 — " + ResultToString(made);
        return false;
    }
    return true;
}

void VulkanDescriptorPoolRecycler::Shutdown(VkDevice device)
{
    if (VK_NULL_HANDLE != device)
    {
        for (VkDescriptorPool pool : m_pools)
            if (VK_NULL_HANDLE != pool) vkDestroyDescriptorPool(device, pool, nullptr);
    }
    m_pools.clear();
    m_versions.Reset();
    m_activeVersion = {};
    m_activePool = VK_NULL_HANDLE;
    m_recordingSets = 0;
}

void VulkanDescriptorPoolRecycler::Collect(RHICompletionPoint completed)
{
    m_versions.Collect(completed);
}

bool VulkanDescriptorPoolRecycler::BeginRecording(VkDevice device,
    uint64_t recordingId, std::string& outError)
{
    const RHIDescriptorVersionAcquire acquired =
        m_versions.BeginRecording(recordingId);
    if (!acquired.IsValid())
    {
        outError = "Vulkan 디스크립터 recording version 획득 실패";
        return false;
    }

    // 같은 recordingId로 복구 진입한 경우 이미 작성한 set을 보존한다.
    // 같은 pool을 여기서 reset하면 아직 기록 중인 command buffer의 set이 무효다.
    if (VK_NULL_HANDLE != m_activePool &&
        m_activeVersion.slot == acquired.handle.slot &&
        m_activeVersion.generation == acquired.handle.generation)
    {
        return true;
    }
    if (!EnsurePool(device, acquired.handle.slot, outError))
    {
        m_versions.AbortRecording(recordingId);
        return false;
    }

    const VkResult reset = vkResetDescriptorPool(
        device, m_pools[acquired.handle.slot], 0);
    if (VK_SUCCESS != reset)
    {
        m_versions.AbortRecording(recordingId);
        outError = "디스크립터 pool version reset 실패 — " + ResultToString(reset);
        return false;
    }

    m_activeVersion = acquired.handle;
    m_activePool = m_pools[acquired.handle.slot];
    m_recordingSets = 0;
    return true;
}

void VulkanDescriptorPoolRecycler::OnSubmitted(uint64_t recordingId,
    RHICompletionPoint completion)
{
    if (!m_versions.OnSubmitted(recordingId, completion)) return;
    m_activeVersion = {};
    m_activePool = VK_NULL_HANDLE;
    m_recordingSets = 0;
}

void VulkanDescriptorPoolRecycler::AbortRecording(uint64_t recordingId)
{
    if (!m_versions.AbortRecording(recordingId)) return;
    m_activeVersion = {};
    m_activePool = VK_NULL_HANDLE;
    m_recordingSets = 0;
}

void VulkanDescriptorPoolRecycler::RecordPeak(uint32_t used)
{
    if (used > m_peakRecordingSets) m_peakRecordingSets = used;
}

VkDescriptorSet VulkanDescriptorPoolRecycler::Allocate(VkDevice device,
    VkDescriptorSetLayout setLayout)
{
    // VkDescriptorPool은 host access가 externally synchronized다. 병렬 command
    // recording worker가 같은 version에서 set을 요청해도 native 호출은 직렬화한다.
    const std::lock_guard lock(m_allocationMutex);
    if (VK_NULL_HANDLE == device || VK_NULL_HANDLE == setLayout ||
        VK_NULL_HANDLE == m_activePool || !m_versions.IsCurrent(m_activeVersion))
    {
        ++m_allocationFailures;
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    info.descriptorPool = m_activePool;
    info.descriptorSetCount = 1;
    info.pSetLayouts = &setLayout;

    VkDescriptorSet set = VK_NULL_HANDLE;
    if (VK_SUCCESS != vkAllocateDescriptorSets(device, &info, &set))
    {
        ++m_allocationFailures;
        return VK_NULL_HANDLE;
    }
    ++m_allocations;
    RecordPeak(++m_recordingSets);
    return set;
}

VulkanDescriptorRecyclerStats VulkanDescriptorPoolRecycler::GetStats() const
{
    VulkanDescriptorRecyclerStats stats{};
    stats.versions = m_versions.GetStats();
    stats.allocations = m_allocations;
    stats.allocationFailures = m_allocationFailures;
    stats.peakRecordingSets = m_peakRecordingSets;
    return stats;
}

#endif
