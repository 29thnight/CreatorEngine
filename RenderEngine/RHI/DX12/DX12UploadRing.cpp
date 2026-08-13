#include "DX12UploadRing.h"

#ifndef DYNAMICCPP_EXPORTS

#include <algorithm>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다
    // (HrToString이 이미 한 번 충돌했다).
    std::string UploadRingHrToString(HRESULT hr)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(hr));
        return buffer;
    }

    uint64_t UploadRingAlignUp(uint64_t value, uint64_t alignment)
    {
        if (0 == alignment) return value;
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

bool DX12UploadRing::Initialize(ID3D12Device* device, uint64_t segmentBytes, uint32_t frameCount,
    std::string& outError)
{
    if (nullptr == device || 0 == segmentBytes || 0 == frameCount)
    {
        outError = "업로드 링 인자가 잘못됐다";
        return false;
    }

    // 세그먼트 크기도 정렬해 둔다. 경계가 어긋나면 각 세그먼트의 첫 할당부터
    // 정렬이 깨져서, 상수 버퍼 뷰 생성이 프레임에 따라 실패한다.
    m_device = device;
    m_segmentBytes = UploadRingAlignUp(segmentBytes, kTexturePlacementAlignment);
    m_frameCount = frameCount;
    m_frameSegments.assign(frameCount, {});

    // 프레임마다 하나씩 깔고 시작한다 — 예전의 정적 분할과 같은 출발점이라
    // 기존 거동이 하한이 된다. 모자라면 BeginFrame이 늘린다.
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        if (!CreateSegment(m_segmentBytes, outError)) return false;
        m_frameSegments[i].push_back(static_cast<uint32_t>(m_segments.size() - 1));
    }

    m_frameIndex = 0;
    m_cursorState.store(0, std::memory_order_relaxed);
    m_frameBytesUsed.store(0, std::memory_order_relaxed);
    m_demandTotal.store(0, std::memory_order_relaxed);
    m_demandSingle.store(0, std::memory_order_relaxed);
    m_neededSingle = 0;
    m_growths = 0;

    m_stats.allocations.store(0, std::memory_order_relaxed);
    m_stats.bytesAllocated.store(0, std::memory_order_relaxed);
    m_stats.overflows.store(0, std::memory_order_relaxed);
    m_stats.peakFrameBytes.store(0, std::memory_order_relaxed);
    return true;
}

bool DX12UploadRing::CreateSegment(uint64_t bytes, std::string& outError)
{
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

    Segment segment{};
    HRESULT hr = m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&segment.buffer));
    if (FAILED(hr))
    {
        outError = "업로드 세그먼트 생성 실패 " + UploadRingHrToString(hr);
        return false;
    }

    // 세그먼트는 수명 내내 Map된 상태로 둔다. 조각마다 Map/Unmap 하는 것이
    // 오히려 비싸고, 업로드 힙은 계속 매핑해 두는 것이 정상 사용법이다.
    void* mapped = nullptr;
    hr = segment.buffer->Map(0, nullptr, &mapped);
    if (FAILED(hr))
    {
        outError = "업로드 세그먼트 Map 실패 " + UploadRingHrToString(hr);
        return false;
    }

    segment.mapped = static_cast<uint8_t*>(mapped);
    segment.gpuBase = segment.buffer->GetGPUVirtualAddress();
    segment.bytes = bytes;

    wchar_t name[64]{};
    std::swprintf(name, 64, L"DX12UploadRing.Segment%zu", m_segments.size());
    segment.buffer->SetName(name);

    m_segments.push_back(std::move(segment));
    return true;
}

void DX12UploadRing::Shutdown()
{
    for (Segment& segment : m_segments)
    {
        if (segment.buffer && segment.mapped) segment.buffer->Unmap(0, nullptr);
    }
    m_segments.clear();
    m_frameSegments.clear();
    m_device = nullptr;
    m_segmentBytes = 0;
    m_frameCount = 0;
    m_frameIndex = 0;
    m_cursorState.store(0, std::memory_order_relaxed);
    m_frameBytesUsed.store(0, std::memory_order_relaxed);
}

void DX12UploadRing::BeginFrame(uint32_t frameIndex)
{
    if (0 == m_frameCount || m_segments.empty()) return;

    // 직전 프레임이 얼마나 썼고 얼마나 모자랐는지를 남긴다 — 크기를 정하는
    // 유일한 근거다. 추측하지 않고 실제 수요로만 늘린다.
    const uint64_t used = m_frameBytesUsed.load(std::memory_order_relaxed);
    RecordPeak(used);

    const uint64_t rejections = m_demandTotal.exchange(0, std::memory_order_relaxed);
    const uint64_t biggest = m_demandSingle.exchange(0, std::memory_order_relaxed);
    m_neededSingle = (std::max)(m_neededSingle, biggest);

    m_frameIndex = frameIndex % m_frameCount;
    std::vector<uint32_t>& owned = m_frameSegments[m_frameIndex];

    // ① 단일 요청이 세그먼트 하나보다 크면 그 크기짜리를 따로 만든다
    //    (.NET의 LOH와 같은 자리). 여러 세그먼트에 걸쳐 자를 수는 없다 —
    //    복사 원본은 연속이어야 한다.
    if (m_neededSingle > m_segmentBytes)
    {
        bool fits = false;
        for (const uint32_t id : owned)
        {
            if (m_segments[id].bytes >= m_neededSingle) { fits = true; break; }
        }
        if (!fits)
        {
            std::string error;
            if (CreateSegment(UploadRingAlignUp(m_neededSingle, kTexturePlacementAlignment), error))
            {
                // 큰 것을 앞에 둔다. 뒤에 있으면 앞 세그먼트를 먼저 소진해야
                // 도달하는데, 그 과정에서 버려지는 잔여가 그대로 낭비다.
                owned.insert(owned.begin(), static_cast<uint32_t>(m_segments.size() - 1));
                ++m_growths;
            }
        }
    }

    // ② 거절이 있었으면 세그먼트를 **하나만** 붙인다.
    //
    // ★ 처음에는 거절된 바이트를 합산해 그만큼 한 번에 늘렸다. 그러면
    //   과할당된다 — 패스가 여럿이라(GBuffer·WireFrame·Forward) 같은 메시가
    //   한 프레임에 여러 번 거절되고, 그 바이트가 중복 계상된다. 실측에서
    //   21MB짜리 씬에 240MB(세그먼트 15개)를 잡았다.
    //
    //   한 프레임에 하나씩만 늘리면 몇 프레임 안에 실제 고수위로 수렴하고,
    //   과할당은 최대 세그먼트 하나로 묶인다. 로드 중 몇 프레임이 더
    //   거절되는 것이 유일한 대가다.
    if (0 != rejections && owned.size() < kMaxSegmentsPerFrame)
    {
        std::string error;
        if (CreateSegment(m_segmentBytes, error))
        {
            owned.push_back(static_cast<uint32_t>(m_segments.size() - 1));
            ++m_growths;
        }
    }

    m_cursorState.store(0, std::memory_order_relaxed);
    m_frameBytesUsed.store(0, std::memory_order_relaxed);
}

void DX12UploadRing::RecordPeak(uint64_t used)
{
    uint64_t peak = m_stats.peakFrameBytes.load(std::memory_order_relaxed);
    while (used > peak &&
        !m_stats.peakFrameBytes.compare_exchange_weak(peak, used, std::memory_order_relaxed))
    {
    }
}

void DX12UploadRing::RecordDemand(uint64_t size)
{
    // 남의 세그먼트를 침범하지 않고 수요로만 남긴다 — 다음 BeginFrame이
    // 그만큼 늘린다. 이 프레임은 거절된 채로 지나간다.
    m_stats.overflows.fetch_add(1, std::memory_order_relaxed);
    m_demandTotal.fetch_add(size, std::memory_order_relaxed);

    uint64_t biggest = m_demandSingle.load(std::memory_order_relaxed);
    while (size > biggest &&
        !m_demandSingle.compare_exchange_weak(biggest, size, std::memory_order_relaxed))
    {
    }
}

DX12UploadRing::Allocation DX12UploadRing::Allocate(uint64_t size, uint64_t alignment)
{
    Allocation allocation{};
    if (m_segments.empty() || 0 == size) return allocation;

    const std::vector<uint32_t>& owned = m_frameSegments[m_frameIndex];
    if (owned.empty()) return allocation;

    for (;;)
    {
        const uint64_t state = m_cursorState.load(std::memory_order_relaxed);
        const uint32_t slot = static_cast<uint32_t>(state >> kCursorBits);
        const uint64_t cursor = state & kCursorMask;

        if (slot >= owned.size())
        {
            RecordDemand(size);
            return allocation;
        }

        const Segment& segment = m_segments[owned[slot]];
        const uint64_t aligned = UploadRingAlignUp(cursor, alignment);
        const uint64_t next = aligned + size;

        if (next > segment.bytes)
        {
            // 이 세그먼트로는 안 된다. 다음 세그먼트가 있고 **거기에는 들어갈 때만**
            // 넘긴다.
            //
            // ★ 조건 없이 넘기면 안 된다. 어느 세그먼트에도 안 들어갈 만큼 큰
            //   요청 하나가 남은 세그먼트를 전부 건너뛰게 만들고, 그 뒤의 평범한
            //   요청들까지 같이 죽는다(자가 검증 [4/5]가 이걸로 걸렸다 —
            //   거절 자체는 맞았는데 그 뒤 256바이트 요청이 실패했다).
            const bool fitsNext = (slot + 1 < owned.size()) &&
                (size <= m_segments[owned[slot + 1]].bytes);
            if (fitsNext)
            {
                // 슬롯과 커서를 한 CAS로 함께 옮겨야 다른 스레드가 옛 슬롯에
                // 새 커서를 적용하는 일이 없다. 실패하면(누가 먼저 넘겼다)
                // 그냥 다시 읽는다.
                uint64_t expected = state;
                m_cursorState.compare_exchange_weak(expected, PackState(slot + 1, 0),
                    std::memory_order_relaxed);
                continue;
            }

            // 어디에도 안 들어간다. 커서는 그대로 두고 수요만 남긴다 —
            // 이 프레임의 나머지 요청은 계속 정상으로 받아야 한다.
            RecordDemand(size);
            return allocation;
        }

        uint64_t expected = state;
        if (!m_cursorState.compare_exchange_weak(expected, PackState(slot, next),
            std::memory_order_relaxed))
        {
            continue;
        }

        allocation.cpuAddress = segment.mapped + aligned;
        allocation.gpuAddress = segment.gpuBase + aligned;
        allocation.resource = segment.buffer.Get();
        allocation.offset = aligned;
        allocation.size = size;
        allocation.segment = owned[slot];

        m_stats.allocations.fetch_add(1, std::memory_order_relaxed);
        m_stats.bytesAllocated.fetch_add(size, std::memory_order_relaxed);
        m_frameBytesUsed.fetch_add(size, std::memory_order_relaxed);
        return allocation;
    }
}

DX12UploadRing::Stats DX12UploadRing::GetStats() const
{
    Stats stats{};
    stats.allocations = m_stats.allocations.load(std::memory_order_relaxed);
    stats.bytesAllocated = m_stats.bytesAllocated.load(std::memory_order_relaxed);
    stats.overflows = m_stats.overflows.load(std::memory_order_relaxed);
    stats.peakFrameBytes = m_stats.peakFrameBytes.load(std::memory_order_relaxed);
    stats.segmentCount = static_cast<uint32_t>(m_segments.size());
    for (const Segment& segment : m_segments) stats.segmentBytes += segment.bytes;
    stats.growths = m_growths;
    return stats;
}

#endif
