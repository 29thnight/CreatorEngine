#ifndef DYNAMICCPP_EXPORTS
#include "VulkanResourceTable.h"
#include "VulkanFormat.h"
#include "VulkanResourceState.h"

using namespace VulkanApi;

void VulkanResourceTable::DestroyImage(VkDevice device, VulkanImageEntry& entry)
{
    if (VK_NULL_HANDLE == device) return;

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
    Slot<VulkanImageEntry>* const slot = Find(m_images, handle.id);
    if (nullptr == slot) return;   // 두 번 놓기 · 무효 핸들

    if (slot->owned) DestroyImage(device, slot->entry);
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
    Slot<VulkanBufferEntry>* const slot = Find(m_buffers, handle.id);
    if (nullptr == slot) return;

    if (slot->owned) DestroyBuffer(device, slot->entry);
    else             slot->entry = VulkanBufferEntry{};

    slot->alive = false;
    ++slot->generation;
    m_bufferFree.push_back(RHIHandleBits::SlotOf(handle.id));
}

void VulkanResourceTable::Shutdown(VkDevice device)
{
    for (auto& slot : m_images)
    {
        if (slot.alive && slot.owned) DestroyImage(device, slot.entry);
        else if (slot.alive) DestroyOwnedCubeView(device, slot.entry);
        slot.alive = false;
    }
    for (auto& slot : m_buffers)
    {
        if (slot.alive && slot.owned) DestroyBuffer(device, slot.entry);
        slot.alive = false;
    }

    m_images.clear();
    m_buffers.clear();
    m_imageFree.clear();
    m_bufferFree.clear();
}

#endif
