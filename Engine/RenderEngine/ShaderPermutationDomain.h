#pragma once

#include "RHI/RHIShaderPermutation.h"
#include "ShaderMeta.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

struct ShaderKeywordSelection
{
    std::string axis;
    std::string value;
    std::uint16_t valueIndex{};

    bool operator==(const ShaderKeywordSelection&) const = default;
};

// key는 lookup용 고정 폭 digest다. hash 충돌이 다른 변형을 재사용하게 만들지
// 않도록 GUID/pass/정렬된 선택값도 함께 소유하며, cook manifest도 이 identity를
// 보존해야 한다.
struct ShaderMetaPermutation
{
    FileGuid metaGuid{};
    std::uint32_t passIndex{};
    std::vector<ShaderKeywordSelection> selections;
    RHIShaderPermutationKey key{};
    RHIShaderPermutation defines;
};

struct ShaderMetaPermutationStats
{
    std::uint64_t variantsPerPass{ 1 };
    std::uint64_t compileRequests{};
};

namespace ShaderPermutationDomain
{
    // 전수 cook은 조용히 일부만 만들지 않는다. 이 수를 넘으면 Build가 명시적으로
    // 실패하고, Editor의 단일 Resolve/load는 계속 가능하다.
    inline constexpr std::uint64_t kDefaultBuildCompileLimit = 4096;

    bool Measure(const ShaderMeta& meta, ShaderMetaPermutationStats& outStats,
        std::string& outError);

    // valueIndicesInMetaOrder는 ShaderMeta::keywords 작성 순서다. 결과 identity와
    // define은 축 이름순으로 정규화되며, define 값은 선택값의 0-based ordinal이다.
    bool Resolve(const ShaderMeta& meta, std::uint32_t passIndex,
        std::span<const std::uint16_t> valueIndicesInMetaOrder,
        ShaderMetaPermutation& outPermutation, std::string& outError);

    // pass-major, 축 이름순 mixed-radix 순서로 모든 변형을 만든다. 상한 초과 시
    // 결과를 잘라내지 않고 실패한다. B3 cook도 이 함수의 결과를 소비한다.
    bool EnumerateForBuild(const ShaderMeta& meta,
        std::uint64_t maxCompileRequests,
        std::vector<ShaderMetaPermutation>& outPermutations,
        ShaderMetaPermutationStats& outStats, std::string& outError);
}
