#include "VulkanResourceTable.h"
#include "VulkanFormat.h"
#include "VulkanResourceState.h"
#include "../RHIResourceTypes.h"

using namespace VulkanApi;

bool DescribeVulkanImageView(const VulkanImageEntry& image, const RHIBindingDesc& binding,
                             VulkanImageViewEntry& description)
{
    if (!image.IsValid())
    {
        return false;
    }
    description.format = binding.format == RHIFormat::Unknown ? image.format : binding.format;
    if (image.format == RHIFormat::D32Float && description.format == RHIFormat::R32Float &&
        binding.kind == RHIBindingDesc::Kind::ShaderResource)
    {
        description.format = image.format;
    }
    if (description.format != image.format)
    {
        return false;
    }
    RHITextureSubresourceRange requested;
    requested.aspect = RHIFormatIsDepth(image.format) ? RHITextureAspect::Depth : RHITextureAspect::Color;
    const uint32_t layers = image.is3D ? 1u : image.depthOrArraySize;
    if (binding.dim == RHIBindingDesc::Dim::Default)
    {
        description.type = image.is3D   ? VK_IMAGE_VIEW_TYPE_3D
                           : layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                        : VK_IMAGE_VIEW_TYPE_2D;
    }
    else
    {
        requested.firstMip = binding.mostDetailedMip;
        requested.mipCount = binding.mipLevels;
        requested.firstLayer = binding.firstSlice;
        requested.layerCount = binding.dim == RHIBindingDesc::Dim::TextureCube      ? 6u
                               : binding.dim == RHIBindingDesc::Dim::Texture2DArray ? binding.sliceCount
                                                                                    : 1u;
        switch (binding.dim)
        {
        case RHIBindingDesc::Dim::Texture2D:
            if (image.is3D)
            {
                return false;
            }
            description.type = VK_IMAGE_VIEW_TYPE_2D;
            break;
        case RHIBindingDesc::Dim::Texture2DArray:
            if (image.is3D)
            {
                return false;
            }
            description.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
            break;
        case RHIBindingDesc::Dim::Texture3D:
            if (!image.is3D)
            {
                return false;
            }
            description.type = VK_IMAGE_VIEW_TYPE_3D;
            break;
        case RHIBindingDesc::Dim::TextureCube:
            if (image.is3D || image.width != image.height || binding.firstSlice % 6 != 0 ||
                binding.kind != RHIBindingDesc::Kind::ShaderResource)
            {
                return false;
            }
            description.type = VK_IMAGE_VIEW_TYPE_CUBE;
            break;
        default:
            return false;
        }
    }
    return ResolveRHITextureSubresourceRange(requested, image.mipLevels, layers, RHITextureAspects(image.format),
                                             description.range);
}

VkImageView ResolveVulkanImageView(const VulkanImageEntry& image, const RHIBindingDesc& binding)
{
    VulkanImageViewEntry requested;
    if (!DescribeVulkanImageView(image, binding, requested))
    {
        return VK_NULL_HANDLE;
    }
    const VkImageViewType defaultType = image.is3D                   ? VK_IMAGE_VIEW_TYPE_3D
                                        : image.depthOrArraySize > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                                                     : VK_IMAGE_VIEW_TYPE_2D;
    if (requested.type == defaultType &&
        IsWholeRHITextureRange(requested.range, image.mipLevels, image.is3D ? 1u : image.depthOrArraySize,
                               RHITextureAspects(image.format)))
    {
        return image.view;
    }
    if (requested.type == VK_IMAGE_VIEW_TYPE_CUBE && image.cubeView != VK_NULL_HANDLE &&
        image.cubeViewFormat == requested.format && image.cubeViewBaseMip == requested.range.firstMip &&
        image.cubeViewMipLevels == requested.range.mipCount && image.cubeViewFirstSlice == requested.range.firstLayer)
    {
        return image.cubeView;
    }
    if (image.imageViews)
    {
        for (const VulkanImageViewEntry& view : image.imageViews->entries)
        {
            if (view.type == requested.type && view.format == requested.format && view.range == requested.range)
            {
                return view.view;
            }
        }
    }
    return VK_NULL_HANDLE;
}

VkImageView VulkanResourceTable::GetOrCreateImageView(VkDevice device, const RHIBindingDesc& binding)
{
    const std::lock_guard lock(m_imageRegistryMutex);
    Slot<VulkanImageEntry>* const slot = Find(m_images, binding.resource.id);
    if (!slot || device == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }
    VulkanImageEntry& image = slot->entry;
    if (const VkImageView found = ResolveVulkanImageView(image, binding); found != VK_NULL_HANDLE)
    {
        return found;
    }
    VulkanImageViewEntry view;
    if (!DescribeVulkanImageView(image, binding, view))
    {
        return VK_NULL_HANDLE;
    }
    auto cache = image.imageViews ? own::make_shared<VulkanImageViewCache>(*image.imageViews)
                                  : own::make_shared<VulkanImageViewCache>();
    cache->entries.reserve(cache->entries.size() + 1);
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image.image;
    info.viewType = view.type;
    info.format = ToVulkan(view.format);
    info.subresourceRange = ToVulkanSubresourceRange(view.range, image.mipLevels,
                                                     image.is3D ? 1u : image.depthOrArraySize, image.format, true);
    if (vkCreateImageView(device, &info, nullptr, &view.view) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    cache->entries.push_back(view);
    image.imageViews = std::move(cache);
    return view.view;
}

void VulkanResourceTable::DestroyImage(VkDevice device, VulkanImageEntry& entry)
{
    if (VK_NULL_HANDLE == device) return;
    if (entry.imageViews)
    {
        for (const VulkanImageViewEntry& view : entry.imageViews->entries)
        {
            vkDestroyImageView(device, view.view, nullptr);
        }
        entry.imageViews.reset();
    }

    // ★ 순서가 있다. 뷰가 이미지에 붙은 객체라 이미지보다 먼저 죽어야 하고,
    //   메모리는 이미지가 놓인 뒤라야 자유롭다. DX12 는 ComPtr 한 번이라
    //   이 순서를 생각할 자리가 없었다 — 그것이 칸이 셋을 함께 드는 이유다.
    if (VK_NULL_HANDLE != entry.cubeView) vkDestroyImageView(device, entry.cubeView, nullptr);
    if (VK_NULL_HANDLE != entry.view)     vkDestroyImageView(device, entry.view, nullptr);
    if (VK_NULL_HANDLE != entry.image)  vkDestroyImage(device, entry.image, nullptr);
    if (VK_NULL_HANDLE != entry.memory) vkFreeMemory(device, entry.memory, nullptr);

    entry = VulkanImageEntry{};
}

void VulkanResourceTable::DestroyOwnedCubeView(VkDevice device, VulkanImageEntry& entry)
{
    if (device != VK_NULL_HANDLE && entry.imageViews)
    {
        for (const VulkanImageViewEntry& view : entry.imageViews->entries)
        {
            vkDestroyImageView(device, view.view, nullptr);
        }
        entry.imageViews.reset();
    }
    if (VK_NULL_HANDLE != device && entry.ownsCubeView &&
        VK_NULL_HANDLE != entry.cubeView)
    {
        vkDestroyImageView(device, entry.cubeView, nullptr);
    }
    entry.cubeView = VK_NULL_HANDLE;
    entry.ownsCubeView = false;
}

VkImageView VulkanResourceTable::GetOrCreateCubeView(VkDevice device,
    RHITextureHandle handle, RHIFormat format, uint32_t baseMip,
    uint32_t mipLevels, uint32_t firstSlice)
{
    const std::lock_guard lock(m_imageRegistryMutex);
    Slot<VulkanImageEntry>* const slot = Find(m_images, handle.id);
    if (nullptr == slot || VK_NULL_HANDLE == device) return VK_NULL_HANDLE;

    VulkanImageEntry& entry = slot->entry;
    const RHIFormat resolvedFormat = (RHIFormat::Unknown == format) ? entry.format : format;
    if (VK_NULL_HANDLE != entry.cubeView)
    {
        // 지금은 SkyBox가 요구한 큐브 뷰 하나만 캐시한다. 다른 부분 뷰를
        // 같은 실물로 조용히 돌려주지 않는다 — 다음 소비자가 생기면 이 칸을
        // 키 기반 뷰 캐시로 넓힐 자리다.
        return entry.cubeViewFormat == resolvedFormat &&
            entry.cubeViewBaseMip == baseMip &&
            entry.cubeViewMipLevels == mipLevels &&
            entry.cubeViewFirstSlice == firstSlice
            ? entry.cubeView : VK_NULL_HANDLE;
    }

    // Vulkan 큐브 뷰는 정확히 여섯 레이어 단위여야 한다. 생성 시점에 같은
    // 조건으로 CUBE_COMPATIBLE 플래그를 켰지만, 외부 리소스도 표에 올 수
    // 있으므로 여기서도 입력을 검증한다.
    if (!entry.IsValid() || resolvedFormat != entry.format || 0 == mipLevels ||
        baseMip >= entry.mipLevels || mipLevels > entry.mipLevels - baseMip ||
        firstSlice >= entry.depthOrArraySize ||
        0 != (firstSlice % 6) || entry.depthOrArraySize - firstSlice < 6)
    {
        return VK_NULL_HANDLE;
    }

    VkImageViewCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    info.image = entry.image;
    info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    info.format = ToVulkan(resolvedFormat);
    info.subresourceRange.aspectMask = AspectOf(resolvedFormat);
    info.subresourceRange.baseMipLevel = baseMip;
    info.subresourceRange.levelCount = mipLevels;
    info.subresourceRange.baseArrayLayer = firstSlice;
    info.subresourceRange.layerCount = 6;

    if (VK_SUCCESS != vkCreateImageView(device, &info, nullptr, &entry.cubeView))
    {
        entry.cubeView = VK_NULL_HANDLE;
    }
    else
    {
        entry.cubeViewFormat = resolvedFormat;
        entry.cubeViewBaseMip = baseMip;
        entry.cubeViewMipLevels = mipLevels;
        entry.cubeViewFirstSlice = firstSlice;
        entry.ownsCubeView = true;
    }
    return entry.cubeView;
}

void VulkanResourceTable::DestroyBuffer(VkDevice device, VulkanBufferEntry& entry)
{
    if (VK_NULL_HANDLE == device) return;

    // 매핑해 둔 것은 풀기 전에 내린다. GPU 가 읽는 중이 아니어야 한다는 것은
    // 부르는 쪽의 계약이다(표는 펜스를 보지 않는다).
    if (nullptr != entry.mapped && VK_NULL_HANDLE != entry.memory)
    {
        vkUnmapMemory(device, entry.memory);
        entry.mapped = nullptr;
    }

    if (VK_NULL_HANDLE != entry.buffer) vkDestroyBuffer(device, entry.buffer, nullptr);
    if (VK_NULL_HANDLE != entry.memory) vkFreeMemory(device, entry.memory, nullptr);

    entry = VulkanBufferEntry{};
}

void VulkanResourceTable::Release(VkDevice device, RHITextureHandle handle)
{
    const std::lock_guard lock(m_imageRegistryMutex);
    Slot<VulkanImageEntry>* const slot = Find(m_images, handle.id);
    if (nullptr == slot) return;   // 두 번 놓기 · 무효 핸들

    if (slot->m_lease.use_count() > 1)
    {
        m_pendingImages.push_back(*slot);
        slot->entry = {};
        slot->m_lease.reset();
    }
    else if (slot->owned)
    {
        DestroyImage(device, slot->entry);
    }
    else
    {
        // 이미지·기본 뷰는 외부 소유여도, 이 표가 지연 생성한 큐브 뷰는
        // 표의 것이다. 소유권을 한 덩어리로 보아 그냥 비우면 그 뷰가 샌다.
        DestroyOwnedCubeView(device, slot->entry);
        slot->entry = VulkanImageEntry{};
    }

    slot->alive = false;
    ++slot->generation;
    m_imageFree.push_back(RHIHandleBits::SlotOf(handle.id));
}

void VulkanResourceTable::Release(VkDevice device, RHIBufferHandle handle)
{
    if (!handle.IsValid()) return;
    std::lock_guard lock(m_bufferRegistryMutex);
    const uint32_t index = RHIHandleBits::SlotOf(handle.id);
    if (index >= m_bufferHighWater.load(std::memory_order_relaxed)) return;
    Slot<VulkanBufferEntry>* const slot = &m_buffers[index];
    if (!slot->alive ||
        slot->generation != RHIHandleBits::GenerationOf(handle.id)) return;

    if (slot->m_lease.use_count() > 1)
    {
        m_pendingBuffers.push_back(*slot);
        slot->entry = {};
        slot->m_lease.reset();
    }
    else if (slot->owned)
    {
        DestroyBuffer(device, slot->entry);
    }
    else
    {
        slot->entry = VulkanBufferEntry{};
    }

    slot->alive = false;
    ++slot->generation;
    m_bufferFree.push_back(index);
    m_liveBuffers.fetch_sub(1, std::memory_order_relaxed);
}

void VulkanResourceTable::CollectRecordingLeases(VkDevice device)
{
    const std::scoped_lock lock(m_imageRegistryMutex, m_bufferRegistryMutex);
    const auto collect = [device](auto& entries, auto destroy)
    {
        size_t write = 0;
        for (auto& slot : entries)
        {
            if (slot.m_lease.use_count() == 1)
            {
                destroy(device, slot);
            }
            else
            {
                if (&slot != &entries[write])
                {
                    entries[write] = std::move(slot);
                }
                ++write;
            }
        }
        entries.resize(write);
    };
    collect(m_pendingImages,
            [](VkDevice device, auto& slot)
            {
                if (slot.owned)
                {
                    DestroyImage(device, slot.entry);
                }
                else
                {
                    DestroyOwnedCubeView(device, slot.entry);
                }
            });
    collect(m_pendingBuffers,
            [](VkDevice device, auto& slot)
            {
                if (slot.owned)
                {
                    DestroyBuffer(device, slot.entry);
                }
            });
}

void VulkanResourceTable::Shutdown(VkDevice device)
{
    CollectRecordingLeases(device);
    // Native owners cannot be torn down while a frozen recording still refers to them.
    if (!m_pendingImages.empty() || !m_pendingBuffers.empty())
    {
        std::terminate();
    }
    const std::scoped_lock lock(m_imageRegistryMutex, m_bufferRegistryMutex);
    for (auto& slot : m_images)
    {
        if (slot.m_lease.use_count() > 1)
        {
            std::terminate();
        }
        if (slot.alive && slot.owned) DestroyImage(device, slot.entry);
        else if (slot.alive) DestroyOwnedCubeView(device, slot.entry);
        slot.alive = false;
    }
    const uint32_t bufferCount = m_bufferHighWater.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < bufferCount; ++i)
    {
        auto& slot = m_buffers[i];
        if (slot.m_lease.use_count() > 1)
        {
            std::terminate();
        }
        if (slot.alive && slot.owned) DestroyBuffer(device, slot.entry);
        else slot.entry = VulkanBufferEntry{};
        slot.alive = false;
        slot.owned = false;
        slot.generation = 0;
    }

    m_images.clear();
    m_imageFree.clear();
    m_bufferFree.clear();
    m_bufferHighWater.store(0, std::memory_order_release);
    m_liveBuffers.store(0, std::memory_order_release);
    m_bufferSlotReuses.store(0, std::memory_order_release);
}

