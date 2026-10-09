#include "CookedCodeMaterial.h"

#include "CookedShaderMeta.h"
#include "MaterialAssetSetCodec.h"
#include "../MaterialAuthoringCodec.h"
#include "../MaterialPropertyBlock.h"
#include "../MaterialResolver.h"
#include "../../Assets/AssetIdentityProfile.h"
#include "../../ShaderPermutationDomain.h"
#include "../../LXMaterialRuntime.h"
#include "../../StandardMaterialProperty.h"
#include "../../RHI/ModelVertexInputLayout.h"
#include "../../RHI/RHIShaderVerifiedCache.h"
#include "../../Render/Graph/EnhancedForwardLighting.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <exception>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        constexpr std::uint32_t kMagic = 0x50434543u; // CECP
        constexpr std::size_t kMaxInputs = 4096u;
        constexpr std::size_t kMaxText = 4096u;

        bool Fail(std::string& failure, std::string message)
        {
            failure = std::move(message);
            return false;
        }

        bool ValidId(const AssetId& id)
        {
            return IsAssetIdV4(id) || assets::IsUuidV8(id.value);
        }

        std::string IdText(const AssetId& id) { return FileGuid{ id.value }.ToString(); }

        bool ParseId(std::string_view text, AssetId& id)
        {
            return TryParseCanonicalAssetId(text, id) || assets::TryParseCanonicalUuidV8(text, id.value);
        }

        bool Path(std::string_view value)
        {
            if (value.empty() || value.size() > kMaxText || value.front() == '/' ||
                value.find('\\') != value.npos || value.find(':') != value.npos || value.find('\0') != value.npos)
            {
                return false;
            }
            std::size_t begin{};
            while (begin < value.size())
            {
                const auto end = value.find('/', begin);
                const auto part = value.substr(begin, end == value.npos ? value.size() - begin : end - begin);
                if (part.empty() || part == "." || part == ".." || part.back() == ' ' || part.back() == '.' ||
                    std::ranges::any_of(part, [](unsigned char ch) { return ch < 32u || ch == 127u; }))
                {
                    return false;
                }
                if (end == value.npos)
                {
                    return true;
                }
                begin = end + 1u;
            }
            return false;
        }

        bool DigestText(std::string_view value)
        {
            return value.size() == 64u && std::ranges::all_of(value, [](char ch)
            { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
        }

        bool AddEdge(std::vector<AssetDependency>& edges, const AssetId& owner,
            const AssetId& target, CookedAssetKind kind, std::string& failure)
        {
            if (!ValidId(target) || owner == target)
            {
                return Fail(failure, "Code material dependency is invalid or self-referencing.");
            }
            const auto found = std::ranges::find_if(edges, [&](const auto& edge)
            { return edge.target.key.assetId == target; });
            if (found != edges.end())
            {
                return found->target.kind == kind || Fail(failure, "Code material uses one identity for different types.");
            }
            edges.push_back({ {{target, {}}, kind}, AssetDependencyKind::Hard, AssetDependencyScope::Internal });
            return true;
        }

        bool MatchEdges(std::span<const AssetDependency> expected, std::span<const AssetDependency> actual,
            std::string& failure)
        {
            if (expected.size() != actual.size())
            {
                return Fail(failure, "Code material typed dependency count differs.");
            }
            std::vector<TypedAssetReference> targets;
            std::set<AssetIdentity> keys;
            for (const auto& edge : expected)
            {
                if (edge.kind != AssetDependencyKind::Hard ||
                    (edge.scope != AssetDependencyScope::Internal && edge.scope != AssetDependencyScope::External) ||
                    edge.target.key.subassetId.IsValid() || !keys.insert(edge.target.key).second)
                {
                    return Fail(failure, "Code material dependencies require unique exact typed Hard edges and explicit scopes.");
                }
                targets.push_back(edge.target);
            }
            std::ranges::sort(targets);
            for (std::size_t i = 0; i < targets.size(); ++i)
            {
                if (targets[i] != actual[i].target)
                {
                    return Fail(failure, "Code material typed dependency target differs.");
                }
            }
            return true;
        }

        bool MaterialEdges(const AuthoredMaterialDocument& document,
            std::vector<AssetDependency>& edges, std::string& failure)
        {
            if (!ValidId(document.material.assetId) || !IsAssetIdV4(document.material.shaderAssetId) ||
                !AddEdge(edges, document.material.assetId, document.programAssetId, CookedAssetKind::MaterialProgram, failure) ||
                !AddEdge(edges, document.material.assetId, document.material.shaderAssetId, CookedAssetKind::ShaderMeta, failure))
            {
                return Fail(failure, "Authored material requires distinct valid Material, MaterialProgram and ShaderMeta identities.");
            }
            if (!std::ranges::is_sorted(document.defaultTextureAssetIds) ||
                std::adjacent_find(document.defaultTextureAssetIds.begin(), document.defaultTextureAssetIds.end()) !=
                    document.defaultTextureAssetIds.end())
            {
                return Fail(failure, "Authored material default textures must be sorted and unique.");
            }
            std::set<std::string> names;
            for (const auto& property : document.material.properties)
            {
                if (property.name.empty() || !names.insert(property.name).second)
                {
                    return Fail(failure, "Authored material contains an empty or duplicate property name.");
                }
                if (const auto* texture = std::get_if<TextureReference>(&property.value);
                    texture && texture->assetId.IsValid() &&
                    !AddEdge(edges, document.material.assetId, texture->assetId, CookedAssetKind::Texture, failure))
                {
                    return false;
                }
            }
            for (const auto& texture : document.defaultTextureAssetIds)
            {
                if (!AddEdge(edges, document.material.assetId, texture, CookedAssetKind::Texture, failure))
                {
                    return false;
                }
            }
            std::ranges::sort(edges);
            return true;
        }

        bool ProgramEdges(const CodeProgram& program, std::vector<AssetDependency>& edges, std::string& failure)
        {
            if (!AddEdge(edges, program.programAssetId, program.shaderMetaAssetId, CookedAssetKind::ShaderMeta, failure))
            {
                return false;
            }
            std::vector<AssetId> defaults;
            if (!CollectCodeProgramDefaultTextures(program.meta, defaults, failure))
            {
                return false;
            }
            for (const auto& texture : defaults)
            {
                if (!AddEdge(edges, program.programAssetId, texture, CookedAssetKind::Texture, failure))
                {
                    return false;
                }
            }
            std::ranges::sort(edges);
            return true;
        }

        using VariantKey = std::tuple<RHIShaderBinary, std::uint32_t, std::vector<std::uint16_t>, std::uint32_t, bool>;
        VariantKey Key(const CodeProgramVariant& value)
        {
            return {value.backend, value.passIndex, value.keywordSelections, value.vertexAttributeMask, value.referencePath};
        }

        bool ValidateProgram(CodeProgram& program, std::string& failure)
        {
            if (!ValidId(program.programAssetId) || !IsAssetIdV4(program.shaderMetaAssetId) ||
                program.programAssetId == program.shaderMetaAssetId ||
                !Path(program.metadataPath) || !Path(program.rootSourcePath) ||
                !DigestText(program.compilerStamp) || program.formatStamp != kCodeProgramFormatStamp ||
                program.abiStamp != kCodeProgramAbiStamp || program.inputs.empty() || program.inputs.size() > kMaxInputs ||
                program.metadataBytes.empty() || program.metadataBytes.size() > kShaderMetaDocumentMaxBytes ||
                program.variants.empty() || program.variants.size() > kCodeProgramMaxVariants)
            {
                return Fail(failure, "Code program identity, compiler/format/ABI recipe or bounded inventory is invalid.");
            }
            std::string previous;
            std::set<std::string> inputPaths;
            for (const auto& input : program.inputs)
            {
                if (!Path(input.path) || (!previous.empty() && input.path <= previous) || !input.byteSize ||
                    input.byteSize > 32u * 1024u * 1024u ||
                    std::ranges::all_of(input.sha256, [](auto byte) { return byte == 0u; }))
                {
                    return Fail(failure, "Code program input inventory must contain sorted unique paths, bounded sizes and SHA-256 values.");
                }
                previous = input.path;
                inputPaths.insert(input.path);
            }
            if (!inputPaths.contains(program.metadataPath) || !inputPaths.contains(program.metadataPath + ".meta") ||
                !inputPaths.contains(program.rootSourcePath) || !inputPaths.contains(program.rootSourcePath + ".meta"))
            {
                return Fail(failure, "Code program inventory omits metadata/source bytes or their identity sidecars.");
            }
            if (!ReadShaderMetaArtifact(program.metadataBytes, program.shaderMetaAssetId, program.meta, failure))
            {
                return false;
            }
            const auto rootPath = (std::filesystem::u8path(program.metadataPath).parent_path() / program.meta.source)
                .lexically_normal().generic_string();
            if (rootPath != program.rootSourcePath || !Path(rootPath))
            {
                return Fail(failure, "Code program root source differs from the carried metadata source token.");
            }
            ShaderMetaPermutationStats stats;
            if (!ShaderPermutationDomain::Measure(program.meta, stats, failure))
            {
                return false;
            }
            std::uint64_t variantsPerSelection{};
            for (const auto& pass : program.meta.passes)
            {
                if (!pass.IsCompute() && (!pass.vertex || !pass.pixel))
                {
                    return Fail(failure, "Code program graphics passes require vertex and pixel stages.");
                }
                variantsPerSelection += pass.IsCompute() ? 2u :
                    2u * (assets::kModelVertexMasks.size() + 1u) * (pass.name == "Forward" ? 2u : 1u);
            }
            if (stats.variantsPerPass > kCodeProgramMaxVariants / variantsPerSelection ||
                program.variants.size() != stats.variantsPerPass * variantsPerSelection)
            {
                return Fail(failure, "Code program must carry complete backend/pass/keyword/vertex-mask/reference-path coverage.");
            }
            std::set<VariantKey> keys;
            bool firstLayout = true;
            for (auto& variant : program.variants)
            {
                if ((variant.backend != RHIShaderBinary::Dxil && variant.backend != RHIShaderBinary::SpirV) ||
                    variant.passIndex >= program.meta.passes.size() || !keys.insert(Key(variant)).second)
                {
                    return Fail(failure, "Code program variant is invalid or duplicated.");
                }
                const auto& pass = program.meta.passes[variant.passIndex];
                if ((pass.IsCompute() && (variant.vertexAttributeMask || variant.referencePath)) ||
                    (!pass.IsCompute() && variant.vertexAttributeMask &&
                        std::ranges::find(assets::kModelVertexMasks, variant.vertexAttributeMask) == assets::kModelVertexMasks.end()) ||
                    (variant.referencePath && pass.name != "Forward"))
                {
                    return Fail(failure, "Code program carries an unsupported vertex-mask/reference variant.");
                }
                ShaderMetaPermutation selected;
                if (!ShaderPermutationDomain::Resolve(program.meta, variant.passIndex, variant.keywordSelections, selected, failure))
                {
                    return false;
                }
                auto permutation = selected.defines;
                if (pass.name == "Forward" &&
                    (!permutation.Set("TILE_SIZE", std::to_string(EnhancedForwardLighting::TileSize), failure) ||
                        !permutation.Set("MAX_LIGHTS_PER_TILE", std::to_string(EnhancedForwardLighting::MaxLightsPerTile), failure)))
                {
                    return false;
                }
                if ((variant.referencePath && !permutation.Enable("REFERENCE_PATH", failure)) ||
                    (variant.vertexAttributeMask && !ModelVertexInput::ApplyShaderPermutation(variant.vertexAttributeMask, permutation, failure)))
                {
                    return false;
                }
                if (variant.permutation.Entries() != permutation.Entries())
                {
                    return Fail(failure, "Code program compile defines differ from its exact material/renderer recipe.");
                }
                variant.materialPermutationKey = selected.key;
                if (variant.stages.size() != (pass.IsCompute() ? 1u : 2u))
                {
                    return Fail(failure, "Code program variant has incomplete or extra stages.");
                }
                std::vector<RHIShaderReflection> reflections;
                for (std::size_t index = 0; index < variant.stages.size(); ++index)
                {
                    auto& stage = variant.stages[index];
                    const auto expectedStage = pass.IsCompute() ? RHIShaderStage::Compute :
                        (index == 0u ? RHIShaderStage::Vertex : RHIShaderStage::Pixel);
                    const auto& expectedEntry = pass.IsCompute() ? pass.compute->entry :
                        (index == 0u ? pass.vertex->entry : pass.pixel->entry);
                    const auto expectedProfile = pass.IsCompute() ? "cs_6_0" : (index == 0u ? "vs_6_0" : "ps_6_0");
                    if (stage.stage != expectedStage || stage.reflection.stage != expectedStage || stage.entry != expectedEntry ||
                        stage.profile != expectedProfile || !stage.bytecode.IsValid())
                    {
                        return Fail(failure, "Code program stage, entry point, profile, bytecode or reflection differs from its recipe.");
                    }
                    reflections.push_back(stage.reflection);
                }
                ShaderMetaBindingLayout layout;
                if (!ShaderMetaReflection::Resolve(program.meta, reflections, layout, failure))
                {
                    return false;
                }
                if (!firstLayout && program.layout != layout)
                {
                    return Fail(failure, "Code program variants disagree on their immutable material binding layout.");
                }
                program.layout = std::move(layout);
                firstLayout = false;
            }
            if (!std::ranges::is_sorted(program.variants, {}, Key))
            {
                return Fail(failure, "Code program variants must use canonical backend/pass/selection/mask/reference order.");
            }
            // Share the runtime's value-only invariant. Reflection resolution
            // alone does not reject overlapping numeric fields, oversized
            // constant buffers or aliased texture registers for authored Code.
            LX::Runtime::ShaderGeneration generation;
            generation.meta = program.meta;
            generation.layout = program.layout;
            return LX::Runtime::ValidateShaderGeneration(generation, failure);
        }

        struct Writer final
        {
            std::vector<std::byte> bytes;
            void Raw(std::span<const std::byte> value)
            {
                if (value.size() > kCodeProgramMaxBytes - bytes.size())
                {
                    throw std::runtime_error("Code program exceeds its byte budget.");
                }
                bytes.insert(bytes.end(), value.begin(), value.end());
            }
            template<class T>
            void Integer(T value)
            {
                static_assert(std::is_unsigned_v<T>);
                for (std::size_t i = 0; i < sizeof(T); ++i)
                {
                    const std::byte byte{ static_cast<std::uint8_t>(value >> (8u * i)) };
                    Raw({ &byte, 1u });
                }
            }
            void Text(std::string_view value)
            {
                if (value.size() > kMaxText)
                {
                    throw std::runtime_error("Code program string exceeds its byte budget.");
                }
                Integer(static_cast<std::uint32_t>(value.size()));
                Raw(std::as_bytes(std::span(value.data(), value.size())));
            }
            void Blob(std::span<const std::byte> value)
            {
                Integer(static_cast<std::uint64_t>(value.size()));
                Raw(value);
            }
        };

        struct Reader final
        {
            std::span<const std::byte> bytes;
            std::size_t offset{};
            std::span<const std::byte> Raw(std::size_t size)
            {
                if (size > bytes.size() - offset)
                {
                    throw std::runtime_error("Truncated code program.");
                }
                const auto result = bytes.subspan(offset, size);
                offset += size;
                return result;
            }
            template<class T>
            T Integer()
            {
                static_assert(std::is_unsigned_v<T>);
                T value{};
                const auto source = Raw(sizeof(T));
                for (std::size_t i = 0; i < sizeof(T); ++i)
                {
                    value = static_cast<T>(value | (static_cast<T>(std::to_integer<unsigned>(source[i])) << (8u * i)));
                }
                return value;
            }
            std::string Text()
            {
                const auto size = Integer<std::uint32_t>();
                if (size > kMaxText)
                {
                    throw std::runtime_error("Code program string exceeds its byte budget.");
                }
                const auto data = Raw(size);
                return { reinterpret_cast<const char*>(data.data()), data.size() };
            }
            std::span<const std::byte> Blob(std::size_t maximum)
            {
                const auto size = Integer<std::uint64_t>();
                if (size > maximum)
                {
                    throw std::runtime_error("Code program blob exceeds its byte budget.");
                }
                return Raw(static_cast<std::size_t>(size));
            }
            AssetId Id()
            {
                AssetId id;
                if (!ParseId(Text(), id))
                {
                    throw std::runtime_error("Code program identity is noncanonical.");
                }
                return id;
            }
        };
    }

    namespace
    {
        struct CodeCharge final
        {
            std::size_t bytes{};
            void Add(std::size_t count) noexcept { bytes = count > SIZE_MAX - bytes ? SIZE_MAX : bytes + count; }
            void Text(const std::string& value) noexcept { Add(value.capacity()); Add(1u); }
            template<class T>
            void Vector(const std::vector<T>& value) noexcept
            { Add(value.capacity() > SIZE_MAX / sizeof(T) ? SIZE_MAX : value.capacity() * sizeof(T)); }
            void Path(const std::filesystem::path& value) noexcept
            {
                const auto count = value.native().capacity();
                Add(count > SIZE_MAX / sizeof(std::filesystem::path::value_type) ? SIZE_MAX :
                    count * sizeof(std::filesystem::path::value_type));
            }
            void Resources(const std::vector<RHIShaderResourceReflection>& values) noexcept
            {
                Vector(values);
                for (const auto& resource : values)
                {
                    Text(resource.name); Vector(resource.fields);
                    for (const auto& field : resource.fields)
                    {
                        Text(field.name);
                    }
                }
            }
        };
    }

    std::size_t CodeProgramRetainedBytes(const CodeProgram& program) noexcept
    {
        CodeCharge charge;
        charge.Add(sizeof(program));
        charge.Text(program.metadataPath); charge.Text(program.rootSourcePath); charge.Text(program.compilerStamp);
        charge.Text(program.formatStamp); charge.Text(program.abiStamp);
        charge.Vector(program.inputs); charge.Vector(program.metadataBytes); charge.Vector(program.variants);
        for (const auto& input : program.inputs)
        {
            charge.Text(input.path);
        }
        const auto& meta = program.meta;
        charge.Text(meta.name); charge.Path(meta.source); charge.Path(meta.originPath);
        charge.Vector(meta.properties); charge.Vector(meta.keywords); charge.Vector(meta.passes);
        for (const auto& property : meta.properties)
        {
            charge.Text(property.name); charge.Text(property.label); charge.Text(property.semantic); charge.Text(property.colorSpace);
        }
        for (const auto& keyword : meta.keywords)
        {
            charge.Text(keyword.name); charge.Vector(keyword.values);
            for (const auto& value : keyword.values)
            {
                charge.Text(value);
            }
        }
        for (const auto& pass : meta.passes)
        {
            charge.Text(pass.name);
            if (pass.vertex)
            {
                charge.Text(pass.vertex->entry);
            }
            if (pass.pixel)
            {
                charge.Text(pass.pixel->entry);
            }
            if (pass.compute)
            {
                charge.Text(pass.compute->entry);
            }
        }
        charge.Text(program.layout.constantBufferName); charge.Vector(program.layout.properties);
        for (const auto& property : program.layout.properties)
        {
            charge.Text(property.name);
            charge.Text(property.resourceName);
        }
        charge.Resources(program.layout.samplers);
        for (const auto& variant : program.variants)
        {
            charge.Vector(variant.keywordSelections); charge.Vector(variant.stages);
            charge.Vector(variant.permutation.Entries());
            for (const auto& define : variant.permutation.Entries())
            {
                charge.Text(define.name);
                charge.Text(define.value);
            }
            for (const auto& stage : variant.stages)
            {
                charge.Text(stage.entry); charge.Text(stage.profile); charge.Add(stage.bytecode.Capacity());
                charge.Resources(stage.reflection.resources);
            }
        }
        return charge.bytes;
    }

    bool CollectCodeProgramDefaultTextures(const ShaderMeta& meta, std::vector<AssetId>& result, std::string& failure)
    {
        std::vector<AssetId> defaults;
        for (const auto& property : meta.properties)
        {
            if (property.type != ShaderPropertyType::Texture2D)
            {
                continue;
            }
            if (const auto* guid = std::get_if<FileGuid>(&property.defaultValue); guid && *guid != FileGuid{})
            {
                const AssetId id{ guid->m_guid };
                if (!ValidId(id))
                {
                    return Fail(failure, "Code shader default texture identity is invalid.");
                }
                if (std::ranges::find(defaults, id) == defaults.end())
                {
                    defaults.push_back(id);
                }
            }
        }
        std::ranges::sort(defaults);
        result = std::move(defaults);
        failure.clear();
        return true;
    }

    bool EncodeAuthoredMaterialArtifact(const AuthoredMaterialDocument& document,
        std::vector<std::byte>& bytes, std::vector<AssetDependency>& dependencies, std::string& failure)
    {
        try
        {
            std::vector<AssetDependency> edges;
            if (!MaterialEdges(document, edges, failure))
            {
                return false;
            }
            Authoring::WriteDocument staging;
            staging.Root().SetMap();
            auto node = staging.Root().Child("authored_material");
            node.SetMap();
            node.Child("schema").SetScalar(kAuthoredMaterialVersion);
            node.Child("programAssetId").SetScalar(IdText(document.programAssetId));
            auto defaults = node.Child("defaultTextures");
            defaults.SetSequence();
            for (const auto& id : document.defaultTextureAssetIds)
            {
                defaults.Append().SetScalar(IdText(id));
            }
            if (!SerializeMaterialAuthoring(document.material, node.Child("material"), failure))
            {
                return false;
            }
            Material checked;
            if (!DeserializeMaterialAuthoring(node.Read()["material"], checked, failure))
            {
                return false;
            }
            std::vector<std::byte> candidate;
            if (!Authoring::EncodeCookedDocument(staging.Root().Read(), candidate, failure))
            {
                return false;
            }
            if (candidate.size() > kMaterialDocumentMaxBytes)
            {
                return Fail(failure, "Authored material exceeds its byte budget.");
            }
            bytes = std::move(candidate);
            dependencies = std::move(edges);
            failure.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            return Fail(failure, exception.what());
        }
    }

    bool ReadAuthoredMaterialArtifact(std::span<const std::byte> bytes, const TypedAssetReference& expected,
        std::span<const AssetDependency> dependencies, AuthoredMaterialDocument& result, std::string& failure)
    {
        try
        {
            if (bytes.empty() || bytes.size() > kMaterialDocumentMaxBytes || expected.kind != CookedAssetKind::Material ||
                expected.key.subassetId.IsValid() || !ValidId(expected.key.assetId))
            {
                return Fail(failure, "Authored material expected identity, kind or byte budget is invalid.");
            }
            const auto parsed = Authoring::ParsedDocument::ParseCooked(bytes, failure);
            if (!parsed)
            {
                return false;
            }
            const auto node = parsed.Root()["authored_material"];
            if (!node.IsMap() || !node["schema"].IsScalar() || node["schema"].As<std::uint32_t>() != kAuthoredMaterialVersion ||
                !node["programAssetId"].IsScalar() || !node["defaultTextures"].IsSequence() || node["defaultTextures"].Size() > 4096u)
            {
                return Fail(failure, "Authored material envelope schema is invalid.");
            }
            AuthoredMaterialDocument candidate;
            if (!ParseId(node["programAssetId"].Scalar(), candidate.programAssetId) ||
                !DeserializeMaterialAuthoring(node["material"], candidate.material, failure) ||
                candidate.material.assetId != expected.key.assetId)
            {
                return Fail(failure, "Authored material payload identity differs from its catalog entry.");
            }
            for (const auto value : node["defaultTextures"])
            {
                AssetId id;
                if (!value.IsScalar() || !ParseId(value.Scalar(), id))
                {
                    return Fail(failure, "Authored material default texture identity is invalid.");
                }
                candidate.defaultTextureAssetIds.push_back(id);
            }
            std::vector<AssetDependency> edges;
            if (!MaterialEdges(candidate, edges, failure) || !MatchEdges(dependencies, edges, failure))
            {
                return false;
            }
            std::vector<std::byte> canonical;
            if (!EncodeAuthoredMaterialArtifact(candidate, canonical, edges, failure) ||
                !std::ranges::equal(bytes, canonical))
            {
                return Fail(failure, "Authored material document is not canonical or contains extra fields.");
            }
            result = std::move(candidate);
            failure.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            return Fail(failure, exception.what());
        }
    }

    bool EncodeCodeProgramArtifact(const CodeProgram& program, std::vector<std::byte>& bytes,
        std::vector<AssetDependency>& dependencies, std::string& failure)
    {
        try
        {
            CodeProgram candidate = program;
            if (!ValidateProgram(candidate, failure))
            {
                return false;
            }
            std::vector<AssetDependency> edges;
            if (!ProgramEdges(candidate, edges, failure))
            {
                return false;
            }
            Writer writer;
            writer.Integer(kMagic); writer.Integer(kCodeProgramVersion);
            writer.Text(IdText(candidate.programAssetId)); writer.Text(IdText(candidate.shaderMetaAssetId));
            writer.Text(candidate.metadataPath); writer.Text(candidate.rootSourcePath);
            writer.Text(candidate.compilerStamp); writer.Text(candidate.formatStamp); writer.Text(candidate.abiStamp);
            writer.Integer(static_cast<std::uint32_t>(candidate.inputs.size()));
            for (const auto& input : candidate.inputs)
            {
                writer.Text(input.path); writer.Integer(input.byteSize);
                writer.Raw(std::as_bytes(std::span(input.sha256)));
            }
            writer.Blob(candidate.metadataBytes);
            writer.Integer(static_cast<std::uint32_t>(candidate.variants.size()));
            for (const auto& variant : candidate.variants)
            {
                writer.Integer(static_cast<std::uint8_t>(variant.backend)); writer.Integer(variant.passIndex);
                writer.Integer(static_cast<std::uint32_t>(variant.keywordSelections.size()));
                for (auto selection : variant.keywordSelections)
                {
                    writer.Integer(selection);
                }
                writer.Integer(variant.vertexAttributeMask); writer.Integer(static_cast<std::uint8_t>(variant.referencePath));
                writer.Integer(static_cast<std::uint32_t>(variant.permutation.Size()));
                for (const auto& define : variant.permutation.Entries())
                {
                    writer.Text(define.name);
                    writer.Text(define.value);
                }
                writer.Integer(static_cast<std::uint8_t>(variant.stages.size()));
                for (const auto& stage : variant.stages)
                {
                    writer.Integer(static_cast<std::uint8_t>(stage.stage)); writer.Text(stage.entry); writer.Text(stage.profile);
                    std::vector<std::uint8_t> payload;
                    if (!rhi_shader_verified_cache::Encode(stage.bytecode, stage.reflection, payload))
                    {
                        return Fail(failure, "Code program verified stage payload cannot be serialized.");
                    }
                    writer.Blob(std::as_bytes(std::span(payload)));
                }
            }
            bytes = std::move(writer.bytes);
            dependencies = std::move(edges);
            failure.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            return Fail(failure, exception.what());
        }
    }

    bool ReadVerifiedCodeProgram(std::span<const std::byte> bytes, CodeProgram& result,
        std::vector<AssetDependency>& dependencies, std::string& failure)
    {
        try
        {
            if (bytes.empty() || bytes.size() > kCodeProgramMaxBytes)
            {
                return Fail(failure, "Code program exceeds its byte budget.");
            }
            Reader reader{ bytes };
            if (reader.Integer<std::uint32_t>() != kMagic || reader.Integer<std::uint32_t>() != kCodeProgramVersion)
            {
                return Fail(failure, "Code program magic/version is incompatible.");
            }
            CodeProgram candidate;
            candidate.programAssetId = reader.Id(); candidate.shaderMetaAssetId = reader.Id();
            candidate.metadataPath = reader.Text(); candidate.rootSourcePath = reader.Text();
            candidate.compilerStamp = reader.Text(); candidate.formatStamp = reader.Text(); candidate.abiStamp = reader.Text();
            const auto inputCount = reader.Integer<std::uint32_t>();
            if (!inputCount || inputCount > kMaxInputs)
            {
                return Fail(failure, "Code program input inventory count is invalid.");
            }
            for (std::uint32_t i = 0; i < inputCount; ++i)
            {
                CodeProgramInput input;
                input.path = reader.Text(); input.byteSize = reader.Integer<std::uint64_t>();
                const auto digest = reader.Raw(input.sha256.size());
                std::ranges::transform(digest, input.sha256.begin(), [](auto byte) { return std::to_integer<std::uint8_t>(byte); });
                candidate.inputs.push_back(std::move(input));
            }
            const auto metadata = reader.Blob(kShaderMetaDocumentMaxBytes);
            candidate.metadataBytes.assign(metadata.begin(), metadata.end());
            const auto variantCount = reader.Integer<std::uint32_t>();
            if (!variantCount || variantCount > kCodeProgramMaxVariants)
            {
                return Fail(failure, "Code program variant count is invalid.");
            }
            for (std::uint32_t i = 0; i < variantCount; ++i)
            {
                CodeProgramVariant variant;
                variant.backend = static_cast<RHIShaderBinary>(reader.Integer<std::uint8_t>());
                variant.passIndex = reader.Integer<std::uint32_t>();
                const auto selections = reader.Integer<std::uint32_t>();
                if (selections > RHIShaderPermutation::kMaxEntries)
                {
                    return Fail(failure, "Code program keyword axis count is invalid.");
                }
                for (std::uint32_t axis = 0; axis < selections; ++axis)
                {
                    variant.keywordSelections.push_back(reader.Integer<std::uint16_t>());
                }
                variant.vertexAttributeMask = reader.Integer<std::uint32_t>();
                const auto reference = reader.Integer<std::uint8_t>();
                if (reference > 1u)
                {
                    return Fail(failure, "Code program reference flag is invalid.");
                }
                variant.referencePath = reference != 0u;
                const auto defines = reader.Integer<std::uint32_t>();
                if (defines > RHIShaderPermutation::kMaxEntries)
                {
                    return Fail(failure, "Code program define count is invalid.");
                }
                for (std::uint32_t define = 0; define < defines; ++define)
                {
                    const auto name = reader.Text(); const auto value = reader.Text();
                    if (!variant.permutation.Set(name, value, failure))
                    {
                        return false;
                    }
                }
                const auto stages = reader.Integer<std::uint8_t>();
                if (!stages || stages > 2u)
                {
                    return Fail(failure, "Code program stage count is invalid.");
                }
                for (std::uint8_t index = 0; index < stages; ++index)
                {
                    CodeProgramStage stage;
                    stage.stage = static_cast<RHIShaderStage>(reader.Integer<std::uint8_t>());
                    stage.entry = reader.Text(); stage.profile = reader.Text();
                    const auto payload = reader.Blob(rhi_shader_verified_cache::kMaxPayloadBytes);
                    if (!rhi_shader_verified_cache::Decode(
                        {reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()},
                        stage.stage, stage.bytecode, stage.reflection))
                    {
                        return Fail(failure, "Invalid code program verified bytecode/reflection payload.");
                    }
                    variant.stages.push_back(std::move(stage));
                }
                candidate.variants.push_back(std::move(variant));
            }
            if (reader.offset != bytes.size())
            {
                return Fail(failure, "Code program has trailing bytes.");
            }
            if (!ValidateProgram(candidate, failure))
            {
                return false;
            }
            std::vector<AssetDependency> edges;
            if (!ProgramEdges(candidate, edges, failure))
            {
                return false;
            }
            result = std::move(candidate);
            dependencies = std::move(edges);
            failure.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            return Fail(failure, exception.what());
        }
    }

    bool ReadCodeProgramArtifact(std::span<const std::byte> bytes, const TypedAssetReference& expected,
        std::span<const AssetDependency> dependencies, CodeProgram& result, std::string& failure)
    {
        if (expected.kind != CookedAssetKind::MaterialProgram || expected.key.subassetId.IsValid() || !ValidId(expected.key.assetId))
        {
            return Fail(failure, "Code program requires a global MaterialProgram identity.");
        }
        CodeProgram candidate;
        std::vector<AssetDependency> edges;
        if (!ReadVerifiedCodeProgram(bytes, candidate, edges, failure) || !MatchEdges(dependencies, edges, failure))
        {
            return false;
        }
        if (candidate.programAssetId != expected.key.assetId)
        {
            return Fail(failure, "Code program identity differs from its catalog entry.");
        }
        result = std::move(candidate);
        failure.clear();
        return true;
    }

    bool ValidateAuthoredMaterialBinding(const AuthoredMaterialDocument& document,
        const CodeProgram& program, std::string& failure)
    {
        if (document.programAssetId != program.programAssetId || document.material.shaderAssetId != program.shaderMetaAssetId)
        {
            return Fail(failure, "Authored material program/metadata identities do not match the selected code product.");
        }
        std::vector<AssetId> defaults;
        if (!CollectCodeProgramDefaultTextures(program.meta, defaults, failure))
        {
            return false;
        }
        if (defaults != document.defaultTextureAssetIds)
        {
            return Fail(failure, "Authored material default texture closure differs from the selected code product.");
        }
        std::vector<AssetDependency> edges;
        if (!MaterialEdges(document, edges, failure))
        {
            return false;
        }
        std::vector<std::uint16_t> selections;
        if (!NormalizeMaterialKeywordSelections(document.material, program.meta.keywords, selections, failure))
        {
            return false;
        }
        for (const auto& property : document.material.properties)
        {
            const auto found = std::ranges::find(program.meta.properties, property.name, &ShaderPropertyDesc::name);
            if (found == program.meta.properties.end() && property.name == standard_material::property::DoubleSided)
            {
                if (!std::holds_alternative<bool>(property.value))
                {
                    return Fail(failure, "Authored material doubleSided raster policy must be bool.");
                }
                continue;
            }
            ::MaterialPropertyValue converted;
            if (found == program.meta.properties.end() || !TryConvertMaterialProperty(property, *found, converted, failure))
            {
                return Fail(failure, "Authored material property is unknown or incompatible: " + property.name + ": " + failure);
            }
        }
        failure.clear();
        return true;
    }
}
