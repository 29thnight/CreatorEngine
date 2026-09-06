#pragma once
#include "Reflection.hpp"
#include "TypeTrait.h"
#include <cstdint>
#include <string>
#include <vector>
#include <mathematics/vector2.hpp>

// Material의 디스크 정본은 ShaderMeta GUID와 이름 기반 논리 값이다. GPU byte
// offset은 Slang reflection 결과이므로 저장하지 않고 ConfigureShaderProperties에서
// 매 세대 다시 만든다. 한 타입만 유효하지만 variant를 YAML 정본에 끌어들이지
// 않도록 고정된 필드로 둔다.
struct MaterialPropertyValue
{
    static consteval auto reflect()
    {
        using Self = MaterialPropertyValue;
        return meta::schema<Self>(
            meta::field<&Self::m_name>,
            meta::field<&Self::m_numericValue>,
            meta::field<&Self::m_integerValue>,
            meta::field<&Self::m_boolValue>,
            meta::field<&Self::m_textureGuid>,
            meta::field<&Self::m_textureUvSet>, meta::field<&Self::m_textureUvOffset>,
            meta::field<&Self::m_textureUvScale>, meta::field<&Self::m_textureUvRotation>);
    }

    std::string m_name{};
    std::vector<float> m_numericValue{};
    std::int32_t m_integerValue{};
    bool m_boolValue{};
    FileGuid m_textureGuid{};
    std::uint32_t m_textureUvSet{};
    math::vector2 m_textureUvOffset{};
    math::vector2 m_textureUvScale{1.f, 1.f};
    float m_textureUvRotation{};
};
