#include "DX12UploadRing.h"

#ifndef DYNAMICCPP_EXPORTS

#include "DX12ResourceTable.h"

#include <algorithm>
#include <cstdio>

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
        return ((value + alignment - 1) / alignment) * alignment;
    }
}

bool DX12UploadSegmentAllocator::Initialize(ID3D12Device* device,
    DX12ResourceTable& table, uint64_t regularSegmentBytes, uint64_t largeThreshold,
    uint32_t standbyRegularCount, std::string& outError)
{
    if (nullptr == device || 0 == regularSegmentBytes || 0 == largeThreshold)
    {
        outError = "DX12 업로드 세그먼트 인자가 잘못됐다";
        return false;
    }

    m_device = device;
    m_table = &table;
    m_regularSegmentBytes = UploadSegmentsAlignUp(
        regularSegmentBytes, kTexturePlacementAlignment);
    m_largeThreshold = (std::min)(largeThreshold, m_regularSegmentBytes);
    m_creationThread = std::this_thread::get_id();

    for (uint32_t i = 0; i < standbyRegularCount; ++i)
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
        ++m_oomFailures;
        return nullptr;
    }

    void* mapped = nullptr;
    hr = segment->buffer->Map(0, nullptr, &mapped);
    if (FAILED(hr))
    {
        outError = "DX12 업로드 세그먼트 Map 실패 " + UploadSegmentsHrToString(hr);
        ++m_oomFailures;
        return nullptr;
    }

    segment->mapped = static_cast<uint8_t*>(mapped);
    segment->capacity = bytes;
    segment->large = large;
    segment->handle = m_table->AddExternalBuffer(segment->buffer.Get());
    if (!segment->handle.IsValid())
    {
        segment->buffer->Unmap(0, nullptr);
        outError = "DX12 업로드 세그먼트를 RHI 표에 등록하지 못했다";
        ++m_oomFailures;
        return nullptr;
    }

    wchar_t name[80]{};
    std::swprintf(name, std::size(name), L"DX12UploadSegment.%s.%zu",
        large ? L"Large" : L"Regular", m_segments.size());
    segment->buffer->SetName(name);

    Segment* result = segment.get();
    m_segments.push_back(std::move(segment));
    ++m_slowPathCreates;
    return result;
}

void DX12UploadSegmentAllocator::Shutdown()
{
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

bool DX12UploadSegmentAllocator::TryPack(const Segment& segment,
    std::span<const RHIUploadRequest> requests, std::vector<uint64_t>& offsets,
    uint64_t& outEnd) const
{
    offsets.resize(requests.size());
    uint64_t cursor = segment.cursor;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        if (0 == requests[i].bytes) return false;
        cursor = UploadSegmentsAlignUp(cursor, RequiredAlignment(requests[i]));
        if (cursor > segment.capacity || requests[i].bytes > segment.capacity - cursor)
            return false;
        offsets[i] = cursor;
        cursor += requests[i].bytes;
    }
    outEnd = cursor;
    return true;
}

DX12UploadSegmentAllocator::Segment* DX12UploadSegmentAllocator::FindSegment(
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
        {
            return candidate.get();
        }
        if (candidate->state == RHIUploadSegmentState::Available &&
            TryPack(*candidate, requests, offsets, outEnd) &&
            (nullptr == available || candidate->capacity < available->capacity))
        {
            available = candidate.get();
        }
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

void DX12UploadSegmentAllocator::Collect(uint64_t completedValue)
{
    std::lock_guard lock(m_mutex);
    ++m_collectEpoch;
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
            segment->lastCollectedEpoch = m_collectEpoch;
        }
    }
}

void DX12UploadSegmentAllocator::BeginRecording(uint64_t recordingId)
{
    std::lock_guard lock(m_mutex);
    m_currentRecordingId = recordingId;
    m_recordingBytes = 0;
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

    std::lock_guard lock(m_mutex);
    if (recordingId != m_currentRecordingId)
    {
        outError = "DX12 업로드 배치가 현재 recording과 일치하지 않는다";
        return false;
    }

    uint64_t minimumPacked = 0;
    for (const auto& request : requests)
    {
        if (0 == request.bytes)
        {
            outError = "0바이트 업로드 요청은 예약할 수 없다";
            ++m_batchRollbacks;
            return false;
        }
        minimumPacked = UploadSegmentsAlignUp(minimumPacked, RequiredAlignment(request));
        minimumPacked += request.bytes;
    }

    const bool large = minimumPacked > m_largeThreshold;
    std::vector<uint64_t> offsets;
    uint64_t end = 0;
    Segment* segment = FindSegment(large, recordingId, requests, offsets, end);
    if (nullptr == segment)
    {
        // 리소스 표는 아직 append-only lock-free registry가 아니다. 그 전까지
        // worker가 native segment를 만들게 두면 resolve와 vector 변경이 경합한다.
        if (std::this_thread::get_id() != m_creationThread)
        {
            outError = "DX12 worker 기록 중 업로드 세그먼트 증가가 필요하다";
            ++m_batchRollbacks;
            ++m_oomFailures;
            return false;
        }
        const uint64_t bytes = large
            ? UploadSegmentsAlignUp(minimumPacked, 4ull * 1024 * 1024)
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
            outError = "새 DX12 업로드 세그먼트에 배치를 배치하지 못했다";
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
        slices[i].cpuAddress = segment->mapped + offsets[i];
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

void DX12UploadSegmentAllocator::OnSubmitted(uint64_t recordingId,
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

void DX12UploadSegmentAllocator::AbortRecording(uint64_t recordingId)
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

RHIUploadStats DX12UploadSegmentAllocator::GetStats() const
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
    if (!ReserveBatch(m_currentRecordingId,
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
    std::lock_guard lock(m_mutex);
    return m_recordingBytes;
}

#endif
