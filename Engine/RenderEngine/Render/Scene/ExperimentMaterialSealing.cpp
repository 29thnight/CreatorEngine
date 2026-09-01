#include "ExperimentMaterialSealing.h"

#include "../../Experiment/MaterialPropertyBlock.h"
#include "../../ExperimentMaterialMigration.h"
#include "../../Material.h"
#include "../../ShaderMeta.h"

#include <algorithm>

namespace ExperimentMaterialSealing
{
    bool BuildSealSourceFromLegacy(const Material& legacy, const ShaderMeta& meta,
        SealSource& outSource, std::string& outError)
    {
        SealSource source;
        // legacy → experiment 변환은 ExperimentMaterialMigration이 단일
        // 정본이다(MaterialInfo 폴백 승계 포함). 여기서는 sealing 전용 부속만
        // 더한다: texture generation owner·flow·legacy 호환 스칼라.
        if (!ExperimentMaterialMigration::ConvertLegacyMaterial(legacy, meta,
            source.material, outError))
        {
            return false;
        }

        for (const ShaderPropertyDesc& desc : meta.properties)
        {
            if (ShaderPropertyType::Texture2D != desc.type) continue;
            source.textures.push_back({ desc.name,
                legacy.GetTextureMapShared(desc.name) });
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

    void ApplyAuthoredMaterial(SealSource& source,
        const experiment::Material& authored)
    {
        // debugName은 legacy 것을 유지한다 — 진단 로그의 이름이 슬라이스 경계에서
        // 바뀌면 기존 게이트 메시지 매칭이 조용히 깨진다.
        std::string debugName = std::move(source.debugName);
        source.material = authored;
        source.debugName = std::move(debugName);
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
