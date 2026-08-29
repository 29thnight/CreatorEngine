#include "MaterialPropertyPacker.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <variant>

namespace MaterialPropertyPacker
{
    std::size_t NumericElementCount(ShaderPropertyType type)
    {
        switch (type)
        {
        case ShaderPropertyType::Float: return 1;
        case ShaderPropertyType::Float2: return 2;
        case ShaderPropertyType::Float3: return 3;
        case ShaderPropertyType::Float4: return 4;
        case ShaderPropertyType::Float4x4: return 16;
        default: return 0;
        }
    }

    std::size_t LogicalByteSize(ShaderPropertyType type)
    {
        const std::size_t numericCount = NumericElementCount(type);
        if (0 != numericCount) return numericCount * sizeof(float);
        if (ShaderPropertyType::Int == type || ShaderPropertyType::Bool == type)
            return sizeof(std::int32_t);
        return 0;
    }

    const ShaderMetaPropertyBinding* FindBinding(
        const ShaderMetaBindingLayout& layout, std::string_view name)
    {
        const auto found = std::find_if(layout.properties.begin(), layout.properties.end(),
            [&](const ShaderMetaPropertyBinding& binding)
            {
                return binding.name == name;
            });
        return found == layout.properties.end() ? nullptr : &*found;
    }

    bool ApplyDefault(const ShaderPropertyDesc& desc, MaterialPropertyValue& outValue,
        std::string& outError)
    {
        outValue = {};
        outValue.m_name = desc.name;

        if (std::holds_alternative<std::monostate>(desc.defaultValue))
        {
            const std::size_t numericCount = NumericElementCount(desc.type);
            if (0 != numericCount) outValue.m_numericValue.assign(numericCount, 0.0f);
            return true;
        }
        if (const auto* value = std::get_if<float>(&desc.defaultValue))
            outValue.m_numericValue = { *value };
        else if (const auto* value = std::get_if<std::array<float, 2>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::array<float, 3>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::array<float, 4>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::array<float, 16>>(&desc.defaultValue))
            outValue.m_numericValue.assign(value->begin(), value->end());
        else if (const auto* value = std::get_if<std::int32_t>(&desc.defaultValue))
            outValue.m_integerValue = *value;
        else if (const auto* value = std::get_if<bool>(&desc.defaultValue))
            outValue.m_boolValue = *value;
        else if (const auto* value = std::get_if<FileGuid>(&desc.defaultValue))
            outValue.m_textureGuid = *value;
        else
        {
            outError = "Material property default type이 schema와 맞지 않는다: " + desc.name;
            return false;
        }
        return true;
    }

    bool ValidateLogicalValue(const ShaderPropertyDesc& desc,
        const MaterialPropertyValue& value, std::string& outError)
    {
        const std::size_t numericCount = NumericElementCount(desc.type);
        if (0 != numericCount && value.m_numericValue.size() != numericCount)
        {
            outError = "Material numeric property 크기가 schema와 맞지 않는다: " + desc.name;
            return false;
        }
        return true;
    }

    bool PackProperty(const ShaderPropertyDesc& desc,
        const ShaderMetaPropertyBinding& binding, const MaterialPropertyValue& value,
        std::vector<std::uint8_t>& bytes, std::string& outError)
    {
        if (ShaderPropertyType::Texture2D == desc.type)
            return RHIShaderResourceKind::Texture == binding.resourceKind;

        const std::size_t payloadSize = LogicalByteSize(desc.type);
        if (RHIShaderResourceKind::ConstantBuffer != binding.resourceKind
            || payloadSize > binding.byteSize
            || binding.byteOffset + payloadSize > bytes.size())
        {
            outError = "Material property binding 범위가 잘못됐다: " + desc.name;
            return false;
        }

        void* destination = bytes.data() + binding.byteOffset;
        if (0 != NumericElementCount(desc.type))
            std::memcpy(destination, value.m_numericValue.data(), payloadSize);
        else if (ShaderPropertyType::Int == desc.type)
            std::memcpy(destination, &value.m_integerValue, payloadSize);
        else if (ShaderPropertyType::Bool == desc.type)
        {
            const std::int32_t encoded = value.m_boolValue ? 1 : 0;
            std::memcpy(destination, &encoded, payloadSize);
        }
        return true;
    }
}
