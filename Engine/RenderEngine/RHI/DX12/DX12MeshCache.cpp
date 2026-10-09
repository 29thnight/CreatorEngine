#include "DX12MeshCache.h"
#include "DX12DeviceResources.h"
#include "../RHIModelMeshletUpload.h"
#include "../../../EngineDiagnostics/ProfileScope.h"
#include "../../Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>

DX12MeshCache::~DX12MeshCache()
{
    // 자원 해제는 명시 Shutdown 의 몫이다. 여기서는 리스너 등록만 걷는다(헤더 주석).
    if (nullptr != m_resources) m_resources->UnregisterUploadTransactionListener(this);
}

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
    m_modelEntries.clear();
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
            m_resources->ReleaseBuffer(entry.second.entry.meshlets.data.buffer);
            ReleaseCoarseHandles(entry.second.entry);
        }
        for (const auto& entry : m_modelEntries)
        {
            m_resources->ReleaseBuffer(entry.second.entry.vertices.buffer);
            m_resources->ReleaseBuffer(entry.second.entry.indices.buffer);
            m_resources->ReleaseBuffer(entry.second.entry.meshlets.data.buffer);
            ReleaseCoarseHandles(entry.second.entry);
        }
        for (auto& entry : m_entries)
        {
            m_persistentHeap.Release(entry.second.vertexBuffer);
            m_persistentHeap.Release(entry.second.indexBuffer);
            m_persistentHeap.Release(entry.second.meshletBuffer);
            ReleaseCoarseAllocations(entry.second.coarseIndexBuffers, entry.second.coarseMeshletBuffers);
        }
        for (auto& entry : m_modelEntries)
        {
            m_persistentHeap.Release(entry.second.vertexBuffer);
            m_persistentHeap.Release(entry.second.indexBuffer);
            m_persistentHeap.Release(entry.second.meshletBuffer);
            ReleaseCoarseAllocations(entry.second.coarseIndexBuffers, entry.second.coarseMeshletBuffers);
        }
    }

    m_entries.clear();
    m_modelEntries.clear();
    m_retireQueue.Drain([&](RetiredBuffers& retired)
        {
            m_persistentHeap.Release(retired.vertexBuffer);
            m_persistentHeap.Release(retired.indexBuffer);
            m_persistentHeap.Release(retired.meshletBuffer);
            ReleaseCoarseAllocations(retired.coarseIndexBuffers, retired.coarseMeshletBuffers);
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
            m_resources->ReleaseBuffer(it->second.entry.meshlets.data.buffer);
            ReleaseCoarseHandles(it->second.entry);
        }

        m_retireQueue.Enqueue(RHICompletionPoint{ fenceValue }, RetiredBuffers{
            std::move(it->second.vertexBuffer),
            std::move(it->second.indexBuffer), std::move(it->second.meshletBuffer),
            std::move(it->second.coarseIndexBuffers), std::move(it->second.coarseMeshletBuffers) }, bytes);
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

    // Model generation cache는 축약 assetId로 다시 찾지 않는다. selection의
    // assetId는 이번 호출 안에서만 쓰는 정렬된 key 배열 인덱스다.
    std::vector<assets::ModelMeshHandle> modelKeys;
    std::vector<RHIAssetEvictionCandidate> modelCandidates;
    modelKeys.reserve(m_modelEntries.size());
    modelCandidates.reserve(m_modelEntries.size());
    for (const auto& pair : m_modelEntries)
    {
        modelKeys.push_back(pair.first);
        modelCandidates.push_back(RHIAssetEvictionCandidate{
            static_cast<uint64_t>(modelKeys.size() - 1u),
            pair.second.lastUsedFrame, pair.second.bytes,
            pair.second.uploadState == RHIUploadTransactionState::Resident });
    }
    const RHIAssetEvictionSelection modelSelection =
        SelectRHIAssetEvictionCandidates(modelCandidates, m_frameIndex, evictionPass);
    for (const RHIAssetEvictionSelectionEntry& selected : modelSelection.entries)
    {
        if (selected.assetId >= modelKeys.size()) continue;
        const auto it = m_modelEntries.find(modelKeys[static_cast<size_t>(selected.assetId)]);
        if (it == m_modelEntries.end()) continue;
        const uint64_t bytes = it->second.bytes;
        if (nullptr != m_resources)
        {
            m_resources->ReleaseBuffer(it->second.entry.vertices.buffer);
            m_resources->ReleaseBuffer(it->second.entry.indices.buffer);
            m_resources->ReleaseBuffer(it->second.entry.meshlets.data.buffer);
            ReleaseCoarseHandles(it->second.entry);
        }
        m_retireQueue.Enqueue(RHICompletionPoint{ fenceValue }, RetiredBuffers{
            std::move(it->second.vertexBuffer), std::move(it->second.indexBuffer), std::move(it->second.meshletBuffer),
            std::move(it->second.coarseIndexBuffers), std::move(it->second.coarseMeshletBuffers) }, bytes);
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
        m_modelEntries.erase(it);
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
            m_persistentHeap.Release(retired.meshletBuffer);
            ReleaseCoarseAllocations(retired.coarseIndexBuffers, retired.coarseMeshletBuffers);
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

void DX12MeshCache::ReleaseCoarseHandles(const Entry& entry)
{
    if (!m_resources)
    {
        return;
    }
    for (uint32_t i = 0; i < entry.coarseLodCount; ++i)
    {
        m_resources->ReleaseBuffer(entry.coarseLods[i].indices.buffer);
        m_resources->ReleaseBuffer(entry.coarseLods[i].meshlets.data.buffer);
    }
}

void DX12MeshCache::ReleaseCoarseAllocations(
    std::array<DX12PersistentHeap::Allocation, kRHIMaxCoarseMeshLods>& indices,
    std::array<DX12PersistentHeap::Allocation, kRHIMaxCoarseMeshLods>& meshlets)
{
    for (uint32_t i = 0; i < kRHIMaxCoarseMeshLods; ++i)
    {
        m_persistentHeap.Release(indices[i]);
        m_persistentHeap.Release(meshlets[i]);
    }
}

void DX12MeshCache::RecordBufferUpload(const void* data, uint64_t bytes,
    const RHIBufferSlice& staging, D3D12_RESOURCE_STATES finalState,
    DX12PersistentHeap::Allocation& destination)
{
    ID3D12Resource* const stagingResource = m_resources->Resolve(staging.buffer);
    // All staging, allocations, handles and the command list were validated
    // together before the first copy; recording this batch has no failure path.
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
}

DX12MeshCache::Entry DX12MeshCache::GetOrUpload(Mesh* mesh, std::string& outError)
{
    Entry empty{};
    if (nullptr == m_resources || nullptr == mesh)
    {
        outError = "메시 캐시: 인자가 잘못됐다";
        return empty;
    }

    // 키는 주소가 아니라 자산 신원이다(멤버 선언부 주석 참고). 히트를 여기서
    // 먼저 조회해 lookup(뮤텍스+해시) 비용을 히트 경로에서 치르지 않는다.
    const HashedGuid assetId = mesh->m_hashingMesh;
    const auto found = m_entries.find(assetId);
    if (found != m_entries.end())
    {
        ++m_stats.hits;
        found->second.lastUsedFrame = m_frameIndex;   // 쓰였다고 찍는다(③)
        return found->second.entry;
    }

    // MBC9 — 절차 지오메트리(스프라이트 쿼드·지형·기즈모)의 legacy 96B 경로.
    // 모델 지오메트리는 GetOrUploadModel(typed generation 뷰)만 탄다.
    const auto& vertices = mesh->GetVertices();
    const auto& indices = mesh->GetIndices();
    if (vertices.empty() || indices.empty())
    {
        // 빈 메시는 실패가 아니라 '그릴 것이 없음'이다. 그리기 목록에서 빠진다.
        return empty;
    }
    return UploadResolved(assetId, nullptr, vertices.data(),
        static_cast<uint64_t>(vertices.size()) * sizeof(Vertex),
        sizeof(Vertex), 0, indices.data(),
        static_cast<uint32_t>(indices.size()), outError);
}

DX12MeshCache::Entry DX12MeshCache::FindModel(const assets::ModelMeshHandle& handle) const
{
    if (!m_resources || !handle.IsValid())
    {
        return {};
    }
    const auto found = m_modelEntries.find(handle);
    return found != m_modelEntries.end() &&
        found->second.uploadState == RHIUploadTransactionState::Resident
        ? found->second.entry : Entry{};
}

DX12MeshCache::Entry DX12MeshCache::GetOrUploadModel(
    const RHIModelMeshView& view, std::string& outError)
{
    if (!m_resources || !view.IsMetadataComplete() || view.sourceLodIndex != 0)
    {
        outError = "Mesh cache requires valid base-LOD model metadata.";
        return {};
    }
    // The immutable descriptor is enough to reuse GPU bytes after CPU eviction.
    const auto found = m_modelEntries.find(view.handle);
    if (found != m_modelEntries.end())
    {
        ++m_stats.hits;
        found->second.lastUsedFrame = m_frameIndex;
        outError.clear();
        return found->second.entry;
    }
    if (!view.IsComplete())
    {
        outError = "Mesh geometry preparation is required before upload: no compatible CPU payload is pinned.";
        return {};
    }
    return UploadResolved({}, &view.handle, view.vertexData, view.vertexBytes,
        view.vertexStride, view.vertexAttributeMask, view.indexData,
        view.indexCount, outError, &view);
}

DX12MeshCache::Entry DX12MeshCache::UploadResolved(HashedGuid legacyKey,
    const assets::ModelMeshHandle* modelKey,
    const void* vertexData, uint64_t vertexBytes, uint32_t vertexStride,
    uint32_t attributeMask, const uint32_t* indexData, uint32_t indexCount,
    std::string& outError, const RHIModelMeshView* modelView)
{
    ce::profile_scope profile{ ce::marker<"DX12MeshUpload">() };
    Entry empty{};
    Buffers* cached = nullptr;
    if (modelKey)
    {
        const auto found = m_modelEntries.find(*modelKey);
        if (found != m_modelEntries.end())
        {
            cached = &found->second;
        }
    }
    else
    {
        const auto found = m_entries.find(legacyKey);
        if (found != m_entries.end())
        {
            cached = &found->second;
        }
    }
    if (cached)
    {
        ++m_stats.hits;
        cached->lastUsedFrame = m_frameIndex;
        return cached->entry;
    }
    if (!vertexData || !vertexBytes || !vertexStride || !indexData || !indexCount)
    {
        return empty;
    }
    auto* const commandList = m_resources->GetCommandList();
    const uint64_t recordingId = m_resources->GetCurrentUploadRecordingId();
    if (!commandList || !recordingId)
    {
        outError = "Mesh upload requires an active recording.";
        ++m_stats.failures;
        return empty;
    }

    RHIModelMeshletUpload meshlets;
    std::string meshletError;
    if (modelView && m_resources->GetMeshShaderCapabilities().meshShader &&
        !BuildRHIModelMeshletUpload(*modelView, meshlets, meshletError))
    {
        ++m_stats.meshletFallbacks;
    }
    bool withMeshlets = !meshlets.bytes.empty();
    const auto omitMeshlets = [&]
    {
        if (withMeshlets)
        {
            ++m_stats.meshletFallbacks;
        }
        withMeshlets = false;
    };
    const uint64_t indexBytes = uint64_t(indexCount) * sizeof(uint32_t);
    const std::array<RHIUploadRequest, 3> requests = {{
        { vertexBytes, RHIUploadUsage::VertexData, alignof(Vertex) },
        { indexBytes, RHIUploadUsage::IndexData, alignof(uint32_t) },
        { meshlets.bytes.size(), RHIUploadUsage::BufferCopy, 16 }
    }};
    std::array<RHIBufferSlice, 3> staging{};
    const auto reserve = [&](size_t count)
    {
        return m_resources->ReserveUploadBatch(std::span(requests).first(count),
            std::span(staging).first(count), outError);
    };
    if (withMeshlets && !reserve(3))
    {
        omitMeshlets();
    }
    if (!withMeshlets && !reserve(2))
    {
        outError = "Mesh vertex/index upload reservation failed: " + outError;
        ++m_stats.failures;
        return empty;
    }
    outError.clear();
    const auto validStaging = [&](size_t slot)
    {
        return staging[slot].IsWritable() && staging[slot].size >= requests[slot].bytes &&
            m_resources->Resolve(staging[slot].buffer) != nullptr;
    };
    if (!validStaging(0) || !validStaging(1))
    {
        outError = "Invalid mesh vertex/index upload staging.";
        ++m_stats.failures;
        return empty;
    }
    if (withMeshlets && !validStaging(2))
    {
        omitMeshlets();
    }

    Buffers buffers{};
    const auto release = [&]
    {
        m_resources->ReleaseBuffer(buffers.entry.vertices.buffer);
        m_resources->ReleaseBuffer(buffers.entry.indices.buffer);
        m_resources->ReleaseBuffer(buffers.entry.meshlets.data.buffer);
        ReleaseCoarseHandles(buffers.entry);
        m_persistentHeap.Release(buffers.vertexBuffer);
        m_persistentHeap.Release(buffers.indexBuffer);
        m_persistentHeap.Release(buffers.meshletBuffer);
        ReleaseCoarseAllocations(buffers.coarseIndexBuffers, buffers.coarseMeshletBuffers);
    };
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = vertexBytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (!m_persistentHeap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
        L"MeshVertices", buffers.vertexBuffer, outError))
    {
        ++m_stats.failures;
        return empty;
    }
    desc.Width = indexBytes;
    if (!m_persistentHeap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
        L"MeshIndices", buffers.indexBuffer, outError))
    {
        release();
        ++m_stats.failures;
        return empty;
    }
    if (withMeshlets)
    {
        desc.Width = meshlets.bytes.size();
        if (!m_persistentHeap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
            L"MeshMeshlets", buffers.meshletBuffer, meshletError))
        {
            omitMeshlets();
        }
    }

    // Register every handle before recording any copies. Optional allocation or
    // registration failure cannot leave native commands pointing at freed data.
    buffers.entry.vertices = RHIBufferSlice::Whole(
        m_resources->RegisterExternalBuffer(buffers.vertexBuffer.resource.Get()));
    buffers.entry.vertices.size = vertexBytes;
    buffers.entry.vertexStride = vertexStride;
    buffers.entry.vertexAttributeMask = attributeMask;
    buffers.entry.indices = RHIBufferSlice::Whole(
        m_resources->RegisterExternalBuffer(buffers.indexBuffer.resource.Get()));
    buffers.entry.indices.size = indexBytes;
    buffers.entry.indexFormat = RHIFormat::R32Uint;
    buffers.entry.indexCount = indexCount;
    if (!buffers.entry.vertices.IsValid() || !buffers.entry.indices.IsValid())
    {
        release();
        outError = "Mesh persistent buffer registration failed.";
        ++m_stats.failures;
        return empty;
    }
    if (withMeshlets)
    {
        buffers.entry.meshlets.data = RHIBufferSlice::Whole(
            m_resources->RegisterExternalBuffer(buffers.meshletBuffer.resource.Get()));
        if (!buffers.entry.meshlets.data.IsValid())
        {
            m_persistentHeap.Release(buffers.meshletBuffer);
            omitMeshlets();
        }
        else
        {
            buffers.entry.meshlets.data.size = meshlets.bytes.size();
            buffers.entry.meshlets.meshletCount = meshlets.meshletCount;
            buffers.entry.meshlets.profileVersion = meshlets.profileVersion;
        }
    }
    struct CoarseUpload
    {
        RHIModelMeshView view;
        RHIModelMeshletUpload meshlets;
        RHIBufferSlice indexStaging, meshletStaging;
    };
    std::array<CoarseUpload, kRHIMaxCoarseMeshLods> coarseUploads;
    uint64_t coarseBytes = 0;
    const auto* sourceMesh = modelView ? modelView->SourceMesh() : nullptr;
    if (sourceMesh)
    {
        static_assert(kRHIMaxCoarseMeshLods == experiment::kMeshLodMaxLevels);
        const uint32_t count = static_cast<uint32_t>((std::min)(sourceMesh->coarseLods.levels.size(),
            size_t(kRHIMaxCoarseMeshLods)));
        for (uint32_t i = 0; i < count; ++i)
        {
            auto& upload = coarseUploads[i];
            auto& binding = buffers.entry.coarseLods[i];
            auto& indexAllocation = buffers.coarseIndexBuffers[i];
            auto& meshletAllocation = buffers.coarseMeshletBuffers[i];
            const auto& level = sourceMesh->coarseLods.levels[i];
            std::string optionalError;
            if (!BuildRHIModelMeshLodView(*modelView, i, upload.view) ||
                !std::isfinite(level.geometricError) || level.geometricError < 0.0f)
            {
                ++m_stats.coarseLodFallbacks;
                break;
            }
            const uint64_t bytes = uint64_t(upload.view.indexCount) * sizeof(uint32_t);
            desc.Width = bytes;
            if (!m_persistentHeap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
                L"MeshLodIndices", indexAllocation, optionalError))
            {
                ++m_stats.coarseLodFallbacks;
                break;
            }
            binding.indices = RHIBufferSlice::Whole(m_resources->RegisterExternalBuffer(indexAllocation.resource.Get()));
            if (!binding.indices.IsValid())
            {
                m_persistentHeap.Release(indexAllocation);
                ++m_stats.coarseLodFallbacks;
                break;
            }
            const auto discardLodMeshlets = [&]
            {
                m_resources->ReleaseBuffer(binding.meshlets.data.buffer);
                m_persistentHeap.Release(meshletAllocation);
                binding.meshlets = {};
                upload.meshlets = {};
                ++m_stats.meshletFallbacks;
            };
            if (m_resources->GetMeshShaderCapabilities().meshShader &&
                !BuildRHIModelMeshletUpload(upload.view, upload.meshlets, optionalError))
            {
                ++m_stats.meshletFallbacks;
            }
            if (!upload.meshlets.bytes.empty())
            {
                desc.Width = upload.meshlets.bytes.size();
                if (!m_persistentHeap.CreateBuffer(desc, D3D12_RESOURCE_STATE_COMMON,
                    L"MeshLodMeshlets", meshletAllocation, optionalError))
                {
                    discardLodMeshlets();
                }
                else
                {
                    binding.meshlets.data = RHIBufferSlice::Whole(
                        m_resources->RegisterExternalBuffer(meshletAllocation.resource.Get()));
                    if (!binding.meshlets.data.IsValid())
                    {
                        discardLodMeshlets();
                    }
                }
            }
            const std::array<RHIUploadRequest, 2> lodRequests = {{
                { bytes, RHIUploadUsage::IndexData, alignof(uint32_t) },
                { upload.meshlets.bytes.size(), RHIUploadUsage::BufferCopy, 16 }
            }};
            std::array<RHIBufferSlice, 2> lodStaging{};
            bool reserved = false;
            if (!upload.meshlets.bytes.empty())
            {
                reserved = m_resources->ReserveUploadBatch(lodRequests, lodStaging, optionalError);
                if (!reserved)
                {
                    discardLodMeshlets();
                }
            }
            if (!reserved)
            {
                reserved = m_resources->ReserveUploadBatch(std::span(lodRequests).first(1),
                    std::span(lodStaging).first(1), optionalError);
            }
            if (!reserved || !lodStaging[0].IsWritable() || lodStaging[0].size < bytes ||
                !m_resources->Resolve(lodStaging[0].buffer))
            {
                m_resources->ReleaseBuffer(binding.indices.buffer);
                m_persistentHeap.Release(indexAllocation);
                if (!upload.meshlets.bytes.empty())
                {
                    discardLodMeshlets();
                }
                binding = {};
                ++m_stats.coarseLodFallbacks;
                break;
            }
            if (!upload.meshlets.bytes.empty() && (!lodStaging[1].IsWritable() ||
                lodStaging[1].size < upload.meshlets.bytes.size() || !m_resources->Resolve(lodStaging[1].buffer)))
            {
                discardLodMeshlets();
            }
            upload.indexStaging = lodStaging[0];
            upload.meshletStaging = lodStaging[1];
            binding.indices.size = bytes;
            binding.indexCount = upload.view.indexCount;
            binding.geometricError = level.geometricError;
            if (!upload.meshlets.bytes.empty())
            {
                binding.meshlets.data.size = upload.meshlets.bytes.size();
                binding.meshlets.meshletCount = upload.meshlets.meshletCount;
                binding.meshlets.profileVersion = upload.meshlets.profileVersion;
            }
            coarseBytes += bytes + upload.meshlets.bytes.size();
            ++buffers.entry.coarseLodCount;
        }
    }

    if (m_resources->GetCommandList() != commandList ||
        m_resources->GetCurrentUploadRecordingId() != recordingId)
    {
        release();
        outError = "Mesh upload recording changed while preparing coarse LODs.";
        ++m_stats.failures;
        return empty;
    }
    const auto shaderRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    RecordBufferUpload(vertexData, vertexBytes, staging[0],
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | shaderRead, buffers.vertexBuffer);
    RecordBufferUpload(indexData, indexBytes, staging[1], D3D12_RESOURCE_STATE_INDEX_BUFFER, buffers.indexBuffer);
    if (withMeshlets)
    {
        RecordBufferUpload(meshlets.bytes.data(), meshlets.bytes.size(), staging[2], shaderRead, buffers.meshletBuffer);
        ++m_stats.meshletUploads;
    }
    for (uint32_t i = 0; i < buffers.entry.coarseLodCount; ++i)
    {
        const auto& upload = coarseUploads[i];
        RecordBufferUpload(upload.view.indexData, uint64_t(upload.view.indexCount) * sizeof(uint32_t),
            upload.indexStaging, D3D12_RESOURCE_STATE_INDEX_BUFFER, buffers.coarseIndexBuffers[i]);
        if (!upload.meshlets.bytes.empty())
        {
            RecordBufferUpload(upload.meshlets.bytes.data(), upload.meshlets.bytes.size(),
                upload.meshletStaging, shaderRead, buffers.coarseMeshletBuffers[i]);
            ++m_stats.meshletUploads;
        }
        ++m_stats.coarseLodUploads;
    }
    buffers.bytes = vertexBytes + indexBytes + (withMeshlets ? meshlets.bytes.size() : 0);
    buffers.bytes += coarseBytes;
    buffers.lastUsedFrame = m_frameIndex;
    buffers.recordingId = recordingId;
    buffers.uploadState = RHIUploadTransactionState::Recording;
    ++m_stats.uploads;
    if (modelKey)
    {
        ++m_stats.modelGenerationUploads;
    }
    ++m_stats.residentCount;
    m_stats.residentBytes += buffers.bytes;
    if (modelKey)
    {
        const auto inserted = m_modelEntries.emplace(*modelKey, std::move(buffers));
        return inserted.first->second.entry;
    }
    const auto inserted = m_entries.emplace(legacyKey, std::move(buffers));
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
    for (auto& pair : m_modelEntries)
    {
        Buffers& buffers = pair.second;
        if (buffers.uploadState != RHIUploadTransactionState::Recording
            || buffers.recordingId != recordingId) continue;
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
    for (auto& pair : m_modelEntries)
    {
        Buffers& buffers = pair.second;
        if (buffers.uploadState == RHIUploadTransactionState::Queued
            && buffers.completionValue <= completedValue)
            buffers.uploadState = RHIUploadTransactionState::Resident;
    }
}

void DX12MeshCache::OnUploadSubmissionRejected(uint64_t recordingId, RHICompletionPoint completion)
{
    const auto reject = [recordingId, completion](auto& entries)
    {
        for (auto& [key, buffers] : entries)
        {
            if (buffers.recordingId == recordingId && buffers.completionValue == completion.value &&
                (buffers.uploadState == RHIUploadTransactionState::Queued ||
                    buffers.uploadState == RHIUploadTransactionState::Resident))
            {
                buffers.uploadState = RHIUploadTransactionState::Recording;
            }
        }
    };
    reject(m_entries);
    reject(m_modelEntries);
    OnUploadAborted(recordingId);
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
        m_resources->ReleaseBuffer(buffers.entry.meshlets.data.buffer);
        ReleaseCoarseHandles(buffers.entry);
        m_persistentHeap.Release(buffers.vertexBuffer);
        m_persistentHeap.Release(buffers.indexBuffer);
        m_persistentHeap.Release(buffers.meshletBuffer);
        ReleaseCoarseAllocations(buffers.coarseIndexBuffers, buffers.coarseMeshletBuffers);
        --m_stats.residentCount;
        m_stats.residentBytes -= buffers.bytes;
        it = m_entries.erase(it);
    }
    auto modelIt = m_modelEntries.begin();
    while (modelIt != m_modelEntries.end())
    {
        Buffers& buffers = modelIt->second;
        if (buffers.uploadState != RHIUploadTransactionState::Recording
            || buffers.recordingId != recordingId)
        {
            ++modelIt;
            continue;
        }
        m_resources->ReleaseBuffer(buffers.entry.vertices.buffer);
        m_resources->ReleaseBuffer(buffers.entry.indices.buffer);
        m_resources->ReleaseBuffer(buffers.entry.meshlets.data.buffer);
        ReleaseCoarseHandles(buffers.entry);
        m_persistentHeap.Release(buffers.vertexBuffer);
        m_persistentHeap.Release(buffers.indexBuffer);
        m_persistentHeap.Release(buffers.meshletBuffer);
        ReleaseCoarseAllocations(buffers.coarseIndexBuffers, buffers.coarseMeshletBuffers);
        --m_stats.residentCount;
        m_stats.residentBytes -= buffers.bytes;
        modelIt = m_modelEntries.erase(modelIt);
    }
}
