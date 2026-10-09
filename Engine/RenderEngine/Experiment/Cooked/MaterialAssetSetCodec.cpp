#include "MaterialAssetSetCodec.h"

#include "../../Assets/AssetIdentityProfile.h"
#include "../../MaterialGraphSceneCompiler.h"
#include "../../MaterialGraphShaderMeta.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringParsedDocument.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace experiment::cooked
{
    static_assert(kMaterialProgramArtifactVersion == material_graph::CookedProgramVersion);

    namespace material_asset_set_codec_detail
    {
        bool Fail(std::string& failure, std::string message)
        {
            failure = std::move(message);
            return false;
        }

        bool ValidId(const AssetId& id) noexcept
        {
            return IsAssetIdV4(id) || assets::IsUuidV8(id.value);
        }

        bool ValidExpected(const TypedAssetReference& expected, CookedAssetKind kind,
            std::string& failure)
        {
            if (expected.kind != kind || !ValidId(expected.key.assetId) || expected.key.subassetId.IsValid())
            {
                return Fail(failure, "Material artifact requires the expected type and a global UUIDv4/v8 identity.");
            }
            return true;
        }

        bool AddDependency(std::vector<AssetDependency>& dependencies, const AssetId& owner,
            const AssetId& target, CookedAssetKind kind, std::string& failure)
        {
            if (!ValidId(target) || target == owner)
            {
                return Fail(failure, "Material dependency has an invalid or self-referencing identity.");
            }
            const auto found = std::ranges::find_if(dependencies, [&](const AssetDependency& dependency)
            {
                return dependency.target.key.assetId == target;
            });
            if (found != dependencies.end())
            {
                if (found->target.kind != kind)
                {
                    return Fail(failure, "One material dependency identity is used for incompatible asset types.");
                }
                return true;
            }
            dependencies.push_back({ {{ target, {} }, kind}, AssetDependencyKind::Hard,
                AssetDependencyScope::Internal });
            return true;
        }

        bool DocumentDependencies(const material_graph::InstanceDocument& document,
            std::vector<AssetDependency>& out, std::string& failure)
        {
            if (!ValidId(document.materialId))
            {
                return Fail(failure, "Material document requires a nonnil UUIDv4/v8 identity.");
            }
            std::vector<AssetDependency> dependencies;
            if (!AddDependency(dependencies, document.materialId, document.description.graphId,
                CookedAssetKind::MaterialProgram, failure))
            {
                return false;
            }
            for (const auto& texture : document.description.textures)
            {
                if (!AddDependency(dependencies, document.materialId, texture.assetId,
                    CookedAssetKind::Texture, failure))
                {
                    return false;
                }
            }
            std::ranges::sort(dependencies);
            out = std::move(dependencies);
            return true;
        }

        bool MatchDependencies(std::span<const AssetDependency> expected,
            std::span<const AssetDependency> actual, std::string& failure)
        {
            if (expected.size() != actual.size())
            {
                return Fail(failure, "Material artifact dependency count differs from its typed catalog entry.");
            }
            std::vector<TypedAssetReference> targets;
            targets.reserve(expected.size());
            for (const auto& dependency : expected)
            {
                if (dependency.kind != AssetDependencyKind::Hard ||
                    (dependency.scope != AssetDependencyScope::Internal &&
                        dependency.scope != AssetDependencyScope::External) ||
                    dependency.target.key.subassetId.IsValid() || !ValidId(dependency.target.key.assetId))
                {
                    return Fail(failure, "Material artifact requires valid, explicitly scoped Hard dependencies.");
                }
                if (std::ranges::find_if(targets, [&](const TypedAssetReference& target)
                    {
                        return target.key == dependency.target.key;
                    }) != targets.end())
                {
                    return Fail(failure, "Material artifact catalog entry contains duplicate dependency identities.");
                }
                targets.push_back(dependency.target);
            }
            std::ranges::sort(targets);
            for (std::size_t index = 0; index < targets.size(); ++index)
            {
                if (targets[index] != actual[index].target)
                {
                    return Fail(failure, "Material artifact typed dependencies differ from the decoded payload.");
                }
            }
            return true;
        }
    }

    bool EncodeMaterialAssetSetDocument(const material_graph::InstanceDocument& document,
        std::vector<std::byte>& outBytes, std::vector<AssetDependency>& outDependencies, std::string& failure)
    {
        try
        {
            Authoring::WriteDocument staging;
            if (!material_graph::WriteInstanceDocument(document, staging.Root(), failure))
            {
                return false;
            }
            // Run the canonical reader too: writer defaults cannot silently
            // weaken the typed document contract at this shared boundary.
            material_graph::InstanceDocument validated;
            if (!material_graph::ReadInstanceDocument(staging.Root().Read(), validated, failure))
            {
                return false;
            }
            std::vector<AssetDependency> dependencies;
            if (!material_asset_set_codec_detail::DocumentDependencies(validated, dependencies, failure))
            {
                return false;
            }
            std::vector<std::byte> bytes;
            if (!Authoring::EncodeCookedDocument(staging.Root().Read(), bytes, failure))
            {
                return false;
            }
            if (bytes.empty() || bytes.size() > kMaterialDocumentMaxBytes)
            {
                return material_asset_set_codec_detail::Fail(failure, "Material document exceeds its byte budget.");
            }
            outBytes = std::move(bytes);
            outDependencies = std::move(dependencies);
            failure.clear();
            return true;
        }
        catch (const std::exception& exception)
        {
            return material_asset_set_codec_detail::Fail(failure, exception.what());
        }
    }

    bool CollectMaterialProgramAssetSetDependencies(const material_graph::VerifiedProduct& product,
        const AssetId& programAssetId, std::vector<AssetDependency>& outDependencies, std::string& failure)
    {
        failure.clear();
        if (!material_asset_set_codec_detail::ValidId(programAssetId) || !product.materialShader ||
            product.materialShader->meta.guid.m_guid != programAssetId.value)
        {
            return material_asset_set_codec_detail::Fail(failure,
                "MaterialProgram requires a generated binding contract with the expected UUIDv4/v8 identity.");
        }
        if (!material_graph::HasSceneBackend(product, RHIShaderBinary::Dxil) ||
            !material_graph::HasSceneBackend(product, RHIShaderBinary::SpirV))
        {
            return material_asset_set_codec_detail::Fail(failure,
                "MaterialProgram must carry both DXIL and SPIR-V Scene backends.");
        }
        if (product.program.resources.size() > 128u)
        {
            return material_asset_set_codec_detail::Fail(failure, "MaterialProgram resource count exceeds its budget.");
        }
        // This canonical source-free reader checks every carried backend's
        // complete stage/profile set, including feature-specific Scene stages.
        // Its temporary bytecode copies do not escape this validation scope.
        {
            material_graph::SceneShaderSet sceneShaders;
            if (!material_graph::LoadSceneShaders(product, RHIShaderBinary::Dxil, sceneShaders, failure))
            {
                return false;
            }
        }
        std::vector<AssetDependency> dependencies;
        for (const auto& resource : product.program.resources)
        {
            if (resource.kind == LX::LXMaterialResourceKind::Sampler)
            {
                continue;
            }
            if (resource.kind != LX::LXMaterialResourceKind::Texture)
            {
                return material_asset_set_codec_detail::Fail(failure, "MaterialProgram has an unknown resource type.");
            }
            AssetId textureId;
            if ((!TryParseCanonicalAssetId(resource.reference, textureId) &&
                !assets::TryParseCanonicalUuidV8(resource.reference, textureId.value)) ||
                !material_asset_set_codec_detail::AddDependency(dependencies, programAssetId, textureId,
                    CookedAssetKind::Texture, failure))
            {
                if (failure.empty())
                {
                    failure = "MaterialProgram texture reference is not a canonical UUIDv4/v8.";
                }
                return false;
            }
        }
        std::ranges::sort(dependencies);
        outDependencies = std::move(dependencies);
        failure.clear();
        return true;
    }

    bool ValidateMaterialAssetSetBinding(const material_graph::InstanceDocument& document,
        const material_graph::VerifiedProduct& product, std::string& failure)
    {
        std::vector<std::byte> documentBytes;
        std::vector<AssetDependency> dependencies;
        if (!EncodeMaterialAssetSetDocument(document, documentBytes, dependencies, failure))
        {
            return false;
        }
        if (!product.materialShader || product.materialShader->meta.guid.m_guid != document.description.graphId.value)
        {
            return material_asset_set_codec_detail::Fail(failure,
                "Material instance requires the matching generated MaterialProgram binding contract.");
        }
        std::vector<std::uint8_t> uniforms;
        std::vector<LX::LXMaterialDiagnostic> diagnostics;
        if (!material_graph::PrepareUniforms(product.layout, document.description.parameters, uniforms, diagnostics))
        {
            return material_asset_set_codec_detail::Fail(failure,
                diagnostics.empty() ? "Invalid material numeric override." : diagnostics.front().message);
        }
        for (const auto& texture : document.description.textures)
        {
            const auto parameter = std::ranges::find(product.program.parameters, texture.parameter,
                &LX::LXMaterialParameter::id);
            if (parameter == product.program.parameters.end() || !parameter->exposed ||
                parameter->type != LX::PinType::Texture ||
                std::ranges::none_of(product.layout.textures, [&](const auto& resource)
                {
                    return resource.parameter == texture.parameter;
                }))
            {
                return material_asset_set_codec_detail::Fail(failure,
                    "Material texture override requires an exposed, active Texture parameter.");
            }
        }
        failure.clear();
        return true;
    }

    bool ReadMaterialAssetSetDocument(std::span<const std::byte> bytes, const TypedAssetReference& expected,
        std::span<const AssetDependency> dependencies, material_graph::InstanceDocument& out, std::string& failure)
    {
        if (!material_asset_set_codec_detail::ValidExpected(expected, CookedAssetKind::Material, failure))
        {
            return false;
        }
        if (bytes.empty() || bytes.size() > kMaterialDocumentMaxBytes)
        {
            return material_asset_set_codec_detail::Fail(failure, "Material document is empty or oversized.");
        }
        const auto document = Authoring::ParsedDocument::ParseCooked(bytes, failure);
        if (!document)
        {
            return false;
        }
        if (!document.Root().IsMap() || !document.Root()["lattice_material"])
        {
            return material_asset_set_codec_detail::Fail(failure,
                "Unsupported Material document shape: this representation requires a Lattice InstanceDocument.");
        }
        material_graph::InstanceDocument candidate;
        if (!material_graph::ReadInstanceDocument(document.Root(), candidate, failure))
        {
            return false;
        }
        if (candidate.materialId != expected.key.assetId)
        {
            return material_asset_set_codec_detail::Fail(failure, "Material document identity differs from the catalog.");
        }
        std::vector<AssetDependency> actual;
        if (!material_asset_set_codec_detail::DocumentDependencies(candidate, actual, failure) ||
            !material_asset_set_codec_detail::MatchDependencies(dependencies, actual, failure))
        {
            return false;
        }
        out = std::move(candidate);
        failure.clear();
        return true;
    }

    bool ReadMaterialProgramAssetSetArtifact(std::span<const std::byte> bytes, const TypedAssetReference& expected,
        std::span<const AssetDependency> dependencies, const material_graph::Budget& budget,
        material_graph::CookedProgram& out, std::string& failure)
    {
        if (!material_asset_set_codec_detail::ValidExpected(expected, CookedAssetKind::MaterialProgram, failure))
        {
            return false;
        }
        if (bytes.empty() || bytes.size() > kMaterialProgramAssetSetMaxBytes)
        {
            return material_asset_set_codec_detail::Fail(failure, "MaterialProgram artifact is empty or oversized.");
        }
        material_graph::CookedProgram candidate;
        if (!material_graph::ReadCookedProgram(
            { reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size() }, budget, candidate, failure))
        {
            return false;
        }
        std::vector<AssetDependency> actual;
        if (!CollectMaterialProgramAssetSetDependencies(candidate.product, expected.key.assetId, actual, failure) ||
            !material_asset_set_codec_detail::MatchDependencies(dependencies, actual, failure))
        {
            return false;
        }
        out = std::move(candidate);
        failure.clear();
        return true;
    }
}
