#include "MaterialGraphRuntime.h"
#include "MaterialGraphShaderMeta.h"
#include "MaterialPropertyPacker.h"
#include "Assets/AssetIdentityProfile.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "AuthoringReadNode.h"
#include "AuthoringWriteNode.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace material_graph
{
namespace
{
namespace ck = experiment::cooked;

bool Fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

bool ParseTextureId(std::string_view text, experiment::AssetId& id)
{
    return experiment::TryParseCanonicalAssetId(text, id) || assets::TryParseCanonicalUuidV8(text, id.value);
}

bool ValidTextureId(const experiment::AssetId& id)
{
    return experiment::IsAssetIdV4(id) || assets::IsUuidV8(id.value);
}

bool ValidateDescription(const InstanceDescription& description, std::string& error)
{
    if (!ValidTextureId(description.graphId) || description.parameters.size() > 128 ||
        description.textures.size() > 64)
        return Fail(error, "Invalid graph identity or oversized material instance.");
    std::set<LX::Id> ids;
    for (const auto& parameter : description.parameters)
    {
        if (parameter.id == 0 || !ids.insert(parameter.id).second)
            return Fail(error, "Invalid or duplicate material parameter ID.");
        const bool valid = std::visit(
            [](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                const auto finite = [](double number) {
                    return std::isfinite(number) && std::abs(number) <= std::numeric_limits<float>::max();
                };
                if constexpr (std::is_same_v<T, bool>)
                    return true;
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    return value >= INT32_MIN && value <= INT32_MAX;
                else if constexpr (std::is_same_v<T, double>)
                    return finite(value);
                else if constexpr (std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>>)
                    return std::ranges::all_of(value, finite);
                else
                    return false;
            },
            parameter.value);
        if (!valid)
            return Fail(error, "Material overrides require finite float32/int32-compatible typed values.");
    }
    for (const auto& texture : description.textures)
        if (texture.parameter == 0 || !ids.insert(texture.parameter).second || !ValidTextureId(texture.assetId))
            return Fail(error, "Invalid or duplicate material texture parameter ID/GUID.");
    return true;
}

bool Keys(const Authoring::ReadNode& node, std::initializer_list<std::string_view> allowed)
{
    if (!node.IsMap() || node.Size() != allowed.size())
        return false;
    std::set<std::string_view> seen;
    for (const auto entry : node.Map())
    {
        const auto key = entry.key.Scalar();
        if (std::ranges::find(allowed, key) == allowed.end() || !seen.insert(key).second)
            return false;
    }
    return true;
}

template<typename T>
T Scalar(const Authoring::ReadNode& node)
{
    if (!node.IsScalar())
        throw std::runtime_error("Expected material scalar.");
    if constexpr (std::is_same_v<T, std::string>)
        return node.AsString();
    else
        return node.As<T>();
}

void WriteDouble(Authoring::WriteNode node, double value)
{
    std::array<char, 64> buffer;
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                         std::chars_format::general, std::numeric_limits<double>::max_digits10);
    node.SetScalar(std::string_view(buffer.data(), converted.ptr));
}
} // namespace

std::shared_ptr<const Generation> GenerationStore::Load(const experiment::AssetId& id, const GenerationLoader& loader,
                                                        bool reload, std::string& error)
{
    error.clear();
    if (!ValidTextureId(id) || !loader)
    {
        error = "Material graph generation requires a canonical UUIDv4/UUIDv8 and a loader.";
        return {};
    }
    // Serialize publication with reload/removal. A late candidate cannot revive
    // a removed entry or overwrite a newer accepted candidate.
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    if (!reload && found != entries_.end())
        return found->second.owner;
    CookedProgram loaded;
    if (!loader(loaded, error))
        return {};
    std::vector<std::uint8_t> payload;
    if (!WriteCookedProgram(loaded.product, {}, payload, error) ||
        (loaded.product.materialShader && loaded.product.materialShader->meta.guid.m_guid != id.value) ||
        loaded.boundSource != BuildBoundSource(loaded.product.program) ||
        loaded.metadata != LX::WriteMaterialProgramMetadata(loaded.product.program))
    {
        if (error.empty())
            error = "Material generation metadata differs from its verified product.";
        return {};
    }
    ck::Sha256Digest digest;
    if (!ck::ComputeSha256(std::as_bytes(std::span(payload)), digest, error))
        return {};
    if (found != entries_.end() && found->second.digest == digest)
        return found->second.owner;
    if (serial_ == UINT64_MAX)
    {
        error = "Material generation counter is exhausted.";
        return {};
    }
    auto generation = std::make_shared<Generation>();
    generation->assetId = id;
    generation->generation = ++serial_;
    generation->cooked = std::move(loaded);
    entries_[id] = {generation, digest};
    return generation;
}

std::shared_ptr<const Generation> GenerationStore::Current(const experiment::AssetId& id) const
{
    std::lock_guard lock(mutex_);
    const auto found = entries_.find(id);
    return found == entries_.end() ? nullptr : found->second.owner;
}

void GenerationStore::Remove(const experiment::AssetId& id)
{
    std::lock_guard lock(mutex_);
    entries_.erase(id);
}

void GenerationStore::Clear()
{
    std::lock_guard lock(mutex_);
    entries_.clear();
}

bool LoadCookedGeneration(const ck::CookedAssetCatalog& catalog, const ck::ArtifactByteSource& bytes,
                          const experiment::AssetId& id, const LX::LXMaterialAsset* source, CookedProgram& result,
                          std::string& error)
{
    CookedProgram candidate;
    if (!catalog.OpenMaterialProgram(id, bytes, {}, candidate, error))
        return false;
    if (source)
    {
        const auto generated = LX::GenerateMaterialSlang(*source);
        if (!generated || generated->slang != candidate.product.program.slang ||
            LX::WriteMaterialProgramMetadata(*generated) != candidate.metadata)
            return Fail(error, "Cooked material generation differs from the current source graph; regenerate it.");
    }
    result = std::move(candidate);
    error.clear();
    return true;
}

bool BuildInstance(std::shared_ptr<const Generation> generation, const InstanceDescription& description,
                   const TextureLoader& loadTexture, std::shared_ptr<const Instance>& result, std::string& error)
{
    error.clear();
    if (!generation || generation->generation == 0 || generation->assetId != description.graphId)
        return Fail(error, "Material instance requires its matching owning graph generation.");
    if (!ValidateDescription(description, error))
        return false;
    auto candidate = std::make_shared<Instance>();
    candidate->generation = std::move(generation);
    candidate->description = description;
    std::ranges::sort(candidate->description.parameters, {}, &ParameterOverride::id);
    std::ranges::sort(candidate->description.textures, {}, &TextureOverride::parameter);
    const auto& product = candidate->generation->cooked.product;
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    if (product.materialShader)
    {
        const auto& shader = *product.materialShader;
        const auto& meta = shader.meta;
        for (const auto& edit : description.parameters)
        {
            const auto property = std::ranges::find(meta.properties, edit.id, &ShaderPropertyDesc::parameterId);
            if (property == meta.properties.end() || !property->exposed || property->type == ShaderPropertyType::Texture2D)
                return Fail(error, "Unknown or private generated material numeric override.");
        }
        for (const auto& property : meta.properties)
        {
            MaterialPropertyValue value;
            if (!MaterialPropertyPacker::ApplyDefault(property, value, error)) return false;
            const auto edit = std::ranges::find(description.parameters, property.parameterId, &ParameterOverride::id);
            if (edit != description.parameters.end())
            {
                const auto parameter = std::ranges::find(product.program.parameters, edit->id, &LX::LXMaterialParameter::id);
                if (parameter == product.program.parameters.end() || !LX::IsSocketValueValid(parameter->type, edit->value))
                    return Fail(error, "Generated material override type differs from its Blackboard type.");
                std::visit([&](const auto& input) {
                    using T = std::decay_t<decltype(input)>;
                    if constexpr (std::is_same_v<T, bool>) value.m_boolValue = input;
                    else if constexpr (std::is_same_v<T, std::int64_t>) value.m_integerValue = static_cast<std::int32_t>(input);
                    else if constexpr (std::is_same_v<T, double>) value.m_numericValue = {static_cast<float>(input)};
                    else if constexpr (std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>>)
                    {
                        value.m_numericValue.clear();
                        for (const auto number : input) value.m_numericValue.push_back(static_cast<float>(number));
                    }
                }, edit->value);
            }
            if (property.type == ShaderPropertyType::Texture2D)
            {
                const auto texture = std::ranges::find(description.textures, property.parameterId, &TextureOverride::parameter);
                if (texture != description.textures.end()) value.m_textureGuid = FileGuid{texture->assetId.value};
            }
            candidate->properties.push_back(std::move(value));
        }
    }
    else if (!PrepareUniforms(product.layout, description.parameters, candidate->uniforms, diagnostics))
        return Fail(error, diagnostics.empty() ? "Invalid material instance uniforms." : diagnostics.front().message);
    for (const auto& texture : description.textures)
    {
        const auto parameter =
            std::ranges::find(product.program.parameters, texture.parameter, &LX::LXMaterialParameter::id);
        if (parameter == product.program.parameters.end() || !parameter->exposed ||
            parameter->type != LX::PinType::Texture)
            return Fail(error, "Unknown or private material texture parameter override.");
        if (std::ranges::none_of(product.layout.textures,
                                 [&](const auto& resource) { return resource.parameter == texture.parameter; }))
            return Fail(error, "Material texture parameter has no active resource binding.");
    }
    for (const auto& resource : product.layout.textures)
    {
        experiment::AssetId id;
        const auto override = std::ranges::find(description.textures, resource.parameter, &TextureOverride::parameter);
        if (override != description.textures.end())
            id = override->assetId;
        else if (!ParseTextureId(resource.reference, id))
            return Fail(error, "Graph texture resource requires a canonical asset GUID.");
        if (!loadTexture)
            return Fail(error, "Material instance has no texture generation loader.");
        auto owner = loadTexture(id, resource.colorSpace, error);
        if (!owner)
        {
            if (error.empty())
                error = "Material texture generation could not be loaded: " + Uuid::ToString(id.value);
            return false;
        }
        candidate->textures.push_back({resource.slot, id, resource.colorSpace, std::move(owner)});
        if (product.materialShader)
            candidate->textureOwners.push_back({"lx_texture_" + std::to_string(resource.slot), candidate->textures.back().owner});
    }
    if (product.materialShader)
    {
        std::shared_ptr<const LX::Runtime::Instance> common;
        if (!LX::Runtime::BuildInstance(product.materialShader, candidate->properties, {}, candidate->textureOwners,
                                         common, error)) return false;
        static_cast<LX::Runtime::Instance&>(*candidate) = *common;
    }
    result = std::move(candidate);
    return true;
}

bool WriteInstanceDocument(const InstanceDocument& document, Authoring::WriteNode result, std::string& error)
{
    error.clear();
    if (!result || document.name.size() > 4096 ||
        (document.blendMode != "opaque" && document.blendMode != "masked" && document.blendMode != "transparent") ||
        (document.materialId.IsValid() && !ValidTextureId(document.materialId)) ||
        !ValidateDescription(document.description, error))
        return Fail(error, error.empty() ? "Invalid lattice material document." : error);
    Authoring::WriteDocument staging;
    const auto node = staging.Root();
    node.SetMap();
    node.Child("lattice_material").SetScalar(1u);
    node.Child("name").SetScalar(document.name);
    node.Child("assetId").SetScalar(Uuid::ToString(document.materialId.value));
    node.Child("graphAssetId").SetScalar(Uuid::ToString(document.description.graphId.value));
    node.Child("doubleSided").SetScalar(document.doubleSided);
    node.Child("blendMode").SetScalar(document.blendMode);
    auto parameters = document.description.parameters;
    std::ranges::sort(parameters, {}, &ParameterOverride::id);
    const auto list = node.Child("parameters");
    list.SetSequence();
    for (const auto& parameter : parameters)
    {
        const auto entry = list.Append();
        entry.Child("id").SetScalar(parameter.id);
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, bool>)
                    entry.Child("bool").SetScalar(value);
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    entry.Child("int").SetScalar(value);
                else if constexpr (std::is_same_v<T, double>)
                    WriteDouble(entry.Child("float"), value);
                else if constexpr (std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>>)
                {
                    const auto values = entry.Child(value.size() == 3 ? "vector" : "color");
                    values.SetSequence(true);
                    for (double number : value)
                        WriteDouble(values.Append(), number);
                }
            },
            parameter.value);
    }
    auto textures = document.description.textures;
    std::ranges::sort(textures, {}, &TextureOverride::parameter);
    const auto textureList = node.Child("textures");
    textureList.SetSequence();
    for (const auto& texture : textures)
    {
        const auto entry = textureList.Append();
        entry.Child("id").SetScalar(texture.parameter);
        entry.Child("guid").SetScalar(Uuid::ToString(texture.assetId.value));
    }
    result.Assign(node);
    return true;
}

bool ReadInstanceDocument(const Authoring::ReadNode& node, InstanceDocument& result, std::string& error)
{
    error.clear();
    try
    {
        const bool keys = node["blendMode"]
            ? Keys(node, {"lattice_material", "name", "assetId", "graphAssetId", "doubleSided", "parameters", "textures", "blendMode"})
            : Keys(node, {"lattice_material", "name", "assetId", "graphAssetId", "doubleSided", "parameters", "textures"});
        if (!keys ||
            Scalar<std::uint32_t>(node["lattice_material"]) != 1)
            return Fail(error, "Unknown, duplicate or incomplete lattice material document keys/version.");
        InstanceDocument candidate;
        candidate.name = Scalar<std::string>(node["name"]);
        const auto materialId = Scalar<std::string>(node["assetId"]);
        if (materialId != Uuid::ToString(Uuid::Uuid16{}) && !ParseTextureId(materialId, candidate.materialId))
            return Fail(error, "Invalid lattice material asset GUID.");
        if (!ParseTextureId(Scalar<std::string>(node["graphAssetId"]), candidate.description.graphId))
            return Fail(error, "Invalid lattice graph asset GUID.");
        candidate.doubleSided = Scalar<bool>(node["doubleSided"]);
        if (node["blendMode"])
        {
            candidate.blendMode = Scalar<std::string>(node["blendMode"]);
            if (candidate.blendMode != "opaque" && candidate.blendMode != "masked" && candidate.blendMode != "transparent")
                return Fail(error, "Invalid lattice material alpha mode.");
        }
        if (candidate.name.size() > 4096 || !node["parameters"].IsSequence() || node["parameters"].Size() > 128 ||
            !node["textures"].IsSequence() || node["textures"].Size() > 64)
            return Fail(error, "Invalid lattice material parameter/texture list.");
        for (const auto entry : node["parameters"])
        {
            ParameterOverride parameter;
            parameter.id = Scalar<LX::Id>(entry["id"]);
            if (Keys(entry, {"id", "bool"}))
                parameter.value = Scalar<bool>(entry["bool"]);
            else if (Keys(entry, {"id", "int"}))
                parameter.value = Scalar<std::int64_t>(entry["int"]);
            else if (Keys(entry, {"id", "float"}))
                parameter.value = Scalar<double>(entry["float"]);
            else if (Keys(entry, {"id", "vector"}) && entry["vector"].IsSequence() && entry["vector"].Size() == 3)
                parameter.value =
                    std::array<double, 3>{Scalar<double>(entry["vector"].At(0)), Scalar<double>(entry["vector"].At(1)),
                                          Scalar<double>(entry["vector"].At(2))};
            else if (Keys(entry, {"id", "color"}) && entry["color"].IsSequence() && entry["color"].Size() == 4)
                parameter.value =
                    std::array<double, 4>{Scalar<double>(entry["color"].At(0)), Scalar<double>(entry["color"].At(1)),
                                          Scalar<double>(entry["color"].At(2)), Scalar<double>(entry["color"].At(3))};
            else
                return Fail(error, "Material parameter needs exactly one known typed value.");
            candidate.description.parameters.push_back(std::move(parameter));
        }
        for (const auto entry : node["textures"])
        {
            TextureOverride texture;
            if (!Keys(entry, {"id", "guid"}) || !ParseTextureId(Scalar<std::string>(entry["guid"]), texture.assetId))
                return Fail(error, "Invalid lattice texture override.");
            texture.parameter = Scalar<LX::Id>(entry["id"]);
            candidate.description.textures.push_back(texture);
        }
        if (!ValidateDescription(candidate.description, error))
            return false;
        std::ranges::sort(candidate.description.parameters, {}, &ParameterOverride::id);
        std::ranges::sort(candidate.description.textures, {}, &TextureOverride::parameter);
        result = std::move(candidate);
        return true;
    }
    catch (const std::exception& exception)
    {
        return Fail(error, exception.what());
    }
}
} // namespace material_graph
