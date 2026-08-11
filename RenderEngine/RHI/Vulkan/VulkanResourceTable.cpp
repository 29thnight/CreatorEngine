#ifndef DYNAMICCPP_EXPORTS
#include "VulkanResourceTable.h"

using namespace VulkanApi;

void VulkanResourceTable::DestroyImage(VkDevice device, VulkanImageEntry& entry)
{
    if (VK_NULL_HANDLE == device) return;

    // ★ 순서가 있다. 뷰가 이미지에 붙은 객체라 이미지보다 먼저 죽어야 하고,
    //   메모리는 이미지가 놓인 뒤라야 자유롭다. DX12 는 ComPtr 한 번이라
    //   이 순서를 생각할 자리가 없었다 — 그것이 칸이 셋을 함께 드는 이유다.
    if (VK_NULL_HANDLE != entry.view)   vkDestroyImageView(device, entry.view, nullptr);
    if (VK_NULL_HANDLE != entry.image)  vkDestroyImage(device, entry.image, nullptr);
    if (VK_NULL_HANDLE != entry.memory) vkFreeMemory(device, entry.memory, nullptr);

    entry = VulkanImageEntry{};
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
    else             slot->entry = VulkanImageEntry{};

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
