#pragma once

#include "ModelData.h"
#include "../MaterialPropertyValue.h"
#include "../ShaderMeta.h"
#include "../ShaderMetaReflection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace experiment
{
    // I5-M1 — experiment::Material의 논리 property로 M6 snapshot의 CB bytes를
    // 만든다. packing 규칙은 MaterialPropertyPacker 정본을 그대로 쓴다(두 번째
    // packer 금지). legacy BuildShaderPropertyBlock과 다른 점은 하나다:
    // MaterialInfo 폴백이 없다 — experiment material은 논리 property가 정본이므로
    // 저작값이 없으면 ShaderMeta 기본값이다.

    // 변환은 fail-closed다. desc.type과 variant 대안이 맞지 않으면(float 자리의
    // 문자열, 데이터 모델에 표현이 없는 Float4x4 저작값 등) 조용히 기본값으로
    // 덮지 않고 실패한다 — 패리티 게이트가 재는 것이 이 데이터 모델의 충분성이다.
    // 반환 타입 주의: 이 namespace의 MaterialPropertyValue는 variant 별칭이다.
    // packer가 받는 것은 전역 ::MaterialPropertyValue(논리 값 struct)다.
    [[nodiscard]] bool TryConvertMaterialProperty(const MaterialProperty& property,
        const ShaderPropertyDesc& desc, ::MaterialPropertyValue& outValue,
        std::string& outError);

    [[nodiscard]] bool BuildMaterialPropertyBlock(const Material& material,
        const ShaderMeta& meta, const ShaderMetaBindingLayout& layout,
        std::vector<std::uint8_t>& outBytes, std::string& outError);
}
