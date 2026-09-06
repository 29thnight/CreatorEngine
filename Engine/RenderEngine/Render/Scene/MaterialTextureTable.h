#pragma once

#include "../Graph/EnhancedRenderPass.h"
#include "../../RHI/RHIShaderCompiler.h"
#include "../../StandardMaterialProperty.h"

// Pass buffers/IBL occupy t0..t15. Material textures have a separate range in
// space0 because the current RHI layout does not expose register spaces.
// Table length and semantic defaults come from reflection, including holes.
namespace MaterialTextureTable
{
    inline constexpr uint32_t FirstRegister = 16;
    inline constexpr uint32_t RegisterLimit = 128; // SM5 SRV register limit
    using Schema = std::vector<std::string>;
    using Views = std::vector<RHIBindingDesc>;

    inline bool Add(Schema& schema, std::string_view name, uint32_t index,
        uint32_t space, std::string& error)
    {
        if (name.empty() || space != 0 || index < FirstRegister || index >= RegisterLimit)
        {
            error = "Material texture must use t16..t127/space0: " + std::string(name);
            return false;
        }
        const auto slot = index - FirstRegister;
        if (schema.size() <= slot) schema.resize(slot + 1);
        if (!schema[slot].empty() || std::find(schema.begin(), schema.end(), name) != schema.end())
        {
            error = "Duplicate material texture name/register: " + std::string(name);
            return false;
        }
        schema[slot] = name;
        return true;
    }

    inline bool FromLayout(const ShaderMetaBindingLayout& layout, Schema& schema,
        std::string& error)
    {
        schema.assign(1, {}); // An unused table still receives one null descriptor.
        for (const auto& binding : layout.properties)
        {
            if (binding.propertyType != ShaderPropertyType::Texture2D) continue;
            if (binding.resourceKind != RHIShaderResourceKind::Texture)
            {
                error = "Material Texture2D property is not a texture resource: " + binding.name;
                return false;
            }
            if (!Add(schema, binding.name, binding.registerIndex, binding.registerSpace, error))
                return false;
        }
        return true;
    }

    inline bool FromReflection(const RHIShaderReflection& reflection, Schema& schema,
        std::string& error)
    {
        schema.assign(1, {});
        for (const auto& resource : reflection.resources)
        {
            if (resource.kind != RHIShaderResourceKind::Texture
                || resource.registerIndex < FirstRegister) continue;
            if (resource.arrayElements != 1)
            {
                error = "Material texture arrays are unsupported: " + resource.name;
                return false;
            }
            if (!Add(schema, resource.name, resource.registerIndex, resource.registerSpace, error))
                return false;
        }
        return true;
    }

    inline bool Reflect(std::string_view file, std::string_view entry,
        const RHIShaderPermutation& permutation, Schema& schema, std::string& error)
    {
        RHIShaderReflection reflection;
        return RHIShaderCompiler::ReflectFile(file, entry, "ps_5_0",
            RHIShaderCompiler::GetOutput(), permutation, reflection, error)
            && FromReflection(reflection, schema, error);
    }

    inline bool ValidateLayout(const ShaderMetaBindingLayout& layout,
        const RHIShaderReflection& reflection, std::string& error)
    {
        Schema authored, shader;
        if (!FromLayout(layout, authored, error) || !FromReflection(reflection, shader, error))
            return false;
        if (authored != shader)
        {
            error = "ShaderMeta must declare every reflected material texture";
            return false;
        }
        return true;
    }

    template <typename Snapshot>
    bool ValidateSnapshot(const Snapshot& snapshot, std::string& error)
    {
        Schema schema, owners(1);
        if (!FromLayout(snapshot.bindingLayout, schema, error)) return false;
        for (const auto& texture : snapshot.textureBindings)
        {
            if (!texture.coordinates.IsValid())
            { error = "Invalid texture UV set/transform: " + texture.propertyName; return false; }
            if (!Add(owners, texture.propertyName, texture.registerIndex, texture.registerSpace, error))
                return false;
        }
        if (owners != schema)
        {
            error = "Material texture owners differ from the reflected schema";
            return false;
        }
        return true;
    }

    template <typename Snapshot>
    std::vector<Texture*> Owners(const Snapshot& snapshot)
    {
        Schema schema;
        std::string error;
        if (!FromLayout(snapshot.bindingLayout, schema, error)) return {};
        std::vector<Texture*> owners(schema.size());
        for (const auto& texture : snapshot.textureBindings)
            owners[texture.registerIndex - FirstRegister] = texture.textureOwner.get();
        return owners; // Callers validate the complete snapshot before indexing.
    }

    // CPU/GPU ABI for offset + R * S * UV; w of U selects UV0/UV1.
    struct GpuCoordinates
    {
        std::array<float, 4> u{1, 0, 0, 0};
        std::array<float, 4> v{0, 1, 0, 0};
    };
    static_assert(sizeof(GpuCoordinates) == 32);
    inline GpuCoordinates PackCoordinates(const assets::TextureCoordinates& uv)
    {
        const float c = std::cos(uv.rotation), s = std::sin(uv.rotation);
        return {{c * uv.scale[0], -s * uv.scale[1], uv.offset[0], float(uv.set)},
            {s * uv.scale[0], c * uv.scale[1], uv.offset[1], 0.f}};
    }
    template <typename Snapshot>
    std::vector<assets::TextureCoordinates> Coordinates(const Snapshot& snapshot)
    {
        Schema schema; std::string error;
        if (!FromLayout(snapshot.bindingLayout, schema, error)) return {};
        std::vector<assets::TextureCoordinates> result(schema.size());
        for (const auto& binding : snapshot.textureBindings)
            result[binding.registerIndex - FirstRegister] = binding.coordinates;
        return result;
    }
    template <typename Snapshot>
    bool ValidateMeshCoordinates(const Snapshot& snapshot, uint32_t mask, std::string& error)
    {
        for (const auto& binding : snapshot.textureBindings)
            if (binding.coordinates.set == 1 && !assets::Has(mask, assets::VertexAttribute::Uv1))
            { error = "Material requires absent UV1: " + binding.propertyName; return false; }
        return true;
    }
    inline auto UploadCoordinates(IRenderDeviceServices& resources,
        const std::vector<assets::TextureCoordinates>& coordinates)
    {
        std::array<GpuCoordinates, RegisterLimit - FirstRegister> table{};
        for (size_t i = 0; i < coordinates.size() && i < table.size(); ++i)
            table[i] = PackCoordinates(coordinates[i]);
        return resources.UploadConstants(table.data(), sizeof(table));
    }

    inline std::vector<Texture*> LegacyOwners(const Schema& schema, const EnhancedDrawItem& draw)
    {
        std::vector<Texture*> owners(schema.size());
        for (size_t i = 0; i < schema.size(); ++i)
        {
            using namespace standard_material::property;
            if (schema[i] == BaseColorMap) owners[i] = draw.baseColor;
            else if (schema[i] == NormalMap) owners[i] = draw.normalMap;
            else if (schema[i] == OrmMap) owners[i] = draw.occRoughMetal;
            else if (schema[i] == EmissiveMap) owners[i] = draw.emissive;
        }
        return owners;
    }

    inline bool Upload(IRenderTextureCache& cache, const Schema& schema,
        const std::vector<Texture*>& owners, Views& views, std::string& error, bool legacy)
    {
        if (schema.size() != owners.size())
        {
            error = "Material texture table size mismatch";
            return false;
        }
        views.clear();
        views.reserve(schema.size());
        for (size_t i = 0; i < schema.size(); ++i)
        {
            if (schema[i].empty())
            {
                views.push_back(RHIBindingDesc::Srv2D({}, RHIFormat::RGBA8Unorm).OrNull());
                continue;
            }
            RHITextureEntry uploaded{};
            if (legacy && !owners[i] && schema[i] == standard_material::property::EmissiveMap)
                uploaded = cache.GetBlackTexture(error);
            else if (!owners[i] && schema[i] == standard_material::property::OrmMap)
                uploaded = cache.GetOrmNeutralTexture(error);
            else
                uploaded = cache.GetOrUpload(owners[i], error); // Missing AO and owned emission use neutral white.
            if (!error.empty() || !uploaded.IsValid()) return false;
            views.push_back(RHIBindingDesc::Srv2D(uploaded.handle, uploaded.format, 0, uploaded.mipLevels));
        }
        return true;
    }
}
