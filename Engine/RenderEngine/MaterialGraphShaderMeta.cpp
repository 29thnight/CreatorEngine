#include "MaterialGraphShaderMeta.h"
#include "MaterialGraphProduct.h"

#include "Sha256.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace material_graph
{
namespace
{
std::string Quote(std::string_view text)
{
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text)
    {
        if (c == '\"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '\"';
}

std::string Digest(std::string_view text)
{
    return Hash::ToHex(Hash::Sha256::Compute(text.data(), text.size()));
}

const char* ColorSpace(LX::LXColorSpace value)
{
    switch (value)
    {
    case LX::LXColorSpace::Data: return "data";
    case LX::LXColorSpace::Linear: return "linear";
    case LX::LXColorSpace::SRGB: return "srgb";
    }
    throw std::runtime_error("Unknown Graph color space");
}

void Float(std::ostream& out, double value)
{
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed)) throw std::runtime_error("Graph default exceeds finite float32 range");
    out << narrowed;
}

void Default(std::ostream& out, const LX::LXMaterialParameter& p)
{
    if (!LX::IsSocketValueValid(p.type, p.value)) throw std::runtime_error("Invalid Graph parameter default");
    if (p.type == LX::PinType::Bool) out << (std::get<bool>(p.value) ? "true" : "false");
    else if (p.type == LX::PinType::Int)
    {
        const auto value = std::get<std::int64_t>(p.value);
        if (value < INT32_MIN || value > INT32_MAX) throw std::runtime_error("Graph integer exceeds int32 range");
        out << value;
    }
    else if (p.type == LX::PinType::Float) Float(out, std::get<double>(p.value));
    else
    {
        const auto array = [&out](const auto& values) {
            out << '[';
            for (std::size_t i = 0; i < values.size(); ++i) { if (i) out << ','; Float(out, values[i]); }
            out << ']';
        };
        if (p.type == LX::PinType::Color) array(std::get<std::array<double, 4>>(p.value));
        else array(std::get<std::array<double, 3>>(p.value));
    }
}

const LX::LXMaterialParameter* Parameter(const LX::LXMaterialProgram& program, LX::Id id)
{
    const auto found = std::ranges::find(program.parameters, id, &LX::LXMaterialParameter::id);
    return found == program.parameters.end() ? nullptr : &*found;
}

std::string Document(const LX::LXMaterialProgram& program, FileGuid graphGuid,
    std::string_view sourceDigest, std::string_view generation, std::span<const ShaderPassDesc> passes)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"schema\":" << ShaderMeta::kSchemaVersion
        << ",\"name\":\"LXMaterial\",\"source\":\"material.slang\",\"properties\":[";
    bool comma = false;
    for (const auto& p : program.parameters)
    {
        if (p.type == LX::PinType::Texture || p.type == LX::PinType::Sampler) continue;
        const char* type = nullptr;
        const char* semantic = "value";
        switch (p.type)
        {
        case LX::PinType::Float: type = "float"; break;
        case LX::PinType::Int: type = "int"; break;
        case LX::PinType::Bool: type = "bool"; break;
        case LX::PinType::Vector: type = "float3"; semantic = "vector"; break;
        case LX::PinType::Normal: type = "float3"; semantic = "normal"; break;
        case LX::PinType::Color: type = "float4"; semantic = "color"; break;
        default: throw std::runtime_error("Unsupported Graph property type");
        }
        if (comma) out << ',';
        comma = true;
        out << "{\"name\":" << Quote("lx_bound_p" + std::to_string(p.id))
            << ",\"label\":" << Quote(p.name.empty() ? p.identifier : p.name)
            << ",\"type\":" << Quote(type) << ",\"default\":";
        Default(out, p);
        out << ",\"parameterId\":" << p.id << ",\"semantic\":" << Quote(semantic)
            << ",\"colorSpace\":" << Quote(ColorSpace(p.colorSpace))
            << ",\"exposed\":" << (p.exposed ? "true" : "false") << '}';
    }
    for (const auto& resource : program.resources)
    {
        if (resource.kind != LX::LXMaterialResourceKind::Texture) continue;
        const auto* p = Parameter(program, resource.parameter);
        if (resource.parameter && (!p || p->type != LX::PinType::Texture))
            throw std::runtime_error("Graph texture parameter identity is missing");
        if (comma) out << ',';
        comma = true;
        const auto name = "lx_texture_" + std::to_string(resource.slot);
        out << "{\"name\":" << Quote(name) << ",\"label\":" << Quote(p ? p->name : name)
            << ",\"type\":\"texture2d\",\"default\":" << Quote(resource.reference)
            << ",\"parameterId\":" << resource.parameter << ",\"semantic\":\"texture\",\"colorSpace\":"
            << Quote(ColorSpace(resource.colorSpace)) << ",\"exposed\":" << (p && p->exposed ? "true" : "false") << '}';
    }
    out << "],\"passes\":[";
    comma = false;
    for (const auto& pass : passes)
    {
        if (comma) out << ',';
        comma = true;
        out << "{\"name\":" << Quote(pass.name);
        for (const auto& [name, stage] : {std::pair{"vs", pass.vertex}, {"ps", pass.pixel}, {"cs", pass.compute}})
            if (stage) out << ',' << Quote(name) << ":{\"entry\":" << Quote(stage->entry) << '}';
        const auto& s = pass.state;
        if (!pass.IsCompute())
        {
            const auto depth = s.depthTest == RHICompareOp::None ? "off" :
                s.depthTest == RHICompareOp::Less ? "less" : s.depthTest == RHICompareOp::LessEqual ? "lessEqual" : "invalid";
            out << ",\"state\":{\"fill\":" << Quote(s.fillMode == RHIFillMode::Solid ? "solid" : "wireframe")
                << ",\"cull\":" << Quote(s.cullMode == RHICullMode::None ? "none" : s.cullMode == RHICullMode::Front ? "front" : "back")
                << ",\"blend\":" << Quote(s.blendMode == ShaderBlendMode::Off ? "off" : s.blendMode == ShaderBlendMode::Alpha ? "alpha" : "additive")
                << ",\"depthWrite\":" << (s.depthWrite ? "true" : "false") << ",\"depthTest\":" << Quote(depth)
                << ",\"topology\":" << Quote(s.topologyType == RHITopologyType::Triangle ? "triangle" : s.topologyType == RHITopologyType::Line ? "line" : "point") << '}';
        }
        out << ",\"queue\":" << Quote(pass.queue == ShaderPassQueue::Opaque ? "opaque" :
            pass.queue == ShaderPassQueue::Transparent ? "transparent" : pass.queue == ShaderPassQueue::Shadow ? "shadow" : "compute") << '}';
    }
    out << "],\"generatedMaterial\":{\"graph\":" << Quote(graphGuid.ToString())
        << ",\"adapter\":" << ShaderGeneratedMaterial::kAdapterVersion << ",\"generation\":" << Quote(generation)
        << ",\"sourceSha256\":" << Quote(sourceDigest) << ",\"features\":" << program.features
        << ",\"surface\":" << (program.surface ? "true" : "false")
        << ",\"volume\":" << (program.volume ? "true" : "false") << ",\"samplers\":[";
    comma = false;
    for (const auto& resource : program.resources)
    {
        if (resource.kind != LX::LXMaterialResourceKind::Sampler) continue;
        const auto* p = Parameter(program, resource.parameter);
        if (resource.parameter && (!p || p->type != LX::PinType::Sampler))
            throw std::runtime_error("Graph sampler parameter identity is missing");
        if (comma) out << ',';
        comma = true;
        out << "{\"name\":" << Quote("lx_sampler_" + std::to_string(resource.slot))
            << ",\"description\":" << Quote(resource.reference) << ",\"parameterId\":" << resource.parameter
            << ",\"exposed\":" << (p && p->exposed ? "true" : "false") << '}';
    }
    out << "]}}\n";
    return out.str();
}

bool Write(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    return static_cast<bool>(file);
}

bool Matches(const std::filesystem::path& path, std::string_view expected)
{
    std::error_code error;
    if (std::filesystem::file_size(path, error) != expected.size() || error) return false;
    std::ifstream file(path, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    return file && text == expected;
}
} // namespace

bool PublishMaterialShaderMeta(const LX::LXMaterialProgram& program, FileGuid graphGuid,
    std::string_view source, std::span<const ShaderPassDesc> passes,
    std::span<const RHIShaderReflection> reflections, const std::filesystem::path& root,
    GeneratedMaterialShader& result, std::string& error)
{
    std::filesystem::path staging;
    try
    {
        if (root.empty() || graphGuid == FileGuid{} || source.empty() || source.size() > 16u * 1024u * 1024u)
            throw std::runtime_error("Generated Graph source/identity is invalid");
        const auto hash = Digest(source);
        const auto seed = Document(program, graphGuid, hash, "", passes);
        const auto generation = Digest(std::to_string(program.semanticKey.size()) + ':' + program.semanticKey + seed);
        const auto document = Document(program, graphGuid, hash, generation, passes);
        if (document.size() > 1024u * 1024u) throw std::runtime_error("Generated ShaderMeta exceeds 1MiB");
        const auto directory = root / generation;
        GeneratedMaterialShader candidate;
        candidate.document = document;
        candidate.source = source;
        const auto parsed = Authoring::ParsedDocument::ParseText(document, error);
        if (!parsed || !Authoring::EncodeCookedDocument(parsed.Root(), candidate.cookedContract, error) ||
            candidate.cookedContract.size() > 1024u * 1024u)
            throw std::runtime_error("Cannot encode generated binary contract: " + error);
        const auto validate = [&](const std::filesystem::path& folder) {
            candidate.metaPath = folder / "material.shadermeta";
            return ShaderMetaLoader::LoadFile(candidate.metaPath, graphGuid, candidate.meta, error) &&
                ShaderMetaReflection::Resolve(candidate.meta, reflections, candidate.layout, error);
        };
        if (std::filesystem::exists(directory))
        {
            if (!Matches(directory / "material.shadermeta", document) || !Matches(directory / "material.slang", source))
                throw std::runtime_error("Immutable generated material pair was changed");
            if (!validate(directory)) return false;
        }
        else
        {
            staging = root / (".candidate-" + FileGuid::CreateRandomV4().ToString());
            std::filesystem::create_directories(staging);
            if (!Write(staging / "material.slang", source) || !Write(staging / "material.shadermeta", document))
                throw std::runtime_error("Generated material pair write failed");
            if (!validate(staging)) throw std::runtime_error(error);
            std::error_code renameError;
            std::filesystem::rename(staging, directory, renameError);
            if (renameError)
            {
                // Another producer may have published the exact generation.
                if (!Matches(directory / "material.shadermeta", document) || !Matches(directory / "material.slang", source))
                    throw std::runtime_error("Generated material pair publication failed: " + renameError.message());
                std::filesystem::remove_all(staging);
            }
            staging.clear();
            candidate.metaPath = directory / "material.shadermeta";
            candidate.meta.originPath = candidate.metaPath;
        }
        result = std::move(candidate);
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        if (!staging.empty()) { std::error_code ignored; std::filesystem::remove_all(staging, ignored); }
        return false;
    }
}

bool RestoreMaterialShaderMeta(const VerifiedProduct& product, FileGuid graphGuid,
    std::string_view document, std::string_view source,
    GeneratedMaterialShader& result, std::string& error, std::span<const std::byte> cookedContract)
{
    try
    {
        GeneratedMaterialShader candidate;
        if (!ShaderMetaLoader::ParseGeneratedCooked(cookedContract, source, graphGuid, candidate.meta, error))
            return false;
        const auto& generated = *candidate.meta.generatedMaterial;
        const auto& program = product.program;
        const auto hash = Digest(source);
        const auto seed = Document(program, graphGuid, hash, "", candidate.meta.passes);
        const auto generation = Digest(std::to_string(program.semanticKey.size()) + ':' + program.semanticKey + seed);
        if (generated.graphGuid != graphGuid || generated.features != program.features ||
            generated.surface != program.surface || generated.volume != program.volume ||
            generated.generation != generation || generated.sourceSha256 != hash ||
            document != Document(program, graphGuid, hash, generation, candidate.meta.passes) ||
            !source.starts_with(BuildBoundSource(program)))
            throw std::runtime_error("Generated metadata/source differs from its verified material generation");

        // The cooked tree and diagnostic JSON must describe the same contract.
        // Rebuild the expected typed defaults directly from the verified program;
        // parsing the diagnostic JSON here would reintroduce the Player parser.
        std::vector<ShaderPropertyDesc> properties;
        std::vector<ShaderMaterialSampler> samplers;
        for (const auto& p : program.parameters)
        {
            if (p.type == LX::PinType::Texture || p.type == LX::PinType::Sampler) continue;
            ShaderPropertyDesc property;
            property.name = "lx_bound_p" + std::to_string(p.id);
            property.label = p.name.empty() ? p.identifier : p.name;
            property.parameterId = p.id;
            property.exposed = p.exposed;
            property.colorSpace = ColorSpace(p.colorSpace);
            switch (p.type)
            {
            case LX::PinType::Float:
                property.type = ShaderPropertyType::Float;
                property.defaultValue = static_cast<float>(std::get<double>(p.value)); break;
            case LX::PinType::Int:
                property.type = ShaderPropertyType::Int;
                property.defaultValue = static_cast<std::int32_t>(std::get<std::int64_t>(p.value)); break;
            case LX::PinType::Bool:
                property.type = ShaderPropertyType::Bool;
                property.defaultValue = std::get<bool>(p.value); break;
            case LX::PinType::Vector: case LX::PinType::Normal:
            {
                property.type = ShaderPropertyType::Float3;
                property.semantic = p.type == LX::PinType::Normal ? "normal" : "vector";
                const auto& value = std::get<std::array<double, 3>>(p.value);
                property.defaultValue = std::array<float, 3>{static_cast<float>(value[0]),
                    static_cast<float>(value[1]), static_cast<float>(value[2])}; break;
            }
            case LX::PinType::Color:
            {
                property.type = ShaderPropertyType::Float4;
                property.semantic = "color";
                const auto& value = std::get<std::array<double, 4>>(p.value);
                property.defaultValue = std::array<float, 4>{static_cast<float>(value[0]),
                    static_cast<float>(value[1]), static_cast<float>(value[2]), static_cast<float>(value[3])}; break;
            }
            default: throw std::runtime_error("Unsupported generated property");
            }
            properties.push_back(std::move(property));
        }
        for (const auto& resource : program.resources)
        {
            const auto* p = Parameter(program, resource.parameter);
            if (resource.kind == LX::LXMaterialResourceKind::Sampler)
            {
                samplers.push_back({"lx_sampler_" + std::to_string(resource.slot), resource.reference,
                    resource.parameter, p && p->exposed});
                continue;
            }
            ShaderPropertyDesc property;
            property.name = "lx_texture_" + std::to_string(resource.slot);
            property.label = p ? p->name : property.name;
            property.type = ShaderPropertyType::Texture2D;
            property.defaultValue = FileGuid{resource.reference};
            property.parameterId = resource.parameter;
            property.semantic = "texture";
            property.colorSpace = ColorSpace(resource.colorSpace);
            property.exposed = p && p->exposed;
            properties.push_back(std::move(property));
        }
        if (candidate.meta.name != "LXMaterial" || candidate.meta.source != "material.slang" ||
            !candidate.meta.keywords.empty() || candidate.meta.properties != properties || generated.samplers != samplers)
            throw std::runtime_error("Generated typed contract differs from its verified program");

        // Every declared stage must have verified bytecode in each backend the
        // product carries (the editor carries only its renderer's). No extra
        // entry may silently belong to a different generated contract.
        std::set<RHIShaderBinary> carried;
        std::set<std::tuple<RHIShaderBinary, std::string, std::string>> declared, compiled;
        for (const auto& target : product.targets)
        {
            carried.insert(target.binary);
            compiled.emplace(target.binary, target.entry, target.profile.substr(0, 3));
        }
        for (const auto& pass : candidate.meta.passes)
            for (const auto& [stage, profile] : {std::pair{pass.vertex, "vs_"},
                {pass.pixel, "ps_"}, {pass.compute, "cs_"}})
                if (stage)
                    for (const auto backend : carried)
                        declared.emplace(backend, stage->entry, profile);
        // Scene mesh frontends are fixed host alternatives to these vertex
        // stages, with the same material pixel contract. Require their exact
        // entry names for the current host generation; never accept arbitrary
        // extra compiled stages or relax the authored pass equality below.
        if (program.semanticKey.ends_with(SceneHostIdentity))
        {
            for (const auto& pass : candidate.meta.passes)
            {
                if (!pass.vertex)
                {
                    continue;
                }
                const char* meshEntry = pass.vertex->entry == "LXSceneVS" ? "LXSceneMS"
                    : pass.vertex->entry == "LXSceneShadowVS" ? "LXSceneShadowMS" : nullptr;
                if (meshEntry)
                {
                    for (const auto backend : carried)
                    {
                        declared.emplace(backend, meshEntry, "ms_");
                    }
                }
            }
        }
        if (declared != compiled) throw std::runtime_error("Generated passes differ from verified backend stages");

        RHIShaderReflection reflection;
        RHIShaderResourceReflection uniforms;
        uniforms.name = "LXMaterialProperties";
        uniforms.kind = RHIShaderResourceKind::ConstantBuffer;
        uniforms.registerIndex = UniformRegister;
        uniforms.byteSize = product.layout.uniformBytes;
        for (const auto& binding : product.layout.parameters)
        {
            RHIShaderFieldReflection field;
            field.name = "lx_bound_p" + std::to_string(binding.parameter.id);
            field.byteOffset = binding.offset;
            field.byteSize = binding.bytes;
            switch (binding.parameter.type)
            {
            case LX::PinType::Bool: field.type.scalar = RHIShaderScalarKind::Bool; break;
            case LX::PinType::Int: field.type.scalar = RHIShaderScalarKind::Int32; break;
            case LX::PinType::Vector: case LX::PinType::Normal: field.type.columns = 3; break;
            case LX::PinType::Color: field.type.columns = 4; break;
            case LX::PinType::Float: break;
            default: throw std::runtime_error("Invalid generated uniform type");
            }
            uniforms.fields.push_back(std::move(field));
        }
        if (!uniforms.fields.empty()) reflection.resources.push_back(std::move(uniforms));
        for (const auto& resource : product.layout.textures)
        {
            RHIShaderResourceReflection texture;
            texture.name = "lx_texture_" + std::to_string(resource.slot);
            texture.kind = RHIShaderResourceKind::Texture;
            texture.registerIndex = TextureRegister + resource.slot;
            reflection.resources.push_back(std::move(texture));
        }
        for (const auto& resource : product.layout.samplers)
        {
            RHIShaderResourceReflection sampler;
            sampler.name = "lx_sampler_" + std::to_string(resource.slot);
            sampler.kind = RHIShaderResourceKind::Sampler;
            sampler.registerIndex = SamplerRegister + resource.slot;
            reflection.resources.push_back(std::move(sampler));
        }
        if (!ShaderMetaReflection::Resolve(candidate.meta, std::span(&reflection, 1), candidate.layout, error))
            return false;
        candidate.document = document;
        candidate.source = source;
        candidate.cookedContract.assign(cookedContract.begin(), cookedContract.end());
        result = std::move(candidate);
        error.clear();
        return true;
    }
    catch (const std::exception& exception) { error = exception.what(); return false; }
}
} // namespace material_graph
