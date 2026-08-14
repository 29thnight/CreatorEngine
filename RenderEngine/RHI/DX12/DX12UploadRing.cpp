#include "DX12UploadRing.h"

#ifndef DYNAMICCPP_EXPORTS

#include "DX12ResourceTable.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace
{
    std::string UploadSegmentsHrToString(HRESULT hr)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
        return buffer;
    }

    uint64_t UploadSegmentsAlignUp(uint64_t value, uint64_t alignment)
    {
        alignment = (std::max)(alignment, 1ull);
        if (value > (std::numeric_limits<uint64_t>::max)() - (alignment - 1))
            return (std::numeric_limits<uint64_t>::max)();
        return ((value + alignment - 1) / alignment) * alignment;
    }
}

bool DX12UploadSegmentAllocator::Initialize(ID3D12Device* device,
    DX12ResourceTable& table, const RHIUploadSegmentPolicy& policy,
    std::string& outError)
{
    if (nullptr == device || 0 == policy.regularSegmentBytes ||
        0 == policy.largeThreshold)
    {
        outError = "DX12 업로드 세그먼트 인자가 잘못됐다";
        return false;
    }

    m_device = device;
    m_table = &table;
    m_regularSegmentBytes = UploadSegmentsAlignUp(
        policy.regularSegmentBytes, kTexturePlacementAlignment);
    m_largeThreshold = (std::min)(policy.largeThreshold, m_regularSegmentBytes);
    m_standbyRegularSegments = policy.standbyRegularSegments;
    m_trimDelayCollects = policy.trimDelayCollects;
    m_defaultLargeCacheBudgetBytes = policy.largeCacheBudgetBytes;
    m_softBudgetBytes.store(policy.softBudgetBytes, std::memory_order_release);
    m_largeCacheBudgetBytes.store(policy.largeCacheBudgetBytes, std::memory_order_release);
    m_ownerThread = std::this_thread::get_id();

    for (uint32_t i = 0; i < m_standbyRegularSegments; ++i)
    {
        if (nullptr == CreateSegment(m_regularSegmentBytes, false, outError))
        {
            Shutdown();
            return false;
        }
    }
    return true;
}

DX12UploadSegmentAllocator::Segment* DX12UploadSegmentAllocator::CreateSegment(
    uint64_t bytes, bool large, std::string& outError)
{
    bytes = UploadSegmentsAlignUp(bytes, kTexturePlacementAlignment);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto segment = std::make_unique<Segment>();
    HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&segment->buffer));
    if (FAILED(hr))
    {
        outError = "DX12 업로드 세그먼트 생성 실패 " + UploadSegmentsHrToString(hr);
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    void* mapped = nullptr;
    hr = segment->buffer->Map(0, nullptr, &mapped);
    if (FAILED(hr))
    {
        outError = "DX12 업로드 세그먼트 Map 실패 " + UploadSegmentsHrToString(hr);
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    segment->mapped = static_cast<uint8_t*>(mapped);
    segment->capacity = bytes;
    segment->large = large;
    segment->handle = m_table->AddExternalBuffer(segment->buffer.Get());
    if (!segment->handle.IsValid())
    {
        segment->buffer->Unmap(0, nullptr);
        outError = "DX12 업로드 세그먼트를 stable RHI 표에 등록하지 못했다";
        m_oomFailures.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }

    wchar_t name[80]{};
    std::swprintf(name, std::size(name), L"DX12UploadSegment.%s.%zu",
        large ? L"Large" : L"Regular", m_segments.size());
    segment->buffer->SetName(name);

    Segment* result = segment.get();
    m_segments.push_back(std::move(segment));
    m_slowPathCreates.fetch_add(1, std::memory_order_relaxed);
    if (std::this_thread::get_id() != m_ownerThread)
        m_workerSegmentCreates.fetch_add(1, std::memory_order_relaxed);
    return result;
}

void DX12UploadSegmentAllocator::Shutdown()
{
    m_fastRegular.store(nullptr, std::memory_order_release);
    std::lock_guard lock(m_mutex);
    for (const auto& segment : m_segments)
    {
        if (m_table && segment->handle.IsValid()) m_table->Release(segment->handle);
        if (segment->buffer && segment->mapped) segment->buffer->Unmap(0, nullptr);
    }
    m_segments.clear();
    m_device = nullptr;
    m_table = nullptr;
    m_regularSegmentBytes = 0;
    m_largeThreshold = 0;
    m_standbyRegularSegments = 0;
    m_trimDelayCollects = 0;
    m_defaultLargeCacheBudgetBytes = 0;
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

uint64_t DX12UploadSegmentAllocator::RequiredAlignment(
    const RHIUploadRequest& request) const
{
    uint64_t required = 1;
    switch (request.usage)
    {
    case RHIUploadUsage::ConstantBuffer:
        required = kConstantBufferAlignment;
        break;
    case RHIUploadUsage::TextureCopy:
        required = kTexturePlacementAlignment;
        break;
    case RHIUploadUsage::ShaderTable:
        required = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
        break;
    case RHIUploadUsage::VertexData:
    case RHIUploadUsage::IndexData:
    case RHIUploadUsage::BufferCopy:
        required = 4;
        break;
    default:
        break;
    }
    return (std::max)(required, (std::max)(request.minimumAlignment, 1ull));
}

bool DX12UploadSegmentAllocator::TryPack(uint64_t start, uint64_t capacity,
    std::span<const RHIUploadRequest> requests, uint64_t& outEnd) const
{
    uint64_t cursor = start;
    for (const RHIUploadRequest& request : requests)
    {
        if (0 == request.bytes) return false;
        cursor = UploadSegmentsAlignUp(cursor, RequiredAlignment(request));
        if (cursor > capacity || request.bytes > capacity - cursor) return false;
        cursor += request.bytes;
    }
    outEnd = cursor;
    return true;
}

void DX12UploadSegmentAllocator::UpdatePeakRecordingBytes(uint64_t value)
{
    uint64_t peak = m_peakRecordingBytes.load(std::memory_order_relaxed);
    while (peak < value && !m_peakRecordingBytes.compare_exchange_weak(
        peak, value, std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

bool DX12UploadSegmentAllocator::TryReserveAtomic(Segment& segment,
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
        cursor = UploadSegmentsAlignUp(cursor, RequiredAlignment(requests[i]));
        outSlices[i].buffer = segment.handle;
        outSlices[i].offset = cursor;
        outSlices[i].size = requests[i].bytes;
        outSlices[i].cpuAddress = segment.mapped + cursor;
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

DX12UploadSegmentAllocator::Segment*
DX12UploadSegmentAllocator::FindAvailableSegmentLocked(bool large,
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

uint64_t DX12UploadSegmentAllocator::SegmentBytesLocked() const
{
    uint64_t bytes = 0;
    for (const auto& segment : m_segments) bytes += segment->capacity;
    return bytes;
}

void DX12UploadSegmentAllocator::ReleaseSegmentLocked(Segment& segment)
{
    if (m_table && segment.handle.IsValid()) m_table->Release(segment.handle);
    segment.handle = {};
    if (segment.buffer && segment.mapped) segment.buffer->Unmap(0, nullptr);
    segment.mapped = nullptr;
    segment.buffer.Reset();
}

void DX12UploadSegmentAllocator::TrimAvailableLocked(bool memoryPressure,
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

void DX12UploadSegmentAllocator::EnsureBudgetForCreateLocked(uint64_t bytesNeeded)
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

void DX12UploadSegmentAllocator::UpdateBudget(uint64_t softBudgetBytes,
    bool memoryPressure)
{
    if (m_budgetOverrideForTesting.load(std::memory_order_acquire)) return;
    m_softBudgetBytes.store(softBudgetBytes, std::memory_order_release);
    m_memoryPressure.store(memoryPressure, std::memory_order_release);
}

void DX12UploadSegmentAllocator::SetBudgetForTesting(uint64_t softBudgetBytes,
    uint64_t largeCacheBudgetBytes, bool memoryPressure)
{
    m_budgetOverrideForTesting.store(true, std::memory_order_release);
    m_softBudgetBytes.store(softBudgetBytes, std::memory_order_release);
    m_largeCacheBudgetBytes.store(largeCacheBudgetBytes, std::memory_order_release);
    m_memoryPressure.store(memoryPressure, std::memory_order_release);
}

void DX12UploadSegmentAllocator::ClearBudgetOverrideForTesting()
{
    m_largeCacheBudgetBytes.store(m_defaultLargeCacheBudgetBytes,
        std::memory_order_release);
    m_memoryPressure.store(false, std::memory_order_release);
    m_budgetOverrideForTesting.store(false, std::memory_order_release);
}

void DX12UploadSegmentAllocator::Collect(uint64_t completedValue)
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

void DX12UploadSegmentAllocator::BeginRecording(uint64_t recordingId)
{
    std::lock_guard lock(m_mutex);
    m_fastRegular.store(nullptr, std::memory_order_release);
    m_currentRecordingId.store(recordingId, std::memory_order_release);
    m_recordingBytes.store(0, std::memory_order_release);
}

bool DX12UploadSegmentAllocator::ReserveBatch(uint64_t recordingId,
    std::span<const RHIUploadRequest> requests,
    std::span<RHIBufferSlice> outSlices, std::string& outError)
{
    if (requests.empty() || requests.size() != outSlices.size() || 0 == recordingId)
    {
        outError = "DX12 업로드 배치의 요청/출력 또는 recording id가 잘못됐다";
        return false;
    }
    if (recordingId != m_currentRecordingId.load(std::memory_order_acquire))
    {
        outError = "DX12 업로드 배치가 현재 recording과 일치하지 않는다";
        return false;
    }

    uint64_t minimumPacked = 0;
    if (!TryPack(0, (std::numeric_limits<uint64_t>::max)(), requests, minimumPacked))
    {
        outError = "DX12 업로드 배치가 0바이트이거나 크기 계산이 넘쳤다";
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
        outError = "DX12 업로드 배치의 recording이 느린 경로 진입 중 끝났다";
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
            ? UploadSegmentsAlignUp(minimumPacked, 4ull * 1024 * 1024)
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
        outError = "새 DX12 업로드 세그먼트에 배치를 배치하지 못했다";
        m_batchRollbacks.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    if (!large) m_fastRegular.store(segment, std::memory_order_release);
    return true;
}

void DX12UploadSegmentAllocator::OnSubmitted(uint64_t recordingId,
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

void DX12UploadSegmentAllocator::AbortRecording(uint64_t recordingId)
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

RHIUploadStats DX12UploadSegmentAllocator::GetStats() const
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
        default:
            break;
        }
    }
    return stats;
}

DX12UploadSegmentAllocator::Allocation DX12UploadSegmentAllocator::Allocate(
    uint64_t bytes, uint64_t alignment)
{
    const RHIUploadRequest request{ bytes, RHIUploadUsage::Raw, alignment };
    RHIBufferSlice slice{};
    std::string error;
    if (!ReserveBatch(m_currentRecordingId.load(std::memory_order_acquire),
        std::span<const RHIUploadRequest>(&request, 1),
        std::span<RHIBufferSlice>(&slice, 1), error)) return {};

    std::lock_guard lock(m_mutex);
    for (size_t i = 0; i < m_segments.size(); ++i)
    {
        const auto& segment = m_segments[i];
        if (segment->handle.id != slice.buffer.id) continue;
        Allocation result{};
        result.cpuAddress = slice.cpuAddress;
        result.gpuAddress = segment->buffer->GetGPUVirtualAddress() + slice.offset;
        result.resource = segment->buffer.Get();
        result.offset = slice.offset;
        result.size = slice.size;
        result.segment = static_cast<uint32_t>(i);
        return result;
    }
    return {};
}

uint64_t DX12UploadSegmentAllocator::GetRecordingUsedBytes() const
{
    return m_recordingBytes.load(std::memory_order_acquire);
}

#endif
