#include "DX12MeshCache.h"
#include "DX12DeviceResources.h"
#include "../../Mesh.h"

#include <array>

bool DX12MeshCache::Initialize(DX12DeviceResources* resources, std::string& outError)
{
    if (nullptr == resources || !resources->IsInitialized())
    {
        outError = "메시 캐시: 디바이스가 준비되지 않았다";
        return false;
    }

    m_resources = resources;
    if (!m_persistentHeap.Initialize(resources->GetDevice(),
        resources->GetAdapter(),
        &resources->GetPersistentMemoryBudgetCoordinator(), outError))
    {
        m_resources = nullptr;
        return false;
    }
    m_resources->RegisterUploadTransactionListener(this);
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
        m_resources->UnregisterUploadTransactionListener(this);
        for (const auto& entry : m_entries)
        {
            m_resources->ReleaseBuffer(entry.second.entry.vertices.buffer);
            m_resources->ReleaseBuffer(entry.second.entry.indices.buffer);
        }
        for (auto& entry : m_entries)
        {
            m_persistentHeap.Release(entry.second.vertexBuffer);
            m_persistentHeap.Release(entry.second.indexBuffer);
        }
    }

    m_entries.clear();
    m_retireQueue.Drain([&](RetiredBuffers& retired)
        {
            m_persistentHeap.Release(retired.vertexBuffer);
            m_persistentHeap.Release(retired.indexBuffer);
        });
    m_persistentHeap.Shutdown();

    // 상주량도 함께 0으로. 안 비우면 "다 놓았는데 수치는 남아 있다"가 되어
    // ③의 판정(씬 왕복 후 기준선 복귀)이 성립하지 않는다.
    m_stats.residentCount = 0;
    m_stats.residentBytes = 0;
    m_stats.graveyardCount = 0;
    m_stats.graveyardBytes = 0;

    m_frameIndex = 0;
    m_resources = nullptr;
}

uint64_t DX12MeshCache::RetireUnused(uint64_t fenceValue,
    RHIAssetEvictionPass* evictionPass)
{
    uint64_t retired = 0;
    std::vector<RHIAssetEvictionCandidate> candidates;
    candidates.reserve(m_entries.size());
    for (const auto& pair : m_entries)
    {
        candidates.push_back(RHIAssetEvictionCandidate{
            static_cast<uint64_t>(pair.first.m_ID_Data),
            pair.second.lastUsedFrame, pair.second.bytes,
            pair.second.uploadState == RHIUploadTransactionState::Resident });
    }

    const RHIAssetEvictionSelection selection = SelectRHIAssetEvictionCandidates(
        candidates, m_frameIndex, evictionPass);
    if (nullptr != evictionPass && evictionPass->memoryPressure)
    {
        ++m_stats.eviction.pressurePasses;
        m_stats.eviction.pressureProtectedRecent += selection.pressureProtectedRecent;
        m_stats.eviction.pressureUploadPending += selection.pressureUploadPending;
    }

    for (const RHIAssetEvictionSelectionEntry& selected : selection.entries)
    {
        const auto it = m_entries.find(HashedGuid{
            static_cast<size_t>(selected.assetId) });
        if (it == m_entries.end()) continue;

        const uint64_t bytes = it->second.bytes;

        // 표에서 먼저 놓는다 (A-4). 안 놓으면 은퇴한 메시의 핸들이 살아 있어
        // 죽어 가는 리소스를 가리킨다 — 텍스처 캐시가 같은 자리에서 하는 일이다.
        if (nullptr != m_resources)
        {
            m_resources->ReleaseBuffer(it->second.entry.vertices.buffer);
            m_resources->ReleaseBuffer(it->second.entry.indices.buffer);
        }

        m_retireQueue.Enqueue(RHICompletionPoint{ fenceValue }, RetiredBuffers{
            std::move(it->second.vertexBuffer),
            std::move(it->second.indexBuffer) }, bytes);
        --m_stats.residentCount;
        m_stats.residentBytes -= bytes;
        ++m_stats.retired;
        m_stats.retiredBytes += bytes;
        retired += bytes;
        if (selected.pressureDriven)
        {
            ++m_stats.eviction.pressureRetired;
            m_stats.eviction.pressureRetiredBytes += bytes;
        }
        if (nullptr != evictionPass)
            evictionPass->RecordRetired(bytes, selected.pressureDriven);

        m_entries.erase(it);
    }

    return retired;
}

uint64_t DX12MeshCache::SweepGraveyard(uint64_t completedFenceValue)
{
    const RHIRetireCollection collected = m_retireQueue.Collect(
        RHICompletionPoint{ completedFenceValue }, [&](RetiredBuffers& retired)
        {
            m_persistentHeap.Release(retired.vertexBuffer);
            m_persistentHeap.Release(retired.indexBuffer);
        });
    m_persistentHeap.TrimEmptySegments(false);
    return collected.bytes;
}

void DX12MeshCache::BeginFrame(uint64_t frameIndex)
{
    m_frameIndex = frameIndex;
    m_persistentHeap.RefreshBudget();
    if (m_persistentHeap.IsMemoryPressure())
        m_persistentHeap.TrimEmptySegments(true);
}

bool DX12MeshCache::RecordBufferUpload(const void* data, uint64_t bytes,
    const RHIBufferSlice& staging, D3D12_RESOURCE_STATES finalState,
    DX12PersistentHeap::Allocation& destination, std::string& outError)
{
    ID3D12Resource* const stagingResource = m_resources->Resolve(staging.buffer);
    if (!destination.IsValid() || !staging.IsWritable() ||
        nullptr == stagingResource || staging.size < bytes)
    {
        outError = "메시 업로드 배치의 staging slice가 무효다";
        return false;
    }

    memcpy(staging.cpuAddress, data, static_cast<size_t>(bytes));

    auto* commandList = m_resources->GetCommandList();
    if (destination.block.requiresAliasingBarrier)
    {
        D3D12_RESOURCE_BARRIER aliasing{};
        aliasing.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        aliasing.Aliasing.pResourceAfter = destination.resource.Get();
        commandList->ResourceBarrier(1, &aliasing);
    }
    commandList->CopyBufferRegion(destination.resource.Get(), 0,
        stagingResource, staging.offset, bytes);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = destination.resource.Get();
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

    const std::array<RHIUploadRequest, 2> requests = {{
        { vertexBytes, RHIUploadUsage::VertexData, alignof(Vertex) },
        { indexBytes, RHIUploadUsage::IndexData, alignof(uint32) }
    }};
    std::array<RHIBufferSlice, 2> staging{};
    if (!m_resources->ReserveUploadBatch(requests, staging, outError))
    {
        outError = "메시 정점/인덱스 업로드 배치 예약 실패: " + outError;
        ++m_stats.failures;
        return empty;
    }
    const bool validStaging = staging[0].IsWritable() && staging[1].IsWritable() &&
        staging[0].size >= vertexBytes && staging[1].size >= indexBytes &&
        nullptr != m_resources->Resolve(staging[0].buffer) &&
        nullptr != m_resources->Resolve(staging[1].buffer);
    if (!validStaging)
    {
        outError = "메시 정점/인덱스 persistent batch staging이 무효다";
        ++m_stats.failures;
        return empty;
    }

    D3D12_RESOURCE_DESC vertexDesc{};
    vertexDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vertexDesc.Width = vertexBytes;
    vertexDesc.Height = 1;
    vertexDesc.DepthOrArraySize = 1;
    vertexDesc.MipLevels = 1;
    vertexDesc.SampleDesc.Count = 1;
    vertexDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_RESOURCE_DESC indexDesc = vertexDesc;
    indexDesc.Width = indexBytes;

    // resource 두 개를 모두 만든 뒤 copy를 기록한다. 두 번째 생성 실패 뒤 첫
    // resource를 파괴한 채 이미 기록된 command를 제출하는 partial batch를 막는다.
    if (!m_persistentHeap.CreateBuffer(vertexDesc, D3D12_RESOURCE_STATE_COMMON,
        L"MeshVertices", buffers.vertexBuffer, outError))
    {
        ++m_stats.failures;
        return empty;
    }
    if (!m_persistentHeap.CreateBuffer(indexDesc, D3D12_RESOURCE_STATE_COMMON,
        L"MeshIndices", buffers.indexBuffer, outError))
    {
        m_persistentHeap.Release(buffers.vertexBuffer);
        ++m_stats.failures;
        return empty;
    }

    if (!RecordBufferUpload(vertices.data(), vertexBytes, staging[0],
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
        buffers.vertexBuffer, outError) ||
        !RecordBufferUpload(indices.data(), indexBytes, staging[1],
            D3D12_RESOURCE_STATE_INDEX_BUFFER, buffers.indexBuffer, outError))
    {
        m_persistentHeap.Release(buffers.vertexBuffer);
        m_persistentHeap.Release(buffers.indexBuffer);
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
        m_resources->RegisterExternalBuffer(buffers.vertexBuffer.resource.Get()));
    buffers.entry.vertices.size = vertexBytes;
    buffers.entry.vertexStride = sizeof(Vertex);

    buffers.entry.indices = RHIBufferSlice::Whole(
        m_resources->RegisterExternalBuffer(buffers.indexBuffer.resource.Get()));
    buffers.entry.indices.size = indexBytes;
    buffers.entry.indexFormat = RHIFormat::R32Uint;

    buffers.entry.indexCount = static_cast<uint32_t>(indices.size());
    if (!buffers.entry.vertices.buffer.IsValid() ||
        !buffers.entry.indices.buffer.IsValid())
    {
        m_resources->ReleaseBuffer(buffers.entry.vertices.buffer);
        m_resources->ReleaseBuffer(buffers.entry.indices.buffer);
        m_persistentHeap.Release(buffers.vertexBuffer);
        m_persistentHeap.Release(buffers.indexBuffer);
        outError = "메시 persistent buffer 핸들 등록 실패";
        ++m_stats.failures;
        return empty;
    }
    buffers.bytes = vertexBytes + indexBytes;
    buffers.lastUsedFrame = m_frameIndex;
    buffers.recordingId = m_resources->GetCurrentUploadRecordingId();
    buffers.uploadState = RHIUploadTransactionState::Recording;

    ++m_stats.uploads;
    ++m_stats.residentCount;
    m_stats.residentBytes += buffers.bytes;

    const auto inserted = m_entries.emplace(assetId, std::move(buffers));
    return inserted.first->second.entry;
}

void DX12MeshCache::OnUploadSubmitted(uint64_t recordingId,
    RHICompletionPoint completion)
{
    for (auto& pair : m_entries)
    {
        Buffers& buffers = pair.second;
        if (buffers.uploadState != RHIUploadTransactionState::Recording ||
            buffers.recordingId != recordingId) continue;
        buffers.completionValue = completion.value;
        buffers.uploadState = completion.IsValid()
            ? RHIUploadTransactionState::Queued
            : RHIUploadTransactionState::Quarantined;
    }
}

void DX12MeshCache::OnUploadCompleted(uint64_t completedValue)
{
    for (auto& pair : m_entries)
    {
        Buffers& buffers = pair.second;
        if (buffers.uploadState == RHIUploadTransactionState::Queued &&
            buffers.completionValue <= completedValue)
            buffers.uploadState = RHIUploadTransactionState::Resident;
    }
}

void DX12MeshCache::OnUploadAborted(uint64_t recordingId)
{
    auto it = m_entries.begin();
    while (it != m_entries.end())
    {
        Buffers& buffers = it->second;
        if (buffers.uploadState != RHIUploadTransactionState::Recording ||
            buffers.recordingId != recordingId)
        {
            ++it;
            continue;
        }

        m_resources->ReleaseBuffer(buffers.entry.vertices.buffer);
        m_resources->ReleaseBuffer(buffers.entry.indices.buffer);
        m_persistentHeap.Release(buffers.vertexBuffer);
        m_persistentHeap.Release(buffers.indexBuffer);
        --m_stats.residentCount;
        m_stats.residentBytes -= buffers.bytes;
        it = m_entries.erase(it);
    }
}

