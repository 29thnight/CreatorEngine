#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../RHIPipelineLayout.h"
#include <d3d12.h>

// 중립 레이아웃·샘플러 어휘 → DX12 (V4).
//
// ★ 왜 별도 파일인가: 이 대응표를 쓰는 곳이 둘이다 — 루트 시그니처 캐시가
//   정적 샘플러를, 디스크립터 힙이 힙 샘플러를 만든다. 둘 다 같은
//   RHISamplerDesc 를 받는데 D3D12 쪽 구조체만 다르다
//   (D3D12_STATIC_SAMPLER_DESC vs D3D12_SAMPLER_DESC). 대응표를 각자 두면
//   필터 하나를 더할 때 한쪽만 고치는 날이 오고, 그때 증상은 "샘플러가
//   자리에 따라 다르게 동작한다"가 된다 — 추적이 어려운 부류다.

namespace DX12Translate
{
    inline D3D12_SHADER_VISIBILITY ToD3D12(RHIShaderVisibility visibility)
    {
        switch (visibility)
        {
        case RHIShaderVisibility::Vertex: return D3D12_SHADER_VISIBILITY_VERTEX;
        case RHIShaderVisibility::Pixel:  return D3D12_SHADER_VISIBILITY_PIXEL;
        case RHIShaderVisibility::All:
        default:                          return D3D12_SHADER_VISIBILITY_ALL;
        }
    }

    inline D3D12_DESCRIPTOR_RANGE_TYPE ToD3D12(RHIDescriptorType type)
    {
        switch (type)
        {
        case RHIDescriptorType::UnorderedAccess: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        case RHIDescriptorType::Sampler:         return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        case RHIDescriptorType::ShaderResource:
        default:                                 return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        }
    }

    inline D3D12_ROOT_PARAMETER_TYPE ToD3D12(RHILayoutParamKind kind)
    {
        switch (kind)
        {
        case RHILayoutParamKind::ConstantBuffer:        return D3D12_ROOT_PARAMETER_TYPE_CBV;
        case RHILayoutParamKind::ShaderResourceBuffer:  return D3D12_ROOT_PARAMETER_TYPE_SRV;
        case RHILayoutParamKind::UnorderedAccessBuffer: return D3D12_ROOT_PARAMETER_TYPE_UAV;
        case RHILayoutParamKind::Constants:             return D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        case RHILayoutParamKind::DescriptorTable:
        default:                                        return D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        }
    }

    inline D3D12_TEXTURE_ADDRESS_MODE ToD3D12(RHIAddressMode address)
    {
        switch (address)
        {
        case RHIAddressMode::Clamp:  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        case RHIAddressMode::Border: return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        case RHIAddressMode::Wrap:
        default:                     return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        }
    }

    inline D3D12_COMPARISON_FUNC ToD3D12(RHICompareOp op)
    {
        switch (op)
        {
        case RHICompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case RHICompareOp::None:
        default:                      return D3D12_COMPARISON_FUNC_NEVER;
        }
    }

    // ★ 비교 여부가 필터 값 안으로 접혀 들어가는 자리. 중립 쪽은 compare 를
    //   따로 들고(Vulkan 의 compareEnable), 여기서 D3D12 의 접힌 표기로
    //   되돌린다. 넷을 다 적어 두는 것은 min/mag 와 mip 의 조합이 넷이기
    //   때문이지 넷이 다 쓰이기 때문은 아니다 — 조합을 표현할 수 있게
    //   갈라 든 이상, 대응도 조합 전부를 덮어야 한다.
    inline D3D12_FILTER ToD3D12Filter(const RHISamplerDesc& sampler)
    {
        const bool linearMinMag = (RHIFilterMode::Linear == sampler.minMag);
        const bool linearMip = (RHIFilterMode::Linear == sampler.mip);

        if (RHICompareOp::None != sampler.compare)
        {
            if (linearMinMag)
            {
                return linearMip ? D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR
                                 : D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            }
            return linearMip ? D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR
                             : D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        }

        if (linearMinMag)
        {
            return linearMip ? D3D12_FILTER_MIN_MAG_MIP_LINEAR
                             : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        }
        return linearMip ? D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR
                         : D3D12_FILTER_MIN_MAG_MIP_POINT;
    }

    // 힙 샘플러. 경계색을 값으로 든다.
    inline D3D12_SAMPLER_DESC ToD3D12(const RHISamplerDesc& sampler)
    {
        D3D12_SAMPLER_DESC desc{};
        desc.Filter = ToD3D12Filter(sampler);
        desc.AddressU = ToD3D12(sampler.addressU);
        desc.AddressV = ToD3D12(sampler.addressV);
        desc.AddressW = ToD3D12(sampler.addressW);
        desc.ComparisonFunc = ToD3D12(sampler.compare);
        desc.MaxLOD = sampler.maxLod;

        const float border = (RHIBorderColor::OpaqueWhite == sampler.border) ? 1.f : 0.f;
        desc.BorderColor[0] = border;
        desc.BorderColor[1] = border;
        desc.BorderColor[2] = border;
        desc.BorderColor[3] = border;
        return desc;
    }

    // 정적 샘플러. 같은 상태인데 경계색을 열거로 들고, 걸리는 자리(레지스터·
    // 가시성)가 함께 붙는다.
    inline D3D12_STATIC_SAMPLER_DESC ToD3D12(const RHIStaticSamplerDesc& source)
    {
        D3D12_STATIC_SAMPLER_DESC desc{};
        desc.Filter = ToD3D12Filter(source.sampler);
        desc.AddressU = ToD3D12(source.sampler.addressU);
        desc.AddressV = ToD3D12(source.sampler.addressV);
        desc.AddressW = ToD3D12(source.sampler.addressW);
        desc.ComparisonFunc = ToD3D12(source.sampler.compare);
        desc.MaxLOD = source.sampler.maxLod;
        desc.ShaderRegister = source.shaderRegister;
        desc.ShaderVisibility = ToD3D12(source.visibility);
        desc.BorderColor = (RHIBorderColor::OpaqueWhite == source.sampler.border)
            ? D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
            : D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        return desc;
    }
}

#endif
