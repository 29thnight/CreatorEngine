#pragma once
#include <dxgiformat.h>

#include "../RHIFormat.h"

// RHIFormat ↔ DXGI 대응 (PHASE 3-1 재정의, V1).
//
// ★ 이 파일이 DX12 폴더에 있는 것이 요점이다. 대응표는 백엔드 사정이므로
//   Vulkan이 들어오면 DX12Format.h 옆에 VulkanFormat.h가 생기고, 상위는
//   어느 쪽도 모른다.
//
// ★ 왜 두 방향을 다 두는가: 상위 → 백엔드는 당연하고(뷰·리소스 생성),
//   백엔드 → 상위는 리소스가 스스로 아는 포맷을 되읽는 자리가 있어서다
//   (GetDesc().Format으로 받아 캡처 타입에 담는 리드백 경로).

inline DXGI_FORMAT ToDXGI(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::RGBA8Unorm:     return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::RGBA8UnormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHIFormat::RGBA16Float:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHIFormat::RGBA32Float:    return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHIFormat::RG16Float:      return DXGI_FORMAT_R16G16_FLOAT;
    case RHIFormat::RG32Float:      return DXGI_FORMAT_R32G32_FLOAT;
    case RHIFormat::RGB32Float:     return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHIFormat::R16Float:       return DXGI_FORMAT_R16_FLOAT;
    case RHIFormat::R16Unorm:       return DXGI_FORMAT_R16_UNORM;
    case RHIFormat::R32Float:       return DXGI_FORMAT_R32_FLOAT;
    case RHIFormat::R32Uint:        return DXGI_FORMAT_R32_UINT;
    case RHIFormat::D16Unorm:       return DXGI_FORMAT_D16_UNORM;
    case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32Float:       return DXGI_FORMAT_D32_FLOAT;
    case RHIFormat::D32FloatS8Uint: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case RHIFormat::RGBA8Uint:      return DXGI_FORMAT_R8G8B8A8_UINT;
    default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

inline RHIFormat FromDXGI(DXGI_FORMAT format)
{
    switch (format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:        return RHIFormat::RGBA8Unorm;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return RHIFormat::RGBA8UnormSrgb;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:    return RHIFormat::RGBA16Float;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:    return RHIFormat::RGBA32Float;
    case DXGI_FORMAT_R16G16_FLOAT:          return RHIFormat::RG16Float;
    case DXGI_FORMAT_R32G32_FLOAT:          return RHIFormat::RG32Float;
    case DXGI_FORMAT_R32G32B32_FLOAT:       return RHIFormat::RGB32Float;
    case DXGI_FORMAT_R16_FLOAT:             return RHIFormat::R16Float;
    case DXGI_FORMAT_R16_UNORM:             return RHIFormat::R16Unorm;
    case DXGI_FORMAT_R32_FLOAT:             return RHIFormat::R32Float;
    case DXGI_FORMAT_R32_UINT:              return RHIFormat::R32Uint;
    case DXGI_FORMAT_D16_UNORM:             return RHIFormat::D16Unorm;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:     return RHIFormat::D24UnormS8Uint;
    case DXGI_FORMAT_D32_FLOAT:             return RHIFormat::D32Float;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  return RHIFormat::D32FloatS8Uint;
    case DXGI_FORMAT_R8G8B8A8_UINT:         return RHIFormat::RGBA8Uint;
    default:                                return RHIFormat::Unknown;
    }
}

/// 깊이 리소스를 셰이더로 읽을 때의 색 포맷.
///
/// ★ 이것이 대응표에 있고 RHIFormat에 없는 이유: DX12는 깊이를 타입리스로
///   만들고 뷰에서 해석을 갈라야 하지만, Vulkan은 같은 포맷에 aspect mask로
///   읽는다. 즉 이 함수는 Vulkan 백엔드에서 항등에 가까워진다 — 백엔드
///   사정을 상위 어휘에 올리지 않는다는 것이 이 자리의 뜻이다.
///   (R2a의 SrvDepth가 "읽는 쪽에서 없앤 실수 자리"를 여기로 내린 것이다.)
inline DXGI_FORMAT DepthToShaderReadDXGI(DXGI_FORMAT depthFormat)
{
    switch (depthFormat)
    {
    case DXGI_FORMAT_D32_FLOAT:             return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D16_UNORM:             return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:     return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:                                return depthFormat;
    }
}

