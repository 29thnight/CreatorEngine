#include "ExperimentVertexInputLayout.h"

#include "../Experiment/ModelData.h"

namespace ExperimentVertexInput
{
    RHIFormat ToRhiFormat(experiment::VertexFormat format) noexcept
    {
        switch (format)
        {
        case experiment::VertexFormat::RG32Float:
            return RHIFormat::RG32Float;
        case experiment::VertexFormat::RGB32Float:
            return RHIFormat::RGB32Float;
        case experiment::VertexFormat::RGBA32Float:
            return RHIFormat::RGBA32Float;
        case experiment::VertexFormat::RGBA8Uint:
            return RHIFormat::RGBA8Uint;
        }
        return RHIFormat::Unknown;
    }

    bool BuildInputElements(experiment::VertexAttributeMask mask,
        std::vector<RHIInputElement>& outElements, std::string& outError)
    {
        if (!experiment::VertexBuffer::IsSupportedLayout(mask))
        {
            outError = "지원하지 않는 정점 마스크다(core 필수·skin은"
                " all-or-nothing): mask=" + std::to_string(mask);
            return false;
        }

        std::vector<RHIInputElement> elements;
        for (const experiment::VertexAttributeDesc& desc
            : experiment::kVertexAttributeTable)
        {
            if (!experiment::Has(mask, desc.attribute))
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
                experiment::OffsetOf(mask, desc.attribute);
            element.instanceDataStepRate = 0;
            elements.push_back(element);
        }

        outElements = std::move(elements);
        outError.clear();
        return true;
    }
}
