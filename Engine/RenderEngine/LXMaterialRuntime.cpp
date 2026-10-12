#include "LXMaterialRuntime.h"
#include "MaterialPropertyPacker.h"
#include "Experiment/Cooked/CookedCodeMaterial.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <set>

namespace LX::Runtime
{
    namespace
    {
        bool Fail(std::string& error, std::string message)
        {
            error = std::move(message);
            return false;
        }

        bool ValidateValue(const ShaderPropertyDesc& property, const MaterialPropertyValue& value, std::string& error)
        {
            if (!MaterialPropertyPacker::ValidateLogicalValue(property, value, error))
            {
                return false;
            }
            if (!std::ranges::all_of(value.m_numericValue, [](float number) { return std::isfinite(number); }))
            {
                return Fail(error, "LX runtime numeric value is not finite: " + property.name);
            }
            return true;
        }

        bool ValidateShader(const ShaderGeneration& shader, std::string& error)
        {
            const auto& meta = shader.meta;
            const auto& layout = shader.layout;
            if (meta.properties.size() != layout.properties.size() || layout.constantBufferByteSize > 65536)
            {
                return Fail(error, "LX runtime contract and binding layout differ.");
            }
            std::set<std::string> names;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> regions;
            std::set<std::pair<std::uint32_t, std::uint32_t>> textureRegisters;
            for (const auto& property : meta.properties)
            {
                const auto* binding = MaterialPropertyPacker::FindBinding(layout, property.name);
                if (property.name.empty() || !names.insert(property.name).second || !binding || binding->propertyType != property.type)
                {
                    return Fail(error, "LX runtime has an unknown, duplicate or mismatched property: " + property.name);
                }
                if (property.type == ShaderPropertyType::Texture2D)
                {
                    if (binding->resourceKind != RHIShaderResourceKind::Texture ||
                        !textureRegisters.emplace(binding->registerSpace, binding->registerIndex).second)
                    {
                        return Fail(error, "LX runtime texture binding is invalid: " + property.name);
                    }
                    continue;
                }
                const auto bytes = MaterialPropertyPacker::LogicalByteSize(property.type);
                if (!bytes || layout.constantBufferName.empty() || binding->resourceKind != RHIShaderResourceKind::ConstantBuffer ||
                    binding->resourceName != layout.constantBufferName || binding->registerIndex != layout.constantBufferRegister ||
                    binding->registerSpace != layout.constantBufferSpace || binding->byteSize < bytes ||
                    binding->byteOffset > layout.constantBufferByteSize ||
                    binding->byteSize > layout.constantBufferByteSize - binding->byteOffset)
                {
                    return Fail(error, "LX runtime constant binding is invalid: " + property.name);
                }
                regions.emplace_back(binding->byteOffset, binding->byteOffset + binding->byteSize);
            }
            std::ranges::sort(regions);
            for (std::size_t i = 1; i < regions.size(); ++i)
            {
                if (regions[i].first < regions[i - 1].second)
                {
                    return Fail(error, "LX runtime constant properties overlap.");
                }
            }
            names.clear();
            for (const auto& axis : meta.keywords)
            {
                if (axis.name.empty() || !names.insert(axis.name).second || axis.values.empty() ||
                    axis.values.size() > std::numeric_limits<std::uint16_t>::max())
                {
                    return Fail(error, "LX runtime keyword axis is invalid.");
                }
                std::set<std::string> values;
                for (const auto& value : axis.values)
                {
                    if (value.empty() || !values.insert(value).second)
                    {
                        return Fail(error, "LX runtime keyword values are invalid.");
                    }
                }
            }
            return true;
        }
    }

    bool ValidateShaderGeneration(const ShaderGeneration& shader, std::string& error)
    {
        return ValidateShader(shader, error);
    }

    bool CreateCodeShader(const ShaderMeta& meta, const ShaderMetaBindingLayout& layout,
                          ShaderMetaHandle handle, own::shared_owner<const ShaderGeneration>& result,
                          std::string& error)
    {
        if (meta.assetOrigin && !meta.codeProgram)
        {
            return Fail(error, "Mounted metadata is not a prepared code shader program.");
        }
        if (meta.generatedMaterial)
        {
            return Fail(error, "Generated Graph contracts require their owning graph generation.");
        }
        // Weak entries reuse contract owners across frame seals and permutations;
        // cache retirement cannot destroy an instance's accepted shader generation.
        static std::mutex cacheMutex;
        static std::map<std::uint32_t, std::vector<own::weak_owner<const ShaderGeneration>>> cache;
        if (handle.IsValid())
        {
            std::lock_guard lock(cacheMutex);
            const auto found = cache.find(handle.slot);
            if (found != cache.end())
            {
                std::erase_if(found->second, [](const auto& entry) { return entry.expired(); });
                for (const auto& entry : found->second)
                {
                    if (const auto accepted = entry.lock(); accepted && accepted->codeHandle == handle &&
                        accepted->meta == meta && accepted->meta.codeProgramIdentity == meta.codeProgramIdentity
                        && accepted->layout == layout)
                    {
                        result = accepted;
                        error.clear();
                        return true;
                    }
                }
            }
        }
        ShaderGeneration candidate;
        candidate.meta = meta;
        candidate.layout = layout;
        candidate.codeHandle = handle;
        candidate.codeProgram = meta.codeProgram;
        if (meta.codeProgram && (meta.codeProgram->layout != layout
            || meta.codeProgram->shaderMetaAssetId.value != meta.guid.m_guid))
        {
            return Fail(error, "Cooked code shader does not match its verified metadata/layout.");
        }
        if (!ValidateShader(candidate, error))
        {
            return false;
        }
        auto published = own::make_shared<const ShaderGeneration>(std::move(candidate));
        if (handle.IsValid())
        {
            std::lock_guard lock(cacheMutex);
            cache[handle.slot].push_back(published);
        }
        result = std::move(published);
        error.clear();
        return true;
    }

    bool BuildInstance(own::shared_owner<const ShaderGeneration> shader,
                       std::span<const MaterialPropertyValue> values,
                       std::span<const std::uint16_t> keywords,
                       std::span<const MaterialTextureOwner> textures,
                       own::shared_owner<const Instance>& result, std::string& error)
    {
        if (!shader)
        {
            return Fail(error, "LX runtime instance requires a shader owner.");
        }
        if (!ValidateShader(*shader, error))
        {
            return false;
        }
        Instance candidate;
        candidate.shader = std::move(shader);
        const auto& meta = candidate.shader->meta;
        const auto& layout = candidate.shader->layout;
        candidate.uniforms.resize(layout.constantBufferByteSize);
        std::set<std::string> seen;
        for (const auto& value : values)
        {
            if (!seen.insert(value.m_name).second || !MaterialPropertyPacker::FindBinding(layout, value.m_name))
            {
                return Fail(error, "LX runtime has an unknown or duplicate value: " + value.m_name);
            }
        }
        for (const auto& property : meta.properties)
        {
            MaterialPropertyValue value;
            const auto existing = std::ranges::find(values, property.name, &MaterialPropertyValue::m_name);
            if (existing != values.end())
            {
                value = *existing;
            }
            else if (!MaterialPropertyPacker::ApplyDefault(property, value, error))
            {
                return false;
            }
            const auto* binding = MaterialPropertyPacker::FindBinding(layout, property.name);
            if (!ValidateValue(property, value, error) ||
                !MaterialPropertyPacker::PackProperty(property, *binding, value, candidate.uniforms, error))
            {
                return false;
            }
            candidate.properties.push_back(std::move(value));
        }
        if (!keywords.empty() && keywords.size() != meta.keywords.size())
        {
            return Fail(error, "LX runtime keyword selection count differs from its contract.");
        }
        candidate.keywordSelections.resize(meta.keywords.size());
        for (std::size_t i = 0; i < keywords.size(); ++i)
        {
            if (keywords[i] >= meta.keywords[i].values.size())
            {
                return Fail(error, "LX runtime keyword selection is out of range.");
            }
            candidate.keywordSelections[i] = keywords[i];
        }
        seen.clear();
        for (const auto& texture : textures)
        {
            const auto* binding = MaterialPropertyPacker::FindBinding(layout, texture.propertyName);
            if (!seen.insert(texture.propertyName).second || !binding || binding->propertyType != ShaderPropertyType::Texture2D)
            {
                return Fail(error, "LX runtime texture owner differs from its contract: " + texture.propertyName);
            }
            if (texture.textureOwner)
            {
                candidate.textureOwners.push_back(texture);
            }
        }
        result = own::make_shared<const Instance>(std::move(candidate));
        error.clear();
        return true;
    }

    bool SetValue(const Instance& instance, const MaterialPropertyValue& value,
                  own::shared_owner<const Instance>& result, std::string& error)
    {
        if (!instance.shader)
        {
            return Fail(error, "LX runtime instance has no shader owner.");
        }
        const auto& meta = instance.shader->meta;
        const auto property = std::ranges::find(meta.properties, value.m_name, &ShaderPropertyDesc::name);
        const auto* binding = MaterialPropertyPacker::FindBinding(instance.shader->layout, value.m_name);
        if (property == meta.properties.end() || !binding || !property->exposed)
        {
            return Fail(error, "LX runtime value is unknown or private.");
        }
        if (!ValidateValue(*property, value, error))
        {
            return false;
        }
        Instance candidate(instance);
        const auto existing = std::ranges::find(candidate.properties, value.m_name, &MaterialPropertyValue::m_name);
        if (existing == candidate.properties.end())
        {
            return Fail(error, "LX runtime value is missing from its snapshot.");
        }
        if (!MaterialPropertyPacker::PackProperty(*property, *binding, value, candidate.uniforms, error))
        {
            return false;
        }
        if (property->type == ShaderPropertyType::Texture2D && existing->m_textureGuid != value.m_textureGuid)
        {
            std::erase_if(candidate.textureOwners, [&](const auto& owner) { return owner.propertyName == value.m_name; });
        }
        *existing = value;
        result = own::make_shared<const Instance>(std::move(candidate));
        error.clear();
        return true;
    }

    bool SetTextureOwner(const Instance& instance, std::string_view name, own::shared_owner<const Texture> owner,
                         own::shared_owner<const Instance>& result, std::string& error)
    {
        const auto* binding = instance.shader ? MaterialPropertyPacker::FindBinding(instance.shader->layout, name) : nullptr;
        if (!binding || binding->propertyType != ShaderPropertyType::Texture2D)
        {
            return Fail(error, "LX runtime texture owner is unknown.");
        }
        Instance candidate(instance);
        std::erase_if(candidate.textureOwners, [&](const auto& entry) { return entry.propertyName == name; });
        if (owner)
        {
            candidate.textureOwners.push_back({std::string(name), std::move(owner)});
        }
        result = own::make_shared<const Instance>(std::move(candidate));
        error.clear();
        return true;
    }

    bool SetKeyword(const Instance& instance, std::string_view axis, std::string_view value,
                    own::shared_owner<const Instance>& result, std::string& error)
    {
        if (!instance.shader)
        {
            return Fail(error, "LX runtime instance has no shader owner.");
        }
        const auto& axes = instance.shader->meta.keywords;
        const auto found = std::ranges::find(axes, axis, &ShaderKeywordAxis::name);
        if (found == axes.end())
        {
            return Fail(error, "LX runtime keyword axis is unknown.");
        }
        const auto selected = std::ranges::find(found->values, value);
        if (selected == found->values.end())
        {
            return Fail(error, "LX runtime keyword value is unknown.");
        }
        const auto index = static_cast<std::size_t>(found - axes.begin());
        if (index >= instance.keywordSelections.size())
        {
            return Fail(error, "LX runtime keyword snapshot is incomplete.");
        }
        Instance candidate(instance);
        candidate.keywordSelections[index] = static_cast<std::uint16_t>(selected - found->values.begin());
        result = own::make_shared<const Instance>(std::move(candidate));
        error.clear();
        return true;
    }
} // namespace LX::Runtime
