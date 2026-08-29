#pragma once
#include "MaterialPropertyValue.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// CB 패킹 정본. Material 상태를 읽지 않는다 — ShaderMeta 계약(ShaderPropertyDesc·
// ShaderMetaPropertyBinding)과 논리 값만 받아 bytes를 만든다. 값 조회는 호출자
// 몫이다: legacy Material과 experiment::Material이 각자의 값 모델에서 찾아 넘긴다.
// 두 번째 packer를 만들지 않는다 — 패킹 규칙이 갈라지면 비트 단위 패리티 게이트가
// 재는 대상이 사라진다.
namespace MaterialPropertyPacker
{
    std::size_t NumericElementCount(ShaderPropertyType type);

    std::size_t LogicalByteSize(ShaderPropertyType type);

    const ShaderMetaPropertyBinding* FindBinding(
        const ShaderMetaBindingLayout& layout, std::string_view name);

    bool ApplyDefault(const ShaderPropertyDesc& desc, MaterialPropertyValue& outValue,
        std::string& outError);

    bool ValidateLogicalValue(const ShaderPropertyDesc& desc,
        const MaterialPropertyValue& value, std::string& outError);

    bool PackProperty(const ShaderPropertyDesc& desc,
        const ShaderMetaPropertyBinding& binding, const MaterialPropertyValue& value,
        std::vector<std::uint8_t>& bytes, std::string& outError);
}
