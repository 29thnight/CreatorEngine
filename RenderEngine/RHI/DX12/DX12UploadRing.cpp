#include "DX12UploadRing.h"

#ifndef DYNAMICCPP_EXPORTS

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

    uint64_t AlignUp(uint64_t value, uint64_t alignment)
    {
        if (0 == alignment) return value;
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

bool DX12UploadRing::Initialize(ID3D12Device* device, uint64_t bytesPerFrame, uint32_t frameCount,
    std::string& outError)
{
    if (nullptr == device || 0 == bytesPerFrame || 0 == frameCount)
    {
        outError = "업로드 링 인자가 잘못됐다";
        return false;
    }

    // 구간 크기도 정렬해 둔다. 구간 경계가 어긋나면 각 프레임의 첫 할당부터
    // 정렬이 깨져서, 상수 버퍼 뷰 생성이 프레임에 따라 실패한다.
    m_bytesPerFrame = AlignUp(bytesPerFrame, kTexturePlacementAlignment);
    m_frameCount = frameCount;

    const uint64_t totalBytes = m_bytesPerFrame * m_frameCount;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = totalBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_buffer));
    if (FAILED(hr))
    {
        outError = "업로드 링 버퍼 생성 실패 " + UploadRingHrToString(hr);
        return false;
    }

    // 링은 수명 내내 Map된 상태로 둔다. 조각마다 Map/Unmap 하는 것이 오히려
    // 비싸고, 업로드 힙은 계속 매핑해 두는 것이 정상 사용법이다.
    void* mapped = nullptr;
    hr = m_buffer->Map(0, nullptr, &mapped);
    if (FAILED(hr))
    {
        outError = "업로드 링 Map 실패 " + UploadRingHrToString(hr);
        m_buffer.Reset();
        return false;
    }

    m_mapped = static_cast<uint8_t*>(mapped);
    m_gpuBase = m_buffer->GetGPUVirtualAddress();
    m_frameIndex = 0;
    m_cursor = 0;
    m_stats = Stats{};

    m_buffer->SetName(L"DX12UploadRing");
    return true;
}

void DX12UploadRing::Shutdown()
{
    if (m_buffer && m_mapped)
    {
        m_buffer->Unmap(0, nullptr);
    }
    m_mapped = nullptr;
    m_gpuBase = 0;
    m_buffer.Reset();
    m_bytesPerFrame = 0;
    m_frameCount = 0;
    m_frameIndex = 0;
    m_cursor = 0;
}

void DX12UploadRing::BeginFrame(uint32_t frameIndex)
{
    if (0 == m_frameCount) return;

    // 직전 프레임이 얼마나 썼는지를 남긴다 — 구간 크기를 정하는 유일한 근거다.
    if (m_cursor > m_stats.peakFrameBytes)
    {
        m_stats.peakFrameBytes = m_cursor;
    }

    m_frameIndex = frameIndex % m_frameCount;
    m_cursor = 0;
}

DX12UploadRing::Allocation DX12UploadRing::Allocate(uint64_t size, uint64_t alignment)
{
    Allocation allocation{};
    if (nullptr == m_mapped || 0 == size) return allocation;

    const uint64_t alignedCursor = AlignUp(m_cursor, alignment);
    if (alignedCursor + size > m_bytesPerFrame)
    {
        // 다른 프레임 구간을 침범하지 않는다. 침범하면 GPU가 아직 읽고 있는
        // 데이터를 덮어쓰게 되고, 증상은 '다음 프레임에 가끔 이상한 것이 보인다'로
        // 나타나 추적이 매우 어렵다. 거절하고 세어 둔다.
        ++m_stats.overflows;
        return allocation;
    }

    const uint64_t absoluteOffset = static_cast<uint64_t>(m_frameIndex) * m_bytesPerFrame + alignedCursor;

    allocation.cpuAddress = m_mapped + absoluteOffset;
    allocation.gpuAddress = m_gpuBase + absoluteOffset;
    allocation.resource = m_buffer.Get();
    allocation.offset = absoluteOffset;
    allocation.size = size;

    m_cursor = alignedCursor + size;

    ++m_stats.allocations;
    m_stats.bytesAllocated += size;
    if (m_cursor > m_stats.peakFrameBytes)
    {
        m_stats.peakFrameBytes = m_cursor;
    }

    return allocation;
}

#endif
