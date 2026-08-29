#include "ExperimentMaterialSealing.h"

#include "../../Experiment/MaterialPropertyBlock.h"
#include "../../Material.h"
#include "../../ShaderMeta.h"
#include "../../StandardMaterialProperty.h"

#include <algorithm>

namespace ExperimentMaterialSealing
{
    namespace
    {
        [[nodiscard]] const MaterialPropertyValue* FindLegacyValue(
            const Material& legacy, std::string_view name)
        {
            const auto found = std::find_if(legacy.m_propertyValues.begin(),
                legacy.m_propertyValues.end(),
                [&](const MaterialPropertyValue& candidate)
                {
                    return candidate.m_name == name;
                });
            return found == legacy.m_propertyValues.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool ConvertLegacyValue(const MaterialPropertyValue& value,
            const ShaderPropertyDesc& desc,
            experiment::MaterialPropertyValue& outValue, std::string& outError)
        {
            const std::vector<float>& numeric = value.m_numericValue;
            switch (desc.type)
            {
            case ShaderPropertyType::Float:
                if (numeric.size() == 1u) { outValue = numeric[0]; return true; }
                break;
            case ShaderPropertyType::Float2:
                if (numeric.size() == 2u)
                {
                    outValue = math::vector2{ numeric[0], numeric[1] };
                    return true;
                }
                break;
            case ShaderPropertyType::Float3:
                if (numeric.size() == 3u)
                {
                    outValue = math::vector3{ numeric[0], numeric[1], numeric[2] };
                    return true;
                }
                break;
            case ShaderPropertyType::Float4:
                if (numeric.size() == 4u)
                {
                    outValue = math::vector4{ numeric[0], numeric[1], numeric[2],
                        numeric[3] };
                    return true;
                }
                break;
            case ShaderPropertyType::Int:
                outValue = value.m_integerValue;
                return true;
            case ShaderPropertyType::Bool:
                outValue = value.m_boolValue;
                return true;
            case ShaderPropertyType::Texture2D:
            {
                experiment::TextureReference reference;
                reference.assetId.value = value.m_textureGuid.m_guid;
                reference.logicalName = value.m_name;
                outValue = std::move(reference);
                return true;
            }
            case ShaderPropertyType::Float4x4:
                // legacy 논리 값에 행렬 저작이 실재하면 experiment 모델의
                // 격차다 — 침묵하지 않는다(I5-M1과 같은 계약).
                break;
            }
            outError = "legacy material 논리 값이 ShaderMeta type과 맞지 않는다: "
                + value.m_name;
            return false;
        }
    }

    bool BuildSealSourceFromLegacy(const Material& legacy, const ShaderMeta& meta,
        SealSource& outSource, std::string& outError)
    {
        SealSource source;
        // 자산 정체성은 m_fileGuid다 — m_materialGuid는 런타임 HashedGuid라
        // AssetId로 나르면 안 된다.
        source.material.assetId.value = legacy.m_fileGuid.m_guid;
        source.material.shaderAssetId.value = legacy.m_shaderMetaGuid.m_guid;
        source.material.name = legacy.m_name;
        source.material.blendMode =
            MaterialRenderingMode::Transparent == legacy.m_renderingMode
            ? experiment::MaterialBlendMode::Transparent
            : experiment::MaterialBlendMode::Opaque;
        source.material.keywordSelections.assign(
            legacy.GetKeywordSelections().begin(),
            legacy.GetKeywordSelections().end());

        for (const ShaderPropertyDesc& desc : meta.properties)
        {
            const MaterialPropertyValue* authored =
                FindLegacyValue(legacy, desc.name);
            experiment::MaterialProperty property;
            property.name = desc.name;
            if (authored)
            {
                if (!ConvertLegacyValue(*authored, desc, property.value, outError))
                {
                    if (!legacy.m_name.empty())
                        outError += " (material " + legacy.m_name + ")";
                    return false;
                }
            }
            else if (desc.name == standard_material::property::BaseColor
                && ShaderPropertyType::Float4 == desc.type)
            {
                property.value = math::vector4{
                    legacy.m_materialInfo.m_baseColor.r,
                    legacy.m_materialInfo.m_baseColor.g,
                    legacy.m_materialInfo.m_baseColor.b,
                    legacy.m_materialInfo.m_baseColor.a };
            }
            else if (desc.name == standard_material::property::Metallic
                && ShaderPropertyType::Float == desc.type)
            {
                property.value = legacy.m_materialInfo.m_metallic;
            }
            else if (desc.name == standard_material::property::Roughness
                && ShaderPropertyType::Float == desc.type)
            {
                property.value = legacy.m_materialInfo.m_roughness;
            }
            else
            {
                continue; // 저작값 없음 — packer가 ShaderMeta 기본값을 쓴다.
            }
            source.material.properties.push_back(std::move(property));

            if (ShaderPropertyType::Texture2D == desc.type)
            {
                source.textures.push_back({ desc.name,
                    legacy.GetTextureMapShared(desc.name) });
            }
        }

        source.flow.windVector = legacy.m_flowInfo.m_windVector;
        source.flow.uvScroll = legacy.m_flowInfo.m_uvScroll;
        source.baseColorFactor = legacy.m_materialInfo.m_baseColor;
        source.metallic = legacy.m_materialInfo.m_metallic;
        source.roughness = legacy.m_materialInfo.m_roughness;
        source.useNormalMap =
            (0 != legacy.m_materialInfo.m_useNormalMap) ? 1u : 0u;
        source.debugName = legacy.m_name;

        outSource = std::move(source);
        outError.clear();
        return true;
    }

    bool SealCore(const SealSource& source, const ShaderMeta& meta,
        const ShaderMetaBindingLayout& layout,
        std::vector<std::uint8_t>& outPropertyBytes,
        std::vector<EnhancedMaterialTextureBinding>& outTextureBindings,
        std::string& outError)
    {
        if (!experiment::BuildMaterialPropertyBlock(source.material, meta, layout,
            outPropertyBytes, outError))
        {
            return false;
        }

        std::vector<EnhancedMaterialTextureBinding> bindings;
        bindings.reserve(static_cast<std::size_t>(std::count_if(
            meta.properties.begin(), meta.properties.end(),
            [](const ShaderPropertyDesc& property)
            {
                return ShaderPropertyType::Texture2D == property.type;
            })));

        for (const ShaderPropertyDesc& property : meta.properties)
        {
            if (ShaderPropertyType::Texture2D != property.type) continue;
            const auto reflected = std::find_if(layout.properties.begin(),
                layout.properties.end(),
                [&property](const ShaderMetaPropertyBinding& binding)
                {
                    return binding.name == property.name;
                });
            if (reflected == layout.properties.end()
                || ShaderPropertyType::Texture2D != reflected->propertyType
                || RHIShaderResourceKind::Texture != reflected->resourceKind)
            {
                outError = "ShaderMeta texture reflection binding이 없다: "
                    + property.name;
                return false;
            }
            const bool duplicateName = std::any_of(bindings.begin(),
                bindings.end(), [&property](const auto& binding)
                {
                    return binding.propertyName == property.name;
                });
            const bool duplicateRegister = std::any_of(bindings.begin(),
                bindings.end(), [&reflected](const auto& binding)
                {
                    return binding.registerIndex == reflected->registerIndex
                        && binding.registerSpace == reflected->registerSpace;
                });
            if (duplicateName || duplicateRegister)
            {
                outError = "ShaderMeta texture property 이름/register가 중복이다: "
                    + property.name;
                return false;
            }

            EnhancedMaterialTextureBinding binding{};
            binding.propertyName = property.name;
            binding.registerIndex = reflected->registerIndex;
            binding.registerSpace = reflected->registerSpace;

            const auto owner = std::find_if(source.textures.begin(),
                source.textures.end(), [&property](const SealTextureOwner& entry)
                {
                    return entry.propertyName == property.name;
                });
            if (owner != source.textures.end()) binding.textureOwner = owner->owner;

            const auto authored = std::find_if(
                source.material.properties.begin(),
                source.material.properties.end(),
                [&property](const experiment::MaterialProperty& candidate)
                {
                    return candidate.name == property.name;
                });
            if (authored != source.material.properties.end())
            {
                if (const auto* reference =
                    std::get_if<experiment::TextureReference>(&authored->value))
                {
                    binding.textureGuid.m_guid = reference->assetId.value;
                }
            }
            else if (const auto* defaultGuid =
                std::get_if<FileGuid>(&property.defaultValue))
            {
                binding.textureGuid = *defaultGuid;
            }

            bindings.push_back(std::move(binding));
        }

        outTextureBindings = std::move(bindings);
        outError.clear();
        return true;
    }
}
