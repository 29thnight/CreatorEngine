#pragma once

#include "../Graph/EnhancedMaterialSealIdentity.h"
#include "../../Experiment/ModelData.h"

#include <variant>

// W8 — 저작 정본(base + 인스턴스 override를 합성한 값 스냅샷)의 값 digest.
//
// sealing의 중복 제거 키가 legacy `Material*` 하나였던 것이 결함의 뿌리다.
// `DataSystem::Materials`가 이름으로 캐시한 **같은 객체**를 여러 MeshRenderer가
// 공유하는데, 인스턴스 override는 렌더러마다 다르다. 주소가 같다는 이유로
// 먼저 밀봉된 쪽의 스냅샷을 뒤의 draw가 그대로 받으면 override가 통째로
// 사라진다. 주소 대신 이 digest를 키에 넣어 값이 같은 것만 합친다.
namespace EnhancedAuthoredMaterialDigest
{
    inline void AppendAssetId(EnhancedSealDigest& digest,
        const experiment::AssetId& id) noexcept
    {
        digest.Bytes(id.value.data.data(), id.value.data.size());
    }

    inline void AppendValue(EnhancedSealDigest& digest,
        const experiment::MaterialPropertyValue& value) noexcept
    {
        // variant의 인덱스를 먼저 접는다 — 같은 bit 패턴이 다른 타입으로
        // 읽히는 경우를 값 신원이 구분하지 못하면 안 된다.
        digest.U32(static_cast<std::uint32_t>(value.index()));
        std::visit([&digest](const auto& held)
        {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<Held, bool>)
                digest.U32(held ? 1u : 0u);
            else if constexpr (std::is_same_v<Held, std::int32_t>)
                digest.U32(static_cast<std::uint32_t>(held));
            else if constexpr (std::is_same_v<Held, std::uint32_t>)
                digest.U32(held);
            else if constexpr (std::is_same_v<Held, float>)
                digest.F32(held);
            else if constexpr (std::is_same_v<Held, math::vector2>)
            {
                digest.F32(held.x); digest.F32(held.y);
            }
            else if constexpr (std::is_same_v<Held, math::vector3>)
            {
                digest.F32(held.x); digest.F32(held.y); digest.F32(held.z);
            }
            else if constexpr (std::is_same_v<Held, math::vector4>)
            {
                digest.F32(held.x); digest.F32(held.y);
                digest.F32(held.z); digest.F32(held.w);
            }
            else if constexpr (std::is_same_v<Held, std::string>)
                digest.Text(held);
            else if constexpr (std::is_same_v<Held, experiment::TextureReference>)
            {
                AppendAssetId(digest, held.assetId);
                digest.Text(held.logicalName);
                digest.Text(held.fallbackPath.string());
                digest.U32(static_cast<std::uint32_t>(held.colorSpace));
                digest.U32(held.coordinates.set);
                digest.F32(held.coordinates.offset[0]);
                digest.F32(held.coordinates.offset[1]);
                digest.F32(held.coordinates.scale[0]);
                digest.F32(held.coordinates.scale[1]);
                digest.F32(held.coordinates.rotation);
                // W7 — sampler 만 다른 재질 둘이 같은 저작 지문을 갖지 않게 한다.
                // TextureSettingsTest 의 재질 열은 baseColor GUID·UV 가 모두 같고
                // wrap 만 다르다 — 이 네 줄이 없으면 전부 한 지문이 된다.
                digest.U32(static_cast<std::uint32_t>(held.sampler.minMag));
                digest.U32(static_cast<std::uint32_t>(held.sampler.mip));
                digest.U32(static_cast<std::uint32_t>(held.sampler.addressU));
                digest.U32(static_cast<std::uint32_t>(held.sampler.addressV));
            }
        }, value);
    }

    [[nodiscard]] inline std::uint64_t Compute(
        const experiment::Material& material) noexcept
    {
        EnhancedSealDigest digest;
        AppendAssetId(digest, material.assetId);
        AppendAssetId(digest, material.shaderAssetId);
        digest.Text(material.name);
        digest.U32(static_cast<std::uint32_t>(material.blendMode));
        digest.U64(static_cast<std::uint64_t>(material.properties.size()));
        for (const auto& property : material.properties)
        {
            digest.Text(property.name);
            AppendValue(digest, property.value);
        }
        digest.U64(static_cast<std::uint64_t>(material.keywords.size()));
        for (const auto& keyword : material.keywords) digest.Text(keyword);
        digest.U64(static_cast<std::uint64_t>(material.keywordSelections.size()));
        for (std::uint16_t selection : material.keywordSelections)
            digest.U32(static_cast<std::uint32_t>(selection));
        return digest.Value();
    }
}
