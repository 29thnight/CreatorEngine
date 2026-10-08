#include "MaterialGraphProduct.h"
#include "MaterialGraphShaderMeta.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>

namespace material_graph
{
namespace
{
bool Fail(std::vector<LX::LXMaterialDiagnostic>& diagnostics, std::string code, std::string message,
          LX::LXMaterialSource source = {})
{
    diagnostics.push_back({std::move(code), std::move(message), std::move(source)});
    return false;
}

bool Numeric(LX::PinType type)
{
    return type == LX::PinType::Bool || type == LX::PinType::Int || type == LX::PinType::Float ||
           type == LX::PinType::Vector || type == LX::PinType::Normal || type == LX::PinType::Color;
}

const char* TypeName(LX::PinType type)
{
    if (type == LX::PinType::Bool)
        return "bool";
    if (type == LX::PinType::Int)
        return "int";
    if (type == LX::PinType::Float)
        return "float";
    if (type == LX::PinType::Color)
        return "float4";
    return "float3";
}

RHIShaderValueType ValueType(LX::PinType type)
{
    RHIShaderValueType result;
    if (type == LX::PinType::Bool)
        result.scalar = RHIShaderScalarKind::Bool;
    else if (type == LX::PinType::Int)
        result.scalar = RHIShaderScalarKind::Int32;
    else if (type == LX::PinType::Color)
        result.columns = 4;
    else if (type == LX::PinType::Vector || type == LX::PinType::Normal)
        result.columns = 3;
    return result;
}

std::string Symbol(const LX::LXMaterialResource& resource)
{
    return std::string(resource.kind == LX::LXMaterialResourceKind::Texture ? "lx_texture_" : "lx_sampler_") +
           std::to_string(resource.slot);
}

void Replace(std::string& source, const std::string& from, const std::string& to)
{
    std::size_t offset = 0;
    while ((offset = source.find(from, offset)) != std::string::npos)
    {
        source.replace(offset, from.size(), to);
        offset += to.size();
    }
}

bool IsSrgb(RHIFormat format)
{
    return format == RHIFormat::RGBA8UnormSrgb || format == RHIFormat::BGRA8UnormSrgb ||
           format == RHIFormat::BC1UnormSrgb || format == RHIFormat::BC3UnormSrgb;
}

bool IsFloatTextureFormat(RHIFormat format)
{
    // Generated image sockets use Texture2D<float4>. Integer/depth formats
    // require a different typed operation and cannot be sampled by this ABI.
    switch (format)
    {
    case RHIFormat::RGBA8Unorm:
    case RHIFormat::RGBA8UnormSrgb:
    case RHIFormat::RGBA16Float:
    case RHIFormat::RGBA32Float:
    case RHIFormat::RG16Float:
    case RHIFormat::RG32Float:
    case RHIFormat::RGB32Float:
    case RHIFormat::R16Float:
    case RHIFormat::R16Unorm:
    case RHIFormat::R32Float:
    case RHIFormat::BC1Unorm:
    case RHIFormat::BC1UnormSrgb:
    case RHIFormat::BC3Unorm:
    case RHIFormat::BC3UnormSrgb:
    case RHIFormat::BGRA8Unorm:
    case RHIFormat::BGRA8UnormSrgb:
        return true;
    default:
        return false;
    }
}

bool Pack(const ParameterBinding& binding, const LX::LXSocketValue& value, std::vector<std::uint8_t>& bytes)
{
    if (!LX::IsSocketValueValid(binding.parameter.type, value))
        return false;
    std::array<std::uint32_t, 4> words{};
    const auto floating = [](double number, std::uint32_t& word) {
        const float converted = static_cast<float>(number);
        if (!std::isfinite(converted))
            return false;
        word = std::bit_cast<std::uint32_t>(converted);
        return true;
    };
    if (binding.parameter.type == LX::PinType::Bool)
        words[0] = std::get<bool>(value) ? 1u : 0u;
    else if (binding.parameter.type == LX::PinType::Int)
    {
        const auto number = std::get<std::int64_t>(value);
        if (number < INT32_MIN || number > INT32_MAX)
            return false;
        words[0] = std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(number));
    }
    else if (binding.parameter.type == LX::PinType::Float)
    {
        if (!floating(std::get<double>(value), words[0]))
            return false;
    }
    else if (binding.parameter.type == LX::PinType::Color)
    {
        const auto& color = std::get<std::array<double, 4>>(value);
        for (std::size_t index = 0; index < color.size(); ++index)
            if (!floating(color[index], words[index]))
                return false;
    }
    else
    {
        const auto& vector = std::get<std::array<double, 3>>(value);
        for (std::size_t index = 0; index < vector.size(); ++index)
            if (!floating(vector[index], words[index]))
                return false;
    }
    if (binding.bytes != ValueType(binding.parameter.type).columns * 4u ||
        static_cast<std::uint64_t>(binding.offset) + binding.bytes > bytes.size())
        return false;
    std::memcpy(bytes.data() + binding.offset, words.data(), binding.bytes);
    return true;
}

constexpr std::uint32_t CookMagic = 0x434D584Cu; // LXMC, little endian
constexpr std::uint32_t TextLimit = 16u * 1024u * 1024u;

void Word(std::vector<std::uint8_t>& bytes, std::uint32_t word)
{
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::uint8_t>(word >> shift));
}

void Wide(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    Word(bytes, static_cast<std::uint32_t>(value));
    Word(bytes, static_cast<std::uint32_t>(value >> 32));
}

void Block(std::vector<std::uint8_t>& bytes, std::span<const std::uint8_t> block)
{
    Word(bytes, static_cast<std::uint32_t>(block.size()));
    bytes.insert(bytes.end(), block.begin(), block.end());
}

void Text(std::vector<std::uint8_t>& bytes, const std::string& text)
{
    Block(bytes, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

std::uint64_t Checksum(std::span<const std::uint8_t> bytes)
{
    std::uint64_t result = 14695981039346656037ull;
    for (const auto byte : bytes)
        result = (result ^ byte) * 1099511628211ull;
    return result;
}

class Reader
{
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool Word(std::uint32_t& word)
    {
        if (bytes_.size() < 4)
            return false;
        word = 0;
        for (unsigned index = 0; index < 4; ++index)
            word |= static_cast<std::uint32_t>(bytes_[index]) << (index * 8);
        bytes_ = bytes_.subspan(4);
        return true;
    }
    bool Wide(std::uint64_t& value)
    {
        std::uint32_t low{}, high{};
        if (!Word(low) || !Word(high))
            return false;
        value = low | (static_cast<std::uint64_t>(high) << 32);
        return true;
    }
    bool Block(std::span<const std::uint8_t>& block, std::uint64_t limit, bool allowEmpty = false)
    {
        std::uint32_t size{};
        if (!Word(size) || (!allowEmpty && size == 0) || size > limit || size > bytes_.size())
            return false;
        block = bytes_.first(size);
        bytes_ = bytes_.subspan(size);
        return true;
    }
    bool Text(std::string& text, std::uint32_t limit = TextLimit, bool allowEmpty = false)
    {
        std::span<const std::uint8_t> block;
        if (!Block(block, limit, allowEmpty))
            return false;
        text.assign(reinterpret_cast<const char*>(block.data()), block.size());
        return true;
    }
    bool Empty() const { return bytes_.empty(); }

  private:
    std::span<const std::uint8_t> bytes_;
};

void WriteLocation(std::vector<std::uint8_t>& bytes, const LX::LXMaterialSource& source)
{
    Wide(bytes, source.scope);
    Wide(bytes, source.node);
    Wide(bytes, source.pin);
    Word(bytes, static_cast<std::uint32_t>(source.instances.size()));
    for (const auto instance : source.instances)
        Wide(bytes, instance);
    Text(bytes, source.property);
}

bool ReadLocation(Reader& reader, LX::LXMaterialSource& source)
{
    std::uint32_t count{};
    if (!reader.Wide(source.scope) || !reader.Wide(source.node) || !reader.Wide(source.pin) || !reader.Word(count) ||
        count > 128)
        return false;
    source.instances.resize(count);
    for (auto& instance : source.instances)
        if (!reader.Wide(instance))
            return false;
    return reader.Text(source.property, 4096, true);
}

void WriteParameter(std::vector<std::uint8_t>& bytes, const LX::LXMaterialParameter& parameter)
{
    Wide(bytes, parameter.id);
    Text(bytes, parameter.identifier);
    Text(bytes, parameter.name);
    Word(bytes, static_cast<std::uint32_t>(parameter.type));
    Word(bytes, static_cast<std::uint32_t>(parameter.colorSpace));
    Word(bytes, parameter.exposed ? 1u : 0u);
    std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>)
                Word(bytes, value ? 1u : 0u);
            else if constexpr (std::is_same_v<T, std::int64_t>)
                Wide(bytes, std::bit_cast<std::uint64_t>(value));
            else if constexpr (std::is_same_v<T, double>)
                Wide(bytes, std::bit_cast<std::uint64_t>(value));
            else if constexpr (std::is_same_v<T, std::string>)
                Text(bytes, value);
            else if constexpr (!std::is_same_v<T, std::monostate>)
                for (const double component : value)
                    Wide(bytes, std::bit_cast<std::uint64_t>(component));
        },
        parameter.value);
}

bool ReadParameter(Reader& reader, LX::LXMaterialParameter& parameter)
{
    std::uint32_t type{}, color{}, exposed{};
    if (!reader.Wide(parameter.id) || !reader.Text(parameter.identifier, 4096) ||
        !reader.Text(parameter.name, 4096, true) || !reader.Word(type) ||
        type > static_cast<std::uint32_t>(LX::PinType::Closure) || !reader.Word(color) || color > 2 ||
        !reader.Word(exposed) || exposed > 1)
        return false;
    parameter.type = static_cast<LX::PinType>(type);
    parameter.colorSpace = static_cast<LX::LXColorSpace>(color);
    parameter.exposed = exposed != 0;
    std::uint64_t value{};
    if (parameter.type == LX::PinType::Bool)
    {
        std::uint32_t boolean{};
        if (!reader.Word(boolean) || boolean > 1)
            return false;
        parameter.value = boolean != 0;
    }
    else if (parameter.type == LX::PinType::Int || parameter.type == LX::PinType::Float)
    {
        if (!reader.Wide(value))
            return false;
        if (parameter.type == LX::PinType::Int)
            parameter.value = std::bit_cast<std::int64_t>(value);
        else
            parameter.value = std::bit_cast<double>(value);
    }
    else if (parameter.type == LX::PinType::Color || parameter.type == LX::PinType::Vector ||
             parameter.type == LX::PinType::Normal)
    {
        std::array<double, 4> components{};
        const auto count = parameter.type == LX::PinType::Color ? 4u : 3u;
        for (unsigned index = 0; index < count; ++index)
        {
            if (!reader.Wide(value))
                return false;
            components[index] = std::bit_cast<double>(value);
        }
        if (count == 4)
            parameter.value = components;
        else
            parameter.value = std::array<double, 3>{components[0], components[1], components[2]};
    }
    else if (parameter.type == LX::PinType::Texture || parameter.type == LX::PinType::Sampler)
    {
        std::string reference;
        if (!reader.Text(reference, 4096, true))
            return false;
        parameter.value = std::move(reference);
    }
    else
        return false;
    return LX::IsSocketValueValid(parameter.type, parameter.value);
}

RHIShaderReflection LayoutReflection(const BindingLayout& layout)
{
    RHIShaderReflection reflection;
    reflection.stage = RHIShaderStage::Pixel;
    if (layout.uniformBytes != 0)
    {
        RHIShaderResourceReflection cb;
        cb.name = "LXMaterialProperties";
        cb.registerIndex = UniformRegister;
        cb.byteSize = layout.uniformBytes;
        for (const auto& binding : layout.parameters)
            cb.fields.push_back({"lx_bound_p" + std::to_string(binding.parameter.id), ValueType(binding.parameter.type),
                                 binding.offset, binding.bytes});
        reflection.resources.push_back(std::move(cb));
    }
    const auto append = [&](const auto& table, bool texture) {
        for (const auto& logical : table)
        {
            RHIShaderResourceReflection resource;
            resource.name = Symbol(logical);
            resource.kind = texture ? RHIShaderResourceKind::Texture : RHIShaderResourceKind::Sampler;
            resource.registerIndex = logical.slot + (texture ? TextureRegister : SamplerRegister);
            reflection.resources.push_back(std::move(resource));
        }
    };
    append(layout.textures, true);
    append(layout.samplers, false);
    return reflection;
}

// A product may carry one backend (editor: only the running renderer's) or both
// (AssetCooker). Whatever it carries must hold every surface/volume stage.
bool BackendStagesComplete(const LX::LXMaterialProgram& program, std::map<RHIShaderBinary, unsigned>& stages)
{
    const auto required = (program.surface ? 3u : 0u) | (program.volume ? 4u : 0u);
    bool carried = false;
    for (const auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        if (stages[backend] == 0) continue;
        if ((stages[backend] & required) != required) return false;
        carried = true;
    }
    return carried;
}

bool ValidateProduct(const VerifiedProduct& product, const Budget& budget, std::string& error)
{
    const auto invalid = [&]() {
        error = "Invalid verified material generation, target, layout or budget.";
        return false;
    };
    const auto& program = product.program;
    if (budget.compiledBytes > UINT32_MAX || program.semanticKey.empty() || program.semanticKey.size() > TextLimit ||
        program.slang.empty() || program.slang.size() > TextLimit || program.parameters.size() > 128 ||
        program.resources.size() > 128 || program.sourceMap.size() > 4096 || product.targets.empty() ||
        product.targets.size() > 32 || product.targets.size() != product.shaders.size() ||
        product.selection.reason.size() > 4096)
        return invalid();
    Capabilities capabilities;
    capabilities.coreForward = capabilities.layeredLookup = capabilities.refraction = capabilities.subsurface =
        capabilities.volume = true;
    capabilities.deferredFeatures = product.selection.route == Route::Deferred ? program.features : 0;
    Selection selection;
    BindingLayout resolved;
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    if (!SelectRoute(program, capabilities, budget, selection, diagnostics) ||
        selection.tier != product.selection.tier || selection.route != product.selection.route ||
        !ResolveBindings(program, LayoutReflection(product.layout), budget, resolved, diagnostics) ||
        resolved != product.layout)
        return invalid();
    std::set<LX::Id> ids;
    std::set<std::string> identifiers;
    for (const auto& parameter : program.parameters)
    {
        if (parameter.id == 0 || parameter.identifier.empty() || parameter.identifier.size() > 4096 ||
            parameter.name.size() > 4096 || !ids.insert(parameter.id).second ||
            !identifiers.insert(parameter.identifier).second || static_cast<unsigned>(parameter.colorSpace) > 2 ||
            !(Numeric(parameter.type) || parameter.type == LX::PinType::Texture ||
              parameter.type == LX::PinType::Sampler) ||
            !LX::IsSocketValueValid(parameter.type, parameter.value))
            return invalid();
    }
    std::vector<std::uint8_t> defaults(product.layout.uniformBytes);
    for (const auto& binding : product.layout.parameters)
        if (!Pack(binding, binding.parameter.value, defaults))
            return invalid();
    const auto locationValid = [](const LX::LXMaterialSource& source) {
        return source.instances.size() <= 128 && source.property.size() <= 4096;
    };
    for (const auto& resource : program.resources)
        if (static_cast<unsigned>(resource.kind) > 1 || static_cast<unsigned>(resource.colorSpace) > 2 ||
            resource.reference.size() > 4096 || !locationValid(resource.source))
            return invalid();
    for (const auto& range : program.sourceMap)
        if (range.firstLine == 0 || range.lastLine < range.firstLine || !locationValid(range.source))
            return invalid();
    std::set<std::pair<std::string, std::string>> unique;
    std::map<RHIShaderBinary, unsigned> stages;
    std::uint64_t bytes = 0;
    for (const auto& target : product.targets)
    {
        if (target.binary != RHIShaderBinary::Dxil && target.binary != RHIShaderBinary::SpirV)
            return invalid();
        const std::string backend = target.binary == RHIShaderBinary::Dxil ? "dxil" : "spirv";
        const unsigned stage = target.profile.starts_with("vs_")   ? 1u
                               : target.profile.starts_with("ps_") ? 2u
                               : target.profile.starts_with("cs_") ? 4u
                                                                   : 0u;
        if (stage == 0 || target.entry.empty() || target.entry.size() > 128 || target.profile.size() > 128 ||
            !unique.emplace(backend, target.entry).second)
            return invalid();
        stages[target.binary] |= stage;
        const auto artifact = std::ranges::find_if(product.shaders, [&](const auto& shader) {
            return shader.backend == backend && shader.entryPoint == target.entry;
        });
        if (artifact == product.shaders.end() || artifact->bytecode.empty() ||
            artifact->bytecode.size() > budget.compiledBytes - bytes)
            return invalid();
        bytes += artifact->bytecode.size();
    }
    if (!BackendStagesComplete(program, stages))
        return invalid();
    error.clear();
    return true;
}
} // namespace

bool SelectRoute(const LX::LXMaterialProgram& program, const Capabilities& capabilities, const Budget& budget,
                 Selection& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics)
{
    const auto requirements = LX::LXAnalyzeMaterialExecution(program.features);
    if (requirements.error != LX::LXMaterialExecutionError::None || (!program.surface && !program.volume) ||
        requirements.volume != program.volume)
        return Fail(diagnostics, "product.features", "Invalid material feature/output requirements.");
    if (program.textureSamples > budget.textureSamples || budget.variants == 0)
        return Fail(diagnostics, "product.budget", "Material sample/variant budget exceeded.");
    std::uint32_t textures = 0, samplers = 0;
    for (const auto& resource : program.resources)
    {
        if (resource.kind != LX::LXMaterialResourceKind::Texture &&
            resource.kind != LX::LXMaterialResourceKind::Sampler)
            return Fail(diagnostics, "product.resource", "Unknown material resource kind.", resource.source);
        const bool texture = resource.kind == LX::LXMaterialResourceKind::Texture;
        const auto limit = texture ? budget.textures : budget.samplers;
        if (resource.slot >= limit)
            return Fail(diagnostics, "product.budget", "Material resource register budget exceeded.", resource.source);
        texture ? ++textures : ++samplers;
    }
    if (textures > budget.textures || samplers > budget.samplers || textures > 112 || samplers > 125)
        return Fail(diagnostics, "product.budget", "Product material descriptor table budget exceeded.");
    Selection selected;
    selected.tier = requirements.forward           ? Tier::Special
                    : (program.features & 0x0780u) ? Tier::Layered
                                                   : Tier::Standard;
    if (selected.tier == Tier::Special)
    {
        selected.route = program.surface ? Route::Forward : Route::Volume;
        selected.reason = "Nonlocal material transport requires Forward/Volume.";
        if (requirements.refraction && !capabilities.refraction)
            return Fail(diagnostics, "product.refraction", "Refraction transport is not bound.");
        if (requirements.subsurface && !capabilities.subsurface)
            return Fail(diagnostics, "product.subsurface", "Material profile/mask/irradiance transport is not bound.");
        if (requirements.volume && !capabilities.volume)
            return Fail(diagnostics, "product.volume", "Volume integration/composition transport is not bound.");
    }
    else if ((program.features & ~capabilities.deferredFeatures) == 0 && program.surface)
    {
        selected.route = Route::Deferred;
        selected.reason = "All material features are represented by the active GBuffer ABI.";
    }
    else
    {
        selected.route = Route::Forward;
        selected.reason = "Active GBuffer ABI cannot represent this material's IOR/specular/lobes.";
    }
    if (program.surface && selected.route == Route::Forward && !capabilities.coreForward)
        return Fail(diagnostics, "product.forward", "Principled Forward evaluator is not installed.");
    if ((program.features & 0x1F80u) != 0 && !capabilities.layeredLookup)
        return Fail(diagnostics, "product.lookup", "Material reflection/layered lookup is not bound.");
    result = std::move(selected);
    return true;
}

std::string BuildBoundSource(const LX::LXMaterialProgram& program)
{
    std::string source = program.slang;
    for (const auto& resource : program.resources)
    {
        const bool texture = resource.kind == LX::LXMaterialResourceKind::Texture;
        const std::string kind = texture ? "t" : "s";
        Replace(source, "register(" + kind + std::to_string(resource.slot) + ", space1)",
                "register(" + kind + std::to_string(resource.slot + (texture ? TextureRegister : SamplerRegister)) +
                    ")");
    }
    const bool numeric =
        std::ranges::any_of(program.parameters, [](const auto& parameter) { return Numeric(parameter.type); });
    if (numeric)
    {
        source += "\ncbuffer LXMaterialProperties : register(b2)\n{\n";
        for (const auto& parameter : program.parameters)
            if (Numeric(parameter.type))
                source += "    " + std::string(TypeName(parameter.type)) + " lx_bound_p" +
                          std::to_string(parameter.id) + ";\n";
        source += "};\n";
    }
    source += "\nLXMaterialParameters LXBoundMaterialParameters()\n{\n";
    source += "    LXMaterialParameters parameters = LXDefaultMaterialParameters();\n";
    for (const auto& parameter : program.parameters)
        if (Numeric(parameter.type))
            source += "    parameters.lx_p" + std::to_string(parameter.id) + " = lx_bound_p" +
                      std::to_string(parameter.id) + ";\n";
    source += "    return parameters;\n}\n";
    return source;
}

bool ResolveBindings(const LX::LXMaterialProgram& program, const RHIShaderReflection& reflection, const Budget& budget,
                     BindingLayout& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics)
{
    BindingLayout candidate;
    const RHIShaderResourceReflection* uniforms = nullptr;
    std::set<std::pair<RHIShaderResourceKind, std::uint32_t>> registers;
    for (const auto& resource : reflection.resources)
    {
        if (resource.name == "LXMaterialProperties")
        {
            if (uniforms || resource.kind != RHIShaderResourceKind::ConstantBuffer ||
                resource.registerIndex != UniformRegister || resource.registerSpace != 0 ||
                resource.arrayElements != 1 || resource.byteSize == 0 || resource.byteSize > budget.uniformBytes ||
                resource.byteSize > 65536u)
                return Fail(diagnostics, "product.uniform", "Invalid reflected LX material uniform buffer.");
            uniforms = &resource;
            candidate.uniformBytes = resource.byteSize;
        }
        if (resource.name.starts_with("lx_texture_") || resource.name.starts_with("lx_sampler_"))
        {
            const auto found = std::ranges::find_if(
                program.resources, [&](const auto& logical) { return Symbol(logical) == resource.name; });
            if (found == program.resources.end())
                return Fail(diagnostics, "product.resource", "Unexpected reflected LX resource: " + resource.name);
            const bool texture = found->kind == LX::LXMaterialResourceKind::Texture;
            const auto kind = texture ? RHIShaderResourceKind::Texture : RHIShaderResourceKind::Sampler;
            const auto first = texture ? TextureRegister : SamplerRegister;
            const auto limit = texture ? (std::min)(budget.textures, 112u) : (std::min)(budget.samplers, 125u);
            if (resource.kind != kind || resource.registerSpace != 0 || resource.registerIndex != first + found->slot ||
                resource.arrayElements != 1 || found->slot >= limit ||
                !registers.emplace(kind, resource.registerIndex).second)
                return Fail(diagnostics, "product.resource",
                            "Reflected resource differs from LX table: " + resource.name, found->source);
            (texture ? candidate.textures : candidate.samplers).push_back(*found);
        }
    }
    if (candidate.textures.size() + candidate.samplers.size() != program.resources.size())
        return Fail(diagnostics, "product.resource", "Reflected shader omits a generated material resource.");
    std::set<LX::Id> parameterIds;
    for (const auto& parameter : program.parameters)
    {
        if (!Numeric(parameter.type))
            continue;
        if (!uniforms)
            return Fail(diagnostics, "product.uniform", "Generated parameter has no reflected constant buffer.");
        const auto field = std::ranges::find(uniforms->fields, "lx_bound_p" + std::to_string(parameter.id),
                                             &RHIShaderFieldReflection::name);
        const auto type = ValueType(parameter.type);
        if (field == uniforms->fields.end() || field->type != type || field->byteSize != type.columns * 4u ||
            (field->byteOffset % 4u) != 0 ||
            static_cast<std::uint64_t>(field->byteOffset) + field->byteSize > candidate.uniformBytes ||
            !parameterIds.insert(parameter.id).second)
            return Fail(diagnostics, "product.parameter",
                        "Parameter type/offset differs from shader: " + parameter.identifier);
        for (const auto& other : candidate.parameters)
            if (field->byteOffset < other.offset + other.bytes && other.offset < field->byteOffset + field->byteSize)
                return Fail(diagnostics, "product.parameter", "Reflected material parameters overlap.");
        candidate.parameters.push_back({parameter, field->byteOffset, field->byteSize});
    }
    if (uniforms && uniforms->fields.size() != candidate.parameters.size())
        return Fail(diagnostics, "product.uniform", "Unexpected field in LX uniform buffer.");
    const auto sorted = [](const auto& a, const auto& b) { return a.slot < b.slot; };
    std::ranges::sort(candidate.textures, sorted);
    std::ranges::sort(candidate.samplers, sorted);
    result = std::move(candidate);
    return true;
}

bool MergeMaterialReflections(std::span<const RHIShaderReflection* const> stages, RHIShaderReflection& result,
                              std::vector<LX::LXMaterialDiagnostic>& diagnostics)
{
    RHIShaderReflection merged;
    for (const auto* stage : stages)
    {
        if (!stage)
        {
            continue;
        }
        for (const auto& resource : stage->resources)
        {
            if (resource.name != "LXMaterialProperties" && !resource.name.starts_with("lx_texture_") &&
                !resource.name.starts_with("lx_sampler_"))
            {
                continue;
            }
            const auto found = std::ranges::find(merged.resources, resource.name, &RHIShaderResourceReflection::name);
            if (found == merged.resources.end())
            {
                merged.resources.push_back(resource);
            }
            else if (*found != resource)
            {
                return Fail(diagnostics, "product.stageLayout",
                            "Material binding differs between stages: " + resource.name);
            }
        }
    }
    result = std::move(merged);
    return true;
}

bool VerifyProduct(const LX::LXMaterialProgram& program, const std::filesystem::path& sourceFile,
                   std::span<const CompileTarget> targets, const RHIShaderPermutation& permutation,
                   RHIShaderCompileOptions options, const Capabilities& capabilities, const Budget& budget,
                   VerifiedProduct& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics,
                   std::vector<RHIShaderReflection>* materialReflections)
{
    VerifiedProduct candidate;
    candidate.program = program;
    if (!SelectRoute(program, capabilities, budget, candidate.selection, diagnostics))
        return false;
    std::error_code fileError;
    const auto bytes = std::filesystem::file_size(sourceFile, fileError);
    if (fileError || bytes > TextLimit)
        return Fail(diagnostics, "product.source", "Generated host shader is missing or oversized.");
    std::ifstream file(sourceFile, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(file), {}};
    if (!file || !source.starts_with(BuildBoundSource(program)))
        return Fail(diagnostics, "product.source",
                    "Host shader does not contain the requested bound material program.");
    if (targets.empty() || targets.size() > 32)
        return Fail(diagnostics, "product.target", "Material compile target count is invalid.");
    std::set<std::pair<RHIShaderBinary, std::string>> unique;
    std::map<RHIShaderBinary, unsigned> stages;
    for (const auto& target : targets)
    {
        if (target.entry.empty() || target.entry.size() > 128 || !unique.emplace(target.binary, target.entry).second ||
            (target.binary != RHIShaderBinary::Dxil && target.binary != RHIShaderBinary::SpirV))
            return Fail(diagnostics, "product.target", "Unknown or duplicate backend/entry target.");
        unsigned stage = target.profile.starts_with("vs_")   ? 1u
                         : target.profile.starts_with("ps_") ? 2u
                         : target.profile.starts_with("cs_") ? 4u
                                                             : 0u;
        if (stage == 0)
            return Fail(diagnostics, "product.target", "Unknown stage in a material specialization.");
        stages[target.binary] |= stage;
    }
    if (!BackendStagesComplete(program, stages))
        return Fail(diagnostics, "product.target",
                    "Each carried DXIL/SPIR-V backend requires all surface or volume stages.");
    bool hasLayout = false;
    std::uint64_t compiledBytes = 0;
    const std::string name = sourceFile.string();
    std::vector<CompileTarget> ordered(targets.begin(), targets.end());
    std::ranges::sort(ordered, [](const auto& a, const auto& b) {
        return std::tie(a.binary, a.entry, a.profile) < std::tie(b.binary, b.entry, b.profile);
    });
    candidate.targets = ordered;
    std::map<RHIShaderBinary, std::vector<RHIShaderReflection>> reflections;
    // All stages of one backend share source and options: parse the module once.
    RHIShaderCompiler::ModuleReuseScope moduleReuse;
    for (const auto& target : ordered)
    {
        RHIShaderCompiler::VerifiedShader shader;
        std::string error;
        if (!RHIShaderCompiler::VerifyFile(name, target.entry, target.profile, target.binary, permutation, shader,
                                           error, options))
        {
            auto mapped = LX::MapMaterialCompilerDiagnostics(program, sourceFile, error);
            diagnostics.insert(diagnostics.end(), std::make_move_iterator(mapped.begin()),
                               std::make_move_iterator(mapped.end()));
            return false;
        }
        compiledBytes += shader.bytecode.Size();
        if (compiledBytes > budget.compiledBytes)
            return Fail(diagnostics, "product.budget", "Material specialization compiled-byte budget exceeded.");
        if (shader.reflection.stage != RHIShaderStage::Vertex)
        {
            reflections[target.binary].push_back(shader.reflection);
        }
        const auto* begin = static_cast<const std::uint8_t*>(shader.bytecode.Data());
        candidate.shaders.push_back({target.binary == RHIShaderBinary::Dxil ? "dxil" : "spirv",
                                     target.entry,
                                     {begin, begin + shader.bytecode.Size()}});
        candidate.program.semanticKey +=
            "|rhi:" + std::to_string(shader.dependencyIdentity.size()) + ":" + shader.dependencyIdentity;
        candidate.dependencies.insert(candidate.dependencies.end(), shader.dependencies.begin(),
                                      shader.dependencies.end());
    }
    std::ranges::sort(candidate.dependencies);
    candidate.dependencies.erase(std::unique(candidate.dependencies.begin(), candidate.dependencies.end()),
                                 candidate.dependencies.end());
    std::vector<RHIShaderReflection> mergedReflections;
    for (const auto& [backend, consumingStages] : reflections)
    {
        std::vector<const RHIShaderReflection*> views;
        for (const auto& stage : consumingStages)
        {
            views.push_back(&stage);
        }
        RHIShaderReflection merged;
        BindingLayout reflected;
        if (!MergeMaterialReflections(views, merged, diagnostics) ||
            !ResolveBindings(program, merged, budget, reflected, diagnostics))
        {
            return false;
        }
        if (hasLayout && reflected != candidate.layout)
        {
            return Fail(diagnostics, "product.backendLayout", "Material binding layout differs between backends.");
        }
        candidate.layout = std::move(reflected);
        mergedReflections.push_back(std::move(merged));
        hasLayout = true;
    }
    if (!hasLayout)
        return Fail(diagnostics, "product.uniform", "Verified material has no consuming shader stage.");
    result = std::move(candidate);
    if (materialReflections) *materialReflections = std::move(mergedReflections);
    return true;
}

bool DescribeGraphicsShader(const VerifiedProduct& product, RHIShaderBinary backend,
    std::string_view vertex, std::string_view pixel, LX::Runtime::GraphicsShaderDescription& result,
    std::string& error)
{
    if (backend != RHIShaderBinary::Dxil && backend != RHIShaderBinary::SpirV)
    { error = "LX graphics identity requires a supported backend."; return false; }
    const auto select = [&](std::string_view entry, std::string_view stage) -> const CompileTarget* {
        const CompileTarget* found = nullptr;
        for (const auto& target : product.targets)
            if (target.binary == backend && target.profile.starts_with(stage) &&
                (entry.empty() || target.entry == entry))
            {
                if (found) return nullptr;
                found = &target;
            }
        return found;
    };
    const auto* vs = select(vertex, "vs_");
    const auto* ps = select(pixel, "ps_");
    if (!vs || !ps || product.program.semanticKey.empty())
    { error = "LX graphics stages or sealed program identity are missing or ambiguous."; return false; }
    const auto binary = backend == RHIShaderBinary::Dxil ? "dxil" : "spirv";
    for (const auto* target : {vs, ps})
        if (!std::ranges::any_of(product.shaders, [&](const auto& artifact) {
            return artifact.backend == binary && artifact.entryPoint == target->entry && !artifact.bytecode.empty();
        }))
        { error = "LX graphics stage has no owned cooked bytecode."; return false; }
    LX::Runtime::GraphicsShaderDescription candidate;
    candidate.shader = product.materialShader;
    auto& id = candidate.compile;
    id.backend = backend;
    id.vertexEntry = vs->entry; id.vertexProfile = vs->profile;
    id.pixelEntry = ps->entry; id.pixelProfile = ps->profile;
    id.sealedProgramIdentity = product.program.semanticKey;
    if (candidate.shader) id.source = candidate.shader->meta.source.generic_string();
    // Physical include roots need not exist in the Player; their fingerprints
    // and the actual compile flags remain in the verified sealed identity.
    if (product.program.semanticKey.ends_with(SceneHostIdentity))
    {
        id.options.strictMath = id.options.fineDerivatives = true;
        if (!id.permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT", error) ||
            ((product.program.features & 0x3800u) != 0 && !id.permutation.Set("LX_MATERIAL_ROUTE", "2", error)))
            return false;
    }
    result = std::move(candidate);
    error.clear();
    return true;
}

bool DescribeComputeShader(const VerifiedProduct& product, RHIShaderBinary backend,
    std::string_view entry, LX::Runtime::ComputeShaderDescription& result, std::string& error)
{
    if (backend != RHIShaderBinary::Dxil && backend != RHIShaderBinary::SpirV)
    { error = "LX compute identity requires a supported backend."; return false; }
    const CompileTarget* stage = nullptr;
    for (const auto& target : product.targets)
        if (target.binary == backend && target.profile.starts_with("cs_") &&
            (entry.empty() || target.entry == entry))
        {
            if (stage) { error = "LX compute stage is ambiguous."; return false; }
            stage = &target;
        }
    const auto binary = backend == RHIShaderBinary::Dxil ? "dxil" : "spirv";
    if (!stage || product.program.semanticKey.empty() ||
        !std::ranges::any_of(product.shaders, [&](const auto& artifact) {
            return artifact.backend == binary && artifact.entryPoint == stage->entry && !artifact.bytecode.empty();
        }))
    { error = "LX compute stage or sealed bytecode identity is missing."; return false; }
    LX::Runtime::ComputeShaderDescription candidate;
    candidate.shader = product.materialShader;
    auto& id = candidate.compile;
    id.backend = backend; id.entry = stage->entry; id.profile = stage->profile;
    id.sealedProgramIdentity = product.program.semanticKey;
    if (candidate.shader) id.source = candidate.shader->meta.source.generic_string();
    if (product.program.semanticKey.ends_with(SceneHostIdentity))
    {
        id.options.strictMath = id.options.fineDerivatives = true;
        if (!id.permutation.Enable("LX_MATERIAL_PIXEL_FOOTPRINT", error) ||
            ((product.program.features & 0x3800u) != 0 && !id.permutation.Set("LX_MATERIAL_ROUTE", "2", error)))
            return false;
    }
    result = std::move(candidate);
    error.clear();
    return true;
}

bool PrepareUniforms(const BindingLayout& layout, std::span<const ParameterOverride> parameters,
                     std::vector<std::uint8_t>& result, std::vector<LX::LXMaterialDiagnostic>& diagnostics)
{
    if (layout.uniformBytes > 65536)
        return Fail(diagnostics, "product.override", "Material constant buffer exceeds 64 KiB.");
    std::vector<std::uint8_t> candidate(layout.uniformBytes);
    std::set<LX::Id> overridden;
    for (const auto& parameter : parameters)
    {
        const auto found = std::ranges::find(layout.parameters, parameter.id,
                                             [](const auto& binding) { return binding.parameter.id; });
        if (found == layout.parameters.end() || !found->parameter.exposed || !overridden.insert(parameter.id).second)
            return Fail(diagnostics, "product.override", "Unknown, private or duplicate material parameter override.");
    }
    for (const auto& binding : layout.parameters)
    {
        const auto found = std::ranges::find(parameters, binding.parameter.id, &ParameterOverride::id);
        if (!Pack(binding, found == parameters.end() ? binding.parameter.value : found->value, candidate))
            return Fail(diagnostics, "product.override",
                        "Invalid float32/int32 material value: " + binding.parameter.identifier);
    }
    result = std::move(candidate);
    return true;
}

bool PrepareResources(const BindingLayout& layout, std::span<const ParameterOverride> parameters,
                      std::span<const TextureBinding> textures, ResourcePacket& result,
                      std::vector<LX::LXMaterialDiagnostic>& diagnostics)
{
    std::vector<std::uint8_t> uniforms;
    if (!PrepareUniforms(layout, parameters, uniforms, diagnostics))
        return false;
    return PrepareResourcesWithUniforms(layout, uniforms, textures, result, diagnostics);
}

bool PrepareResourcesWithUniforms(const BindingLayout& layout, std::span<const std::uint8_t> uniforms,
                      std::span<const TextureBinding> textures, ResourcePacket& result,
                      std::vector<LX::LXMaterialDiagnostic>& diagnostics)
{
    if (uniforms.size() != layout.uniformBytes || uniforms.size() > 65536)
        return Fail(diagnostics, "product.uniform", "Prepared common material uniform block has an invalid size.");
    ResourcePacket candidate;
    candidate.uniforms.assign(uniforms.begin(), uniforms.end());
    if (textures.size() != layout.textures.size())
        return Fail(diagnostics, "product.texture", "Texture owner count differs from material resource table.");
    std::set<std::uint32_t> textureSlots;
    for (const auto& binding : textures)
        if (!textureSlots.insert(binding.slot).second)
            return Fail(diagnostics, "product.texture", "Duplicate material texture owner slot.");
    for (const auto& resource : layout.textures)
    {
        const auto found = std::ranges::find(textures, resource.slot, &TextureBinding::slot);
        if (found == textures.end() || !found->owner || !found->texture.IsValid() || found->texture.width == 0 ||
            found->texture.height == 0 || found->texture.mipLevels == 0 || found->texture.arraySize != 1 ||
            found->texture.isCube || !IsFloatTextureFormat(found->texture.format))
            return Fail(diagnostics, "product.texture",
                        "Missing or incompatible Texture2D generation: " + resource.reference, resource.source);
        const bool srgb = IsSrgb(found->texture.format);
        if ((resource.colorSpace == LX::LXColorSpace::SRGB && !srgb && !found->linearStorage) ||
            (resource.colorSpace != LX::LXColorSpace::SRGB && srgb))
            return Fail(diagnostics, "product.textureEncoding",
                        "Texture storage/view encoding differs from graph intent.", resource.source);
        while (candidate.textures.size() <= resource.slot)
            candidate.textures.push_back(RHIBindingDesc::Srv2D({}, RHIFormat::RGBA8Unorm).OrNull());
        candidate.textures[resource.slot] =
            RHIBindingDesc::Srv2D(found->texture.handle, found->texture.format, 0, found->texture.mipLevels);
        candidate.owners.push_back(found->owner);
    }
    for (const auto& resource : layout.samplers)
    {
        std::vector<std::string> parts;
        std::istringstream input(resource.reference);
        std::string part;
        while (std::getline(input, part, '-')) parts.push_back(part);
        if (parts.size() == 2) parts = {parts[0], parts[0], parts[1], parts[1]};
        const auto filter = [](const std::string& value) { return value == "linear" || value == "nearest"; };
        const auto address = [](const std::string& value) {
            return value == "repeat" || value == "clamp" || value == "mirror";
        };
        if (parts.size() != 4 || !filter(parts[0]) || !filter(parts[1]) || !address(parts[2]) || !address(parts[3]))
            return Fail(diagnostics, "product.sampler", "Unsupported material sampler: " + resource.reference,
                        resource.source);
        while (candidate.samplers.size() <= resource.slot)
            candidate.samplers.push_back(RHISampler::Point());
        auto& sampler = candidate.samplers[resource.slot];
        sampler.minMag = parts[0] == "linear" ? RHIFilterMode::Linear : RHIFilterMode::Point;
        sampler.mip = parts[1] == "linear" ? RHIFilterMode::Linear : RHIFilterMode::Point;
        const auto mode = [](const std::string& value) {
            return value == "repeat" ? RHIAddressMode::Wrap : value == "mirror" ? RHIAddressMode::Mirror : RHIAddressMode::Clamp;
        };
        sampler.addressU = sampler.addressW = mode(parts[2]);
        sampler.addressV = mode(parts[3]);
    }
    result = std::move(candidate);
    return true;
}

bool WriteCookedProgram(const VerifiedProduct& product, const Budget& budget, std::vector<std::uint8_t>& result,
                        std::string& error)
{
    if (!ValidateProduct(product, budget, error))
        return false;
    const auto& program = product.program;
    const auto metadata = LX::WriteMaterialProgramMetadata(program);
    const auto source = BuildBoundSource(program);
    if (source.size() > TextLimit || metadata.size() > TextLimit || product.selection.reason.size() > 4096)
    {
        error = "Oversized cooked material text.";
        return false;
    }
    std::vector<std::uint8_t> bytes;
    Word(bytes, CookMagic);
    Word(bytes, CookedProgramVersion);
    Word(bytes, LX::LXMaterialProgram::CompilerVersion);
    Text(bytes, program.semanticKey);
    Text(bytes, metadata);
    Text(bytes, source);
    Text(bytes, program.slang);
    Word(bytes, program.features);
    Word(bytes, program.surface ? 1u : 0u);
    Word(bytes, program.volume ? 1u : 0u);
    Word(bytes, program.textureSamples);
    Word(bytes, static_cast<std::uint32_t>(product.selection.tier));
    Word(bytes, static_cast<std::uint32_t>(product.selection.route));
    Text(bytes, product.selection.reason);
    Word(bytes, static_cast<std::uint32_t>(program.parameters.size()));
    for (const auto& parameter : program.parameters)
        WriteParameter(bytes, parameter);
    Word(bytes, static_cast<std::uint32_t>(program.resources.size()));
    for (const auto& resource : program.resources)
    {
        Word(bytes, static_cast<std::uint32_t>(resource.kind));
        Word(bytes, resource.slot);
        Wide(bytes, resource.parameter);
        Text(bytes, resource.reference);
        Word(bytes, static_cast<std::uint32_t>(resource.colorSpace));
        WriteLocation(bytes, resource.source);
    }
    Word(bytes, static_cast<std::uint32_t>(program.sourceMap.size()));
    for (const auto& range : program.sourceMap)
    {
        Word(bytes, range.firstLine);
        Word(bytes, range.lastLine);
        WriteLocation(bytes, range.source);
    }
    Word(bytes, product.layout.uniformBytes);
    Word(bytes, static_cast<std::uint32_t>(product.layout.parameters.size()));
    for (const auto& binding : product.layout.parameters)
    {
        Wide(bytes, binding.parameter.id);
        Word(bytes, binding.offset);
        Word(bytes, binding.bytes);
    }
    auto ordered = product.targets;
    std::ranges::sort(ordered, [](const auto& a, const auto& b) {
        return std::tie(a.binary, a.entry, a.profile) < std::tie(b.binary, b.entry, b.profile);
    });
    Word(bytes, static_cast<std::uint32_t>(ordered.size()));
    for (const auto& target : ordered)
    {
        const std::string backend = target.binary == RHIShaderBinary::Dxil ? "dxil" : "spirv";
        const auto shader = std::ranges::find_if(product.shaders, [&](const auto& artifact) {
            return artifact.backend == backend && artifact.entryPoint == target.entry;
        });
        Word(bytes, static_cast<std::uint32_t>(target.binary));
        Text(bytes, target.entry);
        Text(bytes, target.profile);
        Block(bytes, shader->bytecode);
    }
    Word(bytes, product.materialShader ? 1u : 0u);
    if (product.materialShader)
    {
        const auto& shader = *product.materialShader;
        GeneratedMaterialShader restored;
        if (shader.cookedContract.empty() || shader.cookedContract.size() > 1024u * 1024u ||
            !RestoreMaterialShaderMeta(product, shader.meta.guid, shader.document, shader.source, restored, error,
                shader.cookedContract) ||
            restored.layout != shader.layout)
        {
            if (error.empty()) error = "Generated material binding differs from the verified product layout.";
            return false;
        }
        Text(bytes, shader.meta.guid.ToString());
        Text(bytes, shader.document);
        Text(bytes, shader.source);
        Block(bytes, {reinterpret_cast<const std::uint8_t*>(shader.cookedContract.data()), shader.cookedContract.size()});
    }
    Wide(bytes, Checksum(bytes));
    result = std::move(bytes);
    error.clear();
    return true;
}

bool ReadCookedProgram(std::span<const std::uint8_t> bytes, const Budget& budget, CookedProgram& result,
                       std::string& error)
{
    const auto invalid = [&error]() {
        error = "Corrupt, incompatible or oversized cooked material program.";
        return false;
    };
    if (budget.compiledBytes > UINT32_MAX || bytes.size() < 20 ||
        bytes.size() > budget.compiledBytes + 8ull * TextLimit)
        return invalid();
    Reader checksum(bytes.last(8));
    std::uint64_t hash{};
    if (!checksum.Wide(hash) || hash != Checksum(bytes.first(bytes.size() - 8)))
        return invalid();
    Reader reader(bytes.first(bytes.size() - 8));
    std::uint32_t magic{}, version{}, compiler{}, count{}, surface{}, volume{}, tier{}, route{};
    CookedProgram candidate;
    auto& product = candidate.product;
    auto& program = product.program;
    if (!reader.Word(magic) || !reader.Word(version) || !reader.Word(compiler) || magic != CookMagic ||
        version != CookedProgramVersion || compiler != LX::LXMaterialProgram::CompilerVersion ||
        !reader.Text(program.semanticKey) || !reader.Text(candidate.metadata) || !reader.Text(candidate.boundSource) ||
        !reader.Text(program.slang) || !reader.Word(program.features) || !reader.Word(surface) || surface > 1 ||
        !reader.Word(volume) || volume > 1 || !reader.Word(program.textureSamples) || !reader.Word(tier) || tier > 2 ||
        !reader.Word(route) || route > 2 || !reader.Text(product.selection.reason, 4096, true) || !reader.Word(count) ||
        count > 128)
        return invalid();
    program.surface = surface != 0;
    program.volume = volume != 0;
    program.execution = LX::LXAnalyzeMaterialExecution(program.features);
    product.selection.tier = static_cast<Tier>(tier);
    product.selection.route = static_cast<Route>(route);
    program.parameters.resize(count);
    for (auto& parameter : program.parameters)
        if (!ReadParameter(reader, parameter))
            return invalid();
    if (!reader.Word(count) || count > 128)
        return invalid();
    program.resources.resize(count);
    for (auto& resource : program.resources)
    {
        std::uint32_t kind{}, color{};
        if (!reader.Word(kind) || kind > 1 || !reader.Word(resource.slot) || !reader.Wide(resource.parameter) ||
            !reader.Text(resource.reference, 4096, true) || !reader.Word(color) || color > 2 ||
            !ReadLocation(reader, resource.source))
            return invalid();
        resource.kind = static_cast<LX::LXMaterialResourceKind>(kind);
        resource.colorSpace = static_cast<LX::LXColorSpace>(color);
        (kind == 0 ? product.layout.textures : product.layout.samplers).push_back(resource);
    }
    const auto resourceOrder = [](const auto& a, const auto& b) { return a.slot < b.slot; };
    std::ranges::sort(product.layout.textures, resourceOrder);
    std::ranges::sort(product.layout.samplers, resourceOrder);
    if (!reader.Word(count) || count > 4096)
        return invalid();
    program.sourceMap.resize(count);
    for (auto& range : program.sourceMap)
        if (!reader.Word(range.firstLine) || !reader.Word(range.lastLine) || !ReadLocation(reader, range.source))
            return invalid();
    if (!reader.Word(product.layout.uniformBytes) || product.layout.uniformBytes > budget.uniformBytes ||
        !reader.Word(count) || count > 128)
        return invalid();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        ParameterBinding binding;
        LX::Id id{};
        if (!reader.Wide(id) || !reader.Word(binding.offset) || !reader.Word(binding.bytes))
            return invalid();
        const auto parameter = std::ranges::find(program.parameters, id, &LX::LXMaterialParameter::id);
        if (parameter == program.parameters.end() || !Numeric(parameter->type))
            return invalid();
        binding.parameter = *parameter;
        product.layout.parameters.push_back(std::move(binding));
    }
    if (!reader.Word(count) || count == 0 || count > 32)
        return invalid();
    std::uint64_t total = 0;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        CompileTarget target;
        LX::LXMaterialShaderArtifact shader;
        std::uint32_t backend{};
        std::span<const std::uint8_t> code;
        if (!reader.Word(backend) || backend > 1 || !reader.Text(target.entry, 128) ||
            !reader.Text(target.profile, 128) || !reader.Block(code, budget.compiledBytes - total))
            return invalid();
        total += code.size();
        target.binary = static_cast<RHIShaderBinary>(backend);
        shader.backend = backend == 0 ? "dxil" : "spirv";
        shader.entryPoint = target.entry;
        shader.bytecode.assign(code.begin(), code.end());
        product.targets.push_back(std::move(target));
        product.shaders.push_back(std::move(shader));
    }
    std::uint32_t generated{};
    std::string graphGuid, document, source;
    std::span<const std::uint8_t> contract;
    if (!reader.Word(generated) || generated > 1 ||
        (generated && (!reader.Text(graphGuid, 36) || !reader.Text(document, 1024u * 1024u) || !reader.Text(source) ||
            !reader.Block(contract, 1024u * 1024u))))
        return invalid();
    if (!reader.Empty() || !ValidateProduct(product, budget, error) ||
        candidate.boundSource != BuildBoundSource(program) ||
        candidate.metadata != LX::WriteMaterialProgramMetadata(program))
        return invalid();
    if (generated)
    {
        GeneratedMaterialShader shader;
        if (contract.empty() || !RestoreMaterialShaderMeta(product, FileGuid{graphGuid}, document, source, shader, error,
                {reinterpret_cast<const std::byte*>(contract.data()), contract.size()})) return false;
        product.materialShader = std::make_shared<GeneratedMaterialShader>(std::move(shader));
    }
    result = std::move(candidate);
    error.clear();
    return true;
}

} // namespace material_graph
