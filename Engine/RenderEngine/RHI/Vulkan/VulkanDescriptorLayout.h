#pragma once
#include "VulkanLoader.h"

#include <array>
#include <cstdint>

// The five descriptor kinds supported by the RHI's single-set binding model.
// Counts belong to the complete layout, including immutable samplers, so pool
// growth can accommodate one complete set instead of retrying undersized pages.
inline constexpr std::array<VkDescriptorType, 5> VulkanDescriptorTypes{
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    VK_DESCRIPTOR_TYPE_SAMPLER,
};
using VulkanDescriptorCounts = std::array<uint32_t, VulkanDescriptorTypes.size()>;

struct VulkanDescriptorRequirements
{
    VulkanDescriptorCounts counts{};
    bool indexingLimits{ false };
};
