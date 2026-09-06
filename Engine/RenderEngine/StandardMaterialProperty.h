#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// 엔진 표준 PBR 머테리얼의 논리 property 이름 정본.
//
// ShaderMeta, legacy Material codec/runtime 복원, experiment 임포트 변환이
// 서로 다른 문자열을 쓰면 데이터는 게시돼도 실제 draw binding에서 빠진다.
// 이 파일은 이름만 소유하며 타입·offset·register는 ShaderMeta reflection이
// 계속 정본으로 가진다.
namespace standard_material::property
{
    inline constexpr std::string_view BaseColor{ "baseColor" };
    inline constexpr std::string_view Metallic{ "metallic" };
    inline constexpr std::string_view Roughness{ "roughness" };
    inline constexpr std::string_view Emissive{ "emissive" };
    inline constexpr std::string_view EmissiveStrength{ "emissiveStrength" };
    inline constexpr std::string_view NormalScale{ "normalScale" };
    inline constexpr std::string_view OcclusionStrength{ "occlusionStrength" };
    inline constexpr std::string_view AlphaCutoff{ "alphaCutoff" };
    // Raster policy travels in material properties, outside the numeric shader CB.
    inline constexpr std::string_view DoubleSided{ "doubleSided" };
    // I5-M5 flow 승격 — legacy Material::m_flowInfo의 저작이 이 논리 이름으로
    // material CB(b2)에 실린다. 시간은 프레임 값이라 property가 아니다.
    inline constexpr std::string_view FlowWindVector{ "flowWindVector" };
    inline constexpr std::string_view FlowUvScroll{ "flowUvScroll" };

    inline constexpr std::string_view BaseColorMap{ "baseColorMap" };
    inline constexpr std::string_view OrmMap{ "ormMap" };
    inline constexpr std::string_view NormalMap{ "normalMap" };
    inline constexpr std::string_view AoMap{ "aoMap" };
    inline constexpr std::string_view EmissiveMap{ "emissiveMap" };

    inline constexpr std::array All{
        BaseColor,
        Metallic,
        Roughness,
        Emissive,
        EmissiveStrength,
        NormalScale,
        OcclusionStrength,
        AlphaCutoff,
        DoubleSided,
        FlowWindVector,
        FlowUvScroll,
        BaseColorMap,
        OrmMap,
        NormalMap,
        AoMap,
        EmissiveMap,
    };

    [[nodiscard]] consteval bool NamesAreUnique() noexcept
    {
        for (std::size_t left = 0; left < All.size(); ++left)
        {
            for (std::size_t right = left + 1; right < All.size(); ++right)
            {
                if (All[left] == All[right]) return false;
            }
        }
        return true;
    }

    static_assert(NamesAreUnique(),
        "standard material property names must be unique");
}
