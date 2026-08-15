#ifndef DYNAMICCPP_EXPORTS

#include "VulkanPersistentHeap.h"
#include "VulkanFormat.h"
#include "VulkanResourceState.h"

#include <algorithm>

using namespace VulkanApi;

namespace
{
    constexpr uint32_t kPersistentBufferClass = 1;
    constexpr uint32_t kPersistentOptimalImageClass = 2;

    std::string VulkanPersistentResult(VkResult result)
    {
        return std::to_string(static_cast<int>(result));
    }
}

VulkanPersistentHeap::Allocation::Allocation(Allocation&& other) noexcept
{
    *this = std::move(other);
}

VulkanPersistentHeap::Allocation& VulkanPersistentHeap::Allocation::operator=(
    Allocation&& other) noexcept
{
    if (this == &other) return *this;
    buffer = other.buffer;
    dedicatedMemory = other.dedicatedMemory;
    block = other.block;
    requestedBytes = other.requestedBytes;
    allocationBytes = other.allocationBytes;
    memoryTypeIndex = other.memoryTypeIndex;
    dedicated = other.dedicated;
    other.buffer = VK_NULL_HANDLE;
    other.dedicatedMemory = VK_NULL_HANDLE;
    other.block = {};
    other.requestedBytes = 0;
    other.allocationBytes = 0;
    other.memoryTypeIndex = UINT32_MAX;
    other.dedicated = false;
    return *this;
}

VulkanPersistentHeap::ImageAllocation::ImageAllocation(
    ImageAllocation&& other) noexcept
{
    *this = std::move(other);
}

VulkanPersistentHeap::ImageAllocation&
VulkanPersistentHeap::ImageAllocation::operator=(ImageAllocation&& other) noexcept
{
    if (this == &other) return *this;
    image = other.image;
    view = other.view;
    dedicatedMemory = other.dedicatedMemory;
    block = other.block;
    requestedBytes = other.requestedBytes;
    allocationBytes = other.allocationBytes;
    memoryTypeIndex = other.memoryTypeIndex;
    dedicated = other.dedicated;
    other.image = VK_NULL_HANDLE;
    other.view = VK_NULL_HANDLE;
    other.dedicatedMemory = VK_NULL_HANDLE;
    other.block = {};
    other.requestedBytes = 0;
    other.allocationBytes = 0;
    other.memoryTypeIndex = UINT32_MAX;
    other.dedicated = false;
    return *this;
}

bool VulkanPersistentHeap::Initialize(VkDevice device,
    VkPhysicalDevice physicalDevice, bool memoryBudgetSupported,
    RHIDeviceMemoryBudgetCoordinator* budgetCoordinator,
    std::string& outError,
    const RHIPersistentHeapConfig& config)
{
    Shutdown();
    if (VK_NULL_HANDLE == device || VK_NULL_HANDLE == physicalDevice)
    {
        outError = "Vulkan persistent heap에 device와 physical device가 필요하다";
        return false;
    }
    if (0 == config.defaultSegmentBytes || 0 == config.dedicatedThresholdBytes)
    {
        outError = "Vulkan persistent heap 설정 크기가 0이다";
        return false;
    }

    m_device = device;
    m_physicalDevice = physicalDevice;
    m_memoryBudgetSupported = memoryBudgetSupported;
    m_budgetCoordinator = budgetCoordinator;
    if (nullptr != m_budgetCoordinator)
        m_budgetOwner = m_budgetCoordinator->RegisterOwner();
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
    m_policy.Reset(config);
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; ++i)
    {
        if (0 == (m_memoryProperties.memoryTypes[i].propertyFlags &
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) continue;
        RefreshBudgetForMemoryType(i);
        break;
    }
    return true;
}

void VulkanPersistentHeap::Shutdown()
{
    if (VK_NULL_HANDLE != m_device)
    {
        for (const auto& pair : m_segments)
        {
            if (VK_NULL_HANDLE != pair.second.memory)
            {
                vkFreeMemory(m_device, pair.second.memory, nullptr);
                if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
                    m_budgetCoordinator->RecordRelease(m_budgetOwner,
                        pair.second.budgetDomain, pair.second.bytes);
            }
        }
    }
    m_segments.clear();
    if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
        m_budgetCoordinator->UnregisterOwner(m_budgetOwner);
    m_budgetCoordinator = nullptr;
    m_budgetOwner = 0;
    m_policy.Reset();
    m_softBudgetBytes = 0;
    m_memoryPressure = false;
    m_memoryBudgetSupported = false;
    m_budgetOverrideForTesting = false;
    m_lastMemoryTypeIndex = UINT32_MAX;
    m_memoryProperties = {};
    m_physicalDevice = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
}

uint32_t VulkanPersistentHeap::FindMemoryType(uint32_t typeBits,
    VkMemoryPropertyFlags required) const
{
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; ++i)
    {
        if (0 == (typeBits & (1u << i))) continue;
        if ((m_memoryProperties.memoryTypes[i].propertyFlags & required) == required)
            return i;
    }
    return UINT32_MAX;
}

RHIDeviceMemoryBudgetDomain VulkanPersistentHeap::BudgetDomain(
    uint32_t memoryTypeIndex) const
{
    return memoryTypeIndex < m_memoryProperties.memoryTypeCount
        ? m_memoryProperties.memoryTypes[memoryTypeIndex].heapIndex
        : 0;
}

bool VulkanPersistentHeap::CreateNativeBuffer(VkDeviceSize bytes,
    VkBufferUsageFlags usage, VkBuffer& outBuffer, std::string& outError) const
{
    outBuffer = VK_NULL_HANDLE;
    VkBufferCreateInfo info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    info.size = bytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    const VkResult created = vkCreateBuffer(m_device, &info, nullptr, &outBuffer);
    if (VK_SUCCESS == created) return true;
    outError = "Vulkan persistent vkCreateBuffer 실패 " +
        VulkanPersistentResult(created);
    return false;
}

bool VulkanPersistentHeap::CreateNativeImage(const RHITextureDesc& desc,
    VkImage& outImage, std::string& outError) const
{
    outImage = VK_NULL_HANDLE;
    const VkFormat format = ToVulkan(desc.format);
    if (VK_FORMAT_UNDEFINED == format)
    {
        outError = "Vulkan persistent texture 포맷 대응이 없다";
        return false;
    }

    const bool is3D = RHITextureDesc::Dim::Texture3D == desc.dim;
    VkImageCreateInfo info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = is3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { desc.width, desc.height, is3D ? desc.depthOrArraySize : 1u };
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = is3D ? 1u : desc.depthOrArraySize;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!is3D && 0 != desc.depthOrArraySize &&
        0 == (desc.depthOrArraySize % 6u))
        info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    const VkResult created = vkCreateImage(m_device, &info, nullptr, &outImage);
    if (VK_SUCCESS == created) return true;
    outError = "Vulkan persistent vkCreateImage 실패 " +
        VulkanPersistentResult(created);
    return false;
}

bool VulkanPersistentHeap::CreateNativeSegment(uint64_t compatibilityKey,
    uint32_t memoryTypeIndex, uint64_t bytes,
    RHIPersistentHeapSegmentHandle& outHandle,
    std::string& outError)
{
    outHandle = {};
    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.allocationSize = bytes;
    allocate.memoryTypeIndex = memoryTypeIndex;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkResult result = vkAllocateMemory(m_device, &allocate, nullptr, &memory);
    if (VK_SUCCESS != result)
    {
        outError = "Vulkan persistent segment allocation 실패 " +
            VulkanPersistentResult(result);
        return false;
    }

    outHandle = m_policy.AddSegment(compatibilityKey, bytes);
    if (!outHandle.IsValid())
    {
        vkFreeMemory(m_device, memory, nullptr);
        outError = "Vulkan persistent heap policy segment 등록 실패";
        return false;
    }
    m_segments.emplace(NativeKey(outHandle), NativeSegment{
        memory, BudgetDomain(memoryTypeIndex), bytes });
    return true;
}

bool VulkanPersistentHeap::BindDedicated(VkBuffer buffer,
    const VkMemoryRequirements2& requirements, uint32_t memoryTypeIndex,
    bool fallback, Allocation& outAllocation, std::string& outError)
{
    VkMemoryDedicatedAllocateInfo dedicated{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicated.buffer = buffer;

    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.pNext = &dedicated;
    allocate.allocationSize = requirements.memoryRequirements.size;
    allocate.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(m_device, &allocate, nullptr, &memory);
    if (VK_SUCCESS != result)
    {
        m_policy.RecordAllocationFailure();
        outError = "Vulkan persistent dedicated allocation 실패 " +
            VulkanPersistentResult(result);
        return false;
    }

    result = vkBindBufferMemory(m_device, buffer, memory, 0);
    if (VK_SUCCESS != result)
    {
        vkFreeMemory(m_device, memory, nullptr);
        m_policy.RecordAllocationFailure();
        outError = "Vulkan persistent dedicated bind 실패 " +
            VulkanPersistentResult(result);
        return false;
    }

    outAllocation.buffer = buffer;
    outAllocation.dedicatedMemory = memory;
    outAllocation.allocationBytes = requirements.memoryRequirements.size;
    outAllocation.memoryTypeIndex = memoryTypeIndex;
    outAllocation.dedicated = true;
    m_policy.RecordDedicatedAllocation(requirements.memoryRequirements.size, fallback);
    if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
        m_budgetCoordinator->RecordAllocation(m_budgetOwner,
            BudgetDomain(memoryTypeIndex), requirements.memoryRequirements.size);
    return true;
}

bool VulkanPersistentHeap::BindDedicated(VkImage image,
    const VkMemoryRequirements2& requirements, uint32_t memoryTypeIndex,
    bool fallback, ImageAllocation& outAllocation, std::string& outError)
{
    VkMemoryDedicatedAllocateInfo dedicated{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
    dedicated.image = image;

    VkMemoryAllocateInfo allocate{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocate.pNext = &dedicated;
    allocate.allocationSize = requirements.memoryRequirements.size;
    allocate.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(m_device, &allocate, nullptr, &memory);
    if (VK_SUCCESS != result)
    {
        m_policy.RecordAllocationFailure();
        outError = "Vulkan persistent image dedicated allocation 실패 " +
            VulkanPersistentResult(result);
        return false;
    }

    result = vkBindImageMemory(m_device, image, memory, 0);
    if (VK_SUCCESS != result)
    {
        vkFreeMemory(m_device, memory, nullptr);
        m_policy.RecordAllocationFailure();
        outError = "Vulkan persistent image dedicated bind 실패 " +
            VulkanPersistentResult(result);
        return false;
    }

    outAllocation.image = image;
    outAllocation.dedicatedMemory = memory;
    outAllocation.allocationBytes = requirements.memoryRequirements.size;
    outAllocation.memoryTypeIndex = memoryTypeIndex;
    outAllocation.dedicated = true;
    m_policy.RecordDedicatedAllocation(requirements.memoryRequirements.size,
        fallback);
    if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
        m_budgetCoordinator->RecordAllocation(m_budgetOwner,
            BudgetDomain(memoryTypeIndex), requirements.memoryRequirements.size);
    return true;
}

bool VulkanPersistentHeap::CreateImageView(const RHITextureDesc& desc,
    ImageAllocation& allocation, std::string& outError) const
{
    const bool is3D = RHITextureDesc::Dim::Texture3D == desc.dim;
    VkImageViewCreateInfo view{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = allocation.image;
    view.viewType = is3D ? VK_IMAGE_VIEW_TYPE_3D
        : (1 < desc.depthOrArraySize ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
            : VK_IMAGE_VIEW_TYPE_2D);
    view.format = ToVulkan(desc.format);
    view.subresourceRange.aspectMask = AspectOf(desc.format);
    view.subresourceRange.levelCount = desc.mipLevels;
    view.subresourceRange.layerCount = is3D ? 1u : desc.depthOrArraySize;
    const VkResult result = vkCreateImageView(m_device, &view, nullptr,
        &allocation.view);
    if (VK_SUCCESS == result) return true;
    outError = "Vulkan persistent vkCreateImageView 실패 " +
        VulkanPersistentResult(result);
    return false;
}

void VulkanPersistentHeap::NameBuffer(VkBuffer buffer, const wchar_t* name) const
{
    if (VK_NULL_HANDLE == buffer || nullptr == name ||
        nullptr == vkSetDebugUtilsObjectNameEXT) return;
    std::string narrow;
    for (const wchar_t* cursor = name; L'\0' != *cursor; ++cursor)
        narrow.push_back(static_cast<char>(*cursor));

    VkDebugUtilsObjectNameInfoEXT info{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    info.objectType = VK_OBJECT_TYPE_BUFFER;
    info.objectHandle = reinterpret_cast<uint64_t>(buffer);
    info.pObjectName = narrow.c_str();
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

void VulkanPersistentHeap::NameImage(VkImage image, const wchar_t* name) const
{
    if (VK_NULL_HANDLE == image || nullptr == name ||
        nullptr == vkSetDebugUtilsObjectNameEXT) return;
    std::string narrow;
    for (const wchar_t* cursor = name; L'\0' != *cursor; ++cursor)
        narrow.push_back(static_cast<char>(*cursor));

    VkDebugUtilsObjectNameInfoEXT info{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    info.objectType = VK_OBJECT_TYPE_IMAGE;
    info.objectHandle = reinterpret_cast<uint64_t>(image);
    info.pObjectName = narrow.c_str();
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

void VulkanPersistentHeap::RefreshBudgetForMemoryType(uint32_t memoryTypeIndex)
{
    if (m_budgetOverrideForTesting ||
        memoryTypeIndex >= m_memoryProperties.memoryTypeCount) return;

    m_lastMemoryTypeIndex = memoryTypeIndex;
    const uint32_t heapIndex = BudgetDomain(memoryTypeIndex);
    RHIPersistentHeapBudget heapBudget{};
    bool coordinatorPressure = false;
    if (nullptr != m_budgetCoordinator)
    {
        const RHIDeviceMemoryBudgetDecision decision =
            m_budgetCoordinator->GetDecision(heapIndex);
        heapBudget = decision.budget;
        coordinatorPressure = decision.memoryPressure;
    }
    else
    {
        VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
        VkPhysicalDeviceMemoryProperties2 properties{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
        if (m_memoryBudgetSupported) properties.pNext = &budget;
        vkGetPhysicalDeviceMemoryProperties2(m_physicalDevice, &properties);
        if (heapIndex < properties.memoryProperties.memoryHeapCount)
        {
            if (m_memoryBudgetSupported && 0 != budget.heapBudget[heapIndex])
            {
                heapBudget.usageBytes = budget.heapUsage[heapIndex];
                heapBudget.budgetBytes = budget.heapBudget[heapIndex];
            }
            else
            {
                heapBudget.budgetBytes =
                    properties.memoryProperties.memoryHeaps[heapIndex].size;
                heapBudget.estimated = true;
            }
        }
    }

    const RHIPersistentHeapBudgetDecision decision =
        m_policy.UpdateBudget(heapBudget);
    m_softBudgetBytes = decision.softBudgetBytes;
    m_memoryPressure = nullptr != m_budgetCoordinator
        ? coordinatorPressure : decision.memoryPressure;
}

void VulkanPersistentHeap::RefreshBudget()
{
    if (UINT32_MAX != m_lastMemoryTypeIndex)
        RefreshBudgetForMemoryType(m_lastMemoryTypeIndex);
}

RHIPersistentHeapStats VulkanPersistentHeap::GetStats() const
{
    RHIPersistentHeapStats stats = m_policy.GetStats();
    if (m_budgetOverrideForTesting)
    {
        stats.softBudgetBytes = m_softBudgetBytes;
        stats.memoryPressure = m_memoryPressure;
    }
    return stats;
}

bool VulkanPersistentHeap::CreateBuffer(VkDeviceSize bytes,
    VkBufferUsageFlags usage, const wchar_t* debugName,
    Allocation& outAllocation, std::string& outError)
{
    outAllocation = {};
    if (VK_NULL_HANDLE == m_device || 0 == bytes)
    {
        outError = "Vulkan persistent heap가 초기화되지 않았거나 크기가 0이다";
        return false;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    if (!CreateNativeBuffer(bytes, usage, buffer, outError)) return false;

    VkMemoryDedicatedRequirements dedicatedRequirements{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
    VkMemoryRequirements2 requirements{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    requirements.pNext = &dedicatedRequirements;
    VkBufferMemoryRequirementsInfo2 requirementsInfo{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
    requirementsInfo.buffer = buffer;
    vkGetBufferMemoryRequirements2(m_device, &requirementsInfo, &requirements);

    const uint32_t memoryTypeIndex = FindMemoryType(
        requirements.memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (UINT32_MAX == memoryTypeIndex)
    {
        vkDestroyBuffer(m_device, buffer, nullptr);
        m_policy.RecordAllocationFailure();
        outError = "Vulkan persistent device-local memory type을 찾지 못했다";
        return false;
    }
    RefreshBudgetForMemoryType(memoryTypeIndex);
    const uint64_t compatibilityKey = CompatibilityKey(memoryTypeIndex,
        kPersistentBufferClass);

    const uint64_t requiredBytes = requirements.memoryRequirements.size;
    const bool driverDedicated = dedicatedRequirements.requiresDedicatedAllocation ||
        dedicatedRequirements.prefersDedicatedAllocation;
    if (driverDedicated || m_policy.ShouldUseDedicated(requiredBytes))
    {
        const bool result = BindDedicated(buffer, requirements, memoryTypeIndex,
            false, outAllocation, outError);
        if (!result) vkDestroyBuffer(m_device, buffer, nullptr);
        else
        {
            outAllocation.requestedBytes = bytes;
            NameBuffer(buffer, debugName);
        }
        return result;
    }

    RHIPersistentHeapAllocation block = m_policy.Allocate(compatibilityKey,
        requiredBytes, requirements.memoryRequirements.alignment);
    bool fallback = false;
    if (!block.IsValid())
    {
        if (m_memoryPressure) TrimEmptySegments(true);
        const uint64_t segmentBytes = m_policy.ChooseSegmentBytes(requiredBytes,
            requirements.memoryRequirements.alignment);
        const RHIPersistentHeapStats stats = m_policy.GetStats();
        const bool overBudget = 0 != m_softBudgetBytes &&
            (segmentBytes > m_softBudgetBytes ||
                stats.segmentBytes > m_softBudgetBytes - segmentBytes);
        if (0 == segmentBytes || overBudget)
        {
            fallback = true;
        }
        else
        {
            RHIDeviceMemoryGrowthTicket ticket{};
            if (nullptr != m_budgetCoordinator)
            {
                ticket = m_budgetCoordinator->TryReserveGrowth(m_budgetOwner,
                    BudgetDomain(memoryTypeIndex), segmentBytes);
                if (!ticket.IsValid()) fallback = true;
            }
            RHIPersistentHeapSegmentHandle segment;
            std::string segmentError;
            if (!fallback && !CreateNativeSegment(compatibilityKey, memoryTypeIndex,
                segmentBytes, segment, segmentError))
            {
                if (nullptr != m_budgetCoordinator)
                    m_budgetCoordinator->CancelGrowth(ticket);
                fallback = true;
            }
            else if (!fallback)
            {
                if (nullptr != m_budgetCoordinator)
                    m_budgetCoordinator->CommitGrowth(ticket);
                block = m_policy.Allocate(compatibilityKey, requiredBytes,
                    requirements.memoryRequirements.alignment);
                if (!block.IsValid()) fallback = true;
            }
        }
    }

    if (fallback || !block.IsValid())
    {
        const bool result = BindDedicated(buffer, requirements, memoryTypeIndex,
            true, outAllocation, outError);
        if (!result) vkDestroyBuffer(m_device, buffer, nullptr);
        else
        {
            outAllocation.requestedBytes = bytes;
            NameBuffer(buffer, debugName);
        }
        return result;
    }

    const auto native = m_segments.find(NativeKey(block.segment));
    if (native == m_segments.end())
    {
        m_policy.Release(block);
        vkDestroyBuffer(m_device, buffer, nullptr);
        m_policy.RecordAllocationFailure();
        outError = "Vulkan persistent native segment를 찾지 못했다";
        return false;
    }

    const VkResult bound = vkBindBufferMemory(m_device, buffer, native->second.memory,
        block.offset);
    if (VK_SUCCESS != bound)
    {
        m_policy.Release(block);
        vkDestroyBuffer(m_device, buffer, nullptr);
        if (!CreateNativeBuffer(bytes, usage, buffer, outError)) return false;
        const bool result = BindDedicated(buffer, requirements, memoryTypeIndex,
            true, outAllocation, outError);
        if (!result) vkDestroyBuffer(m_device, buffer, nullptr);
        else
        {
            outAllocation.requestedBytes = bytes;
            NameBuffer(buffer, debugName);
        }
        return result;
    }

    outAllocation.buffer = buffer;
    outAllocation.block = block;
    outAllocation.requestedBytes = bytes;
    outAllocation.allocationBytes = requiredBytes;
    outAllocation.memoryTypeIndex = memoryTypeIndex;
    NameBuffer(buffer, debugName);
    return true;
}

bool VulkanPersistentHeap::CreateTexture(const RHITextureDesc& desc,
    ImageAllocation& outAllocation, std::string& outError)
{
    outAllocation = {};
    if (VK_NULL_HANDLE == m_device || 0 == desc.width || 0 == desc.height ||
        0 == desc.depthOrArraySize || 0 == desc.mipLevels)
    {
        outError = "Vulkan persistent texture desc가 비었거나 heap가 초기화되지 않았다";
        return false;
    }
    if (desc.allowRenderTarget || desc.allowDepthStencil ||
        desc.allowUnorderedAccess)
    {
        outError = "Vulkan texture pool은 sampled non-RT/DS/UAV texture만 받는다";
        return false;
    }

    VkImage image = VK_NULL_HANDLE;
    if (!CreateNativeImage(desc, image, outError)) return false;

    VkMemoryDedicatedRequirements dedicatedRequirements{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
    VkMemoryRequirements2 requirements{ VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
    requirements.pNext = &dedicatedRequirements;
    VkImageMemoryRequirementsInfo2 requirementsInfo{
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
    requirementsInfo.image = image;
    vkGetImageMemoryRequirements2(m_device, &requirementsInfo, &requirements);

    const uint32_t memoryTypeIndex = FindMemoryType(
        requirements.memoryRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (UINT32_MAX == memoryTypeIndex)
    {
        vkDestroyImage(m_device, image, nullptr);
        m_policy.RecordAllocationFailure();
        outError = "Vulkan persistent texture device-local memory type을 찾지 못했다";
        return false;
    }
    RefreshBudgetForMemoryType(memoryTypeIndex);
    const uint64_t compatibilityKey = CompatibilityKey(memoryTypeIndex,
        kPersistentOptimalImageClass);
    const uint64_t requiredBytes = requirements.memoryRequirements.size;
    const bool driverDedicated = dedicatedRequirements.requiresDedicatedAllocation ||
        dedicatedRequirements.prefersDedicatedAllocation;

    bool made = false;
    if (driverDedicated || m_policy.ShouldUseDedicated(requiredBytes))
    {
        made = BindDedicated(image, requirements, memoryTypeIndex, false,
            outAllocation, outError);
        if (!made) vkDestroyImage(m_device, image, nullptr);
    }
    else
    {
        RHIPersistentHeapAllocation block = m_policy.Allocate(compatibilityKey,
            requiredBytes, requirements.memoryRequirements.alignment);
        bool fallback = false;
        if (!block.IsValid())
        {
            if (m_memoryPressure) TrimEmptySegments(true);
            const uint64_t segmentBytes = m_policy.ChooseSegmentBytes(
                requiredBytes, requirements.memoryRequirements.alignment);
            const RHIPersistentHeapStats stats = m_policy.GetStats();
            const bool overBudget = 0 != m_softBudgetBytes &&
                (segmentBytes > m_softBudgetBytes ||
                    stats.segmentBytes > m_softBudgetBytes - segmentBytes);
            if (0 == segmentBytes || overBudget)
            {
                fallback = true;
            }
            else
            {
                RHIDeviceMemoryGrowthTicket ticket{};
                if (nullptr != m_budgetCoordinator)
                {
                    ticket = m_budgetCoordinator->TryReserveGrowth(m_budgetOwner,
                        BudgetDomain(memoryTypeIndex), segmentBytes);
                    if (!ticket.IsValid()) fallback = true;
                }
                RHIPersistentHeapSegmentHandle segment;
                std::string segmentError;
                if (!fallback && !CreateNativeSegment(compatibilityKey, memoryTypeIndex,
                    segmentBytes, segment, segmentError))
                {
                    if (nullptr != m_budgetCoordinator)
                        m_budgetCoordinator->CancelGrowth(ticket);
                    fallback = true;
                }
                else if (!fallback)
                {
                    if (nullptr != m_budgetCoordinator)
                        m_budgetCoordinator->CommitGrowth(ticket);
                    block = m_policy.Allocate(compatibilityKey, requiredBytes,
                        requirements.memoryRequirements.alignment);
                    if (!block.IsValid()) fallback = true;
                }
            }
        }

        if (fallback || !block.IsValid())
        {
            made = BindDedicated(image, requirements, memoryTypeIndex, true,
                outAllocation, outError);
            if (!made) vkDestroyImage(m_device, image, nullptr);
        }
        else
        {
            const auto native = m_segments.find(NativeKey(block.segment));
            if (native == m_segments.end())
            {
                m_policy.Release(block);
                vkDestroyImage(m_device, image, nullptr);
                m_policy.RecordAllocationFailure();
                outError = "Vulkan persistent texture native segment를 찾지 못했다";
                return false;
            }

            const VkResult bound = vkBindImageMemory(m_device, image,
                native->second.memory, block.offset);
            if (VK_SUCCESS != bound)
            {
                m_policy.Release(block);
                vkDestroyImage(m_device, image, nullptr);
                image = VK_NULL_HANDLE;
                if (!CreateNativeImage(desc, image, outError)) return false;
                made = BindDedicated(image, requirements, memoryTypeIndex, true,
                    outAllocation, outError);
                if (!made) vkDestroyImage(m_device, image, nullptr);
            }
            else
            {
                outAllocation.image = image;
                outAllocation.block = block;
                outAllocation.allocationBytes = requiredBytes;
                outAllocation.memoryTypeIndex = memoryTypeIndex;
                made = true;
            }
        }
    }

    if (!made) return false;
    outAllocation.requestedBytes = requiredBytes;
    if (!CreateImageView(desc, outAllocation, outError))
    {
        Release(outAllocation);
        return false;
    }
    NameImage(outAllocation.image, desc.debugName);
    return true;
}

void VulkanPersistentHeap::Release(Allocation& allocation)
{
    if (!allocation.IsValid()) return;
    vkDestroyBuffer(m_device, allocation.buffer, nullptr);
    if (allocation.dedicated)
    {
        if (VK_NULL_HANDLE != allocation.dedicatedMemory)
            vkFreeMemory(m_device, allocation.dedicatedMemory, nullptr);
        m_policy.RecordDedicatedRelease();
        if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
            m_budgetCoordinator->RecordRelease(m_budgetOwner,
                BudgetDomain(allocation.memoryTypeIndex), allocation.allocationBytes);
    }
    else
    {
        m_policy.Release(allocation.block);
    }
    allocation = {};
}

void VulkanPersistentHeap::Release(ImageAllocation& allocation)
{
    if (!allocation.IsValid()) return;
    if (VK_NULL_HANDLE != allocation.view)
        vkDestroyImageView(m_device, allocation.view, nullptr);
    vkDestroyImage(m_device, allocation.image, nullptr);
    if (allocation.dedicated)
    {
        if (VK_NULL_HANDLE != allocation.dedicatedMemory)
            vkFreeMemory(m_device, allocation.dedicatedMemory, nullptr);
        m_policy.RecordDedicatedRelease();
        if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
            m_budgetCoordinator->RecordRelease(m_budgetOwner,
                BudgetDomain(allocation.memoryTypeIndex), allocation.allocationBytes);
    }
    else
    {
        m_policy.Release(allocation.block);
    }
    allocation = {};
}

uint64_t VulkanPersistentHeap::TrimEmptySegments(bool force)
{
    const RHIPersistentHeapConfig config = m_policy.GetConfig();
    const uint64_t trimmedBefore = m_policy.GetStats().trimmedBytes;
    const auto removed = m_policy.TrimEmptySegments(
        force ? 0u : config.standbySegmentCountPerKey);
    for (const auto& handle : removed)
    {
        const auto native = m_segments.find(NativeKey(handle));
        if (native == m_segments.end()) continue;
        vkFreeMemory(m_device, native->second.memory, nullptr);
        if (nullptr != m_budgetCoordinator && 0 != m_budgetOwner)
            m_budgetCoordinator->RecordRelease(m_budgetOwner,
                native->second.budgetDomain, native->second.bytes);
        m_segments.erase(native);
    }
    return m_policy.GetStats().trimmedBytes - trimmedBefore;
}

bool RunVulkanPersistentHeapSelfTest(VkDevice device,
    VkPhysicalDevice physicalDevice, bool memoryBudgetSupported,
    RHIDeviceMemoryBudgetCoordinator* budgetCoordinator,
    std::string& outLog)
{
    RHIPersistentHeapConfig config{};
    config.defaultSegmentBytes = 4ull * 1024ull * 1024ull;
    config.dedicatedThresholdBytes = 2ull * 1024ull * 1024ull;
    config.standbySegmentCountPerKey = 1;

    VulkanPersistentHeap heap;
    std::string error;
    if (!heap.Initialize(device, physicalDevice, memoryBudgetSupported,
        budgetCoordinator,
        error, config))
    {
        outLog += "[Vulkan persistent heap] 초기화 실패: " + error + "\n";
        return false;
    }

    constexpr VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VulkanPersistentHeap::Allocation first;
    VulkanPersistentHeap::Allocation second;
    bool passed = heap.CreateBuffer(64 * 1024, usage,
        L"PersistentHeapTest.First", first, error) &&
        heap.CreateBuffer(64 * 1024, usage,
            L"PersistentHeapTest.Second", second, error);
    passed = passed && !first.dedicated && !second.dedicated &&
        first.block.segment == second.block.segment &&
        first.block.offset != second.block.offset;

    RHITextureDesc textureDesc{};
    textureDesc.width = 64;
    textureDesc.height = 64;
    textureDesc.format = RHIFormat::RGBA8Unorm;
    textureDesc.debugName = L"PersistentHeapTest.Texture";
    VulkanPersistentHeap::ImageAllocation textureA;
    VulkanPersistentHeap::ImageAllocation textureB;
    passed = passed && heap.CreateTexture(textureDesc, textureA, error) &&
        heap.CreateTexture(textureDesc, textureB, error) &&
        !textureA.dedicated && !textureB.dedicated &&
        VK_NULL_HANDLE != textureA.view && VK_NULL_HANDLE != textureB.view &&
        textureA.block.segment == textureB.block.segment &&
        textureA.block.segment != first.block.segment;

    VulkanPersistentHeap peerHeap;
    VulkanPersistentHeap::Allocation peerBuffer;
    const uint64_t grantsBeforePeer = nullptr != budgetCoordinator
        ? budgetCoordinator->GetStats().growthGrants : 0;
    const bool peerInitialized = peerHeap.Initialize(device, physicalDevice,
        memoryBudgetSupported, budgetCoordinator, error, config);
    const bool peerAllocated = peerInitialized && peerHeap.CreateBuffer(64 * 1024,
        usage, L"PersistentHeapTest.Peer", peerBuffer, error);
    const RHIDeviceMemoryBudgetCoordinatorStats sharedStats = nullptr != budgetCoordinator
        ? budgetCoordinator->GetStats() : RHIDeviceMemoryBudgetCoordinatorStats{};
    passed = passed && peerAllocated && !peerBuffer.dedicated &&
        (nullptr == budgetCoordinator || (sharedStats.registeredOwners >= 2 &&
            sharedStats.growthGrants >= grantsBeforePeer + 1));
    peerHeap.Release(peerBuffer);
    peerHeap.TrimEmptySegments(true);
    peerHeap.Shutdown();

    heap.Release(first);
    heap.Release(second);
    heap.Release(textureA);
    heap.Release(textureB);
    heap.TrimEmptySegments(true);
    passed = passed && 0 == heap.GetStats().activeSegments;

    heap.SetBudgetForTesting(1, true);
    VulkanPersistentHeap::Allocation fallback;
    passed = passed && heap.CreateBuffer(64 * 1024, usage,
        L"PersistentHeapTest.Fallback", fallback, error) && fallback.dedicated;
    heap.Release(fallback);
    const RHIPersistentHeapStats stats = heap.GetStats();
    passed = passed && 1 <= stats.segmentCreates && 1 <= stats.coalesces &&
        1 <= stats.trimmedSegments && 1 <= stats.dedicatedFallbacks &&
        0 == stats.livePooledAllocations && 0 == stats.liveDedicatedAllocations;

    heap.Shutdown();
    outLog += passed
        ? "[Vulkan persistent heap] buffer/image compatibility·bound suballocation·병합·empty trim·memory-budget 단일 snapshot·multi-owner ticket·dedicated fallback 검증 통과\n"
        : "[Vulkan persistent heap] backend 검증 실패: " + error + "\n";
    return passed;
}

#endif
