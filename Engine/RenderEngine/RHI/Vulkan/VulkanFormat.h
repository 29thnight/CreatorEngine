#pragma once
#include "VulkanLoader.h"

#include "../RHIFormat.h"

// RHIFormat ↔ VkFormat 대응 (V8-a).
//
// ★ DX12Format.h 가 "Vulkan 이 들어오면 옆에 VulkanFormat.h 가 생기고 상위는
//   어느 쪽도 모른다"고 적어 둔 자리다. 그 예고가 여기서 청구된다.
//
// ★ 되돌리는 방향(FromVulkan)을 두지 않는다. DX12 쪽에 그것이 있는 이유는
//   `GetDesc().Format` 으로 리소스에게 포맷을 되물을 수 있어서인데, Vulkan 은
//   `VkImage` 에게 포맷을 물을 방법이 없다 — 만든 쪽이 기억해야 한다.
//   호출자가 없는 함수를 두지 않는다는 규칙(RHIPipelineState.h)이 여기도 같다.

inline VkFormat ToVulkan(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::RGBA8Unorm:     return VK_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::RGBA8UnormSrgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case RHIFormat::RGBA16Float:    return VK_FORMAT_R16G16B16A16_SFLOAT;
    case RHIFormat::RGBA32Float:    return VK_FORMAT_R32G32B32A32_SFLOAT;
    case RHIFormat::RG16Float:      return VK_FORMAT_R16G16_SFLOAT;
    case RHIFormat::RG32Float:      return VK_FORMAT_R32G32_SFLOAT;
    case RHIFormat::RGB32Float:     return VK_FORMAT_R32G32B32_SFLOAT;
    case RHIFormat::R16Float:       return VK_FORMAT_R16_SFLOAT;
    case RHIFormat::R16Unorm:       return VK_FORMAT_R16_UNORM;
    case RHIFormat::R32Float:       return VK_FORMAT_R32_SFLOAT;
    case RHIFormat::R32Uint:        return VK_FORMAT_R32_UINT;
    case RHIFormat::D16Unorm:       return VK_FORMAT_D16_UNORM;
    case RHIFormat::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32Float:       return VK_FORMAT_D32_SFLOAT;
    case RHIFormat::D32FloatS8Uint: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    default:                        return VK_FORMAT_UNDEFINED;
    }
}

