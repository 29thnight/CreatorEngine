#include "ShaderMetaReflection.h"

#include <optional>

namespace
{
    std::optional<RHIShaderValueType> ExpectedType(ShaderPropertyType type)
    {
        switch (type)
        {
        case ShaderPropertyType::Float:
            return RHIShaderValueType{ RHIShaderScalarKind::Float32, 1, 1, 1 };
        case ShaderPropertyType::Float2:
            return RHIShaderValueType{ RHIShaderScalarKind::Float32, 1, 2, 1 };
        case ShaderPropertyType::Float3:
            return RHIShaderValueType{ RHIShaderScalarKind::Float32, 1, 3, 1 };
        case ShaderPropertyType::Float4:
            return RHIShaderValueType{ RHIShaderScalarKind::Float32, 1, 4, 1 };
        case ShaderPropertyType::Int:
            return RHIShaderValueType{ RHIShaderScalarKind::Int32, 1, 1, 1 };
        case ShaderPropertyType::Bool:
            return RHIShaderValueType{ RHIShaderScalarKind::Bool, 1, 1, 1 };
        case ShaderPropertyType::Float4x4:
            return RHIShaderValueType{ RHIShaderScalarKind::Float32, 4, 4, 1 };
        case ShaderPropertyType::Texture2D:
            return std::nullopt;
        }
        return std::nullopt;
    }

    bool MergeCandidate(const ShaderMetaPropertyBinding& candidate,
        std::optional<ShaderMetaPropertyBinding>& selected, std::string& outError)
    {
        if (!selected)
        {
            selected = candidate;
            return true;
        }
        if (*selected == candidate) return true;
        outError = "ShaderMeta property binding이 stage 사이에서 다르다: "
            + candidate.name;
        return false;
    }
}

bool ShaderMetaReflection::Resolve(const ShaderMeta& meta,
    std::span<const RHIShaderReflection> stageReflections,
    ShaderMetaBindingLayout& outLayout, std::string& outError)
{
    if (stageReflections.empty())
    {
        outError = "ShaderMeta reflection stage가 없다";
        return false;
    }

    ShaderMetaBindingLayout resolved;
    resolved.properties.reserve(meta.properties.size());

    for (const ShaderPropertyDesc& property : meta.properties)
    {
        std::optional<ShaderMetaPropertyBinding> selected;
        const std::optional<RHIShaderValueType> expected = ExpectedType(property.type);

        for (const RHIShaderReflection& reflection : stageReflections)
        {
            for (const RHIShaderResourceReflection& resource : reflection.resources)
            {
                if (ShaderPropertyType::Texture2D == property.type)
                {
                    if (resource.name != property.name
                        || RHIShaderResourceKind::Texture != resource.kind)
                    {
                        continue;
                    }
                    if (1 != resource.arrayElements)
                    {
                        outError = "ShaderMeta texture2d property가 배열 resource다: "
                            + property.name;
                        return false;
                    }
                    const ShaderMetaPropertyBinding candidate{
                        property.name, property.type, resource.kind, resource.name,
                        resource.registerIndex, resource.registerSpace, 0, 0,
                    };
                    if (!MergeCandidate(candidate, selected, outError)) return false;
                    continue;
                }

                if (RHIShaderResourceKind::ConstantBuffer != resource.kind) continue;
                for (const RHIShaderFieldReflection& field : resource.fields)
                {
                    if (field.name != property.name) continue;
                    if (!expected || field.type != *expected)
                    {
                        outError = "ShaderMeta property와 shader field type이 다르다: "
                            + property.name;
                        return false;
                    }
                    if (meta.generatedMaterial && (resource.arrayElements != 1 || resource.byteSize > 65536u ||
                        field.byteSize != expected->rows * expected->columns * 4u || field.byteOffset % 4u != 0 ||
                        static_cast<std::uint64_t>(field.byteOffset) + field.byteSize > resource.byteSize))
                    {
                        outError = "Generated ShaderMeta field exceeds its reflected buffer: " + property.name;
                        return false;
                    }
                    const ShaderMetaPropertyBinding candidate{
                        property.name, property.type, resource.kind, resource.name,
                        resource.registerIndex, resource.registerSpace,
                        field.byteOffset, field.byteSize,
                    };
                    if (!MergeCandidate(candidate, selected, outError)) return false;

                    if (resolved.constantBufferName.empty())
                    {
                        resolved.constantBufferName = resource.name;
                        resolved.constantBufferRegister = resource.registerIndex;
                        resolved.constantBufferSpace = resource.registerSpace;
                        resolved.constantBufferByteSize = resource.byteSize;
                    }
                    else if (resolved.constantBufferName != resource.name
                        || resolved.constantBufferRegister != resource.registerIndex
                        || resolved.constantBufferSpace != resource.registerSpace
                        || resolved.constantBufferByteSize != resource.byteSize)
                    {
                        outError = "ShaderMeta 숫자 property가 둘 이상의 constant buffer에 있다: "
                            + property.name;
                        return false;
                    }
                }
            }
        }

        if (!selected)
        {
            outError = "ShaderMeta property에 대응하는 shader 선언이 없다: "
                + property.name;
            return false;
        }
        if (meta.generatedMaterial && selected->resourceKind == RHIShaderResourceKind::ConstantBuffer)
            for (const auto& other : resolved.properties)
                if (other.resourceKind == RHIShaderResourceKind::ConstantBuffer &&
                    selected->byteOffset < other.byteOffset + other.byteSize &&
                    other.byteOffset < selected->byteOffset + selected->byteSize)
                {
                    outError = "Generated ShaderMeta fields overlap: " + property.name;
                    return false;
                }
        resolved.properties.push_back(std::move(*selected));
    }

    if (meta.generatedMaterial)
    {
        for (const auto& sampler : meta.generatedMaterial->samplers)
        {
            std::optional<RHIShaderResourceReflection> selected;
            for (const auto& reflection : stageReflections)
                for (const auto& resource : reflection.resources)
                    if (resource.name == sampler.name)
                    {
                        if (resource.kind != RHIShaderResourceKind::Sampler || resource.arrayElements != 1 ||
                            (selected && *selected != resource))
                        {
                            outError = "Generated ShaderMeta sampler differs from reflection: " + sampler.name;
                            return false;
                        }
                        selected = resource;
                    }
            if (!selected)
            {
                outError = "Generated ShaderMeta sampler is missing: " + sampler.name;
                return false;
            }
            resolved.samplers.push_back(std::move(*selected));
        }
    }
    outLayout = std::move(resolved);
    outError.clear();
    return true;
}
