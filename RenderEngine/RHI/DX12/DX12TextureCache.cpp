#ifndef DYNAMICCPP_EXPORTS
#include "DX12TextureCache.h"
#include "DX12DeviceResources.h"
#include "../../Texture.h"

#include <d3d11.h>

#include <sstream>
#include <vector>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string TextureCacheHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }
}

bool DX12TextureCache::Initialize(DX12DeviceResources* resources, ID3D11Device* dx11Device,
    ID3D11DeviceContext* dx11Context, std::string& outError)
{
    if (nullptr == resources || !resources->IsInitialized() ||
        nullptr == dx11Device || nullptr == dx11Context)
    {
        outError = "텍스처 캐시: 인자가 불완전하다";
        return false;
    }

    m_resources = resources;
    m_dx11Device = dx11Device;
    m_dx11Context = dx11Context;
    m_entries.clear();
    m_descriptions.clear();
    m_stats = Stats{};

    // 흰색 텍스처는 여기서 만들지 않는다. 만들려면 커맨드를 기록해야 하는데
    // Initialize는 프레임 밖에서 불릴 수 있다. 첫 사용 때(프레임 안) 만든다.
    return true;
}

void DX12TextureCache::Shutdown()
{
    m_entries.clear();
    m_descriptions.clear();
    m_dedicatedStaging.clear();
    m_whiteResource.Reset();
    m_white = Entry{};
    m_blackResource.Reset();
    m_black = Entry{};
    m_ormNeutralResource.Reset();
    m_ormNeutral = Entry{};
    m_resources = nullptr;
    m_dx11Device = nullptr;
    m_dx11Context = nullptr;
}

bool DX12TextureCache::CreateSolidTexture(const uint8_t rgba[4], const wchar_t* name,
    ComPtr<ID3D12Resource>& outResource, Entry& outEntry, std::string& outError)
{
    auto* device = m_resources->GetDevice();

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;

    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outResource));
    if (FAILED(hr))
    {
        outError = "기본 텍스처 생성 실패 " + TextureCacheHrToString(hr);
        return false;
    }
    outResource->SetName(name);

    const auto staging = m_resources->GetUploadRing().Allocate(
        D3D12_TEXTURE_DATA_PITCH_ALIGNMENT, DX12UploadRing::kTexturePlacementAlignment);
    if (!staging.IsValid())
    {
        outError = "기본 텍스처 업로드 링 할당 실패";
        return false;
    }
    memcpy(staging.cpuAddress, rgba, 4);

    auto* commandList = m_resources->GetCommandList();

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = outResource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = staging.resource;
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
    barrier.Transition.pResource = outResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    outEntry = Entry{};
    outEntry.resource = outResource.Get();
    outEntry.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    outEntry.width = 1;
    outEntry.height = 1;
    outEntry.mipLevels = 1;
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

bool DX12TextureCache::UploadFromDX11(ID3D11Texture2D* source, const D3D11_TEXTURE2D_DESC& sourceDesc,
    ComPtr<ID3D12Resource>& outResource, std::string& outError)
{
    auto* device = m_resources->GetDevice();

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = sourceDesc.Width;
    desc.Height = sourceDesc.Height;
    desc.DepthOrArraySize = static_cast<UINT16>(sourceDesc.ArraySize);
    desc.MipLevels = static_cast<UINT16>(sourceDesc.MipLevels);
    desc.Format = sourceDesc.Format;
    desc.SampleDesc.Count = 1;

    HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&outResource));
    if (FAILED(hr))
    {
        outError = "DX12 텍스처 생성 실패 " + TextureCacheHrToString(hr);
        return false;
    }

    // DX11 쪽 스테이징. 원본은 GPU 전용이라 CPU가 바로 읽을 수 없다.
    D3D11_TEXTURE2D_DESC stagingDesc = sourceDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> staging11;
    hr = m_dx11Device->CreateTexture2D(&stagingDesc, nullptr, &staging11);
    if (FAILED(hr))
    {
        outError = "DX11 스테이징 텍스처 생성 실패 " + TextureCacheHrToString(hr);
        return false;
    }

    m_dx11Context->CopyResource(staging11.Get(), source);
    m_dx11Context->Flush();

    // 밉과 배열 슬라이스를 전부 옮긴다. 압축 포맷(BC 계열)은 행이 4픽셀 블록이라
    // 직접 계산하면 틀리기 쉬우므로 GetCopyableFootprints에 맡긴다 — 그쪽이
    // 포맷별 규칙을 안다.
    const uint32_t subresourceCount = sourceDesc.MipLevels * sourceDesc.ArraySize;

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresourceCount);
    std::vector<UINT>   rowCounts(subresourceCount);
    std::vector<UINT64> rowSizes(subresourceCount);
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, subresourceCount, 0,
        footprints.data(), rowCounts.data(), rowSizes.data(), &totalBytes);

    // 스테이징 선택 — 링에 들어가면 링, 아니면 1회용 업로드 버퍼.
    //
    // 링 용량(프레임당 16MB)을 넘는 텍스처가 실재한다 — 4K HDR equirect가
    // 128MB다(실측). 그때는 전용 버퍼를 만들어 나르고, GPU가 복사를 끝낼
    // 때까지 캐시가 붙들고 있는다(ReleaseStagingBuffers 참고). 링이 그
    // 프레임의 다른 업로드로 붐벼 거절한 경우도 같은 경로가 받아 낸다.
    DX12UploadRing::Allocation ringAllocation{};
    ComPtr<ID3D12Resource> dedicatedStaging;
    uint8_t* destinationBase = nullptr;
    ID3D12Resource* stagingResource = nullptr;
    uint64_t stagingBaseOffset = 0;

    if (totalBytes <= m_resources->GetUploadRing().GetBytesPerFrame())
    {
        ringAllocation = m_resources->GetUploadRing().Allocate(
            totalBytes, DX12UploadRing::kTexturePlacementAlignment);
    }

    if (ringAllocation.IsValid())
    {
        destinationBase = static_cast<uint8_t*>(ringAllocation.cpuAddress);
        stagingResource = ringAllocation.resource;
        stagingBaseOffset = ringAllocation.offset;
    }
    else
    {
        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = totalBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,
            &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&dedicatedStaging));
        if (FAILED(hr))
        {
            outError = "대형 텍스처 스테이징 생성 실패 (" + std::to_string(totalBytes)
                + "바이트) " + TextureCacheHrToString(hr);
            return false;
        }
        dedicatedStaging->SetName(L"DX12TextureCache.DedicatedStaging");

        void* mapped = nullptr;
        hr = dedicatedStaging->Map(0, nullptr, &mapped);
        if (FAILED(hr))
        {
            outError = "대형 텍스처 스테이징 Map 실패 " + TextureCacheHrToString(hr);
            return false;
        }
        destinationBase = static_cast<uint8_t*>(mapped);
        stagingResource = dedicatedStaging.Get();
        stagingBaseOffset = 0;
    }

    auto* commandList = m_resources->GetCommandList();

    for (uint32_t subresource = 0; subresource < subresourceCount; ++subresource)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = m_dx11Context->Map(staging11.Get(), subresource, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
        {
            outError = "DX11 스테이징 Map 실패 " + TextureCacheHrToString(hr);
            return false;
        }

        const auto& footprint = footprints[subresource];
        const uint32_t rows = rowCounts[subresource];
        const uint64_t rowSize = rowSizes[subresource];

        // 두 쪽의 행 간격이 다르다. DX11은 드라이버가 정한 값을, DX12는 256바이트
        // 정렬을 쓰므로 행 단위로 옮긴다.
        for (uint32_t row = 0; row < rows; ++row)
        {
            const auto* sourceRow = static_cast<const uint8_t*>(mapped.pData)
                + static_cast<size_t>(row) * mapped.RowPitch;
            auto* destinationRow = destinationBase + footprint.Offset
                + static_cast<size_t>(row) * footprint.Footprint.RowPitch;
            memcpy(destinationRow, sourceRow, static_cast<size_t>(rowSize));
        }

        m_dx11Context->Unmap(staging11.Get(), subresource);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = outResource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = subresource;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = stagingResource;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;
        src.PlacedFootprint.Offset += stagingBaseOffset;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    if (dedicatedStaging)
    {
        dedicatedStaging->Unmap(0, nullptr);
        m_dedicatedStaging.push_back(std::move(dedicatedStaging));
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    m_stats.bytesUploaded += totalBytes;
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

    const auto found = m_descriptions.find(texture);
    if (found != m_descriptions.end())
    {
        ++m_stats.hits;
        return found->second;
    }

    // m_pTexture가 비어 있는 텍스처가 있다. DirectXTex의 CreateShaderResourceView로
    // 만들면 SRV만 남고 텍스처 포인터는 채워지지 않는 경로가 있어서다 —
    // 실측으로 잡았다(재질 텍스처 업로드가 0건인데 검증은 통과했다).
    // SRV에서 리소스를 되찾는다.
    ComPtr<ID3D11Resource> sourceResource;
    if (nullptr != texture->m_pTexture)
    {
        sourceResource = texture->m_pTexture;
    }
    else if (nullptr != texture->m_pSRV)
    {
        texture->m_pSRV->GetResource(&sourceResource);
    }

    ComPtr<ID3D11Texture2D> source2D;
    if (sourceResource) sourceResource.As(&source2D);

    if (!source2D)
    {
        // 2D 텍스처가 아니거나(3D·버퍼) 아무것도 없다. 흰색으로 대신하되 알린다.
        outError = "텍스처에서 2D 리소스를 얻지 못했다: " + texture->m_name;
        ++m_stats.failures;
        return m_white;
    }

    D3D11_TEXTURE2D_DESC sourceDesc{};
    source2D->GetDesc(&sourceDesc);

    // 멀티샘플 텍스처는 그대로 복사할 수 없다. 렌더 타깃 계열이라 재질 경로에는
    // 오지 않지만, 조용히 이상한 것을 만드는 대신 흰색으로 대체하고 알린다.
    if (sourceDesc.SampleDesc.Count > 1)
    {
        outError = "멀티샘플 텍스처는 업로드하지 않는다: " + texture->m_name;
        ++m_stats.failures;
        return m_white;
    }

    ComPtr<ID3D12Resource> resource;
    if (!UploadFromDX11(source2D.Get(), sourceDesc, resource, outError))
    {
        ++m_stats.failures;
        return m_white;
    }

    const std::wstring wideName(texture->m_name.begin(), texture->m_name.end());
    resource->SetName(wideName.c_str());

    Entry entry{};
    entry.resource = resource.Get();
    entry.format = sourceDesc.Format;
    entry.width = sourceDesc.Width;
    entry.height = sourceDesc.Height;
    entry.mipLevels = sourceDesc.MipLevels;
    entry.arraySize = sourceDesc.ArraySize;
    entry.isCube = 0 != (sourceDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE);

    ++m_stats.uploads;
    m_entries.emplace(texture, std::move(resource));
    m_descriptions.emplace(texture, entry);
    return entry;
}

#endif
