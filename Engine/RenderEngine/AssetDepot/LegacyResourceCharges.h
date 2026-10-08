#pragma once

#include "LegacyResourceCache.h"
#include "TextureAssetRuntime.h"
#include "../Material.h"
#include "../Experiment/ModelData.h"

// Reachable CPU payload capacity charges, not allocator/control-block overhead
// or measured reclaimed bytes.
// Shared dependencies may be counted repeatedly rather than under-budgeted.
namespace asset_cache_detail
{
    class Charge
    {
    public:
        void Add(std::size_t bytes) noexcept { AddCharge(bytes_, bytes); }
        template<class T>
        void Vector(const std::vector<T>& values) noexcept
        {
            Add(values.capacity() > SIZE_MAX / sizeof(T) ? SIZE_MAX : values.capacity() * sizeof(T));
        }
        void String(const std::string& value) noexcept
        {
            Add(value.capacity());
            Add(1u);
        }
        void Path(const std::filesystem::path& value) noexcept
        {
            const auto capacity = value.native().capacity();
            Add(capacity > SIZE_MAX / sizeof(std::filesystem::path::value_type)
                ? SIZE_MAX : capacity * sizeof(std::filesystem::path::value_type));
        }
        void Properties(const std::vector<MaterialPropertyValue>& values) noexcept
        {
            Vector(values);
            for (const auto& value : values)
            {
                String(value.m_name);
                Vector(value.m_numericValue);
            }
        }
        void Shader(const LX::Runtime::ShaderGeneration& shader) noexcept
        {
            Add(sizeof(shader));
            Path(shader.metaPath);
            String(shader.document);
            String(shader.source);
            const auto& meta = shader.meta;
            String(meta.name);
            Path(meta.source);
            Path(meta.originPath);
            Vector(meta.properties);
            for (const auto& property : meta.properties)
            {
                String(property.name);
                String(property.label);
                String(property.semantic);
                String(property.colorSpace);
            }
            Vector(meta.keywords);
            for (const auto& keyword : meta.keywords)
            {
                String(keyword.name);
                Vector(keyword.values);
                for (const auto& value : keyword.values)
                {
                    String(value);
                }
            }
            Vector(meta.passes);
            for (const auto& pass : meta.passes)
            {
                String(pass.name);
                if (pass.vertex)
                {
                    String(pass.vertex->entry);
                }
                if (pass.pixel)
                {
                    String(pass.pixel->entry);
                }
                if (pass.compute)
                {
                    String(pass.compute->entry);
                }
            }
            if (meta.generatedMaterial)
            {
                String(meta.generatedMaterial->generation);
                String(meta.generatedMaterial->sourceSha256);
                Vector(meta.generatedMaterial->samplers);
                for (const auto& sampler : meta.generatedMaterial->samplers)
                {
                    String(sampler.name);
                    String(sampler.description);
                }
            }
            String(shader.layout.constantBufferName);
            Vector(shader.layout.properties);
            for (const auto& property : shader.layout.properties)
            {
                String(property.name);
                String(property.resourceName);
            }
            Vector(shader.layout.samplers);
            for (const auto& sampler : shader.layout.samplers)
            {
                String(sampler.name);
                Vector(sampler.fields);
                for (const auto& field : sampler.fields)
                {
                    String(field.name);
                }
            }
        }
        std::size_t Bytes() const noexcept { return bytes_; }
    private:
        std::size_t bytes_{};
    };

    inline std::size_t LegacyTextureRetainedBytes(const Texture& texture) noexcept
    {
        Charge charge;
        charge.Add(sizeof(Texture));
        charge.Add(Texture::ImageRetainedCharge(texture.NonRehydratableImage()));
        charge.String(texture.m_name);
        charge.String(texture.m_extension);
        charge.String(texture.m_assetPath);
        const auto origin = texture.GetAssetOrigin();
        if (origin)
        {
            charge.Add(sizeof(AssetDepot::TextureAssetOrigin));
            charge.Add(origin->hardDependencyChargeBytes);
            charge.Vector(origin->hardDependencies);
            charge.Vector(origin->loadableDependencies);
        }
        return charge.Bytes();
    }

    inline std::size_t LegacyMaterialRetainedBytes(const Material& material) noexcept
    {
        Charge charge;
        charge.Add(sizeof(Material));
        charge.String(material.m_name);
        charge.String(material.m_baseColorTexName);
        charge.String(material.m_normalTexName);
        charge.String(material.m_ORM_TexName);
        charge.String(material.m_AO_TexName);
        charge.String(material.m_EmissiveTexName);
        charge.Properties(material.m_propertyValues);
        charge.Vector(material.m_keywordSelections);
        for (const auto& [name, bytes] : material.m_cbufferValues)
        {
            charge.Add(sizeof(name) + sizeof(bytes));
            charge.String(name);
            charge.Vector(bytes);
        }
        const auto runtime = material.GetLXMaterialInstance();
        if (runtime)
        {
            charge.Add(sizeof(LX::Runtime::Instance));
            charge.Properties(runtime->properties);
            charge.Vector(runtime->uniforms);
            charge.Vector(runtime->keywordSelections);
            charge.Vector(runtime->textureOwners);
            if (runtime->shader)
            {
                charge.Shader(*runtime->shader);
            }
        }
        for (const auto& texture : material.GetTextureOwners())
        {
            charge.Add(sizeof(MaterialTextureOwner));
            charge.String(texture.propertyName);
            if (texture.textureOwner)
            {
                charge.Add(LegacyTextureRetainedBytes(*texture.textureOwner));
            }
        }
        const auto& graph = material.GetMaterialGraphInstance();
        if (graph)
        {
            charge.Add(sizeof(material_graph::Instance));
            charge.Vector(graph->textures);
            charge.Vector(graph->description.parameters);
            charge.Vector(graph->description.textures);
            for (const auto& parameter : graph->description.parameters)
            {
                if (const auto* text = std::get_if<std::string>(&parameter.value))
                {
                    charge.String(*text);
                }
            }
            if (graph->generation)
            {
                charge.Add(graph->generation->RetainedPayloadBytes());
            }
        }
        return charge.Bytes();
    }

    inline std::size_t LegacyAuthoredMaterialRetainedBytes(const experiment::Material& material) noexcept
    {
        Charge charge;
        charge.Add(sizeof(material));
        charge.String(material.name);
        charge.Vector(material.properties);
        charge.Vector(material.keywords);
        charge.Vector(material.keywordSelections);
        for (const auto& keyword : material.keywords)
        {
            charge.String(keyword);
        }
        for (const auto& property : material.properties)
        {
            charge.String(property.name);
            if (const auto* text = std::get_if<std::string>(&property.value))
            {
                charge.String(*text);
            }
            if (const auto* texture = std::get_if<experiment::TextureReference>(&property.value))
            {
                charge.String(texture->logicalName);
                charge.Path(texture->fallbackPath);
            }
        }
        return charge.Bytes();
    }
}
