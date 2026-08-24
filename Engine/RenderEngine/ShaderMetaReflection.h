#pragma once

#include "RHI/RHIShaderReflection.h"
#include "ShaderMeta.h"

#include <span>
#include <string>
#include <vector>

struct ShaderMetaPropertyBinding
{
    std::string name;
    ShaderPropertyType propertyType{ ShaderPropertyType::Float };
    RHIShaderResourceKind resourceKind{ RHIShaderResourceKind::ConstantBuffer };
    std::string resourceName;
    std::uint32_t registerIndex{};
    std::uint32_t registerSpace{};
    std::uint32_t byteOffset{};
    std::uint32_t byteSize{};

    bool operator==(const ShaderMetaPropertyBinding&) const = default;
};

// M6가 프로퍼티 업로드와 texture binding을 만들 때 소비할 소유 레이아웃.
// 숫자 프로퍼티는 한 material constant buffer에 있어야 한다.
struct ShaderMetaBindingLayout
{
    std::string constantBufferName;
    std::uint32_t constantBufferRegister{};
    std::uint32_t constantBufferSpace{};
    std::uint32_t constantBufferByteSize{};
    std::vector<ShaderMetaPropertyBinding> properties;
};

namespace ShaderMetaReflection
{
    bool Resolve(const ShaderMeta& meta,
        std::span<const RHIShaderReflection> stageReflections,
        ShaderMetaBindingLayout& outLayout, std::string& outError);
}
