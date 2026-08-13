#ifndef DYNAMICCPP_EXPORTS
#include "VulkanDeviceResources.h"
#include "VulkanFormat.h"
#include "VulkanResourceState.h"
#include "../../Texture.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

using namespace VulkanApi;

// `IRenderDeviceServices` 의 Vulkan 구현 (5c-4c).
//
// ── 왜 파일을 가르는가 ──
//
// `VulkanDeviceResources.cpp` 가 이미 908줄이다. 여기 것은 성격도 다르다 —
// 저쪽은 디바이스·스왑체인·프레임(즉 `IRHIDeviceResources`)이고, 이쪽은
// 패스가 프레임 동안 쓰는 서비스다. 인터페이스가 둘이니 파일도 둘이다.

namespace
{
    /// 이름을 붙인다. `RHIBufferDesc::debugName` 이 `const wchar_t*` 라
    /// 좁히는데, 이름은 전부 ASCII 라 손실이 없다.
    ///
    /// ★ DX12 의 `SetName` 자리다. Vulkan 은 확장 함수라 없을 수 있고,
    ///   없으면 이름만 못 붙일 뿐 동작은 같다 — 조용히 넘어가도 되는
    ///   유일한 부류다(계약이 요구하는 것은 이름이지 이름의 전달 경로가
    ///   아니고, 못 붙였을 때 잘못 그려지지 않는다).
    void VkNameObject(VkDevice device, VkObjectType type, uint64_t handle,
        const wchar_t* name)
    {
        if (nullptr == name || nullptr == vkSetDebugUtilsObjectNameEXT) return;

        std::string narrow;
        for (const wchar_t* p = name; L'\0' != *p; ++p)
        {
            narrow.push_back((*p < 128) ? static_cast<char>(*p) : '?');
        }

        VkDebugUtilsObjectNameInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
        info.objectType = type;
        info.objectHandle = handle;
        info.pObjectName = narrow.c_str();
        vkSetDebugUtilsObjectNameEXT(device, &info);
    }
}

// ────────────────────────────────────────────────────────────── 자산 텍스처 캐시

struct VulkanTextureCache::Impl
{
    VulkanDeviceResources* resources{ nullptr };
    std::unordered_map<HashedGuid, RHITextureEntry> entries;
    RHITextureEntry white;
    RHITextureEntry black;
    RHITextureEntry ormNeutral;
    Stats stats;

    struct SourceFormat
    {
        RHIFormat format{ RHIFormat::Unknown };
        bool bgra{ false };
    };

    static SourceFormat FormatOf(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:      return { RHIFormat::RGBA8Unorm, false };
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return { RHIFormat::RGBA8UnormSrgb, false };
        // WIC PNG 로더는 FORCE_RGB가 없으면 흔히 BGRA8을 남긴다. Vulkan RHI
        // 어휘에는 BGRA 자산 포맷이 없으므로 업로드 때 채널을 RGBA로 바꾼다.
        case DXGI_FORMAT_B8G8R8A8_UNORM:      return { RHIFormat::RGBA8Unorm, true };
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return { RHIFormat::RGBA8UnormSrgb, true };
        default:                              return {};
        }
    }

    bool UploadRgba8(const DirectX::ScratchImage* image, const uint8_t* solid,
        const wchar_t* name, RHITextureEntry& outEntry, uint64_t& outBytes,
        std::string& outError)
    {
        outEntry = {};
        outBytes = 0;
        if (nullptr == resources)
        {
            outError = "Vulkan 텍스처 캐시가 초기화되지 않았다";
            return false;
        }

        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t mipLevels = 1;
        RHIFormat format = RHIFormat::RGBA8Unorm;
        bool sourceIsBgra = false;
        const DirectX::TexMetadata* metadata = nullptr;
        if (nullptr != image)
        {
            metadata = &image->GetMetadata();
            const SourceFormat sourceFormat = FormatOf(metadata->format);
            format = sourceFormat.format;
            sourceIsBgra = sourceFormat.bgra;
            if (RHIFormat::Unknown == format ||
                DirectX::TEX_DIMENSION_TEXTURE2D != metadata->dimension ||
                1 != metadata->arraySize || 0 == metadata->mipLevels ||
                metadata->width > UINT32_MAX || metadata->height > UINT32_MAX)
            {
                outError = "Vulkan GizmoIcon 슬라이스는 2D RGBA8 자산만 지원한다";
                return false;
            }
            width = static_cast<uint32_t>(metadata->width);
            height = static_cast<uint32_t>(metadata->height);
            mipLevels = static_cast<uint32_t>(metadata->mipLevels);
        }

        uint64_t totalBytes = 0;
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            const uint32_t mipWidth = (std::max)(1u, width >> mip);
            const uint32_t mipHeight = (std::max)(1u, height >> mip);
            totalBytes += static_cast<uint64_t>(mipWidth) * mipHeight * 4u;
        }

        const RHIBufferSlice upload = resources->AllocateUpload(totalBytes, 4);
        if (!upload.IsWritable())
        {
            outError = "Vulkan 텍스처 업로드 링 공간이 부족하다";
            return false;
        }

        RHITextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.mipLevels = mipLevels;
        desc.format = format;
        desc.debugName = name;
        if (!resources->CreateTexture(desc, outEntry.handle, outError)) return false;

        std::vector<VkBufferImageCopy> copies;
        copies.reserve(mipLevels);
        uint64_t localOffset = 0;
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            const uint32_t mipWidth = (std::max)(1u, width >> mip);
            const uint32_t mipHeight = (std::max)(1u, height >> mip);
            const size_t tightRow = static_cast<size_t>(mipWidth) * 4u;
            uint8_t* destination = static_cast<uint8_t*>(upload.cpuAddress) + localOffset;

            if (nullptr != image)
            {
                const DirectX::Image* source = image->GetImage(mip, 0, 0);
                if (nullptr == source || source->rowPitch < tightRow)
                {
                    resources->ReleaseTexture(outEntry.handle);
                    outEntry = {};
                    outError = "Vulkan 텍스처 밉 픽셀을 찾지 못했다";
                    return false;
                }
                for (uint32_t row = 0; row < mipHeight; ++row)
                {
                    uint8_t* destinationRow = destination + static_cast<size_t>(row) * tightRow;
                    const uint8_t* sourceRow = source->pixels +
                        static_cast<size_t>(row) * source->rowPitch;
                    if (!sourceIsBgra)
                    {
                        std::memcpy(destinationRow, sourceRow, tightRow);
                    }
                    else
                    {
                        for (uint32_t x = 0; x < mipWidth; ++x)
                        {
                            destinationRow[x * 4 + 0] = sourceRow[x * 4 + 2];
                            destinationRow[x * 4 + 1] = sourceRow[x * 4 + 1];
                            destinationRow[x * 4 + 2] = sourceRow[x * 4 + 0];
                            destinationRow[x * 4 + 3] = sourceRow[x * 4 + 3];
                        }
                    }
                }
            }
            else
            {
                std::memcpy(destination, solid, 4);
            }

            VkBufferImageCopy copy{};
            copy.bufferOffset = upload.offset + localOffset;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = mip;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = { mipWidth, mipHeight, 1 };
            copies.push_back(copy);
            localOffset += static_cast<uint64_t>(tightRow) * mipHeight;
        }

        const RHITransition toCopy{
            outEntry.handle, RHIResourceState::Common, RHIResourceState::CopyDest };
        resources->TransitionResources({ &toCopy, 1 });

        const VulkanBufferEntry buffer = resources->GetResourceTable().Resolve(upload.buffer);
        const VulkanImageEntry texture = resources->GetResourceTable().Resolve(outEntry.handle);
        if (!buffer.IsValid() || !texture.IsValid())
        {
            resources->ReleaseTexture(outEntry.handle);
            outEntry = {};
            outError = "Vulkan 텍스처 업로드 핸들을 풀지 못했다";
            return false;
        }

        vkCmdCopyBufferToImage(resources->GetCommandBuffer(), buffer.buffer, texture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(copies.size()), copies.data());

        const RHITransition toShader{
            outEntry.handle, RHIResourceState::CopyDest,
            RHIResourceState::PixelShaderResource };
        resources->TransitionResources({ &toShader, 1 });

        outEntry.format = format;
        outEntry.width = width;
        outEntry.height = height;
        outEntry.mipLevels = mipLevels;
        outEntry.arraySize = 1;
        outBytes = totalBytes;
        return true;
    }

    RHITextureEntry Solid(const uint8_t rgba[4], const wchar_t* name,
        RHITextureEntry& cache, std::string& outError)
    {
        if (cache.IsValid()) return cache;
        uint64_t bytes = 0;
        if (!UploadRgba8(nullptr, rgba, name, cache, bytes, outError)) ++stats.failures;
        return cache;
    }
};

VulkanTextureCache::VulkanTextureCache() : m_impl(std::make_unique<Impl>()) {}
VulkanTextureCache::~VulkanTextureCache() { Shutdown(); }

bool VulkanTextureCache::Initialize(VulkanDeviceResources* resources, std::string& outError)
{
    if (nullptr == resources || !resources->IsInitialized())
    {
        outError = "Vulkan 텍스처 캐시에 유효한 디바이스가 필요하다";
        return false;
    }
    m_impl->resources = resources;
    return true;
}

void VulkanTextureCache::Shutdown()
{
    if (!m_impl || nullptr == m_impl->resources) return;
    for (const auto& pair : m_impl->entries)
        m_impl->resources->ReleaseTexture(pair.second.handle);
    m_impl->resources->ReleaseTexture(m_impl->white.handle);
    m_impl->resources->ReleaseTexture(m_impl->black.handle);
    m_impl->resources->ReleaseTexture(m_impl->ormNeutral.handle);
    m_impl->entries.clear();
    m_impl->white = {};
    m_impl->black = {};
    m_impl->ormNeutral = {};
    m_impl->resources = nullptr;
}

RHITextureEntry VulkanTextureCache::GetOrUpload(Texture* texture, std::string& outError)
{
    if (nullptr == texture)
    {
        constexpr uint8_t white[4] = { 255, 255, 255, 255 };
        return m_impl->Solid(white, L"VulkanTexture.White", m_impl->white, outError);
    }

    const auto found = m_impl->entries.find(texture->m_assetId);
    if (m_impl->entries.end() != found)
    {
        ++m_impl->stats.hits;
        return found->second;
    }

    const DirectX::ScratchImage* pixels = texture->GetCpuPixels();
    if (nullptr == pixels)
    {
        ++m_impl->stats.failures;
        outError = "Texture에 CPU 픽셀이 없다";
        constexpr uint8_t white[4] = { 255, 255, 255, 255 };
        return m_impl->Solid(white, L"VulkanTexture.White", m_impl->white, outError);
    }

    RHITextureEntry entry;
    uint64_t bytes = 0;
    if (!m_impl->UploadRgba8(pixels, nullptr, L"VulkanTexture.Asset", entry, bytes, outError))
    {
        ++m_impl->stats.failures;
        constexpr uint8_t white[4] = { 255, 255, 255, 255 };
        return m_impl->Solid(white, L"VulkanTexture.White", m_impl->white, outError);
    }

    m_impl->entries.emplace(texture->m_assetId, entry);
    ++m_impl->stats.uploads;
    ++m_impl->stats.fromCpuPixels;
    m_impl->stats.bytesUploaded += bytes;
    return entry;
}

RHITextureEntry VulkanTextureCache::GetBlackTexture(std::string& outError)
{
    constexpr uint8_t black[4] = { 0, 0, 0, 255 };
    return m_impl->Solid(black, L"VulkanTexture.Black", m_impl->black, outError);
}

RHITextureEntry VulkanTextureCache::GetOrmNeutralTexture(std::string& outError)
{
    constexpr uint8_t orm[4] = { 255, 255, 0, 255 };
    return m_impl->Solid(orm, L"VulkanTexture.OrmNeutral", m_impl->ormNeutral, outError);
}

VulkanTextureCache::Stats VulkanTextureCache::GetStats() const
{
    return m_impl->stats;
}

// ────────────────────────────────────────────────────────────── 만드는 것

bool VulkanDeviceResources::CreateBuffer(const RHIBufferDesc& desc,
    RHIBufferHandle& outHandle, std::string& outError)
{
    outHandle = {};
    if (!IsInitialized()) { outError = "디바이스가 없다"; return false; }
    if (0 == desc.bytes)  { outError = "버퍼 크기가 0이다"; return false; }

    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = desc.bytes;

    // ★ **용도를 전부 켠다.** `RHIBufferDesc` 는 `allowUnorderedAccess` 하나만
    //   말하는데 Vulkan 은 생성 시점에 정확한 용도를 요구한다 — DX12 가
    //   버퍼의 쓰임을 뷰가 정하게 두기 때문에 계약에 그 어휘가 없다.
    //
    //   넓게 켜면 드라이버가 배치를 최적화할 여지를 잃지만 **틀리지는
    //   않는다**. 좁히려면 계약이 용도를 말해야 하고, 그것은 소비자가
    //   Vulkan 쪽에도 서고 나서 정할 일이다(§4.1). 지금 세어 둔다.
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VulkanBufferEntry entry{};
    entry.bytes = desc.bytes;

    VkResult made = vkCreateBuffer(m_device, &info, nullptr, &entry.buffer);
    if (VK_SUCCESS != made)
    {
        outError = "vkCreateBuffer 실패 — " + ResultToString(made);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, entry.buffer, &requirements);

    const uint32_t type = FindMemoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (UINT32_MAX == type)
    {
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "디바이스 로컬 메모리 타입을 찾지 못했다";
        return false;
    }

    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;

    made = vkAllocateMemory(m_device, &allocate, nullptr, &entry.memory);
    if (VK_SUCCESS != made)
    {
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "버퍼 메모리 할당 실패 — " + ResultToString(made);
        return false;
    }

    made = vkBindBufferMemory(m_device, entry.buffer, entry.memory, 0);
    if (VK_SUCCESS != made)
    {
        vkFreeMemory(m_device, entry.memory, nullptr);
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "버퍼 메모리 바인드 실패 — " + ResultToString(made);
        return false;
    }

    VkNameObject(m_device, VK_OBJECT_TYPE_BUFFER,
        reinterpret_cast<uint64_t>(entry.buffer), desc.debugName);

    outHandle = m_resourceTable.AddBuffer(entry);
    if (!outHandle.IsValid())
    {
        vkFreeMemory(m_device, entry.memory, nullptr);
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "핸들 표가 가득 찼다";
        return false;
    }

    // ★ `desc.initialState` 를 여기서 쓰지 않는다. 버퍼에는 레이아웃이 없고
    //   Vulkan 의 버퍼 배리어는 접근 마스크만 다루므로, 만든 직후에 걸 배리어가
    //   없다 — 첫 사용이 자기 배리어를 건다. DX12 가 초기 상태를 생성 인자로
    //   요구하는 것과 갈리는 자리이고, 계약은 안 바뀐다(한쪽이 무시하면 되는
    //   부류다 — `clearColor` 와 같다).
    return true;
}

bool VulkanDeviceResources::CreateTexture(const RHITextureDesc& desc,
    RHITextureHandle& outHandle, std::string& outError)
{
    outHandle = {};
    if (!IsInitialized())                { outError = "디바이스가 없다"; return false; }
    if (0 == desc.width || 0 == desc.height) { outError = "텍스처 크기가 0이다"; return false; }

    const VkFormat format = ToVulkan(desc.format);
    if (VK_FORMAT_UNDEFINED == format)
    {
        outError = "대응표에 없는 포맷이다";
        return false;
    }

    const bool is3D = (RHITextureDesc::Dim::Texture3D == desc.dim);
    const VkImageAspectFlags aspect = AspectOf(desc.format);

    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { desc.width, desc.height, is3D ? desc.depthOrArraySize : 1u };
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = is3D ? 1u : desc.depthOrArraySize;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
        | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (desc.allowUnorderedAccess) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (desc.allowRenderTarget)    info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.allowDepthStencil)    info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    // ★ **큐브가 계약에 없다.** DX12 는 배열 6장을 만들고 **뷰**가 큐브라고
    //   말하는데, Vulkan 은 이미지 **생성 플래그**를 요구한다 — 즉 만들 때
    //   이미 알아야 한다. `RHITextureDesc` 에 큐브 표시가 없는 것은 DX12 가
    //   그것을 요구하지 않았기 때문이고, 계약이 한쪽 API 의 모양을 그대로
    //   물려받은 자리다.
    //
    //   지금은 배열 길이가 6의 배수면 호환 플래그를 켠다. 플래그는 힌트라
    //   켜도 2D 배열로 계속 쓸 수 있으므로 **틀리지 않는 쪽**이고, 정답인지는
    //   실제 큐브 소비자(IBL — 슬라이스 7)가 설 때 답이 나온다.
    if (!is3D && 0 != desc.depthOrArraySize && 0 == (desc.depthOrArraySize % 6))
    {
        info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VulkanImageEntry entry{};
    entry.width = desc.width;
    entry.height = desc.height;
    entry.depthOrArraySize = desc.depthOrArraySize;
    entry.mipLevels = desc.mipLevels;
    entry.format = desc.format;
    entry.layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult made = vkCreateImage(m_device, &info, nullptr, &entry.image);
    if (VK_SUCCESS != made)
    {
        outError = "vkCreateImage 실패 — " + ResultToString(made);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, entry.image, &requirements);

    const uint32_t type = FindMemoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (UINT32_MAX == type)
    {
        vkDestroyImage(m_device, entry.image, nullptr);
        outError = "디바이스 로컬 메모리 타입을 찾지 못했다";
        return false;
    }

    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;

    made = vkAllocateMemory(m_device, &allocate, nullptr, &entry.memory);
    if (VK_SUCCESS != made)
    {
        vkDestroyImage(m_device, entry.image, nullptr);
        outError = "이미지 메모리 할당 실패 — " + ResultToString(made);
        return false;
    }

    made = vkBindImageMemory(m_device, entry.image, entry.memory, 0);
    if (VK_SUCCESS != made)
    {
        vkFreeMemory(m_device, entry.memory, nullptr);
        vkDestroyImage(m_device, entry.image, nullptr);
        outError = "이미지 메모리 바인드 실패 — " + ResultToString(made);
        return false;
    }

    // 기본 뷰. 칸이 이것을 들어야 렌더 타깃 표가 매 프레임 뷰를 만들지 않는다.
    VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = entry.image;
    view.viewType = is3D ? VK_IMAGE_VIEW_TYPE_3D
        : (1 < desc.depthOrArraySize ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D);
    view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = desc.mipLevels;
    view.subresourceRange.layerCount = is3D ? 1u : desc.depthOrArraySize;

    made = vkCreateImageView(m_device, &view, nullptr, &entry.view);
    if (VK_SUCCESS != made)
    {
        vkFreeMemory(m_device, entry.memory, nullptr);
        vkDestroyImage(m_device, entry.image, nullptr);
        outError = "vkCreateImageView 실패 — " + ResultToString(made);
        return false;
    }

    VkNameObject(m_device, VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(entry.image), desc.debugName);

    outHandle = m_resourceTable.AddImage(entry);
    if (!outHandle.IsValid())
    {
        vkDestroyImageView(m_device, entry.view, nullptr);
        vkFreeMemory(m_device, entry.memory, nullptr);
        vkDestroyImage(m_device, entry.image, nullptr);
        outError = "핸들 표가 가득 찼다";
        return false;
    }

    // ★ 초기 상태로 한 번 전이한다. Vulkan 이미지는 UNDEFINED 로 나므로
    //   **아무도 안 걸면 첫 사용이 잘못된 레이아웃을 읽는다** — DX12 는
    //   생성 인자가 그것을 해 줘서 호출부가 신경 쓸 일이 없던 자리다.
    //   프레임이 열려 있을 때만 할 수 있고, 아니면 첫 사용이 건다.
    if (RHIResourceState::Common != desc.initialState)
    {
        const RHITransition transition{ outHandle, RHIResourceState::Common, desc.initialState };
        TransitionResources({ &transition, 1 });
    }

    return true;
}

// ────────────────────────────────────────────────────────────── 되묻기·놓기

RHITextureInfo VulkanDeviceResources::DescribeTexture(RHITextureHandle handle) const
{
    // ★ 5c-4a 가 칸에 크기·포맷을 함께 둔 것이 여기서 값을 한다. 그때
    //   "Vulkan 은 이미지에게 자기 크기를 되물을 방법을 주지 않는다 · DX12 는
    //   `GetDesc()` 가 답하므로 이 필드가 없다 · **되묻는 길이 백엔드마다
    //   다르고, 그래서 `DescribeTexture` 가 계약에 있어야 했다**"고 적었고,
    //   그 예상이 그대로 청구된다.
    const VulkanImageEntry entry = m_resourceTable.Resolve(handle);
    if (!entry.IsValid()) return RHITextureInfo{};

    RHITextureInfo info{};
    info.width = entry.width;
    info.height = entry.height;
    info.depthOrArraySize = entry.depthOrArraySize;
    info.mipLevels = entry.mipLevels;
    info.format = entry.format;
    return info;
}

void VulkanDeviceResources::ReleaseTexture(RHITextureHandle handle)
{
    m_resourceTable.Release(m_device, handle);
}

// ────────────────────────────────────────────────────────────── 전이

void VulkanDeviceResources::TransitionResources(std::span<const RHITransition> transitions)
{
    if (!m_frameOpen || transitions.empty()) return;

    // ★ 배리어는 렌더링 밖이어야 한다 (5d 실측). DX12 는 ResourceBarrier 를
    //   아무 데서나 받지만 vkCmdPipelineBarrier2 는 동적 렌더링 안에서 금지다.
    //   그래프는 "패스 전이 → 패스 본문" 순서로 기록하는데, 앞 패스의 본문이
    //   렌더링을 연 채 끝났으면 다음 패스의 전이가 그 안에서 기록된다 —
    //   그래서 **전이가 스스로 닫는다.** 그래프에게 닫으라고 시키면 DX12 에
    //   없는 개념(여닫이)이 중립 코드로 새는 것이고, 인코더 소멸자가 닫는
    //   것과 같은 판단이 여기 한 번 더 있는 것이다.
    if (nullptr != m_encoder) m_encoder->EndRenderTargets();

    std::vector<VkImageMemoryBarrier2> barriers;
    barriers.reserve(transitions.size());

    for (const RHITransition& transition : transitions)
    {
        const VulkanImageEntry entry = m_resourceTable.Resolve(transition.texture);
        if (!entry.IsValid()) continue;

        const VulkanBarrierState before = ToVulkan(transition.before, true);
        const VulkanBarrierState after = ToVulkan(transition.after, false);

        // ★ **표가 아는 레이아웃을 쓰지 않는다.** 계약이 `before` 를 받는 이유가
        //   "백엔드가 리소스의 현재 상태를 추적하지 않는다 · 아는 쪽이 말한다"
        //   이고, 표의 `layout` 은 그 말과 어긋날 수 있는 두 번째 장부다.
        //   장부 둘을 두면 어긋나는 자리가 생긴다 — 표의 것은 **적어만 두고**
        //   판단에 쓰지 않는다(계수·디버깅용).
        VkImageMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        barrier.srcStageMask = before.stage;
        barrier.srcAccessMask = before.access;
        barrier.dstStageMask = after.stage;
        barrier.dstAccessMask = after.access;
        barrier.oldLayout = before.layout;
        barrier.newLayout = after.layout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = entry.image;
        barrier.subresourceRange.aspectMask = AspectOf(entry.format);
        barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
        barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

        barriers.push_back(barrier);
        m_resourceTable.SetLayout(transition.texture, after.layout);
    }

    if (barriers.empty()) return;

    // ★ **한 번에 묶어 건다.** 계약이 span 을 받는 이유이고(V3), DX12 도
    //   같은 이유로 배열을 받는다 — 배리어를 하나씩 걸면 GPU 가 사이마다
    //   멈춘다. 두 API 가 일치하는 자리다.
    VkDependencyInfo dependency{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dependency.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
    dependency.pImageMemoryBarriers = barriers.data();

    vkCmdPipelineBarrier2(m_commandBuffers[m_frameIndex], &dependency);
}

// ────────────────────────────────────────────────────────────── 렌더 타깃

RHIRenderTargetBinding VulkanDeviceResources::CreateRenderTargets(
    std::span<const RHITextureHandle> colors, const RHIDepthTargetDesc* depth)
{
    RHIRenderTargetBinding result{};
    if (!IsInitialized()) return result;
    if (colors.size() > VulkanRenderTargetBinding::kMaxColors) return result;

    VulkanRenderTargetBinding binding{};

    for (const RHITextureHandle handle : colors)
    {
        const VulkanImageEntry entry = m_resourceTable.Resolve(handle);

        // 계약: 색 리소스가 하나라도 널이면 invalid 다 — 호출부는 그것 하나만
        // 검사한다(`CreateBindings` 와 같은 규약).
        if (!entry.IsValid()) return result;

        binding.colorViews[binding.colorCount++] = entry.view;
        if (0 == binding.width) { binding.width = entry.width; binding.height = entry.height; }
    }

    if (nullptr != depth && depth->resource.IsValid())
    {
        const VulkanImageEntry entry = m_resourceTable.Resolve(depth->resource);
        if (!entry.IsValid()) return result;

        binding.depthView = ResolveDepthView(*depth, entry);
        if (VK_NULL_HANDLE == binding.depthView) return result;

        binding.depthReadOnly = depth->readOnly;
        if (0 == binding.width) { binding.width = entry.width; binding.height = entry.height; }
    }

    if (!binding.IsValid()) return result;

    result.backend = m_renderTargetTable.Add(binding);
    result.colorCount = binding.colorCount;
    result.hasDepth = binding.HasDepth();
    return result;
}

VkImageView VulkanDeviceResources::ResolveDepthView(const RHIDepthTargetDesc& desc,
    const VulkanImageEntry& entry)
{
    // 통째로 보는 경우는 칸의 기본 뷰를 빌린다 — 프레임마다 만들지 않는다.
    if (0 == desc.sliceCount) return entry.view;

    // ★ 배열 한 장만 묶는 경우(그림자 캐스케이드)는 부분 뷰가 필요하다.
    //   DX12 는 DSV 를 프레임 힙에 다시 만드는 것이 정상 비용인데 여기서는
    //   **객체 생성**이라, 표가 소유하고 프레임 끝에 놓는다. 캐시할지는
    //   실제 소비자(슬라이스 7)가 설 때 정한다.
    const RHIFormat viewFormat = (RHIFormat::Unknown != desc.format) ? desc.format : entry.format;

    VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    info.image = entry.image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    info.format = ToVulkan(viewFormat);
    info.subresourceRange.aspectMask = AspectOf(viewFormat);
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = desc.firstSlice;
    info.subresourceRange.layerCount = desc.sliceCount;

    VkImageView view = VK_NULL_HANDLE;
    if (VK_SUCCESS != vkCreateImageView(m_device, &info, nullptr, &view)) return VK_NULL_HANDLE;

    m_renderTargetTable.Own(view);
    return view;
}

// ────────────────────────────────────────────────────────────── 인코더

RHIEncoder& VulkanDeviceResources::GetImmediateEncoder()
{
    // ★ 커맨드 버퍼가 슬롯마다 다른 객체라 **갈아 끼운다**. `DX12Encoder` 는
    //   제자리 되감기(`ResetState`)로 힙 할당을 피하는데, 이쪽은 되감을 것이
    //   아니라 바꿀 것이라 그 최적화가 성립하지 않는다.
    //
    //   프레임당 한 번이면 무해하다. 프레임마다 여러 번 부르면 그때 재사용을
    //   넣는다 — 지금 넣으면 어떤 조건에서 갈아야 하는지를 소비자 없이 정한다.
    const VkCommandBuffer current = m_frameOpen
        ? m_commandBuffers[m_frameIndex] : VK_NULL_HANDLE;

    AccumulateEncoderDiagnostics();
    m_encoder = std::make_unique<VulkanEncoder>(
        current, m_pipelineCache, &m_resourceTable, &m_renderTargetTable,
        m_device, &m_descriptorPool, &m_bindingTable);
    return *m_encoder;
}

// ────────────────────────────────────────────────────────────── 업로드 (5c-4d)

RHIBufferSlice VulkanDeviceResources::AllocateUpload(uint64_t bytes, uint64_t alignment)
{
    return m_uploadRing.Allocate(bytes, alignment);
}

RHIBufferSlice VulkanDeviceResources::UploadConstants(const void* data, size_t bytes)
{
    // ★ 256 은 DX12 의 상수 버퍼 정렬이다. 그대로 요구하고 링이 **디바이스가
    //   요구하는 값과의 최댓값으로 넓힌다** — 계약이 "정렬은 용도가 정한다"고
    //   적어 둔 그 값이 여기서 백엔드에 흡수된다(`VulkanUploadRing` ★).
    const RHIBufferSlice slice = m_uploadRing.Allocate(bytes, 256);
    if (!slice.IsWritable()) return {};

    std::memcpy(slice.cpuAddress, data, bytes);
    return slice;
}

// ────────────────────────────────────────────────────────────── 동적 표

// SkyBox가 첫 소비자로 답을 냈다. CreateBindings는 셋 레이아웃을 모르므로
// 프레임 요청 표의 정수 슬롯을 돌려주고, SetBindings가 현재 파이프라인과
// 합쳐 실제 셋을 만든다. 동적 샘플러는 아직 소비자가 없어 계수로 남는다.

RHISamplerTable VulkanDeviceResources::CreateSamplers(std::span<const RHISamplerDesc>)
{
    NoteUnimplemented("CreateSamplers");     // 소비자 없음 — 슬라이스 7
    return {};
}

RHIBindingTable VulkanDeviceResources::CreateBindings(std::span<const RHIBindingDesc> descs)
{
    if (!m_frameOpen || descs.empty()) return {};

    // 요청은 아직 파이프라인 레이아웃을 모른다. 여기서는 리소스 수명과
    // 기본 종류만 검증하고, 레이아웃 종류·binding 번호는 SetBindings에서
    // 현재 파이프라인과 대조한다.
    for (const RHIBindingDesc& desc : descs)
    {
        if (RHIBindingDesc::Dim::Buffer == desc.dim)
        {
            if (!m_resourceTable.Resolve(desc.bufferResource).IsValid()) return {};
            continue;
        }

        if (!m_resourceTable.Resolve(desc.resource).IsValid())
        {
            // Vulkan의 널 디스크립터는 별도 기능(nullDescriptor)이 필요하다.
            // 그 기능을 켜지 않은 디바이스에서 가짜 널 뷰를 쓰지 않는다.
            if (desc.allowNull) NoteUnimplemented("CreateBindings(널 디스크립터 미지원)");
            return {};
        }
    }

    return m_bindingTable.Add(descs);
}

// ── 리드백 (5d 가 청구했다) ──
//
// ★ R6 로 미뤄 두며 "5d 가 청구하면 그때 연다 — 그 판단은 5d 가 무엇을
//   부르는지 세고 나서 한다"고 적었고, 셌더니 청구가 맞았다: vk.grid 는
//   dx12.grid 의 모양(CreateReadback → 그래프 안 CopyToReadback →
//   MapReadback)을 그대로 밟아야 **같은 자로 판정**할 수 있다. 검사 배관을
//   다르게 만들면 픽셀 대조가 대조가 아니게 된다.
//
// ★ 행 간격이 갈린다. DX12 는 256 정렬을 요구하는데
//   (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) vkCmdCopyImageToBuffer 에는 그
//   제약이 없다 — 촘촘히 담고 rowPitch 에 그 값을 적는다. 읽는 쪽
//   (RHIReadbackImage::At)은 rowPitch 를 계약으로 받으므로 아무것도 모른다.

namespace
{
    /// 리드백이 담을 수 있는 포맷의 픽셀 크기. `RHIReadbackImage::At` 가
    /// 디코드할 줄 아는 목록과 같은 집합이어야 한다 — 여기서 담고 저기서
    /// 못 읽으면 '전부 0'으로 조용히 나온다.
    uint32_t VkReadbackBytesPerPixel(RHIFormat format)
    {
        switch (format)
        {
        case RHIFormat::RGBA32Float:    return 16;
        case RHIFormat::RGBA16Float:    return 8;
        case RHIFormat::RG16Float:      return 4;
        case RHIFormat::R32Float:       return 4;
        case RHIFormat::R32Uint:        return 4;
        case RHIFormat::RGBA8Unorm:     return 4;
        case RHIFormat::RGBA8UnormSrgb: return 4;
        case RHIFormat::R16Float:       return 2;
        default:                        return 0;
        }
    }
}

bool VulkanDeviceResources::CreateReadback(uint32_t width, uint32_t height, RHIFormat format,
    uint32_t sliceCount, RHIReadback& outReadback, std::string& outError)
{
    outReadback = {};
    if (!IsInitialized()) { outError = "디바이스가 없다"; return false; }

    const uint32_t bytesPerPixel = VkReadbackBytesPerPixel(format);
    if (0 == bytesPerPixel)
    {
        outError = "리드백이 모르는 포맷이다";
        return false;
    }
    if (0 == width || 0 == height || 0 == sliceCount)
    {
        outError = "리드백 크기가 0이다";
        return false;
    }

    const uint32_t rowPitch = width * bytesPerPixel;
    const size_t sliceBytes = static_cast<size_t>(rowPitch) * height;
    const VkDeviceSize total = static_cast<VkDeviceSize>(sliceBytes) * sliceCount;

    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = total;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VulkanBufferEntry entry{};
    entry.bytes = total;

    VkResult made = vkCreateBuffer(m_device, &info, nullptr, &entry.buffer);
    if (VK_SUCCESS != made)
    {
        outError = "리드백 버퍼 생성 실패 — " + ResultToString(made);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, entry.buffer, &requirements);

    const uint32_t type = FindMemoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (UINT32_MAX == type)
    {
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "호스트 가시·일관 메모리 타입을 찾지 못했다";
        return false;
    }

    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = type;

    made = vkAllocateMemory(m_device, &allocate, nullptr, &entry.memory);
    if (VK_SUCCESS != made)
    {
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "리드백 메모리 할당 실패 — " + ResultToString(made);
        return false;
    }

    made = vkBindBufferMemory(m_device, entry.buffer, entry.memory, 0);
    if (VK_SUCCESS == made)
    {
        made = vkMapMemory(m_device, entry.memory, 0, VK_WHOLE_SIZE, 0, &entry.mapped);
    }
    if (VK_SUCCESS != made)
    {
        vkFreeMemory(m_device, entry.memory, nullptr);
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "리드백 메모리 바인드·매핑 실패 — " + ResultToString(made);
        return false;
    }

    outReadback.buffer = m_resourceTable.AddBuffer(entry);
    if (!outReadback.buffer.IsValid())
    {
        vkUnmapMemory(m_device, entry.memory);
        vkFreeMemory(m_device, entry.memory, nullptr);
        vkDestroyBuffer(m_device, entry.buffer, nullptr);
        outError = "핸들 표가 가득 찼다";
        return false;
    }

    outReadback.width = width;
    outReadback.height = height;
    outReadback.rowPitch = rowPitch;
    outReadback.format = format;
    outReadback.sliceCount = sliceCount;
    outReadback.sliceBytes = sliceBytes;
    return true;
}

bool VulkanDeviceResources::CreateBufferReadback(uint64_t, RHIReadback&, std::string& outError)
{
    // 소비자가 없다(vk.grid 는 텍스처 리드백만 밟는다). 슬라이스 7 의
    // Forward+ 대조가 첫 소비자다.
    NoteUnimplemented("CreateBufferReadback");
    outError = "Vulkan 버퍼 리드백은 아직 없다 (소비자 없음 — 슬라이스 7)";
    return false;
}

bool VulkanDeviceResources::MapReadback(const RHIReadback& readback,
    RHIReadbackImage& outImage, std::string& outError)
{
    outImage = {};

    const VulkanBufferEntry entry = m_resourceTable.Resolve(readback.buffer);
    if (!entry.IsValid() || nullptr == entry.mapped)
    {
        outError = "리드백 버퍼가 유효하지 않다";
        return false;
    }

    // 계속 매핑돼 있으므로 Map·Unmap 이 없다 — DX12 쪽과 이름이 같은 것은
    // 계약(제출·대기 뒤에 부른다)이 같아서다. HOST_COHERENT 라 캐시 무효화도
    // 필요 없다.
    outImage.data.resize(static_cast<size_t>(entry.bytes));
    std::memcpy(outImage.data.data(), entry.mapped, outImage.data.size());

    outImage.width = readback.width;
    outImage.height = readback.height;
    outImage.rowPitch = readback.rowPitch;
    outImage.format = readback.format;
    outImage.sliceCount = readback.sliceCount;
    outImage.sliceBytes = readback.sliceBytes;
    return true;
}

void VulkanDeviceResources::ReleaseReadback(RHIReadback& readback)
{
    m_resourceTable.Release(m_device, readback.buffer);
    readback = {};
}

#endif
