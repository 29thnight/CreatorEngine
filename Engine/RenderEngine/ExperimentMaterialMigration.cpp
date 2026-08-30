#include "ExperimentMaterialMigration.h"

#include "Experiment/MaterialResolver.h"
#include "Material.h"
#include "ShaderMeta.h"
#include "StandardMaterialProperty.h"

#include <algorithm>
#include <limits>

namespace ExperimentMaterialMigration
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

    bool ConvertLegacyMaterial(const Material& legacy, const ShaderMeta& meta,
        experiment::Material& outMaterial, std::string& outError)
    {
        experiment::Material material;
        // 자산 정체성은 m_fileGuid다 — m_materialGuid는 런타임 HashedGuid라
        // AssetId로 나르면 안 된다.
        material.assetId.value = legacy.m_fileGuid.m_guid;
        material.shaderAssetId.value = legacy.m_shaderMetaGuid.m_guid;
        material.name = legacy.m_name;
        material.blendMode =
            MaterialRenderingMode::Transparent == legacy.m_renderingMode
            ? experiment::MaterialBlendMode::Transparent
            : experiment::MaterialBlendMode::Opaque;
        material.keywordSelections.assign(
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
            // flow 승격(I5-M5) — 저작 정본은 논리 값이고 m_flowInfo는 legacy
            // 사본이다. 승계하지 않으면 meta 기본값 0이 저작된 바람을 지운다.
            else if (desc.name == standard_material::property::FlowWindVector
                && ShaderPropertyType::Float4 == desc.type)
            {
                property.value = math::vector4{
                    legacy.m_flowInfo.m_windVector.x,
                    legacy.m_flowInfo.m_windVector.y,
                    legacy.m_flowInfo.m_windVector.z,
                    legacy.m_flowInfo.m_windVector.w };
            }
            else if (desc.name == standard_material::property::FlowUvScroll
                && ShaderPropertyType::Float2 == desc.type)
            {
                property.value = math::vector2{
                    legacy.m_flowInfo.m_uvScroll.x,
                    legacy.m_flowInfo.m_uvScroll.y };
            }
            else
            {
                continue; // 저작값 없음 — packer가 ShaderMeta 기본값을 쓴다.
            }
            material.properties.push_back(std::move(property));
        }

        outMaterial = std::move(material);
        outError.clear();
        return true;
    }

    bool ConvertToLegacyMaterial(const experiment::Material& material,
        const ShaderMeta* meta, Material& outLegacy, std::string& outError)
    {
        // Material은 복사/이동 대입이 없다 — 성분을 지역에서 완성한 뒤 성공이
        // 확정된 마지막에만 outLegacy의 필드에 쓴다(부분 출력 금지).
        std::vector<std::uint16_t> keywordSelections;
        if (!material.keywords.empty())
        {
            if (nullptr == meta)
            {
                outError = "이름 기반 keywords는 meta 없이 legacy 인덱스로"
                    " 정규화할 수 없다: " + material.name;
                return false;
            }
            if (!experiment::NormalizeMaterialKeywordSelections(material,
                meta->keywords, keywordSelections, outError))
            {
                return false;
            }
        }
        else
        {
            keywordSelections = material.keywordSelections;
        }

        std::vector<MaterialPropertyValue> propertyValues;

        for (const experiment::MaterialProperty& property : material.properties)
        {
            if (property.name.empty())
            {
                outError = "빈 property 이름이 있다: " + material.name;
                return false;
            }
            MaterialPropertyValue value;
            value.m_name = property.name;
            if (const auto* boolean = std::get_if<bool>(&property.value))
            {
                value.m_boolValue = *boolean;
            }
            else if (const auto* integer =
                std::get_if<std::int32_t>(&property.value))
            {
                value.m_integerValue = *integer;
            }
            else if (const auto* unsignedInteger =
                std::get_if<std::uint32_t>(&property.value))
            {
                if (*unsignedInteger > static_cast<std::uint32_t>(
                    std::numeric_limits<std::int32_t>::max()))
                {
                    outError = "uint property가 legacy int32 범위를 넘는다: "
                        + property.name;
                    return false;
                }
                value.m_integerValue =
                    static_cast<std::int32_t>(*unsignedInteger);
            }
            else if (const auto* scalar = std::get_if<float>(&property.value))
            {
                value.m_numericValue = { *scalar };
            }
            else if (const auto* vector2 =
                std::get_if<math::vector2>(&property.value))
            {
                value.m_numericValue = { vector2->x, vector2->y };
            }
            else if (const auto* vector3 =
                std::get_if<math::vector3>(&property.value))
            {
                value.m_numericValue = { vector3->x, vector3->y, vector3->z };
            }
            else if (const auto* vector4 =
                std::get_if<math::vector4>(&property.value))
            {
                value.m_numericValue = { vector4->x, vector4->y, vector4->z,
                    vector4->w };
            }
            else if (const auto* reference =
                std::get_if<experiment::TextureReference>(&property.value))
            {
                value.m_textureGuid.m_guid = reference->assetId.value;
            }
            else
            {
                // string 등 legacy에 표현이 없는 값 — 조용히 떨구면 저작이
                // 사라진다.
                outError = "legacy 재질에 표현이 없는 property 값이다: "
                    + property.name;
                return false;
            }
            propertyValues.push_back(std::move(value));
        }

        // 성공 확정 — 이제부터만 outLegacy에 쓴다.
        outLegacy.m_fileGuid.m_guid = material.assetId.value;
        outLegacy.m_shaderMetaGuid.m_guid = material.shaderAssetId.value;
        outLegacy.m_name = material.name;
        outLegacy.m_renderingMode =
            experiment::MaterialBlendMode::Transparent == material.blendMode
            ? MaterialRenderingMode::Transparent
            : MaterialRenderingMode::Opaque;
        outLegacy.m_keywordSelections = std::move(keywordSelections);
        outLegacy.m_propertyValues = std::move(propertyValues);

        // legacy 스칼라 소비자(Forward snapshot 호환 필드) 동기화 —
        // 논리 값이 정본이고 m_materialInfo는 그 사본이다.
        if (const MaterialPropertyValue* baseColor = FindLegacyValue(outLegacy,
            standard_material::property::BaseColor);
            baseColor && baseColor->m_numericValue.size() == 4u)
        {
            outLegacy.m_materialInfo.m_baseColor = {
                baseColor->m_numericValue[0], baseColor->m_numericValue[1],
                baseColor->m_numericValue[2], baseColor->m_numericValue[3] };
        }
        if (const MaterialPropertyValue* metallic = FindLegacyValue(outLegacy,
            standard_material::property::Metallic);
            metallic && metallic->m_numericValue.size() == 1u)
        {
            outLegacy.m_materialInfo.m_metallic = metallic->m_numericValue[0];
        }
        if (const MaterialPropertyValue* roughness = FindLegacyValue(outLegacy,
            standard_material::property::Roughness);
            roughness && roughness->m_numericValue.size() == 1u)
        {
            outLegacy.m_materialInfo.m_roughness = roughness->m_numericValue[0];
        }
        if (const MaterialPropertyValue* flowWind = FindLegacyValue(outLegacy,
            standard_material::property::FlowWindVector);
            flowWind && flowWind->m_numericValue.size() == 4u)
        {
            outLegacy.m_flowInfo.m_windVector = {
                flowWind->m_numericValue[0], flowWind->m_numericValue[1],
                flowWind->m_numericValue[2], flowWind->m_numericValue[3] };
        }
        if (const MaterialPropertyValue* flowUv = FindLegacyValue(outLegacy,
            standard_material::property::FlowUvScroll);
            flowUv && flowUv->m_numericValue.size() == 2u)
        {
            outLegacy.m_flowInfo.m_uvScroll = {
                flowUv->m_numericValue[0], flowUv->m_numericValue[1] };
        }

        outError.clear();
        return true;
    }
}
