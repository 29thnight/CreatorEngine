#include "ModelAssetSetProducer.h"
#include "CookSupport.h"
#include "MaterialAssetSetCodec.h"
#include "TextureCookProducer.h"
#include "../../Assets/ModelSourcePreparation.h"
#include "../../Assets/ModelMaterialGraph.h"
#include "../Import/SceneToModelDraft.h"
#include "../Import/MeshletBuilder.h"
#include "../Import/MeshLodBuilder.h"

#include <algorithm>
#include <map>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace experiment::cooked
{
    namespace
    {
        constexpr std::uint64_t kMaxInputBytes = 512ull * 1024ull * 1024ull;
        constexpr std::uint64_t kMaxTotalInputBytes = 1024ull * 1024ull * 1024ull;
        constexpr std::size_t kMaxSourceFiles = 4096u;
        namespace im = experiment::importer;

        void RequireSource(bool condition, const std::string& message)
        {
            if (!condition)
            {
                throw std::runtime_error(message);
            }
        }

        std::filesystem::path SourceCanonical(const std::filesystem::path& path)
        {
            RequireSource(!path.empty(), "Empty source input path");
            for (auto probe = std::filesystem::absolute(path); !probe.empty(); probe = probe.parent_path())
            {
                const auto attributes = GetFileAttributesW(probe.c_str());
                RequireSource(attributes == INVALID_FILE_ATTRIBUTES ||
                    (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0, "Source crosses a reparse point: " + path.string());
                if (probe == probe.parent_path())
                {
                    break;
                }
            }
            return std::filesystem::weakly_canonical(path);
        }

        std::vector<std::byte> SourceRead(const std::filesystem::path& path)
        {
            RequireSource(std::filesystem::is_regular_file(path), "Missing model source input: " + path.string());
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            RequireSource(static_cast<bool>(stream), "Cannot open model source input: " + path.string());
            const auto size = stream.tellg();
            RequireSource(size >= 0 && static_cast<std::uint64_t>(size) <= kMaxInputBytes,
                "Oversized model source input: " + path.string());
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            RequireSource(static_cast<bool>(stream) && stream.peek() == std::char_traits<char>::eof(),
                "Model source changed size or could not be read: " + path.string());
            return bytes;
        }

        std::string SourceText(const im::ImportSourceBytes& bytes)
        {
            return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        }

        struct SourceSnapshot final
        {
            std::filesystem::path root{};
            std::map<std::filesystem::path, im::ImportSourceBytes> files{};
            std::uint64_t totalBytes{};

            im::ImportSourceBytes Capture(const std::filesystem::path& path, bool external = true)
            {
                const auto canonical = SourceCanonical(path);
                RequireSource(!external || IsContainedPath(root, canonical),
                    "Model import dependency escapes Assets: " + canonical.string());
                if (const auto found = files.find(canonical); found != files.end())
                {
                    return found->second;
                }
                RequireSource(files.size() < kMaxSourceFiles, "Too many external model source inputs");
                auto bytes = SourceRead(canonical);
                RequireSource(bytes.size() <= kMaxTotalInputBytes - totalBytes, "Total model source byte budget exceeded");
                totalBytes += bytes.size();
                im::ImportSourceBytes owned = own::make_shared<std::vector<std::byte>>(std::move(bytes));
                files.emplace(canonical, owned);
                return owned;
            }

            Sha256Digest VerifyAndDigest() const
            {
                std::vector<std::byte> inventory;
                const auto appendSize = [&](std::uint64_t size)
                {
                    for (std::size_t index = 0; index < 8u; ++index)
                    {
                        inventory.push_back(static_cast<std::byte>((size >> (index * 8u)) & 0xffu));
                    }
                };
                for (const auto& [path, snapshot] : files)
                {
                    RequireSource(SourceCanonical(path) == path && SourceRead(path) == *snapshot,
                        "Model source input changed during import: " + path.string());
                    // Relative to the project so relocation does not poison build keys.
                    const auto relative = path.lexically_relative(root.parent_path()).generic_u8string();
                    appendSize(relative.size());
                    for (const auto ch : relative)
                    {
                        inventory.push_back(static_cast<std::byte>(ch));
                    }
                    appendSize(snapshot->size());
                    Sha256Digest digest{};
                    std::string failure;
                    RequireSource(ComputeSha256(*snapshot, digest, failure), "Cannot hash model input: " + failure);
                    for (const auto byte : digest)
                    {
                        inventory.push_back(static_cast<std::byte>(byte));
                    }
                }
                Sha256Digest digest{};
                std::string failure;
                RequireSource(ComputeSha256(inventory, digest, failure), "Cannot hash model input inventory: " + failure);
                return digest;
            }
        };

        TypedAssetReference SourceReference(AssetId id, CookedAssetKind kind)
        {
            return { { id, {} }, kind };
        }

        AssetDependency SourceEdge(AssetId id, CookedAssetKind kind, AssetDependencyKind relation)
        {
            return { SourceReference(id, kind), relation, AssetDependencyScope::Internal };
        }
    }

    ModelAssetSetCookResult BuildModelAssetSetProducts(const ModelAssetSetCookRequest& request)
    {
        ModelAssetSetCookResult result;
        try
        {
            RequireSource(!request.selected.empty() && request.selected.size() <= 65536u,
                "Model source cook requires a bounded, explicit nonempty selection");
            SourceSnapshot snapshot;
            snapshot.root = SourceCanonical(request.assetRoot);
            const auto source = SourceCanonical(request.sourcePath);
            const auto input = snapshot.Capture(source);
            RequireSource(!input->empty(), "Model source is empty");
            auto metaPath = source;
            metaPath += ".meta";
            RequireSource(std::filesystem::is_regular_file(metaPath),
                "Missing canonical model .meta; run model authoring reconciliation before BuildAssetSet: " + metaPath.string());
            RequireSource(std::filesystem::is_regular_file(request.identityHeaderPath),
                "Missing project identity epoch header; author/reconcile model identities before BuildAssetSet: " +
                request.identityHeaderPath.string());
            const auto meta = snapshot.Capture(metaPath);
            const auto epoch = snapshot.Capture(request.identityHeaderPath, false);
            RequireSource(meta->size() <= 16u * 1024u * 1024u && epoch->size() <= 1024u * 1024u,
                "Model sidecar or identity header exceeds its authoring byte budget");
            assets::IdentityEpochHeader header;
            std::vector<assets::EpochHeaderIssue> headerIssues;
            RequireSource(assets::ReadIdentityEpochHeader(SourceText(epoch), header, headerIssues),
                "Invalid authored identity epoch header; reconcile project identities before BuildAssetSet");
            assets::ModelSidecarV2 sidecar;
            std::vector<assets::SidecarIssue> sidecarIssues;
            RequireSource(assets::ReadModelSidecarV2(SourceText(meta), sidecar, sidecarIssues) &&
                assets::ValidateModelSidecarV2Closure(sidecar, header, sidecarIssues),
                "Missing/invalid canonical model sidecar; run model authoring reconciliation before BuildAssetSet");

            std::string failure;
            assets::ModelGeometryImportSettings geometrySettings;
            RequireSource(assets::ReadModelGeometryImportSettings(SourceText(meta), geometrySettings, failure), failure);
            RequireSource(geometrySettings.buildMeshlets || geometrySettings.lodLevels == 0u,
                "Persisted coarse LODs require buildMeshlets=true; reconcile authoring settings");
            auto importer = assets::CreateModelSourceImporter(source);
            RequireSource(importer != nullptr, "Unsupported model source extension: " + source.extension().string());
            im::ImportRequest importRequest;
            importRequest.sourcePath = source;
            importRequest.sourceBytes = input;
            importRequest.options.buildMeshlets = geometrySettings.buildMeshlets;
            importRequest.options.lodLevels = geometrySettings.lodLevels;
            importRequest.readSourceDependency = [&](const std::filesystem::path& path,
                im::ImportSourceBytes& out, std::string& failure)
            {
                try
                {
                    out = snapshot.Capture(path);
                    return true;
                }
                catch (const std::exception& error)
                {
                    failure = error.what();
                    return false;
                }
            };
            auto imported = importer->Import(importRequest);
            RequireSource(imported.Succeeded(), imported.notes.empty() ? "Model source import failed" : imported.notes.front().message);
            std::vector<assets::StableKeyAssignment> assignments;
            RequireSource(assets::ReconcileAuthoredModelBindings(*imported.scene, sidecar, assignments, failure), failure);
            std::map<AssetId, std::size_t> clipIndices;
            std::map<AssetId, std::size_t> meshIndices;
            std::map<AssetId, std::size_t> materialIndices;
            std::map<AssetId, std::size_t> textureIndices;
            std::vector<AssetId> meshIds(imported.scene->meshes.size());
            std::vector<AssetId> materialIds(imported.scene->materials.size());
            AssetId skeletonId{};
            for (const auto& binding : assignments)
            {
                const auto record = std::ranges::find_if(sidecar.subAssets, [&](const auto& value)
                {
                    return value.kind == binding.kind && value.stableKey == binding.stableKey;
                });
                RequireSource(record != sidecar.subAssets.end(), "Missing stable model identity binding");
                if (binding.kind == assets::SubAssetKind::Animation)
                {
                    clipIndices.emplace(AssetId{ record->assetId }, binding.index);
                }
                else if (binding.kind == assets::SubAssetKind::Mesh)
                {
                    const AssetId id{ record->assetId };
                    RequireSource(binding.index < meshIds.size() && !meshIds[binding.index].IsValid(),
                        "Invalid authored mesh binding");
                    meshIds[binding.index] = id;
                    meshIndices.emplace(id, binding.index);
                }
                else if (binding.kind == assets::SubAssetKind::Material)
                {
                    RequireSource(binding.index < materialIds.size() && !materialIds[binding.index].IsValid(),
                        "Invalid authored material binding");
                    materialIds[binding.index] = AssetId{ record->assetId };
                    materialIndices.emplace(materialIds[binding.index], binding.index);
                }
                else if (binding.kind == assets::SubAssetKind::Texture)
                {
                    RequireSource(binding.index < imported.scene->textures.size() &&
                        imported.scene->textures[binding.index].IsEmbedded(),
                        "Authored model texture binding must select encoded embedded image bytes");
                    RequireSource(textureIndices.emplace(AssetId{ record->assetId }, binding.index).second,
                        "Duplicate authored embedded texture identity");
                }
                else if (binding.kind == assets::SubAssetKind::Skeleton)
                {
                    RequireSource(!skeletonId.IsValid(), "Granular model cooking requires the authored single-skeleton conversion policy");
                    skeletonId = AssetId{ record->assetId };
                }
            }

            std::vector<std::size_t> selectedClips;
            std::set<TypedAssetReference> distinct;
            const AssetId modelId{ sidecar.assetId };
            for (const auto& selection : request.selected)
            {
                RequireSource(!selection.key.subassetId.IsValid() && distinct.insert(selection).second,
                    "Use the authored UUIDv8 as assetId; duplicate or nested subasset selection is invalid");
                switch (selection.kind)
                {
                case CookedAssetKind::Model:
                    RequireSource(selection.key.assetId == modelId, "Model source selection differs from canonical sidecar UUIDv8");
                    break;
                case CookedAssetKind::Mesh:
                    RequireSource(meshIndices.contains(selection.key.assetId),
                        "Selected mesh UUIDv8 is absent from this authored model");
                    break;
                case CookedAssetKind::Material:
                    RequireSource(materialIndices.contains(selection.key.assetId),
                        "Selected Material UUIDv8 is absent from this authored model");
                    break;
                case CookedAssetKind::Texture:
                    RequireSource(textureIndices.contains(selection.key.assetId),
                        "Selected embedded Texture UUIDv8 is absent from this authored model");
                    break;
                case CookedAssetKind::Skeleton:
                    RequireSource(skeletonId.IsValid() && selection.key.assetId == skeletonId,
                        "Selected skeleton UUIDv8 is absent from this authored model");
                    break;
                case CookedAssetKind::AnimationClip:
                {
                    const auto found = clipIndices.find(selection.key.assetId);
                    RequireSource(found != clipIndices.end(), "Selected clip UUIDv8 is absent from this authored model");
                    selectedClips.push_back(found->second);
                    break;
                }
                default:
                    RequireSource(false, "Unsupported granular model source selection kind");
                }
            }
            std::ranges::sort(selectedClips);
            im::ConversionOptions conversion;
            conversion.modelAssetId = modelId;
            conversion.modelName = source.stem().string();
            SkeletonArtifact skeleton;
            std::vector<AnimationClip> clips;
            const auto prepareSkeleton = [&]
            {
                if (!skeleton.skeleton.bones.empty())
                {
                    return;
                }
                RequireSource(skeletonId.IsValid(), "Selected skinned geometry/animation requires an authored skeleton");
                auto converted = im::ConvertToSkeleton(*imported.scene, conversion, selectedClips);
                RequireSource(converted.skeleton.has_value(), converted.notes.empty()
                    ? "Source skeleton conversion failed" : converted.notes.back().message);
                skeleton.skeleton = std::move(*converted.skeleton);
                RequireSource(ComputeBoneLayoutDigest(skeleton.skeleton, skeleton.boneLayoutSha256, failure), failure);
                clips = std::move(skeleton.skeleton.clips);
                skeleton.skeleton.clips.clear();
            };
            RequireSource(skeletonId.IsValid() || clipIndices.empty(), "Authored clips require an authored skeleton");
            if (!selectedClips.empty() || std::ranges::any_of(request.selected, [](const auto& selection)
                { return selection.kind == CookedAssetKind::Skeleton; }))
            {
                prepareSkeleton();
            }

            for (const auto& selection : request.selected)
            {
                ModelAssetSetProduct product;
                product.asset = selection;
                if (selection.kind == CookedAssetKind::Texture)
                {
                    auto& texture = imported.scene->textures.at(textureIndices.at(selection.key.assetId));
                    const auto extension = SniffTextureExtension(texture.embeddedBytes);
                    RequireSource(!extension.empty() && IsSupportedTextureExtension(extension),
                        "Selected embedded texture has an unsupported encoded image format");
                    product.representation = 1u;
                    product.schemaVersion = kTextureArtifactVersion;
                    product.extension = extension;
                    product.artifactBytes = std::move(texture.embeddedBytes);
                }
                else if (selection.kind == CookedAssetKind::Material)
                {
                    const auto index = materialIndices.at(selection.key.assetId);
                    const auto& material = imported.scene->materials.at(index);
                    material_graph::InstanceDocument document;
                    document.materialId = selection.key.assetId;
                    document.name = material.name.empty() ? "material_" + std::to_string(index) : material.name;
                    document.doubleSided = material.doubleSided;
                    document.blendMode = material.alphaMode == im::AlphaMode::Blend ? "transparent" :
                        material.alphaMode == im::AlphaMode::Mask ? "masked" : "opaque";
                    // The existing authored graph is a separate source recipe.
                    // Never regenerate it from imported PBR defaults or repair a
                    // missing graph by silently overwriting the user's edits.
                    document.description.graphId = AssetId{ assets::ModelMaterialGraphId(selection.key.assetId.value) };
                    product.representation = kMaterialDocumentRepresentation;
                    product.schemaVersion = kMaterialArtifactVersion;
                    product.extension = ".asset";
                    RequireSource(EncodeMaterialAssetSetDocument(document, product.artifactBytes,
                        product.dependencies, failure), failure);
                }
                else if (selection.kind == CookedAssetKind::Skeleton)
                {
                    product.representation = kSkeletonRepresentation;
                    product.schemaVersion = kSkeletonArtifactVersion;
                    product.extension = ".cesl";
                    RequireSource(WriteSkeletonArtifact(skeleton, product.artifactBytes, failure), failure);
                }
                else if (selection.kind == CookedAssetKind::Mesh)
                {
                    auto converted = im::ConvertToMesh(*imported.scene, meshIndices.at(selection.key.assetId));
                    RequireSource(converted.mesh.has_value(), converted.notes.empty()
                        ? "Selected source mesh conversion failed" : converted.notes.back().message);
                    ModelGeometryArtifact geometry;
                    geometry.mesh = std::move(*converted.mesh);
                    for (const auto& note : converted.notes)
                    {
                        result.warnings.push_back(note.message);
                    }
                    if (Has(geometry.mesh.vertices.AttributeMask(), VertexAttribute::BoneIndices))
                    {
                        prepareSkeleton();
                        geometry.requiredBoneCount = static_cast<std::uint32_t>(skeleton.skeleton.bones.size());
                        RequireSource(ComputeSkinBindingDigest(skeleton.skeleton,
                            geometry.requiredSkinBindingSha256, failure), failure);
                        RequireSource(ValidateModelGeometryBinding(geometry, skeleton, failure), failure);
                        product.dependencies.push_back(SourceEdge(skeletonId, CookedAssetKind::Skeleton, AssetDependencyKind::Hard));
                    }
                    if (geometrySettings.buildMeshlets)
                    {
                        RequireSource(im::BuildMeshlets(geometry.mesh, geometry.mesh.meshlets, failure), failure);
                    }
                    MeshLodBuildSettings lodSettings;
                    lodSettings.levelCount = geometrySettings.lodLevels;
                    std::string lodDiagnostic;
                    RequireSource(im::BuildMeshLods(geometry.mesh, geometry.mesh.coarseLods, lodDiagnostic, lodSettings),
                        lodDiagnostic);
                    if (geometrySettings.lodLevels != 0u && !lodDiagnostic.empty())
                    {
                        result.warnings.push_back(geometry.mesh.name + ": " + lodDiagnostic);
                    }
                    product.representation = kModelGeometryRepresentation;
                    product.schemaVersion = kModelGeometryArtifactVersion;
                    product.extension = ".cege";
                    RequireSource(WriteModelGeometryArtifact(geometry, product.artifactBytes, failure), failure);
                }
                else if (selection.kind == CookedAssetKind::AnimationClip)
                {
                    const auto index = clipIndices.at(selection.key.assetId);
                    const auto position = std::ranges::lower_bound(selectedClips, index) - selectedClips.begin();
                    AnimationClipArtifact clip;
                    clip.skeletonAssetId = skeletonId;
                    clip.requiredBoneLayoutSha256 = skeleton.boneLayoutSha256;
                    clip.requiredBoneCount = static_cast<std::uint32_t>(skeleton.skeleton.bones.size());
                    clip.clip = std::move(clips.at(static_cast<std::size_t>(position)));
                    product.representation = kAnimationClipRepresentation;
                    product.schemaVersion = kAnimationClipArtifactVersion;
                    product.extension = ".cean";
                    product.dependencies.push_back(SourceEdge(skeletonId, CookedAssetKind::Skeleton, AssetDependencyKind::Hard));
                    RequireSource(ValidateAnimationClipBinding(clip, skeleton, failure) &&
                        WriteAnimationClipArtifact(clip, product.artifactBytes, failure), failure);
                }
                else
                {
                    ModelDescriptorArtifact descriptor;
                    descriptor.modelAssetId = modelId;
                    descriptor.skeletonAssetId = skeletonId;
                    descriptor.name = conversion.modelName;
                    if (skeletonId.IsValid())
                    {
                        product.dependencies.push_back(SourceEdge(skeletonId, CookedAssetKind::Skeleton, AssetDependencyKind::Loadable));
                    }
                    // Legacy controllers persist clip indices. Keep source order
                    // in the summary list while every entry carries its stable ID.
                    std::vector<std::pair<std::size_t, AssetId>> orderedClips;
                    for (const auto& [id, index] : clipIndices)
                    {
                        orderedClips.emplace_back(index, id);
                    }
                    std::ranges::sort(orderedClips);
                    for (const auto& [index, id] : orderedClips)
                    {
                        const auto& clip = imported.scene->clips.at(index);
                        descriptor.clips.push_back({ id, clip.name.empty() ? "clip_" + std::to_string(index) : clip.name,
                            clip.durationSeconds * conversion.ticksPerSecond, conversion.ticksPerSecond, true });
                        product.dependencies.push_back(SourceEdge(id, CookedAssetKind::AnimationClip, AssetDependencyKind::Loadable));
                    }
                    auto metadata = im::ConvertToModelMetadata(*imported.scene, conversion);
                    RequireSource(metadata.succeeded, metadata.notes.empty()
                        ? "Source model metadata conversion failed" : metadata.notes.back().message);
                    for (std::size_t index = 0; index < metadata.meshes.size(); ++index)
                    {
                        const auto& mesh = metadata.meshes[index];
                        const auto materialId = mesh.material.IsValid() ? materialIds.at(mesh.material.Value()) : AssetId{};
                        descriptor.meshes.push_back({ meshIds.at(index), materialId, mesh.name, mesh.bounds,
                            mesh.attributes, mesh.stride, mesh.vertexCount, mesh.indexCount, mesh.skinned });
                        product.dependencies.push_back(SourceEdge(meshIds.at(index), CookedAssetKind::Mesh,
                            AssetDependencyKind::Loadable));
                    }
                    for (const auto& node : metadata.nodes)
                    {
                        ModelNodeSummary summary;
                        summary.name = node.name;
                        summary.parent = node.parent;
                        summary.localTransform = node.localTransform;
                        for (const auto index : node.meshes)
                        {
                            summary.meshAssetIds.push_back(meshIds.at(index.Value()));
                        }
                        descriptor.nodes.push_back(std::move(summary));
                    }
                    for (std::size_t index = 0; index < imported.scene->materials.size(); ++index)
                    {
                        const auto& material = imported.scene->materials[index];
                        const auto blendMode = material.alphaMode == im::AlphaMode::Blend ? MaterialBlendMode::Transparent :
                            material.alphaMode == im::AlphaMode::Mask ? MaterialBlendMode::Masked : MaterialBlendMode::Opaque;
                        descriptor.materials.push_back({ materialIds.at(index), material.name, blendMode });
                        product.dependencies.push_back(SourceEdge(materialIds.at(index), CookedAssetKind::Material,
                            AssetDependencyKind::Loadable));
                    }
                    RequireSource(ValidateModelDescriptorDependencies(descriptor, product.dependencies, failure), failure);
                    product.representation = kModelDescriptorRepresentation;
                    product.schemaVersion = kModelDescriptorVersion;
                    product.extension = ".cemd";
                    RequireSource(WriteModelDescriptorArtifact(descriptor, product.artifactBytes, failure), failure);
                }
                std::ranges::sort(product.dependencies);
                result.products.push_back(std::move(product));
            }
            result.sourceInputsSha256 = snapshot.VerifyAndDigest();
        }
        catch (const std::exception& error)
        {
            result.products.clear();
            result.failure = error.what();
        }
        return result;
    }
}
