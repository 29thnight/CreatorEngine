#include "ExperimentMaterialSealing.h"

#include "../../Experiment/MaterialPropertyBlock.h"
#include "../../ExperimentMaterialMigration.h"
#include "../../ExperimentMaterialResolveBinding.h" // I5-D5c3-2
#include "../../DataSystem.h" // I7-C1: catalog 소유자
#include "../../Material.h"
#include "../../ShaderMeta.h"
#include "../../StandardMaterialProperty.h" // I5-D5c5

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

    namespace
    {
        // 논리 property 조회 — 없으면 기본값을 유지한다(ShaderMeta 선언
        // 기본값은 packer의 ApplyDefault가 CB에 채운다. 여기 스칼라는 legacy
        // 호환 채널이라 CB와 별개다).
        template <typename T>
        [[nodiscard]] const T* FindPropertyValue(
            const experiment::Material& material, std::string_view name)
        {
            for (const experiment::MaterialProperty& property
                : material.properties)
            {
                if (property.name != name) continue;
                return std::get_if<T>(&property.value);
            }
            return nullptr;
        }
    }

    bool BuildSealSourceFromAuthored(const experiment::Material& authored,
        const ShaderMeta& meta, SealSource& outSource, std::string& outError,
        const assets::ModelAssetGeneration* generation)
    {
        SealSource source;
        source.material = authored;
        source.debugName = authored.name;

        // texture owner는 M2 resolver 정본이다(c3-2와 같은 함수). 실패는
        // false로 올려 호출부가 legacy 시공으로 내려가게 한다 — 텍스처가
        // 조용히 빠진 그림보다 전환기 경로가 낫다.
        if (!ApplyAuthoredTextures(source, meta, outError, nullptr, nullptr,
            generation))
        {
            return false;
        }

        if (const auto* wind = FindPropertyValue<math::vector4>(authored,
            standard_material::property::FlowWindVector))
        {
            source.flow.windVector = *wind;
        }
        if (const auto* scroll = FindPropertyValue<math::vector2>(authored,
            standard_material::property::FlowUvScroll))
        {
            source.flow.uvScroll = *scroll;
        }
        if (const auto* baseColor = FindPropertyValue<math::vector4>(authored,
            standard_material::property::BaseColor))
        {
            source.baseColorFactor = math::color{ baseColor->x, baseColor->y,
                baseColor->z, baseColor->w };
        }
        if (const auto* metallic = FindPropertyValue<float>(authored,
            standard_material::property::Metallic))
        {
            source.metallic = *metallic;
        }
        if (const auto* roughness = FindPropertyValue<float>(authored,
            standard_material::property::Roughness))
        {
            source.roughness = *roughness;
        }

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

    bool ApplyAuthoredTextures(SealSource& source, const ShaderMeta& meta,
        std::string& outError, std::size_t* outCooked,
        std::size_t* outSourceFallback,
        const assets::ModelAssetGeneration* generation)
    {
        // I7-C1 — catalog가 서면 texture artifact를 cooked 우선으로 해석한다.
        // c3-2가 "catalog가 서면 이 자리가 그대로 cooked 우선이 된다"고 적어
        // 둔 그 자리다. 미게시(저작 트리)에서는 nullptr이라 예전처럼 source.
        //
        // shared_ptr을 이 스코프에서 붙잡는다 — services는 raw 포인터를 나르고,
        // 마운트가 렌더 중에 표를 갈아 끼울 수 있다.
        const auto catalog = DataSystems->GetCookedCatalog();
        const experiment::MaterialResolveServices services =
            experiment::MakeDataSystemMaterialResolveServices(catalog.get(),
                generation);
        experiment::ResolvedMaterial resolved;
        if (!experiment::ResolveMaterial(source.material, services, resolved,
            outError))
        {
            return false;
        }

        // meta 선언 순서를 유지한다 — SealCore가 이 순서를 전제하지는 않지만
        // (이름으로 찾는다) 진단 로그와 게이트 대조가 순서에 기댄다.
        std::vector<SealTextureOwner> textures;
        for (const ShaderPropertyDesc& desc : meta.properties)
        {
            if (ShaderPropertyType::Texture2D != desc.type) continue;
            const auto found = std::find_if(resolved.textures.begin(),
                resolved.textures.end(),
                [&desc](const experiment::ResolvedMaterialTexture& candidate)
                {
                    return candidate.propertyName == desc.name;
                });
            textures.push_back({ desc.name,
                found != resolved.textures.end() ? found->owner : nullptr });
        }
        source.textures = std::move(textures);

        // I5-D5c5 — useNormalMap은 인스턴스 채널에서 **유일하게 살아 있는**
        // 소비다(ForwardShade:441·GBuffer:237이 usePropertyBlock 분기 밖에서
        // 무조건 읽는다). legacy는 m_materialInfo.m_useNormalMap을 줬는데,
        // 그 의미는 "노멀맵이 붙어 있는가"다 — 저작 정본에서는 resolver가
        // normalMap owner를 실제로 준 것으로 같은 뜻을 만든다.
        source.useNormalMap = 0u;
        for (const SealTextureOwner& texture : source.textures)
        {
            if (texture.propertyName
                != standard_material::property::NormalMap) continue;
            source.useNormalMap = (nullptr != texture.owner) ? 1u : 0u;
            break;
        }
        if (nullptr != outCooked) *outCooked = resolved.notes.cookedTextures;
        if (nullptr != outSourceFallback)
        {
            *outSourceFallback = resolved.notes.sourceFallbackTextures;
        }
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
