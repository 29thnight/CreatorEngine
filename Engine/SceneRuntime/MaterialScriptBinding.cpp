#include "MaterialScriptBinding.h"
#include "../RenderEngine/Experiment/MaterialInstance.h" // I5-D5c3

#include "DataSystem.h"
#include "Material.h"
#include "ShaderMeta.h"
#include "StandardMaterialProperty.h"

#include <algorithm>

namespace MaterialScriptBinding
{
    namespace
    {
        [[nodiscard]] std::shared_ptr<const ShaderMeta> ResolveDeclaredMeta(
            const Material& material)
        {
            if (FileGuid{} == material.m_shaderMetaGuid) return nullptr;
            std::string error;
            const ShaderMetaHandle handle =
                DataSystems->LoadShaderMetaHandle(material.m_shaderMetaGuid,
                    error);
            return DataSystems->ResolveShaderMeta(handle);
        }

        [[nodiscard]] const ShaderPropertyDesc* FindDesc(const ShaderMeta& meta,
            std::string_view name)
        {
            const auto found = std::find_if(meta.properties.begin(),
                meta.properties.end(), [&](const ShaderPropertyDesc& desc)
                {
                    return desc.name == name;
                });
            return found == meta.properties.end() ? nullptr : &*found;
        }

        [[nodiscard]] MaterialPropertyValue& UpsertValue(Material& material,
            std::string_view name)
        {
            const auto found = std::find_if(material.m_propertyValues.begin(),
                material.m_propertyValues.end(),
                [&](const MaterialPropertyValue& candidate)
                {
                    return candidate.m_name == name;
                });
            if (found != material.m_propertyValues.end()) return *found;
            MaterialPropertyValue value;
            value.m_name = std::string(name);
            material.m_propertyValues.push_back(std::move(value));
            return material.m_propertyValues.back();
        }

        void SyncLegacyScalar(Material& material, std::string_view name,
            float value)
        {
            if (name == standard_material::property::Metallic)
                material.m_materialInfo.m_metallic = value;
            else if (name == standard_material::property::Roughness)
                material.m_materialInfo.m_roughness = value;
        }
    }

    // I5-D5c3 — 편집된 논리 값을 experiment 인스턴스 override로 함께 얹는다.
    // 값 생성은 편집 인자에서 직접 한다: legacy 값 모델을 되읽으면 타입 태그가
    // 없어 variant 대안을 정할 수 없다(변환기 헤더가 적은 그 제약).
    void MirrorToInstance(experiment::MaterialInstance* instance,
        std::string_view name, experiment::MaterialPropertyValue value)
    {
        if (nullptr == instance) return;
        (void)instance->SetPropertyOverride(name, std::move(value));
    }

    bool SetFloat(Material& material, const ShaderMeta& meta,
        std::string_view name, float value,
        experiment::MaterialInstance* instance)
    {
        const ShaderPropertyDesc* desc = FindDesc(meta, name);
        if (!desc || ShaderPropertyType::Float != desc->type) return false;

        UpsertValue(material, name).m_numericValue = { value };
        SyncLegacyScalar(material, name, value);
        MirrorToInstance(instance, name, value);
        return true;
    }

    bool SetInt(Material& material, const ShaderMeta& meta,
        std::string_view name, std::int32_t value,
        experiment::MaterialInstance* instance)
    {
        const ShaderPropertyDesc* desc = FindDesc(meta, name);
        if (nullptr == desc) return false;

        if (ShaderPropertyType::Int == desc->type)
        {
            UpsertValue(material, name).m_integerValue = value;
            MirrorToInstance(instance, name, value);
            return true;
        }
        if (ShaderPropertyType::Bool == desc->type)
        {
            // legacy TrySetValue도 Bool 바인딩에 int 4바이트를 받았다 —
            // C# 표면(SetMaterialInt)의 그 관용을 유지한다.
            UpsertValue(material, name).m_boolValue = (0 != value);
            MirrorToInstance(instance, name, (0 != value));
            return true;
        }
        return false;
    }

    bool SetFloatVector(Material& material, const ShaderMeta& meta,
        std::string_view name, std::span<const float> values,
        experiment::MaterialInstance* instance)
    {
        const ShaderPropertyDesc* desc = FindDesc(meta, name);
        if (nullptr == desc) return false;
        std::size_t expected = 0;
        switch (desc->type)
        {
        case ShaderPropertyType::Float: expected = 1; break;
        case ShaderPropertyType::Float2: expected = 2; break;
        case ShaderPropertyType::Float3: expected = 3; break;
        case ShaderPropertyType::Float4: expected = 4; break;
        default: return false;
        }
        if (values.size() != expected) return false;

        UpsertValue(material, name).m_numericValue.assign(values.begin(),
            values.end());
        switch (expected)
        {
        case 1: MirrorToInstance(instance, name, values[0]); break;
        case 2: MirrorToInstance(instance, name,
            math::vector2{ values[0], values[1] }); break;
        case 3: MirrorToInstance(instance, name,
            math::vector3{ values[0], values[1], values[2] }); break;
        case 4: MirrorToInstance(instance, name,
            math::vector4{ values[0], values[1], values[2], values[3] });
            break;
        default: break;
        }
        if (1u == expected) SyncLegacyScalar(material, name, values[0]);
        if (4u == expected
            && name == standard_material::property::BaseColor)
        {
            material.m_materialInfo.m_baseColor =
                { values[0], values[1], values[2], values[3] };
        }
        return true;
    }

    float GetFloat(const Material& material, std::string_view name,
        float fallback)
    {
        const auto found = std::find_if(material.m_propertyValues.begin(),
            material.m_propertyValues.end(),
            [&](const MaterialPropertyValue& candidate)
            {
                return candidate.m_name == name;
            });
        return (found != material.m_propertyValues.end()
            && found->m_numericValue.size() == 1u)
            ? found->m_numericValue[0] : fallback;
    }

    void SetTexture(Material& material, std::string_view name,
        const FileGuid& guid, experiment::MaterialInstance* instance)
    {
        UpsertValue(material, name).m_textureGuid = guid;
        // logicalName/fallbackPath는 정본이 아니다(코덱 주석) — assetId만 싣는다.
        experiment::TextureReference reference;
        reference.assetId.value = guid.m_guid;
        MirrorToInstance(instance, name, std::move(reference));
    }

    bool SetFloat(Material& material, std::string_view name, float value,
        experiment::MaterialInstance* instance)
    {
        const std::shared_ptr<const ShaderMeta> meta =
            ResolveDeclaredMeta(material);
        return meta && SetFloat(material, *meta, name, value, instance);
    }

    bool SetInt(Material& material, std::string_view name, std::int32_t value,
        experiment::MaterialInstance* instance)
    {
        const std::shared_ptr<const ShaderMeta> meta =
            ResolveDeclaredMeta(material);
        // Bool 선언에도 int를 받는 관용은 코어가 유지하고, 인스턴스에는 코어가
        // 선언 타입대로 싣는다 — packer가 fail-closed로 검증한다.
        return meta && SetInt(material, *meta, name, value, instance);
    }

    math::color GetBaseColor(const Material& material)
    {
        const auto found = std::find_if(material.m_propertyValues.begin(),
            material.m_propertyValues.end(),
            [](const MaterialPropertyValue& candidate)
            {
                return candidate.m_name == standard_material::property::BaseColor;
            });
        if (found != material.m_propertyValues.end()
            && found->m_numericValue.size() == 4u)
        {
            return { found->m_numericValue[0], found->m_numericValue[1],
                found->m_numericValue[2], found->m_numericValue[3] };
        }
        return material.m_materialInfo.m_baseColor;
    }

    void SetBaseColor(Material& material, const math::color& color,
        experiment::MaterialInstance* instance)
    {
        UpsertValue(material, standard_material::property::BaseColor)
            .m_numericValue = { color.r, color.g, color.b, color.a };
        material.m_materialInfo.m_baseColor = color;
        MirrorToInstance(instance, standard_material::property::BaseColor,
            math::vector4{ color.r, color.g, color.b, color.a });
    }

    std::shared_ptr<Material> InstantiateOwned(const Material& origin,
        std::string_view newName)
    {
        auto clone = std::make_shared<Material>(origin);
        clone->m_name = newName.empty()
            ? origin.m_name + "_Instance" : std::string(newName);
        // S2c-1: m_fileGuid 비승계 — 클론은 자산이 아니라 씬 소유 인스턴스다.
        // 예전에는 MeshRenderer가 mesh 해석에 재질 fileGuid를 재사용하는
        // 편법 때문에 승계가 강제였는데, 이제 모델 출처는 renderer의
        // m_modelGuid가 진다.
        clone->m_fileGuid = FileGuid{};
        return clone;
    }
}
