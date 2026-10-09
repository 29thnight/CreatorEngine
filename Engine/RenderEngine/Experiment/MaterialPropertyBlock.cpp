#include "MaterialPropertyBlock.h"

#include "../MaterialPropertyPacker.h"
#include "MaterialResolver.h"
#include "../AssetDepot/MaterialAssetRuntime.h"

#include <algorithm>

namespace experiment
{
    namespace
    {
        [[nodiscard]] std::string TypeMismatch(const ShaderPropertyDesc& desc)
        {
            return "experiment material property 값이 schema type과 맞지 않는다: "
                + desc.name;
        }
    }

    bool TryConvertMaterialProperty(const MaterialProperty& property,
        const ShaderPropertyDesc& desc, ::MaterialPropertyValue& outValue,
        std::string& outError)
    {
        outValue = {};
        outValue.m_name = property.name;

        switch (desc.type)
        {
        case ShaderPropertyType::Float:
            if (const auto* value = std::get_if<float>(&property.value))
            {
                outValue.m_numericValue = { *value };
                return true;
            }
            break;
        case ShaderPropertyType::Float2:
            if (const auto* value = std::get_if<math::vector2>(&property.value))
            {
                outValue.m_numericValue = { value->x, value->y };
                return true;
            }
            break;
        case ShaderPropertyType::Float3:
            if (const auto* value = std::get_if<math::vector3>(&property.value))
            {
                outValue.m_numericValue = { value->x, value->y, value->z };
                return true;
            }
            break;
        case ShaderPropertyType::Float4:
            if (const auto* value = std::get_if<math::vector4>(&property.value))
            {
                outValue.m_numericValue = { value->x, value->y, value->z, value->w };
                return true;
            }
            break;
        case ShaderPropertyType::Int:
            if (const auto* value = std::get_if<std::int32_t>(&property.value))
            {
                outValue.m_integerValue = *value;
                return true;
            }
            break;
        case ShaderPropertyType::Bool:
            if (const auto* value = std::get_if<bool>(&property.value))
            {
                outValue.m_boolValue = *value;
                return true;
            }
            break;
        case ShaderPropertyType::Float4x4:
            // MaterialPropertyValue(variant)에 행렬 대안이 없다. 저작값이 실재하면
            // 그것이 데이터 모델의 격차이므로 침묵하지 않는다.
            outError = "experiment material은 Float4x4 저작값을 표현하지 못한다: "
                + desc.name;
            return false;
        case ShaderPropertyType::Texture2D:
            if (const auto* value =
                std::get_if<TextureReference>(&property.value))
            {
                outValue.m_textureGuid.m_guid = value->assetId.value;
                outValue.m_textureUvSet = value->coordinates.set;
                outValue.m_textureUvOffset = {value->coordinates.offset[0], value->coordinates.offset[1]};
                outValue.m_textureUvScale = {value->coordinates.scale[0], value->coordinates.scale[1]};
                outValue.m_textureUvRotation = value->coordinates.rotation;
                return true;
            }
            break;
        }

        outError = TypeMismatch(desc);
        return false;
    }

    bool BuildMaterialPropertyBlock(const Material& material,
        const ShaderMeta& meta, const ShaderMetaBindingLayout& layout,
        std::vector<std::uint8_t>& outBytes, std::string& outError)
    {
        if (layout.properties.size() != meta.properties.size()
            || layout.constantBufferName.empty()
            || 0 == layout.constantBufferByteSize)
        {
            outError = "experiment material property block의 ShaderMeta layout이"
                " 불완전하다";
            return false;
        }

        own::shared_owner<const LX::Runtime::Instance> instance;
        const auto handle = material.assetOrigin && material.assetOrigin->codeProgram
            ? material.assetOrigin->codeProgram->codeHandle : ShaderMetaHandle{};
        if (!BuildMaterialRuntimeInstance(material, meta, layout, handle, {}, instance, outError))
        {
            return false;
        }
        outBytes = instance->uniforms;
        outError.clear();
        return true;
    }

    bool BuildMaterialRuntimeInstance(const Material& material, const ShaderMeta& meta,
        const ShaderMetaBindingLayout& layout, ShaderMetaHandle handle,
        std::span<const MaterialTextureOwner> textures,
        own::shared_owner<const LX::Runtime::Instance>& outInstance, std::string& outError)
    {
        own::shared_owner<const LX::Runtime::ShaderGeneration> shader;
        if (material.assetOrigin)
        {
            shader = material.assetOrigin->codeProgram;
            if (!shader || !shader->codeProgram || shader->codeHandle != handle
                || shader->meta != meta || shader->layout != layout)
            {
                outError = "Cooked material instance requires its exact captured program and layout.";
                return false;
            }
        }
        else if (!LX::Runtime::CreateCodeShader(meta, layout, handle, shader, outError))
        {
            return false;
        }
        std::vector<::MaterialPropertyValue> values;
        for (const ShaderPropertyDesc& desc : meta.properties)
        {
            const ShaderMetaPropertyBinding* binding =
                MaterialPropertyPacker::FindBinding(layout, desc.name);
            if (!binding || binding->propertyType != desc.type)
            {
                outError = "experiment material property binding이 없거나 type이"
                    " 다르다: " + desc.name;
                return false;
            }

            ::MaterialPropertyValue value;
            const auto authored = std::find_if(material.properties.begin(),
                material.properties.end(), [&](const MaterialProperty& candidate)
                {
                    return candidate.name == desc.name;
                });
            if (authored != material.properties.end())
            {
                if (!TryConvertMaterialProperty(*authored, desc, value, outError))
                    return false;
            }
            else if (!MaterialPropertyPacker::ApplyDefault(desc, value, outError))
            {
                return false;
            }

            values.push_back(std::move(value));
        }

        std::vector<std::uint16_t> keywords;
        if (!NormalizeMaterialKeywordSelections(material, meta.keywords, keywords, outError)) return false;
        return LX::Runtime::BuildInstance(std::move(shader), values, keywords, textures, outInstance, outError);
    }
}
