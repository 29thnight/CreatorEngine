#include "../Utility_Framework/Foundation/EngineOrderedMap.h"
#include "../Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "TemporalMotionFixtureAssets.h"
#if CE_DEVELOPMENT && !CE_SHIPPING

#include "../RenderEngine/Assets/ModelMaterialGraph.h"
#include "../RenderEngine/Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "../RenderEngine/Experiment/Cooked/CookedTexture.h"
#include "../RenderEngine/Experiment/Cooked/MaterialAssetSetCodec.h"
#include "../RenderEngine/Experiment/Import/MeshletBuilder.h"
#include "../RenderEngine/MaterialGraphSceneCompiler.h"

#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    namespace cooked = experiment::cooked;
    using ArtifactFiles = ce::engine_ordered_map<std::string, ce::dynamic_array<std::byte>, std::less<>>;

    class MotionArtifactSource final : public cooked::ArtifactByteSource
    {
    public:
        explicit MotionArtifactSource(ArtifactFiles files) : m_files(std::move(files))
        {
        }

        bool Size(std::string_view path, std::uint64_t& size, std::string& failure) const override
        {
            const auto found = m_files.find(path);
            if (found == m_files.end())
            {
                failure = "Temporal motion fixture artifact is absent: " + std::string(path);
                return false;
            }
            size = found->second.size();
            failure.clear();
            return true;
        }

        bool ReadAt(std::string_view path, std::uint64_t offset,
            std::span<std::byte> bytes, std::string& failure) const override
        {
            const auto found = m_files.find(path);
            if (found == m_files.end() || offset > found->second.size()
                || bytes.size() > found->second.size() - offset)
            {
                failure = "Temporal motion fixture artifact read is out of range: " + std::string(path);
                return false;
            }
            std::copy_n(found->second.begin() + static_cast<std::size_t>(offset), bytes.size(), bytes.begin());
            failure.clear();
            return true;
        }

    private:
        // 공개 뒤 변경할 수 없어 CaptureArtifact의 immutable-source 기본 경로가 유효하다.
        const ArtifactFiles m_files;
    };

    experiment::AssetId FixtureId(std::uint8_t discriminator)
    {
        constexpr ce::fixed_array<std::uint8_t, 16> kNamespace{
            0x54u, 0x4du, 0x46u, 0x34u, 0x35u, 0x2du, 0x66u, 0x69u,
            0x78u, 0x74u, 0x75u, 0x72u, 0x65u, 0x2du, 0x30u, 0x31u };
        const std::string key = std::to_string(discriminator);
        const auto derived = assets::DeriveIdentity({ "temporal-motion-fixture", kNamespace, "artifact", key });
        if (!derived.Succeeded())
        {
            throw std::runtime_error("Temporal motion fixture identity failed: " + derived.context);
        }
        return { derived.uuid };
    }

    template<class T>
    AssetDepot::AssetLink<T> FixtureLink(std::uint8_t discriminator)
    {
        return { { FixtureId(discriminator), {} } };
    }

    cooked::AssetDependency Dependency(const cooked::TypedAssetReference& asset,
        cooked::AssetDependencyKind kind)
    {
        return { asset, kind, cooked::AssetDependencyScope::Internal };
    }

    bool MakeQuad(bool skinned, const cooked::SkeletonArtifact& skeleton,
        cooked::ModelGeometryArtifact& result, std::string& failure)
    {
        const auto attributes = skinned ? experiment::kV2VertexAttributes : experiment::kCoreVertexAttributes;
        if (!result.mesh.vertices.SetLayout(attributes))
        {
            failure = "Temporal motion fixture vertex layout is unsupported.";
            return false;
        }
        const ce::fixed_array<math::vector3, 4> positions{
            math::vector3{ -1.0f, -1.0f, 0.0f }, math::vector3{ 1.0f, -1.0f, 0.0f },
            math::vector3{ 1.0f, 1.0f, 0.0f }, math::vector3{ -1.0f, 1.0f, 0.0f } };
        const ce::fixed_array<math::vector2, 4> coordinates{
            math::vector2{ 0.0f, 1.0f }, math::vector2{ 1.0f, 1.0f },
            math::vector2{ 1.0f, 0.0f }, math::vector2{ 0.0f, 0.0f } };
        for (std::size_t index = 0; index < positions.size(); ++index)
        {
            experiment::Vertex vertex;
            vertex.position = positions[index];
            vertex.normal = { 0.0f, 0.0f, -1.0f };
            vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
            vertex.uv0 = coordinates[index];
            if (skinned)
            {
                vertex.boneIndices[0] = 0u;
                vertex.boneIndices[1] = 1u;
                vertex.boneWeights[0] = index < 2u ? 0.9f : 0.1f;
                vertex.boneWeights[1] = 1.0f - vertex.boneWeights[0];
            }
            if (!result.mesh.vertices.Append(vertex))
            {
                failure = "Temporal motion fixture vertex encoding failed.";
                return false;
            }
        }
        // +Z를 보는 카메라의 앞쪽을 향한다. UV의 아래 방향과 tangent handedness도 일치한다.
        result.mesh.indices = { 0u, 2u, 1u, 0u, 3u, 2u };
        result.mesh.bounds = math::aabb::from_min_max({ -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f });
        if (skinned)
        {
            result.requiredBoneCount = static_cast<std::uint32_t>(skeleton.skeleton.bones.size());
            if (!cooked::ComputeSkinBindingDigest(skeleton.skeleton, result.requiredSkinBindingSha256, failure)
                || !cooked::ValidateModelGeometryBinding(result, skeleton, failure))
            {
                return false;
            }
        }
        // Current CEGE requires topology for every mesh; the sealed fixture stamp
        // independently selects the indexed or mesh shader recording route.
        {
            if (!experiment::importer::BuildMeshlets(result.mesh, result.mesh.meshlets, failure))
            {
                return false;
            }
            if (!result.mesh.meshlets.HasMeshlets())
            {
                failure = "Temporal motion fixture meshlet builder produced no meshlets.";
                return false;
            }
        }
        return true;
    }

    bool MakeTexture(bool masked, ce::dynamic_array<std::byte>& bytes, std::string& failure)
    {
        constexpr std::uint32_t kDimension = 16u;
        constexpr std::uint32_t kMipLevels = 5u;
        auto image = TextureImage::Allocate(RHIFormat::RGBA8UnormSrgb,
            kDimension, kDimension, 1u, kMipLevels);
        if (!image.IsValid())
        {
            failure = "Temporal motion fixture texture allocation failed.";
            return false;
        }
        for (std::uint32_t mip = 0; mip < kMipLevels; ++mip)
        {
            const auto* subresource = image.Find(mip, 0u);
            auto* pixels = subresource ? image.MutablePixelsAt(*subresource) : nullptr;
            if (!pixels)
            {
                failure = "Temporal motion fixture texture mip storage is absent.";
                return false;
            }
            for (std::uint32_t y = 0; y < subresource->height; ++y)
            {
                for (std::uint32_t x = 0; x < subresource->width; ++x)
                {
                    // 흰색 sRGB는 선형에서도 정확히 1이다. 기대색은 graph의 baseColor가 정한다.
                    const bool covered = !masked || x >= subresource->width / 2u;
                    const auto offset = static_cast<std::size_t>(y) * subresource->rowPitch + x * 4u;
                    pixels[offset] = std::byte{ 255u };
                    pixels[offset + 1u] = std::byte{ 255u };
                    pixels[offset + 2u] = std::byte{ 255u };
                    pixels[offset + 3u] = std::byte{ static_cast<std::uint8_t>(covered ? 255u : 0u) };
                }
            }
        }
        return cooked::EncodeCookedTexture(image.View(), bytes, failure, { masked, true });
    }

    bool MakeMaterial(const TemporalMotionFixtureAssetOptions& options,
        const std::filesystem::path& temporaryDirectory, const experiment::AssetId& materialId,
        const experiment::AssetId& programId, const experiment::AssetId& textureId, bool masked, bool receiver,
        cooked::MaterialAssetSetProduct& material, cooked::MaterialAssetSetProduct& program, std::string& failure)
    {
        experiment::Material authoring;
        authoring.assetId = materialId;
        authoring.name = receiver ? "Temporal Gray Receiver" : masked ? "Temporal Masked" : "Temporal Opaque";
        authoring.blendMode = masked ? experiment::MaterialBlendMode::Masked : experiment::MaterialBlendMode::Opaque;
        experiment::TextureReference texture;
        texture.assetId = textureId;
        texture.colorSpace = experiment::TextureColorSpace::Srgb;
        texture.sampler.minMag = RHIFilterMode::Point;
        texture.sampler.mip = RHIFilterMode::Point;
        authoring.properties = {
            { "baseColor", receiver ? math::vector4{ 0.125f, 0.125f, 0.125f, 1.0f }
                : math::vector4{ 0.75f, 0.125f, 0.25f, 1.0f } },
            { "metallic", 0.0f }, { "roughness", 0.7f },
            { "alphaCutoff", 0.5f } };
        if (!receiver)
        {
            authoring.properties.push_back({ "baseColorMap", texture });
        }
        const auto graph = assets::BuildModelMaterialGraph(authoring, failure);
        if (!graph)
        {
            return false;
        }
        ce::dynamic_array<LX::LXMaterialDiagnostic> diagnostics;
        const auto generated = LX::GenerateMaterialSlang(*graph, &diagnostics);
        if (!generated)
        {
            failure = diagnostics.empty() ? "Temporal motion fixture graph generation failed."
                : diagnostics.front().message;
            return false;
        }
        material_graph::VerifiedProduct verified;
        const auto sourcePath = temporaryDirectory
            / (receiver ? "receiver.slang" : masked ? "masked.slang" : "opaque.slang");
        // CEMF3는 양쪽 Scene backend를 요구한다. 현재 renderer backend만 만드는 editor 복구는 쓰지 않는다.
        if (!material_graph::CompileSceneProduct(*generated, options.shaderDirectory, sourcePath,
            {}, verified, failure, FileGuid(programId.value), std::nullopt))
        {
            return false;
        }
        program.asset = { { programId, {} }, cooked::CookedAssetKind::MaterialProgram };
        program.representation = cooked::kMaterialProgramRepresentation;
        program.schemaVersion = cooked::kMaterialProgramArtifactVersion;
        program.extension = ".celx";
        ce::dynamic_array<std::uint8_t> programBytes;
        if (!cooked::CollectMaterialProgramAssetSetDependencies(verified, programId, program.dependencies, failure)
            || !material_graph::WriteCookedProgram(verified, {}, programBytes, failure))
        {
            return false;
        }
        const auto view = std::as_bytes(std::span(programBytes));
        program.artifactBytes.assign(view.begin(), view.end());
        material_graph::InstanceDocument document;
        document.materialId = materialId;
        document.name = authoring.name;
        document.description.graphId = programId;
        document.doubleSided = true;
        document.blendMode = masked ? "masked" : "opaque";
        material.asset = { { materialId, {} }, cooked::CookedAssetKind::Material };
        material.representation = cooked::kMaterialDocumentRepresentation;
        material.schemaVersion = cooked::kMaterialArtifactVersion;
        material.extension = ".asset";
        return cooked::ValidateMaterialAssetSetBinding(document, verified, failure)
            && cooked::EncodeMaterialAssetSetDocument(document, material.artifactBytes,
                material.dependencies, failure);
    }
}

bool TemporalMotionFixtureAssets::Build(const TemporalMotionFixtureAssetOptions& options,
    TemporalMotionFixtureAssets& out, std::string& failure)
{
    if (options.targetPlatform.empty() || options.targetAbi.empty()
        || options.shaderDirectory.empty() || options.cacheDirectory.empty())
    {
        failure = "Temporal motion fixture needs host platform, ABI, shader directory and compiler cache directory.";
        return false;
    }
    const char* stage = "identity";
    try
    {
        TemporalMotionFixtureAssets result;
        result.mountOptions = { options.targetPlatform, options.targetAbi, {} };
        result.model = FixtureLink<assets::ModelAnimationDescriptor>(2u);
        result.staticMesh = FixtureLink<assets::ModelMeshDescriptor>(3u);
        result.skinnedMesh = FixtureLink<assets::ModelMeshDescriptor>(4u);
        result.meshletMesh = FixtureLink<assets::ModelMeshDescriptor>(13u);
        result.skeleton = FixtureLink<assets::ModelSkeletonPayload>(5u);
        result.clip = FixtureLink<assets::ModelAnimationPayload>(6u);
        result.opaqueTexture = FixtureLink<Texture>(7u);
        result.alphaTexture = FixtureLink<Texture>(8u);
        result.opaqueMaterial = FixtureLink<Material>(9u);
        result.alphaMaterial = FixtureLink<Material>(10u);
        result.receiverMaterial = FixtureLink<Material>(14u);
        cooked::AssetSetManifest manifest;
        manifest.assetSetId = FixtureId(1u);
        manifest.revision = 1u;
        manifest.targetPlatform = options.targetPlatform;
        manifest.targetAbi = options.targetAbi;
        ArtifactFiles files;
        const auto add = [&](const cooked::TypedAssetReference& asset, std::string path,
            ce::dynamic_array<std::byte> bytes, std::uint32_t representation, std::uint32_t schema,
            ce::dynamic_array<cooked::AssetDependency> dependencies)
        {
            cooked::AssetBlobRecord blob;
            blob.kind = asset.kind;
            blob.representation = representation;
            blob.schemaVersion = schema;
            blob.targetPlatform = options.targetPlatform;
            blob.targetAbi = options.targetAbi;
            if (asset.kind == cooked::CookedAssetKind::Mesh && representation == cooked::kModelGeometryRepresentation)
            {
                cooked::Sha256Digest metadataDigest{};
                if (!cooked::ModelGeometryStreamMetadataDigest(bytes, metadataDigest, failure))
                {
                    return false;
                }
                const auto separator = path.find_last_of('/');
                path.insert(separator == std::string::npos ? 0u : separator + 1u,
                    cooked::ModelGeometryStreamDirectory(metadataDigest) + "/");
            }
            blob.artifactPath = path;
            blob.byteSize = bytes.size();
            if (!cooked::ComputeSha256(bytes, blob.contentSha256, failure))
            {
                failure = std::string(stage) + ": " + failure;
                return false;
            }
            const auto existing = std::ranges::find_if(manifest.blobs, [&](const cooked::AssetBlobRecord& stored)
            {
                return stored.kind == blob.kind && stored.representation == blob.representation &&
                    stored.schemaVersion == blob.schemaVersion && stored.targetPlatform == blob.targetPlatform &&
                    stored.targetAbi == blob.targetAbi && stored.byteSize == blob.byteSize &&
                    stored.contentSha256 == blob.contentSha256;
            });
            if (existing != manifest.blobs.end())
            {
                manifest.entries.push_back({asset, static_cast<std::uint32_t>(existing - manifest.blobs.begin()),
                    std::move(dependencies)});
                manifest.roots.push_back(asset);
                return true;
            }
            manifest.entries.push_back({ asset, static_cast<std::uint32_t>(manifest.blobs.size()),
                std::move(dependencies) });
            manifest.roots.push_back(asset);
            manifest.blobs.push_back(std::move(blob));
            files.emplace(std::move(path), std::move(bytes));
            return true;
        };
        stage = "skeleton";
        cooked::SkeletonArtifact skeleton;
        skeleton.skeleton.rootBone = experiment::BoneIndex(0u);
        skeleton.skeleton.bones = {
            { "MotionRoot", {}, math::matrix4x4::identity() },
            { "MotionTip", experiment::BoneIndex(0u), math::matrix4x4::identity() } };
        ce::dynamic_array<std::byte> bytes;
        if (!cooked::ComputeBoneLayoutDigest(skeleton.skeleton, skeleton.boneLayoutSha256, failure)
            || !cooked::WriteSkeletonArtifact(skeleton, bytes, failure)
            || !add(result.skeleton.ToReference(), "Derived/TemporalMotionFixture/skeleton.cesl",
                std::move(bytes), cooked::kSkeletonRepresentation, cooked::kSkeletonArtifactVersion, {}))
        {
            failure = std::string(stage) + ": " + failure;
            return false;
        }
        stage = "animation";
        cooked::AnimationClipArtifact clip;
        clip.skeletonAssetId = result.skeleton.identity.assetId;
        clip.requiredBoneLayoutSha256 = skeleton.boneLayoutSha256;
        clip.requiredBoneCount = 2u;
        clip.clip.name = "Temporal Two Bone Translation";
        clip.clip.durationTicks = 2.0;
        clip.clip.ticksPerSecond = 1.0;
        clip.clip.looping = true;
        experiment::AnimationChannel channel;
        channel.bone = experiment::BoneIndex(1u);
        channel.translations = {
            { 0.0, { 0.0f, 0.0f, 0.0f } }, { 0.5, { 0.25f, 0.0f, 0.0f } },
            { 1.0, { 0.0f, 0.0f, 0.0f } }, { 1.5, { -0.25f, 0.0f, 0.0f } },
            { 2.0, { 0.0f, 0.0f, 0.0f } } };
        clip.clip.channels.push_back(std::move(channel));
        const auto hardSkeleton = Dependency(result.skeleton.ToReference(), cooked::AssetDependencyKind::Hard);
        if (!cooked::ValidateAnimationClipBinding(clip, skeleton, failure)
            || !cooked::WriteAnimationClipArtifact(clip, bytes, failure)
            || !add(result.clip.ToReference(), "Derived/TemporalMotionFixture/sway.cean",
                std::move(bytes), cooked::kAnimationClipRepresentation, cooked::kAnimationClipArtifactVersion,
                { hardSkeleton }))
        {
            failure = std::string(stage) + ": " + failure;
            return false;
        }
        stage = "geometry";
        cooked::ModelGeometryArtifact staticGeometry;
        cooked::ModelGeometryArtifact skinnedGeometry;
        cooked::ModelGeometryArtifact meshletGeometry;
        if (!MakeQuad(false, skeleton, staticGeometry, failure)
            || !MakeQuad(true, skeleton, skinnedGeometry, failure)
            || !MakeQuad(false, skeleton, meshletGeometry, failure)
            || !cooked::WriteModelGeometryArtifact(staticGeometry, bytes, failure)
            || !add(result.staticMesh.ToReference(), "Derived/TemporalMotionFixture/static.cege",
                std::move(bytes), cooked::kModelGeometryRepresentation, cooked::kModelGeometryArtifactVersion, {})
            || !cooked::WriteModelGeometryArtifact(skinnedGeometry, bytes, failure)
            || !add(result.skinnedMesh.ToReference(), "Derived/TemporalMotionFixture/skinned.cege",
                std::move(bytes), cooked::kModelGeometryRepresentation, cooked::kModelGeometryArtifactVersion,
                { hardSkeleton })
            || !cooked::WriteModelGeometryArtifact(meshletGeometry, bytes, failure)
            || !add(result.meshletMesh.ToReference(), "Derived/TemporalMotionFixture/meshlet.cege",
                std::move(bytes), cooked::kModelGeometryRepresentation, cooked::kModelGeometryArtifactVersion, {}))
        {
            failure = std::string(stage) + ": " + failure;
            return false;
        }
        stage = "texture";
        if (!MakeTexture(false, bytes, failure)
            || !add(result.opaqueTexture.ToReference(), "Derived/TemporalMotionFixture/opaque.cetex",
                std::move(bytes), cooked::kCookedTextureRepresentationVersion, cooked::kCookedTextureSchemaVersion, {})
            || !MakeTexture(true, bytes, failure)
            || !add(result.alphaTexture.ToReference(), "Derived/TemporalMotionFixture/alpha.cetex",
                std::move(bytes), cooked::kCookedTextureRepresentationVersion, cooked::kCookedTextureSchemaVersion, {}))
        {
            failure = std::string(stage) + ": " + failure;
            return false;
        }
        stage = "material";
        const auto temporaryDirectory = options.cacheDirectory / "TemporalMotionFixture"
            / FileGuid::CreateRandomV4().ToString();
        std::filesystem::create_directories(temporaryDirectory);
        struct CompilerFiles final
        {
            std::filesystem::path path;
            ~CompilerFiles()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } compilerFiles{ temporaryDirectory };
        for (std::uint32_t index = 0; index < 3u; ++index)
        {
            const bool masked = index == 1u;
            const bool receiver = index == 2u;
            cooked::MaterialAssetSetProduct material;
            cooked::MaterialAssetSetProduct program;
            const auto materialId = receiver ? result.receiverMaterial.identity.assetId
                : masked ? result.alphaMaterial.identity.assetId : result.opaqueMaterial.identity.assetId;
            const auto textureId = masked ? result.alphaTexture.identity.assetId
                : result.opaqueTexture.identity.assetId;
            if (!MakeMaterial(options, temporaryDirectory, materialId, FixtureId(receiver ? 15u : masked ? 12u : 11u),
                textureId, masked, receiver, material, program, failure))
            {
                failure = std::string(stage) + ": " + failure;
                return false;
            }
            const std::string stem = receiver ? "receiver" : masked ? "masked" : "opaque";
            if (!add(program.asset, "Derived/TemporalMotionFixture/" + stem + program.extension,
                std::move(program.artifactBytes), program.representation, program.schemaVersion,
                std::move(program.dependencies))
                || !add(material.asset, "Derived/TemporalMotionFixture/" + stem + material.extension,
                    std::move(material.artifactBytes), material.representation, material.schemaVersion,
                    std::move(material.dependencies)))
            {
                failure = std::string(stage) + ": " + failure;
                return false;
            }
        }
        cooked::ModelDescriptorArtifact descriptor;
        descriptor.modelAssetId = result.model.identity.assetId;
        descriptor.skeletonAssetId = result.skeleton.identity.assetId;
        descriptor.name = "Temporal Motion Fixture";
        descriptor.clips = { { result.clip.identity.assetId, clip.clip.name, 2.0, 1.0, true } };
        descriptor.materials = {
            { result.opaqueMaterial.identity.assetId, "Temporal Opaque", experiment::MaterialBlendMode::Opaque },
            { result.alphaMaterial.identity.assetId, "Temporal Masked", experiment::MaterialBlendMode::Masked },
            { result.receiverMaterial.identity.assetId, "Temporal Gray Receiver",
                experiment::MaterialBlendMode::Opaque } };
        descriptor.meshes = {
            { result.staticMesh.identity.assetId, result.opaqueMaterial.identity.assetId, "Temporal Static Quad",
                staticGeometry.mesh.bounds, staticGeometry.mesh.vertices.AttributeMask(),
                staticGeometry.mesh.vertices.Stride(), 4u, 6u, false },
            { result.skinnedMesh.identity.assetId, result.opaqueMaterial.identity.assetId, "Temporal Skinned Quad",
                skinnedGeometry.mesh.bounds, skinnedGeometry.mesh.vertices.AttributeMask(),
                skinnedGeometry.mesh.vertices.Stride(), 4u, 6u, true },
            { result.meshletMesh.identity.assetId, result.opaqueMaterial.identity.assetId, "Temporal Meshlet Quad",
                meshletGeometry.mesh.bounds, meshletGeometry.mesh.vertices.AttributeMask(),
                meshletGeometry.mesh.vertices.Stride(), 4u, 6u, false } };
        descriptor.nodes = { { "TemporalFixtureRoot", {}, math::matrix4x4::identity(),
            { result.staticMesh.identity.assetId, result.skinnedMesh.identity.assetId,
                result.meshletMesh.identity.assetId } } };
        // 작은 표시용 fixture가 물리 cook를 일으키지 않게 명시한다.
        descriptor.createMeshCollider = false;
        const ce::dynamic_array<cooked::AssetDependency> modelDependencies{
            Dependency(result.skeleton.ToReference(), cooked::AssetDependencyKind::Loadable),
            Dependency(result.clip.ToReference(), cooked::AssetDependencyKind::Loadable),
            Dependency(result.staticMesh.ToReference(), cooked::AssetDependencyKind::Loadable),
            Dependency(result.skinnedMesh.ToReference(), cooked::AssetDependencyKind::Loadable),
            Dependency(result.meshletMesh.ToReference(), cooked::AssetDependencyKind::Loadable),
            Dependency(result.opaqueMaterial.ToReference(), cooked::AssetDependencyKind::Loadable),
            Dependency(result.alphaMaterial.ToReference(), cooked::AssetDependencyKind::Loadable),
            Dependency(result.receiverMaterial.ToReference(), cooked::AssetDependencyKind::Loadable) };
        if (!cooked::ValidateModelMeshSummary(descriptor.meshes[0], staticGeometry, failure)
            || !cooked::ValidateModelMeshSummary(descriptor.meshes[1], skinnedGeometry, failure)
            || !cooked::ValidateModelMeshSummary(descriptor.meshes[2], meshletGeometry, failure)
            || !cooked::ValidateModelDescriptorDependencies(descriptor, modelDependencies, failure)
            || !cooked::WriteModelDescriptorArtifact(descriptor, bytes, failure)
            || !add(result.model.ToReference(), "Derived/TemporalMotionFixture/model.cemd",
                std::move(bytes), cooked::kModelDescriptorRepresentation, cooked::kModelDescriptorVersion,
                modelDependencies))
        {
            failure = std::string(stage) + ": " + failure;
            return false;
        }
        stage = "manifest";
        auto encoded = cooked::WriteAssetSetManifest(manifest);
        if (!encoded.Succeeded())
        {
            failure = encoded.issues.empty() ? "Temporal motion fixture manifest encoding failed."
                : encoded.issues.front().context + ": " + encoded.issues.front().message;
            failure = std::string(stage) + ": " + failure;
            return false;
        }
        result.manifest = std::move(encoded.bytes);
        result.artifacts = std::move(manifest.blobs);
        result.source = own::make_shared<MotionArtifactSource>(std::move(files));
        out = std::move(result);
        failure.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        failure = std::string(stage) + ": " + exception.what();
        return false;
    }
}
#else
bool TemporalMotionFixtureAssets::Build(const TemporalMotionFixtureAssetOptions&,
    TemporalMotionFixtureAssets&, std::string& failure)
{
    failure = "Temporal motion fixture assets require a non-shipping development build";
    return false;
}
#endif
