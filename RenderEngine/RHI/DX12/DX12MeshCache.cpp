#ifndef DYNAMICCPP_EXPORTS
#include "DX12MeshCache.h"
#include "DX12DeviceResources.h"
#include "DX12TextureCache.h"   // kRetireAfterFrames — 임계값을 하나로 둔다
#include "../../Mesh.h"

#include <sstream>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string MeshCacheHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }
}

bool DX12MeshCache::Initialize(DX12DeviceResources* resources, std::string& outError)
{
    if (nullptr == resources || !resources->IsInitialized())
    {
        outError = "메시 캐시: 디바이스가 준비되지 않았다";
        return false;
    }

    m_resources = resources;
    m_entries.clear();
    m_stats = Stats{};
    return true;
}

void DX12MeshCache::Shutdown()
{
    // 표에서 먼저 놓는다 — m_resources 를 null 로 만들기 전이어야 한다(A-4,
    // 텍스처 캐시 Shutdown 과 같은 순서).
    if (nullptr != m_resources)
    {
        for (const auto& entry : m_entries)
        {
            m_resources->ReleaseBuffer(entry.second.entry.vertices.buffer);
            m_resources->ReleaseBuffer(entry.second.entry.indices.buffer);
        }
    }

    m_entries.clear();
    m_graveyard.clear();

    // 상주량도 함께 0으로. 안 비우면 "다 놓았는데 수치는 남아 있다"가 되어
    // ③의 판정(씬 왕복 후 기준선 복귀)이 성립하지 않는다.
    m_stats.residentCount = 0;
    m_stats.residentBytes = 0;
    m_stats.graveyardCount = 0;
    m_stats.graveyardBytes = 0;

    m_frameIndex = 0;
    m_resources = nullptr;
}

uint64_t DX12MeshCache::RetireUnused(uint64_t fenceValue)
{
    uint64_t retired = 0;

    auto it = m_entries.begin();
    while (it != m_entries.end())
    {
        if (m_frameIndex < it->second.lastUsedFrame + DX12TextureCache::kRetireAfterFrames)
        {
            ++it;
            continue;
        }

        const uint64_t bytes = it->second.bytes;

        // 표에서 먼저 놓는다 (A-4). 안 놓으면 은퇴한 메시의 핸들이 살아 있어
        // 죽어 가는 리소스를 가리킨다 — 텍스처 캐시가 같은 자리에서 하는 일이다.
        if (nullptr != m_resources)
        {
            m_resources->ReleaseBuffer(it->second.entry.vertices.buffer);
            m_resources->ReleaseBuffer(it->second.entry.indices.buffer);
        }

        m_graveyard.push_back(Grave{
            std::move(it->second.vertexBuffer),
            std::move(it->second.indexBuffer),
            bytes, fenceValue });
        ++m_stats.graveyardCount;
        m_stats.graveyardBytes += bytes;

        --m_stats.residentCount;
        m_stats.residentBytes -= bytes;
        ++m_stats.retired;
        m_stats.retiredBytes += bytes;
        retired += bytes;

        it = m_entries.erase(it);
    }

    return retired;
}

uint64_t DX12MeshCache::SweepGraveyard(uint64_t completedFenceValue)
{
    uint64_t freed = 0;

    auto it = m_graveyard.begin();
    while (it != m_graveyard.end())
    {
        if (completedFenceValue < it->fenceValue)
        {
            ++it;
            continue;
        }

        freed += it->bytes;
        --m_stats.graveyardCount;
        m_stats.graveyardBytes -= it->bytes;
        it = m_graveyard.erase(it);
    }

    return freed;
}

bool DX12MeshCache::UploadBuffer(const void* data, uint64_t bytes,
    D3D12_RESOURCE_STATES finalState, ComPtr<ID3D12Resource>& outBuffer,
    const wchar_t* name, std::string& outError)
{
    auto* device = m_resources->GetDevice();

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // 버퍼는 초기 상태 지정이 무시되고 COMMON으로 만들어진다(3-3에서 검증
    // 레이어가 잡아 준 규칙). COPY_DEST로는 첫 사용 시 암묵 승격된다.
    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outBuffer));
    if (FAILED(hr))
    {
        outError = "메시 버퍼 생성 실패 " + MeshCacheHrToString(hr);
        return false;
    }
    outBuffer->SetName(name);

    // 스테이징은 업로드 링에서 자른다. 정점 데이터는 정렬 요구가 없지만
    // 상수 버퍼 정렬(256)로 맞춰 두면 링 안에서 다음 할당도 정렬된 채 시작한다.
    const auto staging = m_resources->GetUploadRing().Allocate(
        bytes, DX12UploadRing::kConstantBufferAlignment);
    if (!staging.IsValid())
    {
        outError = "메시 업로드 링 할당 실패 (" + std::to_string(bytes) + "바이트) — "
            "구간이 모자라면 DX12DeviceResources의 kUploadBytesPerFrame을 늘려야 한다";
        return false;
    }

    memcpy(staging.cpuAddress, data, static_cast<size_t>(bytes));

    auto* commandList = m_resources->GetCommandList();
    commandList->CopyBufferRegion(outBuffer.Get(), 0, staging.resource, staging.offset, bytes);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = finalState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    m_stats.bytesUploaded += bytes;
    return true;
}

DX12MeshCache::Entry DX12MeshCache::GetOrUpload(Mesh* mesh, std::string& outError)
{
    Entry empty{};
    if (nullptr == m_resources || nullptr == mesh)
    {
        outError = "메시 캐시: 인자가 잘못됐다";
        return empty;
    }

    // 키는 주소가 아니라 자산 신원이다(멤버 선언부 주석 참고).
    const HashedGuid assetId = mesh->m_hashingMesh;

    const auto found = m_entries.find(assetId);
    if (found != m_entries.end())
    {
        ++m_stats.hits;
        found->second.lastUsedFrame = m_frameIndex;   // 쓰였다고 찍는다(③)
        return found->second.entry;
    }

    const auto& vertices = mesh->GetVertices();
    const auto& indices = mesh->GetIndices();
    if (vertices.empty() || indices.empty())
    {
        // 빈 메시는 실패가 아니라 '그릴 것이 없음'이다. 그리기 목록에서 빠진다.
        return empty;
    }

    Buffers buffers{};
    const uint64_t vertexBytes = static_cast<uint64_t>(vertices.size()) * sizeof(Vertex);
    const uint64_t indexBytes = static_cast<uint64_t>(indices.size()) * sizeof(uint32);

    if (!UploadBuffer(vertices.data(), vertexBytes,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, buffers.vertexBuffer,
        L"MeshVertices", outError))
    {
        ++m_stats.failures;
        return empty;
    }

    if (!UploadBuffer(indices.data(), indexBytes,
        D3D12_RESOURCE_STATE_INDEX_BUFFER, buffers.indexBuffer, L"MeshIndices", outError))
    {
        ++m_stats.failures;
        return empty;
    }

    // 표에 빌려주기로 올린다 (A-4). 소유는 위의 ComPtr 이 계속 들고, 표는
    // 핸들로 가리키기만 한다 — 텍스처 캐시가 V2-b 에서 한 것과 같은 형태이고,
    // 은퇴할 때 표에서도 놓는다(RetireUnused).
    //
    // ★ 슬라이스가 버퍼 전체를 가리킨다. 링 조각이 아니므로 CPU 주소가 없고,
    //   `RHIBufferSlice::IsValid` 가 그것을 유효로 보는 이유가 이 자리다.
    buffers.entry.vertices = RHIBufferSlice::Whole(
        m_resources->RegisterExternalBuffer(buffers.vertexBuffer.Get()));
    buffers.entry.vertices.size = vertexBytes;
    buffers.entry.vertexStride = sizeof(Vertex);

    buffers.entry.indices = RHIBufferSlice::Whole(
        m_resources->RegisterExternalBuffer(buffers.indexBuffer.Get()));
    buffers.entry.indices.size = indexBytes;
    buffers.entry.indexFormat = RHIFormat::R32Uint;

    buffers.entry.indexCount = static_cast<uint32_t>(indices.size());
    buffers.bytes = vertexBytes + indexBytes;
    buffers.lastUsedFrame = m_frameIndex;

    ++m_stats.uploads;
    ++m_stats.residentCount;
    m_stats.residentBytes += buffers.bytes;

    const auto inserted = m_entries.emplace(assetId, std::move(buffers));
    return inserted.first->second.entry;
}

#endif
