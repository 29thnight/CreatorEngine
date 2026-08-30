#include "MaterialScriptBinding.h"

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

    bool SetFloat(Material& material, const ShaderMeta& meta,
        std::string_view name, float value)
    {
        const ShaderPropertyDesc* desc = FindDesc(meta, name);
        if (!desc || ShaderPropertyType::Float != desc->type) return false;

        UpsertValue(material, name).m_numericValue = { value };
        SyncLegacyScalar(material, name, value);
        return true;
    }

    bool SetInt(Material& material, const ShaderMeta& meta,
        std::string_view name, std::int32_t value)
    {
        const ShaderPropertyDesc* desc = FindDesc(meta, name);
        if (nullptr == desc) return false;

        if (ShaderPropertyType::Int == desc->type)
        {
            UpsertValue(material, name).m_integerValue = value;
            return true;
        }
        if (ShaderPropertyType::Bool == desc->type)
        {
            // legacy TrySetValue도 Bool 바인딩에 int 4바이트를 받았다 —
            // C# 표면(SetMaterialInt)의 그 관용을 유지한다.
            UpsertValue(material, name).m_boolValue = (0 != value);
            return true;
        }
        return false;
    }

    bool SetFloatVector(Material& material, const ShaderMeta& meta,
        std::string_view name, std::span<const float> values)
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
        const FileGuid& guid)
    {
        UpsertValue(material, name).m_textureGuid = guid;
    }

    bool SetFloat(Material& material, std::string_view name, float value)
    {
        const std::shared_ptr<const ShaderMeta> meta =
            ResolveDeclaredMeta(material);
        return meta && SetFloat(material, *meta, name, value);
    }

    bool SetInt(Material& material, std::string_view name, std::int32_t value)
    {
        const std::shared_ptr<const ShaderMeta> meta =
            ResolveDeclaredMeta(material);
        return meta && SetInt(material, *meta, name, value);
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

    void SetBaseColor(Material& material, const math::color& color)
    {
        UpsertValue(material, standard_material::property::BaseColor)
            .m_numericValue = { color.r, color.g, color.b, color.a };
        material.m_materialInfo.m_baseColor = color;
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
