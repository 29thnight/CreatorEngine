#include "ModelVertexInputLayout.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace ModelVertexInput
{
    RHIFormat ToRhiFormat(assets::VertexFormat format) noexcept
    {
        switch (format)
        {
        case assets::VertexFormat::RG32Float:
            return RHIFormat::RG32Float;
        case assets::VertexFormat::RGB32Float:
            return RHIFormat::RGB32Float;
        case assets::VertexFormat::RGBA32Float:
            return RHIFormat::RGBA32Float;
        case assets::VertexFormat::RGBA8Uint:
            return RHIFormat::RGBA8Uint;
        }
        return RHIFormat::Unknown;
    }

    bool BuildInputElements(assets::VertexAttributeMask mask,
        std::vector<RHIInputElement>& outElements, std::string& outError)
    {
        return BuildInputElements(mask, mask, outElements, outError);
    }

    bool BuildInputElements(assets::VertexAttributeMask layoutMask,
        assets::VertexAttributeMask consumedMask,
        std::vector<RHIInputElement>& outElements, std::string& outError)
    {
        if (!assets::IsSupportedModelVertexLayout(layoutMask))
        {
            outError = "지원하지 않는 정점 마스크다(core 필수·skin은"
                " all-or-nothing): mask=" + std::to_string(layoutMask);
            return false;
        }
        if (0 == consumedMask || (consumedMask & ~layoutMask) != 0)
        {
            outError = "pass 소비 마스크가 비었거나 model layout의 부분집합이 아니다:"
                " layout=" + std::to_string(layoutMask)
                + " consumed=" + std::to_string(consumedMask);
            return false;
        }

        std::vector<RHIInputElement> elements;
        for (const assets::VertexAttributeDesc& desc
            : assets::kVertexAttributeTable)
        {
            if (!assets::Has(consumedMask, desc.attribute))
            {
                continue;
            }
            const RHIFormat format = ToRhiFormat(desc.format);
            if (RHIFormat::Unknown == format)
            {
                outError = std::string("RHIFormat 대응이 없는 정점 포맷이다: ")
                    + desc.name;
                return false;
            }
            RHIInputElement element{};
            // 표의 semantic은 constexpr 문자열 리터럴이라 수명이 프로그램
            // 전체다 — RHIInputElement의 비소유 const char*와 계약이 맞는다.
            element.semantic = desc.semantic;
            element.semanticIndex = desc.semanticIndex;
            element.format = format;
            element.inputSlot = 0;
            element.alignedByteOffset =
                assets::OffsetOf(layoutMask, desc.attribute);
            element.instanceDataStepRate = 0;
            elements.push_back(element);
        }

        outElements = std::move(elements);
        outError.clear();
        return true;
    }

    const std::vector<RHIInputElement>* ResolveInputElements(
        assets::VertexAttributeMask mask, std::string& outError)
    {
        return ResolveInputElements(mask, mask, outError);
    }

    const std::vector<RHIInputElement>* ResolveInputElements(
        assets::VertexAttributeMask layoutMask,
        assets::VertexAttributeMask consumedMask, std::string& outError)
    {
        static std::mutex mutex;
        using CacheKey = std::pair<assets::VertexAttributeMask,
            assets::VertexAttributeMask>;
        static std::map<CacheKey,
            std::unique_ptr<const std::vector<RHIInputElement>>> cache;
        std::lock_guard lock(mutex);
        const CacheKey key{ layoutMask, consumedMask };
        if (const auto found = cache.find(key); found != cache.end())
        {
            outError.clear();
            return found->second.get();
        }
        std::vector<RHIInputElement> elements;
        if (!BuildInputElements(layoutMask, consumedMask, elements, outError))
            return nullptr;
        auto stable = std::make_unique<const std::vector<RHIInputElement>>(
            std::move(elements));
        const auto* result = stable.get();
        cache.emplace(key, std::move(stable));
        return result;
    }

    bool ApplyShaderPermutation(assets::VertexAttributeMask mask,
        RHIShaderPermutation& permutation, std::string& outError)
    {
        if (!assets::IsSupportedModelVertexLayout(mask))
        {
            outError = "지원하지 않는 model vertex mask다: mask="
                + std::to_string(mask);
            return false;
        }
        if (!permutation.Enable("MODEL_VERTEX_LAYOUT", outError)) return false;
        std::vector<std::string_view> enabledAxes;
        for (const assets::VertexAttributeDesc& desc : assets::kVertexAttributeTable)
        {
            if (desc.permutationAxis == nullptr || !assets::Has(mask, desc.attribute))
                continue;
            const std::string_view axis(desc.permutationAxis);
            if (std::find(enabledAxes.begin(), enabledAxes.end(), axis)
                != enabledAxes.end()) continue;
            if (!permutation.Enable(axis, outError))
            {
                return false;
            }
            enabledAxes.push_back(axis);
        }
        return true;
    }
}
