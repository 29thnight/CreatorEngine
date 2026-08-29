#pragma once

#include "ModelData.h"

#include <string_view>

namespace experiment
{
    // Runtime/authoring 양쪽이 같은 UUIDv4 판정을 쓰게 하는 단일 경계다.
    // nil, 다른 UUID version, RFC variant가 아닌 값은 asset identity가 아니다.
    [[nodiscard]] inline bool IsAssetIdV4(const AssetId& id) noexcept
    {
        return id.IsValid()
            && (id.value.data[6] & 0xf0u) == 0x40u
            && (id.value.data[8] & 0xc0u) == 0x80u;
    }

    // 이번 리팩터링은 legacy 표기 호환을 두지 않는다. Uuid::TryParse가 읽을 수
    // 있는 대문자/brace/무하이픈 표기도 .meta/manifest 정본으로는 거부하고,
    // 소문자 8-4-4-4-12 UUIDv4만 받는다.
    [[nodiscard]] inline bool TryParseCanonicalAssetId(
        std::string_view text, AssetId& out) noexcept
    {
        Uuid::Uuid16 parsed{};
        if (!Uuid::TryParse(text, parsed)) return false;

        AssetId candidate{ parsed };
        if (!IsAssetIdV4(candidate) || Uuid::ToString(parsed) != text)
            return false;

        out = candidate;
        return true;
    }
}
