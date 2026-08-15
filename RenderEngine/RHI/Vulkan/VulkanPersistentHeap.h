#pragma once

#ifndef DYNAMICCPP_EXPORTS

#include "VulkanLoader.h"
#include "../RHIPersistentHeapPolicy.h"
#include "../RHIDeviceMemoryBudgetCoordinator.h"
#include "../RHIResourceTypes.h"

#include <cstdint>
#include <string>
#include <unordered_map>

/// VkDeviceMemory segment + vkBindBufferMemory adapter.
class VulkanPersistentHeap
{
public:
    struct Allocation
    {
        VkBuffer buffer{ VK_NULL_HANDLE };
        VkDeviceMemory dedicatedMemory{ VK_NULL_HANDLE };
        RHIPersistentHeapAllocation block;
        VkDeviceSize requestedBytes{ 0 };
        VkDeviceSize allocationBytes{ 0 };
        uint32_t memoryTypeIndex{ UINT32_MAX };
        bool dedicated{ false };

        Allocation() = default;
        Allocation(Allocation&& other) noexcept;
        Allocation& operator=(Allocation&& other) noexcept;
        Allocation(const Allocation&) = delete;
        Allocation& operator=(const Allocation&) = delete;

        bool IsValid() const { return VK_NULL_HANDLE != buffer; }
    };

    struct ImageAllocation
    {
        VkImage image{ VK_NULL_HANDLE };
        VkImageView view{ VK_NULL_HANDLE };
        VkDeviceMemory dedicatedMemory{ VK_NULL_HANDLE };
        RHIPersistentHeapAllocation block;
        VkDeviceSize requestedBytes{ 0 };
        VkDeviceSize allocationBytes{ 0 };
        uint32_t memoryTypeIndex{ UINT32_MAX };
        bool dedicated{ false };

        ImageAllocation() = default;
        ImageAllocation(ImageAllocation&& other) noexcept;
        ImageAllocation& operator=(ImageAllocation&& other) noexcept;
        ImageAllocation(const ImageAllocation&) = delete;
        ImageAllocation& operator=(const ImageAllocation&) = delete;

        bool IsValid() const { return VK_NULL_HANDLE != image; }
    };

    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
        bool memoryBudgetSupported,
        RHIDeviceMemoryBudgetCoordinator* budgetCoordinator,
        std::string& outError,
        const RHIPersistentHeapConfig& config = RHIPersistentHeapConfig{});
    void Shutdown();

    bool CreateBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage,
        const wchar_t* debugName, Allocation& outAllocation,
        std::string& outError);
    bool CreateTexture(const RHITextureDesc& desc,
        ImageAllocation& outAllocation, std::string& outError);
    void Release(Allocation& allocation);
    void Release(ImageAllocation& allocation);

    uint64_t TrimEmptySegments(bool force);
    void RefreshBudget();
    bool IsMemoryPressure() const { return m_memoryPressure; }
    RHIPersistentHeapStats GetStats() const;

    void SetBudgetForTesting(uint64_t softBudgetBytes, bool memoryPressure)
    {
        m_softBudgetBytes = softBudgetBytes;
        m_memoryPressure = memoryPressure;
        m_budgetOverrideForTesting = true;
    }
    void ClearBudgetOverrideForTesting()
    {
        m_softBudgetBytes = 0;
        m_memoryPressure = false;
        m_budgetOverrideForTesting = false;
        RefreshBudget();
    }

private:
    static uint64_t NativeKey(const RHIPersistentHeapSegmentHandle& handle)
    {
        return (static_cast<uint64_t>(handle.generation) << 32) | handle.slot;
    }

    uint32_t FindMemoryType(uint32_t typeBits,
        VkMemoryPropertyFlags required) const;
    static uint64_t CompatibilityKey(uint32_t memoryTypeIndex,
        uint32_t resourceClass)
    {
        return (static_cast<uint64_t>(memoryTypeIndex) << 8) | resourceClass;
    }
    bool CreateNativeBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage,
        VkBuffer& outBuffer, std::string& outError) const;
    bool CreateNativeImage(const RHITextureDesc& desc, VkImage& outImage,
        std::string& outError) const;
    bool CreateNativeSegment(uint64_t compatibilityKey,
        uint32_t memoryTypeIndex, uint64_t bytes,
        RHIPersistentHeapSegmentHandle& outHandle, std::string& outError);
    bool BindDedicated(VkBuffer buffer, const VkMemoryRequirements2& requirements,
        uint32_t memoryTypeIndex, bool fallback, Allocation& outAllocation,
        std::string& outError);
    bool BindDedicated(VkImage image, const VkMemoryRequirements2& requirements,
        uint32_t memoryTypeIndex, bool fallback,
        ImageAllocation& outAllocation, std::string& outError);
    bool CreateImageView(const RHITextureDesc& desc,
        ImageAllocation& allocation, std::string& outError) const;
    RHIDeviceMemoryBudgetDomain BudgetDomain(uint32_t memoryTypeIndex) const;
    void RefreshBudgetForMemoryType(uint32_t memoryTypeIndex);
    void NameBuffer(VkBuffer buffer, const wchar_t* name) const;
    void NameImage(VkImage image, const wchar_t* name) const;

    VkDevice m_device{ VK_NULL_HANDLE };
    VkPhysicalDevice m_physicalDevice{ VK_NULL_HANDLE };
    VkPhysicalDeviceMemoryProperties m_memoryProperties{};
    RHIPersistentHeapPolicy m_policy;
    struct NativeSegment
    {
        VkDeviceMemory memory{ VK_NULL_HANDLE };
        RHIDeviceMemoryBudgetDomain budgetDomain{ 0 };
        uint64_t bytes{ 0 };
    };
    std::unordered_map<uint64_t, NativeSegment> m_segments;
    RHIDeviceMemoryBudgetCoordinator* m_budgetCoordinator{ nullptr };
    RHIDeviceMemoryBudgetOwner m_budgetOwner{ 0 };
    uint64_t m_softBudgetBytes{ 0 };
    bool m_memoryPressure{ false };
    bool m_memoryBudgetSupported{ false };
    bool m_budgetOverrideForTesting{ false };
    uint32_t m_lastMemoryTypeIndex{ UINT32_MAX };
};

bool RunVulkanPersistentHeapSelfTest(VkDevice device,
    VkPhysicalDevice physicalDevice, bool memoryBudgetSupported,
    RHIDeviceMemoryBudgetCoordinator* budgetCoordinator,
    std::string& outLog);

#endif
