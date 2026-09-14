#pragma once
#include "../RHI/RHIPipelineLayout.h"

#include <compare>

namespace assets
{
    // 텍스처 **참조**마다 갖는 샘플러 상태다. TextureCoordinates 와 같은 자리에
    // 산다 — 이미지/캐시 신원과 무관하고, 같은 이미지를 wrap 만 달리해 여러 번
    // 참조하는 것이 glTF 에서는 정상이다(texture = { source, sampler }).
    //
    // ★ 별도 그래픽 어휘를 만들지 않는다. ShaderRenderState 가 선 규약이다 —
    //   "실제 PSO 기술과 같은 RHI 열거를 소유한다". 여기서 또 한 벌을 만들면
    //   glTF · import IR · 저작 IR · RHI 로 어휘가 넷이 되고, 변환표도 넷이
    //   된다. import IR 의 TextureWrap 만 따로인 이유는 그쪽이 도구와 공유하는
    //   RHI-free 경계이기 때문이고, 그 변환은 SceneToModelDraft 한 곳에 있다.
    //
    // ★ minMag 를 하나로 드는 이유는 RHISamplerDesc 와 같다(D3D12_FILTER 가
    //   min·mag·mip 을 한 값에 접는다). glTF 가 min≠mag 를 주면 임포터가
    //   UnsupportedFeature 로 적고 min 을 택한다 — 여기까지 오지 않는다.
    //
    // 기본값은 패스가 지금 고정으로 걸고 있는 값(선형 · WRAP)이다. 배선이
    // 붙어도 샘플러를 선언하지 않은 기존 자산의 그림은 달라지지 않는다.
    struct TextureSampler final
    {
        RHIFilterMode  minMag{ RHIFilterMode::Linear };
        RHIFilterMode  mip{ RHIFilterMode::Linear };
        RHIAddressMode addressU{ RHIAddressMode::Wrap };
        RHIAddressMode addressV{ RHIAddressMode::Wrap };

        friend auto operator<=>(const TextureSampler&, const TextureSampler&) = default;

        // 패스가 거는 RHISamplerDesc 로 편다. addressW 는 2D 텍스처라 의미가
        // 없지만 desc 가 셋을 요구하므로 U 를 따른다 — 기본값 Wrap 을 그대로
        // 두면 Clamp 재질에서 W 만 Wrap 이 되어 신원이 갈린다.
        [[nodiscard]] RHISamplerDesc ToDesc() const noexcept
        {
            RHISamplerDesc desc{};
            desc.minMag = minMag;
            desc.mip = mip;
            desc.addressU = addressU;
            desc.addressV = addressV;
            desc.addressW = addressU;
            return desc;
        }
    };
}
