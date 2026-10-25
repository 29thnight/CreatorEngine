#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Uuid.h"
#else
import Creator.Core.Uuid;
#endif
#include "ModelCpuGeometry.h"
#include "ModelStreamDescriptor.h"
#include "../Experiment/Cooked/CookedModelCodec.h"
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets::generation_streaming
{
namespace ck = experiment::cooked;
struct File { std::string path; std::vector<std::byte> bytes; };
struct Bundle { std::vector<File> files; std::string metadataFingerprint, manifestFingerprint; };
inline std::string Fingerprint(std::span<const std::byte> bytes)
{
    ck::Sha256Digest digest{}; std::string failure;
    if (!ck::ComputeSha256(bytes,digest,failure)) throw std::runtime_error(failure);
    const auto path = cpu_geometry::Path(digest);
    return "sha256:" + path.substr(path.find_last_of('/') + 1,64);
}
inline Bundle Build(const experiment::ModelDraft& draft, std::span<const uuid::uuid16> meshIds,
                     std::span<const uuid::uuid16> materialIds, uuid::uuid16 skeletonId, uint64_t generation,
                     bool colliderDefault)
{
    Bundle result;
    if (meshIds.size() != draft.meshes.size() || materialIds.size() != draft.materials.size())
        throw std::runtime_error("Authoring stream identity closure");
    ck::AssetSetManifest manifest; manifest.assetSetId = draft.metadata.assetId; manifest.revision = generation;
    manifest.targetPlatform = "win-x64"; manifest.targetAbi = "model-stream-v1";
    const auto add = [&](ck::TypedAssetReference reference, uint32_t representation, uint32_t schema,
                         std::string path, std::vector<std::byte> bytes, std::vector<ck::AssetDependency> dependencies) {
        ck::AssetBlobRecord blob; std::string failure;
        if (!ck::ComputeSha256(bytes,blob.contentSha256,failure)) throw std::runtime_error(failure);
        blob.byteSize = bytes.size(); blob.kind = reference.kind; blob.representation = representation; blob.schemaVersion = schema;
        blob.targetPlatform = manifest.targetPlatform; blob.targetAbi = manifest.targetAbi; blob.artifactPath = path;
        const auto found = std::ranges::find(manifest.blobs,path,&ck::AssetBlobRecord::artifactPath);
        const auto index = found == manifest.blobs.end() ? static_cast<uint32_t>(manifest.blobs.size()) : static_cast<uint32_t>(found-manifest.blobs.begin());
        if (found == manifest.blobs.end()) manifest.blobs.push_back(blob);
        else if (*found != blob) throw std::runtime_error("Authoring stream blob locator collision");
        if (reference.key.assetId.IsValid()) manifest.entries.push_back({reference,index,std::move(dependencies)});
        if (std::ranges::none_of(result.files,[&](const auto& file){return file.path == path;}))
            result.files.push_back({std::move(path),std::move(bytes)});
    };
    const auto reference = [](uuid::uuid16 id, ck::CookedAssetKind kind) { return ck::TypedAssetReference{{experiment::AssetId{id},{}},kind}; };
    const auto edge = [&](uuid::uuid16 id, ck::CookedAssetKind kind, ck::AssetDependencyKind dependency) {
        return ck::AssetDependency{reference(id,kind),dependency,ck::AssetDependencyScope::Internal}; };
    ck::SkeletonArtifact skeleton; std::string failure;
    if (draft.skeleton)
    {
        skeleton.skeleton = *draft.skeleton; skeleton.skeleton.clips.clear();
        if (skeletonId.is_nil() || !ck::ComputeBoneLayoutDigest(skeleton.skeleton,skeleton.boneLayoutSha256,failure))
            throw std::runtime_error("Authoring stream skeleton: " + failure);
        std::vector<std::byte> bytes;
        if (!ck::WriteSkeletonArtifact(skeleton,bytes,failure)) throw std::runtime_error(failure);
        add(reference(skeletonId,ck::CookedAssetKind::Skeleton),ck::kSkeletonRepresentation,ck::kSkeletonArtifactVersion,
            "Derived/Skeleton.cesl",std::move(bytes),{});
    }
    ck::ModelDescriptorArtifact summary; summary.modelAssetId = draft.metadata.assetId;
    summary.name = draft.metadata.name; summary.skeletonAssetId = experiment::AssetId{skeletonId}; summary.createMeshCollider = colliderDefault;
    std::vector<ck::AssetDependency> modelDependencies;
    if (draft.skeleton) modelDependencies.push_back(edge(skeletonId,ck::CookedAssetKind::Skeleton,ck::AssetDependencyKind::Loadable));
    for (size_t index = 0; index < draft.meshes.size(); ++index)
    {
        const auto& mesh = draft.meshes[index]; ck::ModelGeometryArtifact geometry; geometry.mesh = mesh;
        std::vector<ck::AssetDependency> dependencies;
        if (Has(mesh.vertices.AttributeMask(),VertexAttribute::BoneIndices))
        {
            geometry.requiredBoneCount = static_cast<uint32_t>(skeleton.skeleton.bones.size());
            if (!ck::ComputeSkinBindingDigest(skeleton.skeleton,geometry.requiredSkinBindingSha256,failure)) throw std::runtime_error(failure);
            dependencies.push_back(edge(skeletonId,ck::CookedAssetKind::Skeleton,ck::AssetDependencyKind::Hard));
        }
        std::vector<std::byte> bytes; ck::Sha256Digest metadataDigest{};
        if (!ck::WriteModelGeometryArtifact(geometry,bytes,failure) || !ck::ModelGeometryStreamMetadataDigest(bytes,metadataDigest,failure))
            throw std::runtime_error(failure);
        const auto path = "Derived/Geometry/" + ck::ModelGeometryStreamDirectory(metadataDigest) + "/" + uuid::to_string(meshIds[index]) + ".cege";
        add(reference(meshIds[index],ck::CookedAssetKind::Mesh),ck::kModelGeometryRepresentation,ck::kModelGeometryArtifactVersion,
            path,std::move(bytes),std::move(dependencies));
        auto cpu = cpu_geometry::Encode(mesh.vertices.AttributeMask(),static_cast<uint32_t>(mesh.vertices.size()),mesh.vertices.Bytes(),mesh.indices);
        ck::Sha256Digest cpuDigest{}; if (!ck::ComputeSha256(cpu,cpuDigest,failure)) throw std::runtime_error(failure);
        add(reference({},ck::CookedAssetKind::CollisionGeometry),2,1,cpu_geometry::Path(cpuDigest),std::move(cpu),{});
        summary.meshes.push_back({experiment::AssetId{meshIds[index]},
            mesh.material.IsValid() ? experiment::AssetId{materialIds[mesh.material.Value()]} : experiment::AssetId{},
            mesh.name,mesh.bounds,mesh.vertices.AttributeMask(),mesh.vertices.Stride(),static_cast<uint32_t>(mesh.vertices.size()),
            static_cast<uint32_t>(mesh.indices.size()),Has(mesh.vertices.AttributeMask(),VertexAttribute::BoneIndices)});
        modelDependencies.push_back(edge(meshIds[index],ck::CookedAssetKind::Mesh,ck::AssetDependencyKind::Loadable));
    }
    for (size_t index = 0; index < draft.materials.size(); ++index)
        summary.materials.push_back({experiment::AssetId{materialIds[index]},draft.materials[index].name,draft.materials[index].blendMode});
    for (const auto& node : draft.nodes)
    {
        ck::ModelNodeSummary value; value.name = node.name; value.parent = node.parent; value.localTransform = node.localTransform;
        for (auto index : node.meshes) value.meshAssetIds.push_back(experiment::AssetId{meshIds[index.Value()]});
        summary.nodes.push_back(std::move(value));
    }
    std::vector<std::byte> summaryBytes;
    if (!ck::WriteModelDescriptorArtifact(summary,summaryBytes,failure)) throw std::runtime_error(failure);
    add(reference(draft.metadata.assetId.value,ck::CookedAssetKind::Model),ck::kModelDescriptorRepresentation,ck::kModelDescriptorVersion,
        "Derived/model.cemd",std::move(summaryBytes),std::move(modelDependencies));
    manifest.roots.push_back(reference(draft.metadata.assetId.value,ck::CookedAssetKind::Model));
    const auto encoded = ck::WriteAssetSetManifest(manifest);
    if (!encoded.Succeeded()) { for (const auto& issue : encoded.issues) failure += issue.context + ": " + issue.message + "; "; throw std::runtime_error(failure); }
    result.manifestFingerprint = Fingerprint(encoded.bytes); result.files.push_back({"streaming.cemf",encoded.bytes});
    auto metadata = draft; metadata.meshes.clear(); for (auto& node : metadata.nodes) node.meshes.clear();
    const auto envelope = ck::Write(metadata);
    if (!envelope.Succeeded()) { for (const auto& issue : envelope.issues) failure += issue.context + ": " + issue.message + "; "; throw std::runtime_error("Metadata envelope: " + failure); }
    result.metadataFingerprint = Fingerprint(envelope.bytes); result.files.push_back({"metadata.cemc",envelope.bytes});
    return result;
}
struct Loaded
{
    ck::ModelDescriptorArtifact summary;
    std::vector<own::shared_owner<const ModelMeshDescriptor>> meshes;
};
inline std::vector<std::byte> Read(const std::filesystem::path& path, uint64_t maximum)
{
    std::ifstream file(path,std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < 0 || uint64_t(size) > maximum) throw std::runtime_error("Generation stream file extent: " + path.string());
    std::vector<std::byte> bytes(static_cast<size_t>(size)); file.seekg(0);
    if (!bytes.empty() && !file.read(reinterpret_cast<char*>(bytes.data()),bytes.size())) throw std::runtime_error("Generation stream short read");
    return bytes;
}
inline Loaded Load(const std::filesystem::path& root, std::string_view manifestFingerprint, uuid::uuid16 modelId, uint64_t generation)
{
    const auto bytes = Read(root / "streaming.cemf",16ull << 20);
    if (Fingerprint(bytes) != manifestFingerprint) throw std::runtime_error("Generation streaming manifest SHA-256");
    ck::AssetSetManifest manifest; std::vector<ck::AssetManifestIssue> issues;
    if (!ck::ReadAssetSetManifest(bytes,manifest,issues) || manifest.assetSetId.value != modelId || manifest.revision != generation)
        throw std::runtime_error("Generation streaming manifest identity");
    auto source = own::make_shared<ck::LooseArtifactByteSource>(root);
    ck::CookedAssetCatalog catalog, candidate;
    if (!catalog.WithMountedAssetSet(manifest,source,AssetDepot::AssetMountId{generation},generation,
        {.expectedTargetPlatform="win-x64",.expectedTargetAbi="model-stream-v1"},candidate,issues))
        throw std::runtime_error("Generation streaming manifest closure");
    const auto resolve = [&](uuid::uuid16 id, ck::CookedAssetKind kind) {
        ck::ResolvedAssetEntry entry;
        if (candidate.Find({{experiment::AssetId{id},{}},kind},entry) != ck::AssetLookupStatus::Found)
            throw std::runtime_error("Generation streaming child identity"); return entry;
    };
    const auto model = resolve(modelId,ck::CookedAssetKind::Model);
    auto summaryBytes = Read(root / model.blob.artifactPath,16ull << 20); ck::Sha256Digest hash{}; std::string failure;
    if (summaryBytes.size() != model.blob.byteSize || !ck::ComputeSha256(summaryBytes,hash,failure) || hash != model.blob.contentSha256)
        throw std::runtime_error("Generation streaming summary SHA-256");
    Loaded result;
    if (!ck::ReadModelDescriptorArtifact(summaryBytes,result.summary,failure) || result.summary.modelAssetId.value != modelId)
        throw std::runtime_error("Generation streaming summary: " + failure);
    own::shared_owner<const ModelSkeletonPayload> skeleton;
    if (result.summary.skeletonAssetId.IsValid())
    {
        skeleton = LoadModelStreamSkeleton(resolve(result.summary.skeletonAssetId.value,ck::CookedAssetKind::Skeleton),failure);
        if (!skeleton) throw std::runtime_error(failure);
    }
    for (const auto& summary : result.summary.meshes)
    {
        auto mesh = LoadModelStreamDescriptor(resolve(summary.meshAssetId.value,ck::CookedAssetKind::Mesh),summary.skinned ? skeleton : nullptr,failure);
        if (!mesh || !mesh->Matches(summary)) throw std::runtime_error("Generation streaming mesh: " + failure);
        result.meshes.push_back(std::move(mesh));
    }
    return result;
}
}
