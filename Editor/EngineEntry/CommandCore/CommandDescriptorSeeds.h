#pragma once
// LC3 (PHASE 14.5) — 명령 schema seed 표의 조회 표면.
//
// 표 본문은 `CommandDescriptorSeeds.cpp` 에 있다. 등록 줄이 아니라 별도 표에
// 둔 이유는 그 파일 머리말에 적었다.

#include "CommandDescriptor.h"

#include <cstddef>
#include <string_view>

namespace CommandCore
{
    struct DescriptorSeed
    {
        const char* name;
        CommandCost cost;
        const char* usage;    ///< `<필수> [선택]`. 없으면 빈 문자열
        const char* summary;  ///< 한 줄. 비어 있으면 등록이 거부된다
    };

    /// canonical 이름으로 찾는다. 없으면 nullptr — 그 명령은 등록되지 않는다.
    const DescriptorSeed* FindDescriptorSeed(std::string_view canonical);

    std::size_t DescriptorSeedCount() noexcept;
}
