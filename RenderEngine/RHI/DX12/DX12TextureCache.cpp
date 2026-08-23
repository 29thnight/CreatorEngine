#include "DX12TextureCache.h"
#include <DirectXTex.h>
#include "DX12DeviceResources.h"
#include "../../Texture.h"

// d3d11.h를 직접 include하지 않는다 — T4로 이 파일에 DX11 타입이 남지 않았다.
// (Texture.h가 아직 전이로 끌어오지만 그건 T6이 걷을 몫이다.)

#include <vector>

uint64_t DX12TextureCache::RetireUnused(uint64_t fenceValue,
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

        m_retireQueue.Enqueue(RHICompletionPoint{ fenceValue },
            RetiredResource{ std::move(it->second.allocation) }, bytes);
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

        // 설명도 함께 지운다 — 남겨 두면 다음 GetOrUpload가 히트로 판정하고
        // 이미 놓은 리소스의 핸들을 돌려준다.
        //
        // ★ 표에서도 놓는다(V2-b1). 안 놓으면 표가 업로드 횟수만큼 자라고,
        //   더 나쁘게는 은퇴한 핸들이 무덤에서 죽어 가는 리소스로 계속
        //   풀린다. 놓으면 그 핸들은 nullptr이 된다 — 잘못 쓰면 안 그려지고,
        //   조용히 엉뚱한 것을 그리지 않는다.
        if (nullptr != m_resources)
        {
            const auto described = m_descriptions.find(it->first);
            if (described != m_descriptions.end()) m_resources->ReleaseTexture(described->second.handle);
        }
        m_descriptions.erase(it->first);
        m_entries.erase(it);
    }

    return retired;
}

uint64_t DX12TextureCache::SweepGraveyard(uint64_t completedFenceValue)
{
    const RHIRetireCollection collected = m_retireQueue.Collect(
        RHICompletionPoint{ completedFenceValue },
        [&](RetiredResource& retired)
        { m_persistentHeap.Release(retired.allocation); });
    m_persistentHeap.TrimEmptySegments(false);
    return collected.bytes;
}

bool DX12TextureCache::Initialize(DX12DeviceResources* resources, std::string& outError)
{
    if (nullptr == resources || !resources->IsInitialized())
    {
        outError = "텍스처 캐시: 인자가 불완전하다";
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
    m_descriptions.clear();
    m_fallbackTransactions.clear();
    m_stats = Stats{};

    // 흰색 텍스처는 여기서 만들지 않는다. 만들려면 커맨드를 기록해야 하는데
    // Initialize는 프레임 밖에서 불릴 수 있다. 첫 사용 때(프레임 안) 만든다.
    return true;
}

void DX12TextureCache::BeginFrame(uint64_t frameIndex)
{
    m_frameIndex = frameIndex;
    m_persistentHeap.RefreshBudget();
    if (m_persistentHeap.IsMemoryPressure())
        m_persistentHeap.TrimEmptySegments(true);
}

void DX12TextureCache::Shutdown()
{
    // 표에서 먼저 놓는다 — m_resources를 null로 만들기 전이어야 한다(V2-b1).
    if (nullptr != m_resources)
    {
        m_resources->UnregisterUploadTransactionListener(this);
        for (const auto& described : m_descriptions) m_resources->ReleaseTexture(described.second.handle);
        m_resources->ReleaseTexture(m_white.handle);
        m_resources->ReleaseTexture(m_black.handle);
        m_resources->ReleaseTexture(m_ormNeutral.handle);
    }

    for (auto& resident : m_entries)
        m_persistentHeap.Release(resident.second.allocation);

    m_entries.clear();
    m_descriptions.clear();
    m_persistentHeap.Release(m_whiteResource);
    m_white = Entry{};
    m_persistentHeap.Release(m_blackResource);
    m_black = Entry{};
    m_persistentHeap.Release(m_ormNeutralResource);
    m_ormNeutral = Entry{};
    m_fallbackTransactions.clear();

    m_retireQueue.Drain([&](RetiredResource& retired)
        { m_persistentHeap.Release(retired.allocation); });
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

bool DX12TextureCache::CreateSolidTexture(const uint8_t rgba[4], const wchar_t* name,
    DX12PersistentHeap::Allocation& outAllocation, Entry& outEntry,
    std::string& outError)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    const RHIBufferSlice staging = m_resources->AllocateUpload(RHIUploadRequest{
        D3D12_TEXTURE_DATA_PITCH_ALIGNMENT, RHIUploadUsage::TextureCopy, 1 });
    ID3D12Resource* const stagingResource = m_resources->Resolve(staging.buffer);
    if (!staging.IsWritable() || nullptr == stagingResource)
    {
        outError = "기본 텍스처 업로드 링 할당 실패";
        return false;
    }
    memcpy(staging.cpuAddress, rgba, 4);

    DX12PersistentHeap::Allocation allocation;
    if (!m_persistentHeap.CreateTexture(desc, D3D12_RESOURCE_STATE_COPY_DEST,
        name, allocation, outError))
        return false;

    outEntry = Entry{};
    outEntry.handle = m_resources->RegisterExternalTexture(allocation.resource.Get());
    if (!outEntry.handle.IsValid())
    {
        m_persistentHeap.Release(allocation);
        outError = "기본 텍스처를 RHI 표에 등록하지 못했다";
        return false;
    }

    auto* commandList = m_resources->GetCommandList();
    if (allocation.block.requiresAliasingBarrier)
    {
        D3D12_RESOURCE_BARRIER aliasing{};
        aliasing.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        aliasing.Aliasing.pResourceAfter = allocation.resource.Get();
        commandList->ResourceBarrier(1, &aliasing);
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = allocation.resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = stagingResource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = staging.offset;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = 1;
    src.PlacedFootprint.Footprint.Height = 1;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = allocation.resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    outEntry.format = RHIFormat::RGBA8Unorm;
    outEntry.width = 1;
    outEntry.height = 1;
    outEntry.mipLevels = 1;
    m_fallbackTransactions.push_back(FallbackTransaction{
        outEntry.handle, m_resources->GetCurrentUploadRecordingId(), 0,
        RHIUploadTransactionState::Recording });
    outAllocation = std::move(allocation);
    return true;
}

bool DX12TextureCache::CreateWhiteTexture(std::string& outError)
{
    const uint8_t white[4] = { 255, 255, 255, 255 };
    return CreateSolidTexture(white, L"DX12DefaultWhite", m_whiteResource, m_white, outError);
}

DX12TextureCache::Entry DX12TextureCache::GetBlackTexture(std::string& outError)
{
    if (nullptr == m_resources) return Entry{};
    if (!m_black.IsValid())
    {
        const uint8_t black[4] = { 0, 0, 0, 255 };
        if (!CreateSolidTexture(black, L"DX12DefaultBlack",
            m_blackResource, m_black, outError))
        {
            return Entry{};
        }
    }
    return m_black;
}

DX12TextureCache::Entry DX12TextureCache::GetOrmNeutralTexture(std::string& outError)
{
    if (nullptr == m_resources) return Entry{};
    if (!m_ormNeutral.IsValid())
    {
        // R 오클루전 1 · G 거칠기 1 · B 금속 0 — 팩터가 곱해지고 더해지는
        // 슬롯이라 이 조합이라야 팩터 값이 그대로 살아남는다.
        const uint8_t ormNeutral[4] = { 255, 255, 0, 255 };
        if (!CreateSolidTexture(ormNeutral, L"DX12DefaultOrmNeutral",
            m_ormNeutralResource, m_ormNeutral, outError))
        {
            return Entry{};
        }
    }
    return m_ormNeutral;
}


// ── CPU 픽셀 업로드 (PHASE 3-1 재정의, T1 → T4에서 유일 경로) ──
//
// 예전에는 짝이 되는 UploadFromDX11이 있었다. DX11 스테이징을 만들어
// CopyResource로 GPU에서 끌어내린 뒤 Map으로 읽는 경로였고, Texture가 픽셀을
// 들고 있지 않던 시절의 유일한 방법이었다. T1이 로더에게 최종 이미지를
// 남기게 하면서 그쪽 소비자가 사라졌고, T4에서 함께 걷었다.
bool DX12TextureCache::UploadFromCpuPixels(const DirectX::ScratchImage& image,
    const wchar_t* debugName, DX12PersistentHeap::Allocation& outAllocation,
    Entry& outEntry, std::string& outError)
{
    auto* device = m_resources->GetDevice();
    const DirectX::TexMetadata& metadata = image.GetMetadata();
    outEntry = {};

    if (0 == metadata.width || 0 == metadata.height || 0 == metadata.mipLevels)
    {
        outError = "CPU 픽셀 메타데이터가 비어 있다";
        return false;
    }

    // 3D 텍스처는 이 경로로 오지 않는다(재질·아이콘은 전부 2D나 큐브다).
    // 조용히 이상한 것을 만드는 대신 거절하고 호출부가 DX11 경로로 물러선다.
    if (DirectX::TEX_DIMENSION_TEXTURE2D != metadata.dimension)
    {
        outError = "2D가 아닌 CPU 이미지는 직결 업로드하지 않는다";
        return false;
    }

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = metadata.width;
    desc.Height = static_cast<UINT>(metadata.height);
    desc.DepthOrArraySize = static_cast<UINT16>(metadata.arraySize);
    desc.MipLevels = static_cast<UINT16>(metadata.mipLevels);
    desc.Format = metadata.format;
    desc.SampleDesc.Count = 1;

    const uint32_t subresourceCount =
        static_cast<uint32_t>(metadata.mipLevels * metadata.arraySize);

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresourceCount);
    std::vector<UINT>   rowCounts(subresourceCount);
    std::vector<UINT64> rowSizes(subresourceCount);
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, subresourceCount, 0,
        footprints.data(), rowCounts.data(), rowSizes.data(), &totalBytes);

    const RHIBufferSlice staging = m_resources->AllocateUpload(
        RHIUploadRequest{ totalBytes, RHIUploadUsage::TextureCopy, 1 });
    ID3D12Resource* const stagingResource = m_resources->Resolve(staging.buffer);
    if (!staging.IsWritable() || nullptr == stagingResource)
    {
        outError = "텍스처 업로드 세그먼트 예약 실패 (" +
            std::to_string(totalBytes) + "바이트)";
        return false;
    }
    uint8_t* const destinationBase = static_cast<uint8_t*>(staging.cpuAddress);
    const uint64_t stagingBaseOffset = staging.offset;

    // 모든 CPU 입력을 먼저 검증·복사한다. resource를 만든 뒤 중간 mip에서 실패해
    // 이미 기록한 command가 파괴된 resource를 가리키는 partial batch를 막는다.
    for (uint32_t subresource = 0; subresource < subresourceCount; ++subresource)
    {
        // DX12 서브리소스 번호를 (밉, 배열 슬라이스)로 되돌린다 —
        // subresource = mip + arraySlice * mipLevels 가 규칙이다.
        const size_t mip = subresource % metadata.mipLevels;
        const size_t item = subresource / metadata.mipLevels;

        const DirectX::Image* sourceImage = image.GetImage(mip, item, 0);
        if (nullptr == sourceImage)
        {
            outError = "CPU 이미지에 서브리소스가 없다";
            return false;
        }

        const auto& footprint = footprints[subresource];
        const uint32_t rows = rowCounts[subresource];
        const uint64_t rowSize = rowSizes[subresource];

        // 행 간격이 다르다. DirectXTex는 자기 rowPitch를, DX12는 256바이트
        // 정렬을 쓰므로 행 단위로 옮긴다(DX11 경로와 같은 이유).
        for (uint32_t row = 0; row < rows; ++row)
        {
            const uint8_t* sourceRow = sourceImage->pixels
                + static_cast<size_t>(row) * sourceImage->rowPitch;
            uint8_t* destinationRow = destinationBase + footprint.Offset
                + static_cast<size_t>(row) * footprint.Footprint.RowPitch;
            memcpy(destinationRow, sourceRow, static_cast<size_t>(rowSize));
        }
    }

    DX12PersistentHeap::Allocation allocation;
    if (!m_persistentHeap.CreateTexture(desc, D3D12_RESOURCE_STATE_COPY_DEST,
        debugName, allocation, outError))
        return false;

    outEntry.handle = m_resources->RegisterExternalTexture(allocation.resource.Get());
    if (!outEntry.handle.IsValid())
    {
        m_persistentHeap.Release(allocation);
        outError = "DX12 pooled texture를 RHI 표에 등록하지 못했다";
        return false;
    }

    auto* commandList = m_resources->GetCommandList();
    if (allocation.block.requiresAliasingBarrier)
    {
        D3D12_RESOURCE_BARRIER aliasing{};
        aliasing.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        aliasing.Aliasing.pResourceAfter = allocation.resource.Get();
        commandList->ResourceBarrier(1, &aliasing);
    }

    for (uint32_t subresource = 0; subresource < subresourceCount; ++subresource)
    {
        const auto& footprint = footprints[subresource];

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = allocation.resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = stagingResource;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;
        src.PlacedFootprint.Offset += stagingBaseOffset;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = allocation.resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    outEntry.format = FromDXGI(metadata.format);
    outEntry.width = static_cast<uint32_t>(metadata.width);
    outEntry.height = static_cast<uint32_t>(metadata.height);
    outEntry.mipLevels = static_cast<uint32_t>(metadata.mipLevels);
    outEntry.arraySize = static_cast<uint32_t>(metadata.arraySize);
    outEntry.isCube = metadata.IsCubemap();

    m_stats.bytesUploaded += totalBytes;
    outAllocation = std::move(allocation);
    return true;
}

DX12TextureCache::Entry DX12TextureCache::GetOrUpload(Texture* texture, std::string& outError)
{
    if (nullptr == m_resources) return Entry{};

    // 첫 호출에 흰색을 만든다(프레임 안이어야 하므로 여기가 유일하게 안전한 자리).
    if (!m_white.IsValid())
    {
        std::string whiteError;
        if (!CreateWhiteTexture(whiteError))
        {
            outError = whiteError;
            return Entry{};
        }
    }

    // 텍스처가 없는 재질 슬롯은 흰색으로 대신한다 — 호출부가 분기하지 않게.
    if (nullptr == texture) return m_white;

    // 키는 주소가 아니라 자산 신원이다(멤버 선언부 주석 참고).
    const HashedGuid assetId = texture->m_assetId;

    const auto found = m_descriptions.find(assetId);
    if (found != m_descriptions.end())
    {
        ++m_stats.hits;
        // 쓰였다고 찍는다(③). 이 값이 은퇴 판정의 전부다.
        const auto resident = m_entries.find(assetId);
        if (resident != m_entries.end()) resident->second.lastUsedFrame = m_frameIndex;
        return found->second;
    }

    // ── 파일에서 읽은 CPU 픽셀로 올린다 (T1 · T4에서 유일 경로) ──
    //
    // 로더가 압축까지 끝낸 이미지를 남겨 둔다(Texture::m_cpuPixels).
    //
    // ★ 소유권을 가져가지 않는다(2026-08-08 정정). 예전에는 TakeCpuPixels로
    //   move해 왔는데, 그 전제인 "한 번 올리면 캐시가 영원히 들고 있다"를
    //   ③(미사용 기반 은퇴)이 깼다. 은퇴 뒤 재요청에서 픽셀이 비어 있어
    //   재업로드가 실패했다(실측 3102건 · 화면에 흰색).
    const DirectX::ScratchImage* pixels = texture->GetCpuPixels();
    if (nullptr == pixels)
    {
        // ★ CPU 픽셀이 없는 텍스처가 여기 오면 그것 자체가 신호다.
        //
        //   지금 GetOrUpload를 부르는 곳 일곱은 전부 파일에서 읽은 텍스처를
        //   넘긴다(재질·데칼·아이콘·UI·블루노이즈·스카이박스 equirect).
        //   런타임에 만든 텍스처(렌더 타깃·지형 레이어 배열 등)가 이 경로로
        //   오면 DX11로 만들어진 것이고, 그건 T5·T6이 풀어야 할 문제이지
        //   여기서 DX11로 되읽어 덮을 일이 아니다(T4에서 그 폴백을 걷었다).
        //
        //   흰색을 돌려주고 통계에 남긴다 — 조용히 다른 그림이 나오는 것보다
        //   'failures가 늘었다'가 낫다.
        outError = "CPU 픽셀이 없어 DX12로 올릴 수 없다: " + texture->m_name;
        ++m_stats.failures;
        return m_white;
    }

    const std::wstring wideName(texture->m_name.begin(), texture->m_name.end());
    DX12PersistentHeap::Allocation allocation;
    Entry entry{};
    if (!UploadFromCpuPixels(*pixels, wideName.c_str(), allocation, entry, outError))
    {
        ++m_stats.failures;
        return m_white;
    }

    // 상주량은 풀이 예약한 실제 allocation 크기로 잰다. bytesUploaded가 쓰는
    // 풋프린트 총합은 스테이징 기준이라 행 정렬 패딩이 섞여 있다.
    const uint64_t residentBytes = allocation.allocationBytes;

    ++m_stats.uploads;
    ++m_stats.fromCpuPixels;
    ++m_stats.residentCount;
    m_stats.residentBytes += residentBytes;

    Resident resident{};
    resident.allocation = std::move(allocation);
    resident.bytes = residentBytes;
    resident.lastUsedFrame = m_frameIndex;
    resident.recordingId = m_resources->GetCurrentUploadRecordingId();
    resident.uploadState = RHIUploadTransactionState::Recording;
    m_entries.emplace(assetId, std::move(resident));
    m_descriptions.emplace(assetId, entry);
    return entry;
}

void DX12TextureCache::OnUploadSubmitted(uint64_t recordingId,
    RHICompletionPoint completion)
{
    for (auto& pair : m_entries)
    {
        Resident& resident = pair.second;
        if (resident.uploadState != RHIUploadTransactionState::Recording ||
            resident.recordingId != recordingId) continue;
        resident.completionValue = completion.value;
        resident.uploadState = completion.IsValid()
            ? RHIUploadTransactionState::Queued
            : RHIUploadTransactionState::Quarantined;
    }
    for (FallbackTransaction& transaction : m_fallbackTransactions)
    {
        if (transaction.state != RHIUploadTransactionState::Recording ||
            transaction.recordingId != recordingId) continue;
        transaction.completionValue = completion.value;
        transaction.state = completion.IsValid()
            ? RHIUploadTransactionState::Queued
            : RHIUploadTransactionState::Quarantined;
    }
}

void DX12TextureCache::OnUploadCompleted(uint64_t completedValue)
{
    for (auto& pair : m_entries)
    {
        Resident& resident = pair.second;
        if (resident.uploadState == RHIUploadTransactionState::Queued &&
            resident.completionValue <= completedValue)
            resident.uploadState = RHIUploadTransactionState::Resident;
    }
    for (FallbackTransaction& transaction : m_fallbackTransactions)
    {
        if (transaction.state == RHIUploadTransactionState::Queued &&
            transaction.completionValue <= completedValue)
            transaction.state = RHIUploadTransactionState::Resident;
    }
}

void DX12TextureCache::OnUploadAborted(uint64_t recordingId)
{
    auto it = m_entries.begin();
    while (it != m_entries.end())
    {
        Resident& resident = it->second;
        if (resident.uploadState != RHIUploadTransactionState::Recording ||
            resident.recordingId != recordingId)
        {
            ++it;
            continue;
        }

        const auto described = m_descriptions.find(it->first);
        if (described != m_descriptions.end())
        {
            m_resources->ReleaseTexture(described->second.handle);
            m_descriptions.erase(described);
        }
        m_persistentHeap.Release(resident.allocation);
        --m_stats.residentCount;
        m_stats.residentBytes -= resident.bytes;
        m_entries.erase(it);
    }

    auto transaction = m_fallbackTransactions.begin();
    while (transaction != m_fallbackTransactions.end())
    {
        if (transaction->state != RHIUploadTransactionState::Recording ||
            transaction->recordingId != recordingId)
        {
            ++transaction;
            continue;
        }

        const RHITextureHandle handle = transaction->handle;
        m_resources->ReleaseTexture(handle);
        if (m_white.handle.id == handle.id)
        {
            m_white = Entry{};
            m_persistentHeap.Release(m_whiteResource);
        }
        if (m_black.handle.id == handle.id)
        {
            m_black = Entry{};
            m_persistentHeap.Release(m_blackResource);
        }
        if (m_ormNeutral.handle.id == handle.id)
        {
            m_ormNeutral = Entry{};
            m_persistentHeap.Release(m_ormNeutralResource);
        }
        transaction = m_fallbackTransactions.erase(transaction);
    }
}

