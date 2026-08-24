#include "ShaderPermutationDomain.h"

#include <algorithm>
#include <limits>

namespace
{
    struct CanonicalAxis
    {
        std::size_t authoredIndex{};
        const ShaderKeywordAxis* axis{};
    };

    std::vector<CanonicalAxis> CanonicalAxes(const ShaderMeta& meta)
    {
        std::vector<CanonicalAxis> axes;
        axes.reserve(meta.keywords.size());
        for (std::size_t index = 0; index < meta.keywords.size(); ++index)
            axes.push_back({ index, &meta.keywords[index] });
        std::ranges::sort(axes, {}, [](const CanonicalAxis& item)
        {
            return std::string_view(item.axis->name);
        });
        return axes;
    }

    void AddByte(RHIShaderPermutationKey& key, std::uint8_t byte)
    {
        key.lo = (key.lo ^ byte) * 1099511628211ull;
        key.hi = (key.hi ^ static_cast<std::uint8_t>(byte + 0x6du))
            * 14029467366897019727ull;
    }

    void AddUint64(RHIShaderPermutationKey& key, std::uint64_t value)
    {
        for (std::uint32_t shift = 0; shift < 64; shift += 8)
            AddByte(key, static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }

    void AddString(RHIShaderPermutationKey& key, std::string_view value)
    {
        AddUint64(key, static_cast<std::uint64_t>(value.size()));
        for (const char character : value)
            AddByte(key, static_cast<std::uint8_t>(character));
    }

    RHIShaderPermutationKey MakeKey(const ShaderMeta& meta,
        std::uint32_t passIndex,
        std::span<const ShaderKeywordSelection> selections)
    {
        RHIShaderPermutationKey key{
            1469598103934665603ull ^ 0x4d325342ull,
            1099511628211ull ^ 0x9e3779b97f4a7c15ull,
        };
        AddString(key, "CreatorEngine.ShaderMetaPermutation.v1");
        AddString(key, meta.guid.ToString());
        AddUint64(key, passIndex);
        AddUint64(key, static_cast<std::uint64_t>(selections.size()));
        for (const ShaderKeywordSelection& selection : selections)
        {
            AddString(key, selection.axis);
            AddUint64(key, selection.valueIndex);
            AddString(key, selection.value);
        }
        return key;
    }
}

bool ShaderPermutationDomain::Measure(const ShaderMeta& meta,
    ShaderMetaPermutationStats& outStats, std::string& outError)
{
    if (meta.passes.empty())
    {
        outError = "ShaderMeta permutation을 계산할 pass가 없다";
        return false;
    }

    std::uint64_t variants = 1;
    for (const ShaderKeywordAxis& axis : meta.keywords)
    {
        if (axis.values.empty())
        {
            outError = "ShaderMeta keyword 축에 값이 없다: " + axis.name;
            return false;
        }
        const std::uint64_t count = static_cast<std::uint64_t>(axis.values.size());
        if (variants > (std::numeric_limits<std::uint64_t>::max)() / count)
        {
            outError = "ShaderMeta keyword 조합 수가 uint64 범위를 넘었다: "
                + axis.name;
            return false;
        }
        variants *= count;
    }

    const std::uint64_t passCount = static_cast<std::uint64_t>(meta.passes.size());
    if (variants > (std::numeric_limits<std::uint64_t>::max)() / passCount)
    {
        outError = "ShaderMeta pass 포함 컴파일 요청 수가 uint64 범위를 넘었다";
        return false;
    }

    outStats = { variants, variants * passCount };
    outError.clear();
    return true;
}

bool ShaderPermutationDomain::Resolve(const ShaderMeta& meta,
    std::uint32_t passIndex,
    std::span<const std::uint16_t> valueIndicesInMetaOrder,
    ShaderMetaPermutation& outPermutation, std::string& outError)
{
    if (FileGuid{} == meta.guid)
    {
        outError = "ShaderMeta permutation에 catalog GUID가 없다";
        return false;
    }
    if (passIndex >= meta.passes.size())
    {
        outError = "ShaderMeta permutation pass index가 범위를 벗어났다: "
            + std::to_string(passIndex);
        return false;
    }
    if (valueIndicesInMetaOrder.size() != meta.keywords.size())
    {
        outError = "ShaderMeta keyword 선택 수가 축 수와 다르다";
        return false;
    }

    ShaderMetaPermutation resolved;
    resolved.metaGuid = meta.guid;
    resolved.passIndex = passIndex;
    resolved.selections.reserve(meta.keywords.size());

    const std::vector<CanonicalAxis> axes = CanonicalAxes(meta);
    for (const CanonicalAxis& canonical : axes)
    {
        const ShaderKeywordAxis& axis = *canonical.axis;
        const std::uint16_t valueIndex =
            valueIndicesInMetaOrder[canonical.authoredIndex];
        if (valueIndex >= axis.values.size())
        {
            outError = "ShaderMeta keyword 선택 index가 범위를 벗어났다: "
                + axis.name + "=" + std::to_string(valueIndex);
            return false;
        }

        if (!resolved.defines.Set(axis.name, std::to_string(valueIndex), outError))
            return false;
        resolved.selections.push_back(
            { axis.name, axis.values[valueIndex], valueIndex });
    }

    resolved.key = MakeKey(meta, passIndex, resolved.selections);
    outPermutation = std::move(resolved);
    outError.clear();
    return true;
}

bool ShaderPermutationDomain::EnumerateForBuild(const ShaderMeta& meta,
    std::uint64_t maxCompileRequests,
    std::vector<ShaderMetaPermutation>& outPermutations,
    ShaderMetaPermutationStats& outStats, std::string& outError)
{
    ShaderMetaPermutationStats stats;
    if (!Measure(meta, stats, outError)) return false;
    outStats = stats;
    if (stats.compileRequests > maxCompileRequests)
    {
        outError = "ShaderMeta 전수 컴파일 요청 "
            + std::to_string(stats.compileRequests) + "개가 Build 상한 "
            + std::to_string(maxCompileRequests) + "개를 넘었다";
        return false;
    }
    if (stats.compileRequests > (std::numeric_limits<std::size_t>::max)())
    {
        outError = "ShaderMeta 전수 컴파일 결과를 메모리에 표현할 수 없다";
        return false;
    }

    const std::vector<CanonicalAxis> axes = CanonicalAxes(meta);
    std::vector<ShaderMetaPermutation> permutations;
    permutations.reserve(static_cast<std::size_t>(stats.compileRequests));
    std::vector<std::uint16_t> indices(meta.keywords.size(), 0);

    for (std::uint32_t passIndex = 0; passIndex < meta.passes.size(); ++passIndex)
    {
        for (std::uint64_t variant = 0; variant < stats.variantsPerPass; ++variant)
        {
            std::uint64_t remainder = variant;
            for (auto axis = axes.rbegin(); axis != axes.rend(); ++axis)
            {
                const std::uint64_t radix = axis->axis->values.size();
                indices[axis->authoredIndex] = static_cast<std::uint16_t>(
                    remainder % radix);
                remainder /= radix;
            }

            ShaderMetaPermutation permutation;
            if (!Resolve(meta, passIndex, indices, permutation, outError))
                return false;
            permutations.push_back(std::move(permutation));
        }
    }

    outPermutations = std::move(permutations);
    outError.clear();
    return true;
}
