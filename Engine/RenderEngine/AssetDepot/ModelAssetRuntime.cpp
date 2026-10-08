#include "ModelAssetRuntime.h"
#include "../DataSystem.h"
#include "../Assets/ModelAnimationSampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <exception>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace
{
    namespace model_cooked = experiment::cooked;
    using Status = AssetDepot::AssetRequestStatus;
    using Error = AssetDepot::AssetRequestError;

    // Borrowed submission context only; no resource identity is a native address.
    thread_local std::uint64_t ModelSubmittingRequestId{};

    struct ModelSubmissionScope final
    {
        explicit ModelSubmissionScope(std::uint64_t id)
            : previous(std::exchange(ModelSubmittingRequestId, id)) {}
        ~ModelSubmissionScope() { ModelSubmittingRequestId = previous; }
        std::uint64_t previous{};
    };

    std::size_t AddModelCharge(std::size_t left, std::size_t right) noexcept
    {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        return right > maximum - left ? maximum : left + right;
    }

    std::size_t ModelOriginDynamicCharge(const model_cooked::ResolvedAssetEntry& origin) noexcept
    {
        auto charge = origin.entry.dependencies.capacity() * sizeof(model_cooked::AssetDependency);
        charge = AddModelCharge(charge, origin.blob.targetPlatform.capacity());
        charge = AddModelCharge(charge, origin.blob.targetAbi.capacity());
        return AddModelCharge(charge, origin.blob.artifactPath.capacity());
    }

    assets::ModelGeometryKey GeometryKey(const model_cooked::ResolvedAssetEntry& resolved)
    {
        const auto& blob = resolved.blob;
        return { blob.contentSha256, blob.byteSize, blob.kind, blob.representation,
            blob.schemaVersion, blob.targetPlatform, blob.targetAbi, 1u };
    }

    template<class T>
    AssetDepot::ModelAssetKey ModelKey(const model_cooked::ResolvedAssetEntry& resolved, bool exact)
    {
        if constexpr (std::is_same_v<T, assets::ModelGeometryPayload>)
        {
            auto blob = resolved.blob;
            blob.artifactPath.clear();
            return { { {}, model_cooked::CookedAssetKind::Mesh }, std::move(blob), 0u, true, 1u };
        }
        else
        {
            return { resolved.entry.asset, resolved.blob, resolved.resolverRevision, exact, 0u };
        }
    }

    template<class T>
    void NotifyModelConsumer(const own::shared_owner<AssetDepot::AssetRequestState<T>>& consumer,
        Status status, Error error, const std::string& message, const own::shared_owner<const T>& asset = {})
    {
        std::lock_guard lock(consumer->mutex);
        if (consumer->status != Status::Pending)
        {
            return;
        }
        consumer->status = status;
        consumer->error = error;
        consumer->asset = asset;
        try
        {
            consumer->message = message;
        }
        catch (...)
        {
            consumer->message.clear();
        }
    }

    template<class T, class Visit>
    void VisitModelFlights(const own::shared_owner<AssetDepot::ModelAssetWork<T>>& tail, const Visit& visit)
    {
        auto work = tail;
        while (work)
        {
            visit(work);
            if constexpr (std::is_same_v<T, assets::ModelGeometryPayload>)
            {
                work = work->dependencies.predecessor;
            }
            else
            {
                break;
            }
        }
    }

    bool SupportedModelRepresentation(const model_cooked::ResolvedAssetEntry& resolved)
    {
        using namespace model_cooked;
        if (resolved.entry.asset.kind != resolved.blob.kind || !resolved.byteSource)
        {
            return false;
        }
        switch (resolved.blob.kind)
        {
        case CookedAssetKind::Model:
            return resolved.blob.representation == kModelDescriptorRepresentation
                && resolved.blob.schemaVersion == kModelDescriptorVersion;
        case CookedAssetKind::Mesh:
            return resolved.blob.representation == kModelGeometryRepresentation
                && resolved.blob.schemaVersion == kModelGeometryArtifactVersion;
        case CookedAssetKind::Skeleton:
            return resolved.blob.representation == kSkeletonRepresentation
                && resolved.blob.schemaVersion == kSkeletonArtifactVersion;
        case CookedAssetKind::AnimationClip:
            return resolved.blob.representation == kAnimationClipRepresentation
                && resolved.blob.schemaVersion == kAnimationClipArtifactVersion;
        default:
            return false;
        }
    }

    Error ModelLookupError(model_cooked::AssetLookupStatus status)
    {
        switch (status)
        {
        case model_cooked::AssetLookupStatus::NotMounted:
            return Error::NotMounted;
        case model_cooked::AssetLookupStatus::TypeMismatch:
            return Error::TypeMismatch;
        case model_cooked::AssetLookupStatus::HardDependencyCycle:
            return Error::HardDependencyCycle;
        default:
            return Error::DependencyFailed;
        }
    }

    bool HasNoHardDependencies(const model_cooked::ResolvedAssetEntry& resolved)
    {
        return std::none_of(resolved.entry.dependencies.begin(), resolved.entry.dependencies.end(),
            [](const auto& dependency) { return dependency.kind == model_cooked::AssetDependencyKind::Hard; });
    }

    bool HasClipSkeletonDependency(const model_cooked::ResolvedAssetEntry& clip,
        const model_cooked::TypedAssetReference& skeleton)
    {
        std::size_t hardCount{};
        for (const auto& dependency : clip.entry.dependencies)
        {
            if (dependency.kind == model_cooked::AssetDependencyKind::Hard)
            {
                if (dependency.target != skeleton)
                {
                    return false;
                }
                ++hardCount;
            }
        }
        return hardCount == 1u && skeleton.kind == model_cooked::CookedAssetKind::Skeleton;
    }

    bool ValidateClipDependency(const model_cooked::ResolvedAssetEntry& clip,
        const model_cooked::ResolvedAssetEntry& skeleton)
    {
        return HasClipSkeletonDependency(clip, skeleton.entry.asset)
            && skeleton.resolverRevision == clip.resolverRevision
            && SupportedModelRepresentation(skeleton) && HasNoHardDependencies(skeleton);
    }

    bool ResolveModelLoadable(const model_cooked::CookedAssetCatalog& catalog,
        const model_cooked::TypedAssetReference& reference,
        model_cooked::ResolvedAssetEntry& child, std::string& failure)
    {
        const auto status = catalog.Find(reference, child);
        if (status == model_cooked::AssetLookupStatus::NotMounted)
        {
            // Preserve the requested type/identity and exact snapshot's absence.
            // No source means this unresolved Loadable never falls through to a
            // newer resolver when requested through the old descriptor.
            child.entry.asset = reference;
            child.blob.kind = reference.kind;
            child.resolverRevision = catalog.ResolverRevision();
            return true;
        }
        if (status != model_cooked::AssetLookupStatus::Found || !SupportedModelRepresentation(child))
        {
            failure = "Model descriptor loadable metadata has an incompatible type or representation.";
            return false;
        }
        // This is metadata only: do not CaptureArtifact, Size or read a Loadable
        // child's file until a request selects it. The immutable release-root
        // source and captured blob/hash are sufficient for later validation.
        return true;
    }

    Error ReadModelBytes(model_cooked::ResolvedAssetEntry& resolved,
        std::vector<std::byte>& bytes, std::string& failure)
    {
        std::uint64_t size{};
        constexpr std::uint64_t maxArtifactBytes = model_cooked::kModelSubAssetMaxBytes;
        if (!model_cooked::CaptureArtifactSource(resolved.byteSource, resolved.blob.artifactPath, failure)
            || !resolved.byteSource->Size(resolved.blob.artifactPath, size, failure))
        {
            return Error::ReadFailed;
        }
        if (size == 0u || size != resolved.blob.byteSize || size > maxArtifactBytes
            || size > (std::numeric_limits<std::size_t>::max)())
        {
            failure = "Model subasset size does not match its bounded manifest record.";
            return Error::IntegrityFailed;
        }
        bytes.resize(static_cast<std::size_t>(size));
        if (!resolved.byteSource->ReadAt(resolved.blob.artifactPath, 0u, bytes, failure))
        {
            return Error::ReadFailed;
        }
        model_cooked::Sha256Digest hash{};
        if (!model_cooked::ComputeSha256(bytes, hash, failure) || hash != resolved.blob.contentSha256)
        {
            failure = "Model subasset SHA-256 does not match the exact captured artifact.";
            return Error::IntegrityFailed;
        }
        return Error::None;
    }

    own::shared_owner<const assets::ModelSkeletonPayload> DecodeModelSkeleton(
        const model_cooked::ResolvedAssetEntry& resolved, std::span<const std::byte> bytes,
        std::string& failure)
    {
        model_cooked::SkeletonArtifact decoded;
        if (!model_cooked::ReadSkeletonArtifact(bytes, decoded, failure))
        {
            return {};
        }
        auto result = own::make_shared<assets::ModelSkeletonPayload>();
        result->origin = resolved;
        result->boneLayoutSha256 = decoded.boneLayoutSha256;
        if (!model_cooked::ComputeSkinBindingDigest(decoded.skeleton, result->skinBindingSha256, failure))
        {
            return {};
        }
        auto& target = result->skeleton;
        target.skeletonId = resolved.entry.asset.key.assetId.value;
        target.rootBone = decoded.skeleton.rootBone.Value();
        target.rootTransform = decoded.skeleton.rootTransform;
        target.globalInverseTransform = decoded.skeleton.globalInverseTransform;
        target.bones.reserve(decoded.skeleton.bones.size());
        result->decodedBytes = AddModelCharge(sizeof(assets::ModelSkeletonPayload),
            ModelOriginDynamicCharge(result->origin));
        result->decodedBytes = AddModelCharge(result->decodedBytes,
            target.bones.capacity() * sizeof(assets::ModelBoneAsset));
        for (auto& bone : decoded.skeleton.bones)
        {
            result->decodedBytes = AddModelCharge(result->decodedBytes, bone.name.capacity());
            target.bones.push_back({ std::move(bone.name), bone.parent.Value(), bone.inverseBindMatrix });
        }
        return result;
    }

    own::shared_owner<const assets::ModelAnimationPayload> DecodeModelAnimation(
        const model_cooked::ResolvedAssetEntry& resolved, std::span<const std::byte> bytes,
        own::shared_owner<const assets::ModelSkeletonPayload> skeleton, std::string& failure)
    {
        model_cooked::AnimationClipArtifact decoded;
        if (!model_cooked::ReadAnimationClipArtifact(bytes, decoded, failure))
        {
            return {};
        }
        if (!skeleton || decoded.skeletonAssetId.value != skeleton->skeleton.skeletonId
            || decoded.requiredBoneCount != skeleton->skeleton.bones.size()
            || decoded.requiredBoneLayoutSha256 != skeleton->boneLayoutSha256)
        {
            failure = "Clip hard dependency has a different ordered bone layout or skeleton identity.";
            return {};
        }
        auto result = own::make_shared<assets::ModelAnimationPayload>();
        result->origin = resolved;
        result->skeleton = std::move(skeleton);
        auto& clip = result->clip;
        clip.animationId = resolved.entry.asset.key.assetId.value;
        clip.name = std::move(decoded.clip.name);
        clip.durationTicks = decoded.clip.durationTicks;
        clip.ticksPerSecond = decoded.clip.ticksPerSecond;
        clip.looping = decoded.clip.looping;
        clip.tracks.reserve(decoded.clip.channels.size());
        std::size_t charge = AddModelCharge(sizeof(assets::ModelAnimationPayload), clip.name.capacity());
        charge = AddModelCharge(charge, ModelOriginDynamicCharge(result->origin));
        charge = AddModelCharge(charge, clip.tracks.capacity() * sizeof(assets::ModelAnimationTrack));
        const auto interpolation = [](experiment::InterpolationMode mode)
        {
            return mode == experiment::InterpolationMode::Step
                ? assets::ModelInterpolationMode::Step : assets::ModelInterpolationMode::Linear;
        };
        for (const auto& channel : decoded.clip.channels)
        {
            assets::ModelAnimationTrack track;
            track.bone = channel.bone.Value();
            track.translationInterpolation = interpolation(channel.translationInterpolation);
            track.rotationInterpolation = interpolation(channel.rotationInterpolation);
            track.scaleInterpolation = interpolation(channel.scaleInterpolation);
            track.translations.reserve(channel.translations.size());
            track.rotations.reserve(channel.rotations.size());
            track.scales.reserve(channel.scales.size());
            for (const auto& key : channel.translations)
            {
                track.translations.push_back({ key.time, key.value });
            }
            for (const auto& key : channel.rotations)
            {
                track.rotations.push_back({ key.time, key.quaternion });
            }
            for (const auto& key : channel.scales)
            {
                track.scales.push_back({ key.time, key.value });
            }
            charge = AddModelCharge(charge, track.translations.capacity() * sizeof(assets::ModelTranslationKey));
            charge = AddModelCharge(charge, track.rotations.capacity() * sizeof(assets::ModelRotationKey));
            charge = AddModelCharge(charge, track.scales.capacity() * sizeof(assets::ModelScaleKey));
            clip.tracks.push_back(std::move(track));
        }
        assets::animation::BuildTrackTable(clip, result->skeleton->skeleton.bones.size(), result->tracks);
        charge = AddModelCharge(charge, result->tracks.capacity() * sizeof(const assets::ModelAnimationTrack*));
        result->decodedBytes = AddModelCharge(charge, result->skeleton->ByteSize());
        return result;
    }

    std::size_t MeshletDynamicCharge(const experiment::MeshletPayload& meshlets) noexcept
    {
        auto charge = meshlets.descriptors.capacity() * sizeof(experiment::MeshletDescriptor);
        charge = AddModelCharge(charge, meshlets.vertexRemap.capacity() * sizeof(std::uint32_t));
        charge = AddModelCharge(charge, meshlets.triangleIndices.capacity());
        return AddModelCharge(charge, meshlets.primitiveRemap.capacity() * sizeof(std::uint32_t));
    }

    math::vector4 MeshletLocalBounds(const experiment::MeshletPayload& payload)
    {
        if (payload.descriptors.empty())
        {
            return {};
        }
        std::array<double, 3> minimum{}, maximum{};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            minimum[axis] = std::numeric_limits<double>::infinity();
            maximum[axis] = -std::numeric_limits<double>::infinity();
        }
        for (const auto& descriptor : payload.descriptors)
        {
            for (size_t axis = 0; axis < 3; ++axis)
            {
                const double center = descriptor.sphereCenter[axis];
                minimum[axis] = (std::min)(minimum[axis], center - descriptor.sphereRadius);
                maximum[axis] = (std::max)(maximum[axis], center + descriptor.sphereRadius);
            }
        }
        std::array<float, 3> center{};
        for (size_t axis = 0; axis < 3; ++axis)
        {
            const double midpoint = (minimum[axis] + maximum[axis]) * 0.5;
            if (!std::isfinite(midpoint) || std::abs(midpoint) > (std::numeric_limits<float>::max)())
            {
                return {};
            }
            center[axis] = static_cast<float>(midpoint);
        }
        double radius = 0.0;
        for (const auto& descriptor : payload.descriptors)
        {
            const double x = static_cast<double>(center[0]) - descriptor.sphereCenter[0];
            const double y = static_cast<double>(center[1]) - descriptor.sphereCenter[1];
            const double z = static_cast<double>(center[2]) - descriptor.sphereCenter[2];
            radius = (std::max)(radius, std::sqrt(x * x + y * y + z * z) + descriptor.sphereRadius);
        }
        if (!std::isfinite(radius) || radius > (std::numeric_limits<float>::max)())
        {
            return {};
        }
        const float outward = radius > 0.0
            ? std::nextafter(static_cast<float>(radius), std::numeric_limits<float>::infinity()) : 0.0f;
        // Unknown bounds force LOD0; they never permit additional simplification.
        return std::isfinite(outward) ? math::vector4{center[0], center[1], center[2], outward} : math::vector4{};
    }

    assets::ModelMeshletSummary MeshletSummary(const experiment::MeshletPayload& meshlets);

    own::shared_owner<const assets::ModelGeometryPayload> DecodeModelGeometry(
        const model_cooked::ResolvedAssetEntry& resolved, std::span<const std::byte> bytes,
        std::string& failure)
    {
        model_cooked::ModelGeometryArtifact decoded;
        if (!model_cooked::ReadModelGeometryArtifact(bytes, decoded, failure))
        {
            return {};
        }
        auto result = own::make_shared<assets::ModelGeometryPayload>();
        result->key = GeometryKey(resolved);
        result->artifactPath = resolved.blob.artifactPath;
        result->byteSource = resolved.byteSource;
        result->requiredBoneCount = decoded.requiredBoneCount;
        result->requiredSkinBindingSha256 = decoded.requiredSkinBindingSha256;
        auto& mesh = result->mesh;
        mesh.vertexAttributeMask = decoded.mesh.vertices.AttributeMask();
        mesh.vertexStride = decoded.mesh.vertices.Stride();
        mesh.vertexLayoutHash = assets::VertexLayoutHash(mesh.vertexAttributeMask);
        const auto vertices = decoded.mesh.vertices.Bytes();
        mesh.vertexBytes.assign(vertices.begin(), vertices.end());
        mesh.indices = std::move(decoded.mesh.indices);
        mesh.bounds = decoded.mesh.bounds;
        mesh.meshlets = std::move(decoded.mesh.meshlets);
        mesh.coarseLods = std::move(decoded.mesh.coarseLods);
        result->meshletSummary = MeshletSummary(mesh.meshlets);
        for (std::size_t index = 0u; index < mesh.coarseLods.levels.size(); ++index)
        {
            const auto& lod = mesh.coarseLods.levels[index];
            result->lodSummaries[index] = { lod.geometricError,
                static_cast<std::uint32_t>(lod.indices.size()), MeshletSummary(lod.meshlets) };
        }
        auto charge = AddModelCharge(sizeof(assets::ModelGeometryPayload), mesh.vertexBytes.capacity());
        charge = AddModelCharge(charge, result->artifactPath.capacity());
        charge = AddModelCharge(charge, result->key.targetPlatform.capacity());
        charge = AddModelCharge(charge, result->key.targetAbi.capacity());
        charge = AddModelCharge(charge, mesh.indices.capacity() * sizeof(std::uint32_t));
        charge = AddModelCharge(charge, MeshletDynamicCharge(mesh.meshlets));
        charge = AddModelCharge(charge, mesh.coarseLods.levels.capacity() * sizeof(experiment::MeshLodLevel));
        for (const auto& lod : mesh.coarseLods.levels)
        {
            charge = AddModelCharge(charge, lod.indices.capacity() * sizeof(std::uint32_t));
            charge = AddModelCharge(charge, MeshletDynamicCharge(lod.meshlets));
        }
        result->decodedBytes = charge;
        return result;
    }

    assets::ModelMeshletSummary MeshletSummary(const experiment::MeshletPayload& meshlets)
    {
        return { meshlets.settings, meshlets.geometryDigest,
            static_cast<std::uint32_t>(meshlets.descriptors.size()),
            static_cast<std::uint32_t>(meshlets.vertexRemap.size()),
            static_cast<std::uint32_t>(meshlets.triangleIndices.size()),
            static_cast<std::uint32_t>(meshlets.primitiveRemap.size()), meshlets.lod0, MeshletLocalBounds(meshlets) };
    }

    own::shared_owner<const assets::ModelMeshDescriptor> DescribeModelGeometry(
        model_cooked::ResolvedAssetEntry resolved,
        const assets::ModelGeometryPayload& geometry,
        own::shared_owner<const assets::ModelSkeletonPayload> skeleton,
        Error& error, std::string& failure)
    {
        if (!geometry.byteSource || geometry.key != GeometryKey(resolved))
        {
            error = Error::IntegrityFailed;
            failure = "Mesh descriptor and compatible raw decode identities differ.";
            return {};
        }
        // Logical identity/closure stay on this descriptor. Canonical content
        // backing is copied as a matched locator/source pair, already captured
        // and SHA-verified by the joined raw read. Never mix source A with path B.
        // This joins reads as well as decode for same- or cross-source aliases.
        resolved.blob.artifactPath = geometry.artifactPath;
        resolved.byteSource = geometry.byteSource;
        const bool skinned = assets::Has(geometry.mesh.vertexAttributeMask, assets::VertexAttribute::BoneIndices);
        if ((!skinned && (!HasNoHardDependencies(resolved) || skeleton))
            || (skinned && (!skeleton || !ValidateClipDependency(resolved, skeleton->origin)
                || geometry.requiredBoneCount != skeleton->skeleton.bones.size()
                || geometry.requiredSkinBindingSha256 != skeleton->skinBindingSha256)))
        {
            error = Error::DependencyFailed;
            failure = "Mesh hard dependency does not match its exact full skin binding.";
            return {};
        }
        auto result = own::make_shared<assets::ModelMeshDescriptor>();
        result->origin = std::move(resolved);
        result->geometryKey = geometry.key;
        result->meshId = result->origin.entry.asset.key.assetId.value;
        const auto& mesh = geometry.mesh;
        result->vertexAttributeMask = mesh.vertexAttributeMask;
        result->vertexStride = mesh.vertexStride;
        result->vertexLayoutHash = mesh.vertexLayoutHash;
        result->vertexCount = static_cast<std::uint32_t>(mesh.vertexBytes.size() / mesh.vertexStride);
        result->indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        result->bounds = mesh.bounds;
        result->skinned = skinned;
        result->requiredBoneCount = geometry.requiredBoneCount;
        result->requiredSkinBindingSha256 = geometry.requiredSkinBindingSha256;
        result->skeleton = std::move(skeleton);
        result->meshlets = geometry.meshletSummary;
        result->lodSettings = mesh.coarseLods.settings;
        result->lodGeometryDigest = mesh.coarseLods.geometryDigest;
        result->coarseLodCount = static_cast<std::uint32_t>(mesh.coarseLods.levels.size());
        for (std::size_t index = 0u; index < mesh.coarseLods.levels.size(); ++index)
        {
            result->coarseLods[index] = geometry.lodSummaries[index];
        }
        auto charge = AddModelCharge(sizeof(assets::ModelMeshDescriptor), ModelOriginDynamicCharge(result->origin));
        charge = AddModelCharge(charge, result->geometryKey.targetPlatform.capacity());
        result->metadataBytes = AddModelCharge(charge, result->geometryKey.targetAbi.capacity());
        return result;
    }

    own::shared_owner<const assets::ModelAnimationDescriptor> DecodeModelDescriptor(
        const model_cooked::ResolvedAssetEntry& resolved, std::span<const std::byte> bytes,
        own::shared_owner<const model_cooked::CookedAssetCatalog> catalog, std::string& failure)
    {
        auto result = own::make_shared<assets::ModelAnimationDescriptor>();
        if (!catalog || !model_cooked::ReadModelDescriptorArtifact(bytes, result->summary, failure)
            || !model_cooked::ValidateModelDescriptorDependencies(result->summary, resolved.entry.dependencies, failure))
        {
            return {};
        }
        if (result->summary.modelAssetId != resolved.entry.asset.key.assetId)
        {
            failure = "Model descriptor identity differs from its manifest entry.";
            return {};
        }
        result->origin = resolved;
        const model_cooked::TypedAssetReference skeletonReference{
            { result->summary.skeletonAssetId, {} }, model_cooked::CookedAssetKind::Skeleton };
        if (result->summary.skeletonAssetId.IsValid()
            && (!ResolveModelLoadable(*catalog, skeletonReference, result->skeleton, failure)
                || (result->skeleton.byteSource && !HasNoHardDependencies(result->skeleton))))
        {
            failure = "Model descriptor skeleton metadata is incompatible.";
            return {};
        }
        result->clips.reserve(result->summary.clips.size());
        for (const auto& clip : result->summary.clips)
        {
            model_cooked::ResolvedAssetEntry child;
            const model_cooked::TypedAssetReference reference{
                { clip.clipAssetId, {} }, model_cooked::CookedAssetKind::AnimationClip };
            if (!ResolveModelLoadable(*catalog, reference, child, failure)
                || (child.byteSource && !HasClipSkeletonDependency(child, skeletonReference)))
            {
                failure = "Model descriptor clip metadata has an invalid declared skeleton dependency.";
                return {};
            }
            result->clips.push_back(std::move(child));
        }
        result->meshes.reserve(result->summary.meshes.size());
        for (const auto& mesh : result->summary.meshes)
        {
            model_cooked::ResolvedAssetEntry child;
            const model_cooked::TypedAssetReference reference{
                { mesh.meshAssetId, {} }, model_cooked::CookedAssetKind::Mesh };
            if (!ResolveModelLoadable(*catalog, reference, child, failure)
                || (child.byteSource && (mesh.skinned
                    ? !HasClipSkeletonDependency(child, skeletonReference) : !HasNoHardDependencies(child))))
            {
                failure = "Model descriptor mesh metadata has an invalid declared skeleton dependency.";
                return {};
            }
            result->meshes.push_back(std::move(child));
        }
        std::set<experiment::AssetId> instantiatedMeshes;
        for (const auto& node : result->summary.nodes)
        {
            instantiatedMeshes.insert(node.meshAssetIds.begin(), node.meshAssetIds.end());
        }
        result->instantiatedMeshes.assign(instantiatedMeshes.begin(), instantiatedMeshes.end());
        auto charge = AddModelCharge(sizeof(assets::ModelAnimationDescriptor), ModelOriginDynamicCharge(result->origin));
        charge = AddModelCharge(charge, result->instantiatedMeshes.capacity() * sizeof(experiment::AssetId));
        charge = AddModelCharge(charge, result->summary.name.capacity());
        charge = AddModelCharge(charge, result->summary.clips.capacity() * sizeof(model_cooked::ModelClipSummary));
        charge = AddModelCharge(charge, result->summary.meshes.capacity() * sizeof(model_cooked::ModelMeshSummary));
        charge = AddModelCharge(charge, result->summary.nodes.capacity() * sizeof(model_cooked::ModelNodeSummary));
        charge = AddModelCharge(charge, result->summary.materials.capacity() * sizeof(model_cooked::ModelMaterialSummary));
        for (const auto& clip : result->summary.clips)
        {
            charge = AddModelCharge(charge, clip.name.capacity());
        }
        for (const auto& mesh : result->summary.meshes)
        {
            charge = AddModelCharge(charge, mesh.name.capacity());
        }
        for (const auto& node : result->summary.nodes)
        {
            charge = AddModelCharge(charge, node.name.capacity());
            charge = AddModelCharge(charge, node.meshAssetIds.capacity() * sizeof(experiment::AssetId));
        }
        for (const auto& material : result->summary.materials)
        {
            charge = AddModelCharge(charge, material.name.capacity());
        }
        charge = AddModelCharge(charge, result->clips.capacity() * sizeof(model_cooked::ResolvedAssetEntry));
        charge = AddModelCharge(charge, result->meshes.capacity() * sizeof(model_cooked::ResolvedAssetEntry));
        charge = AddModelCharge(charge, ModelOriginDynamicCharge(result->skeleton));
        for (const auto& clip : result->clips)
        {
            charge = AddModelCharge(charge, ModelOriginDynamicCharge(clip));
        }
        for (const auto& mesh : result->meshes)
        {
            charge = AddModelCharge(charge, ModelOriginDynamicCharge(mesh));
        }
        result->metadataBytes = charge;
        return result;
    }

    bool MatchesModelMeshSummary(const assets::ModelMeshDescriptor& mesh,
        const model_cooked::ModelMeshSummary& summary) noexcept
    {
        return mesh.Matches(summary);
    }

    template<class T>
    void TrimModelCache(AssetDepot::ModelAssetCache<T>& cache,
        std::vector<own::shared_owner<const T>>* retired = nullptr)
    {
        while (cache.retainedBytes > cache.budgetBytes)
        {
            auto oldest = cache.entries.end();
            for (auto item = cache.entries.begin(); item != cache.entries.end(); ++item)
            {
                if (item->second.retained && (oldest == cache.entries.end()
                    || item->second.lastUse < oldest->second.lastUse))
                {
                    oldest = item;
                }
            }
            if (oldest == cache.entries.end())
            {
                break;
            }
            cache.retainedBytes -= oldest->second.retainedCharge;
            oldest->second.retainedCharge = 0u;
            if (retired)
            {
                // Caller stages capacity before changing publication/budget.
                assert(retired->size() < retired->capacity());
                retired->push_back(std::move(oldest->second.retained));
            }
            else
            {
                oldest->second.retained.reset();
            }
            ++cache.logicalEvictions;
        }
        std::erase_if(cache.entries, [](const auto& item)
        {
            return !item.second.retained && !item.second.inFlight && item.second.live.expired();
        });
    }
}

template<class T>
AssetDepot::ModelAssetCache<T>& DataSystem::ModelAssetCacheLocked()
{
    if constexpr (std::is_same_v<T, assets::ModelAnimationDescriptor>)
    {
        return m_modelAssets.descriptors;
    }
    else if constexpr (std::is_same_v<T, assets::ModelSkeletonPayload>)
    {
        return m_modelAssets.skeletons;
    }
    else if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>)
    {
        return m_modelAssets.animations;
    }
    else if constexpr (std::is_same_v<T, assets::ModelMeshDescriptor>)
    {
        return m_modelAssets.meshes;
    }
    else
    {
        static_assert(std::is_same_v<T, assets::ModelGeometryPayload>);
        return m_modelAssets.geometry;
    }
}

template<class T>
own::shared_owner<const T> DataSystem::TryAcquireResolvedModelAssetLocked(
    const model_cooked::ResolvedAssetEntry& resolved, bool exactGeneration)
{
    if (!resolved.byteSource)
    {
        return {};
    }
    auto& cache = ModelAssetCacheLocked<T>();
    const auto found = cache.entries.find(ModelKey<T>(resolved, exactGeneration));
    if (found == cache.entries.end())
    {
        return {};
    }
    auto owner = found->second.retained ? found->second.retained : found->second.live.lock();
    if (owner)
    {
        found->second.lastUse = ++m_modelAssets.clock;
    }
    return owner;
}

template<class T>
own::shared_owner<const T> DataSystem::TryAcquireCurrentModelAsset(AssetDepot::AssetLink<T> link)
{
    if (!link.IsValid())
    {
        return {};
    }
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
    {
        return {};
    }
    model_cooked::ResolvedAssetEntry resolved;
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (!m_cookedCatalog || m_cookedCatalog->Find(link.ToReference(), resolved)
            != model_cooked::AssetLookupStatus::Found)
        {
            return {};
        }
    }
    return TryAcquireResolvedModelAssetLocked<T>(resolved, false);
}

template<class T>
AssetDepot::AssetRequest<T> DataSystem::RequestCurrentModelAssetAsync(AssetDepot::AssetLink<T> link)
{
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<T>>();
    AssetDepot::AssetRequest<T> failed(consumer);
    const auto fail = [&](Status status, Error error, const std::string& message)
    {
        NotifyModelConsumer(consumer, status, error, message);
        return failed;
    };
    if (!link.IsValid())
    {
        return fail(Status::Failed, Error::InvalidLink, "Invalid model subasset link.");
    }
    own::shared_owner<const model_cooked::CookedAssetCatalog> catalog;
    std::uint64_t epoch{};
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
        {
            return fail(m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged, {});
        }
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        catalog = m_cookedCatalog;
        epoch = m_assetPreparationEpoch;
    }
    return RequestModelAssetFromSnapshot(link, std::move(catalog), epoch);
}

template<class T>
AssetDepot::AssetRequest<T> DataSystem::RequestModelAssetFromSnapshot(AssetDepot::AssetLink<T> link,
    own::shared_owner<const model_cooked::CookedAssetCatalog> catalog, std::uint64_t epoch)
{
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<T>>();
    AssetDepot::AssetRequest<T> failed(consumer);
    const auto fail = [&](Status status, Error error, const std::string& message)
    {
        NotifyModelConsumer(consumer, status, error, message);
        return failed;
    };
    if (!link.IsValid())
    {
        return fail(Status::Failed, Error::InvalidLink, "Invalid same-snapshot model link.");
    }
    if (!catalog)
    {
        return fail(Status::Failed, Error::NotMounted, "No asset catalog is mounted.");
    }
    std::vector<model_cooked::ResolvedAssetEntry> closure;
    model_cooked::AssetCatalogLookupIssue issue;
    const auto status = catalog->CollectHardClosure(link.ToReference(), closure, issue);
    if (status != model_cooked::AssetLookupStatus::Found)
    {
        return fail(Status::Failed, ModelLookupError(status), issue.message);
    }
    if (closure.empty() || !SupportedModelRepresentation(closure.back()))
    {
        return fail(Status::Failed, Error::UnsupportedRepresentation, "Unsupported model subasset representation.");
    }
    model_cooked::ResolvedAssetEntry skeleton;
    if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>)
    {
        if (closure.size() != 2u || !ValidateClipDependency(closure.back(), closure.front()))
        {
            return fail(Status::Failed, Error::DependencyFailed, "Clip requires one supported skeleton hard dependency.");
        }
        skeleton = closure.front();
    }
    else if constexpr (std::is_same_v<T, assets::ModelMeshDescriptor>)
    {
        if (closure.size() == 2u && ValidateClipDependency(closure.back(), closure.front()))
        {
            skeleton = closure.front();
        }
        else if (closure.size() != 1u || !HasNoHardDependencies(closure.back()))
        {
            return fail(Status::Failed, Error::DependencyFailed,
                "Mesh requires either no hard dependency or one supported skeleton.");
        }
    }
    else if (closure.size() != 1u)
    {
        return fail(Status::Failed, Error::DependencyFailed, "Descriptor/skeleton has an unsupported hard dependency.");
    }
    return RequestResolvedModelAssetAsync<T>(std::move(closure.back()), std::move(catalog), false, epoch,
        std::move(skeleton));
}

template<class T>
AssetDepot::AssetRequest<T> DataSystem::RequestResolvedModelAssetAsync(
    model_cooked::ResolvedAssetEntry resolved,
    own::shared_owner<const model_cooked::CookedAssetCatalog> catalog,
    bool exactGeneration, std::uint64_t epoch, model_cooked::ResolvedAssetEntry skeleton)
{
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<T>>();
    AssetDepot::AssetRequest<T> request(consumer);
    std::lock_guard lock(m_assetPreparationMutex);
    const auto fail = [&](Status status, Error error)
    {
        NotifyModelConsumer(consumer, status, error, {});
        return request;
    };
    if (m_assetPreparationStopping)
    {
        return fail(Status::Cancelled, Error::ShuttingDown);
    }
    if (exactGeneration && !resolved.byteSource
        && resolved.entry.asset.kind == AssetDepot::AssetLink<T>::kKind
        && AssetDepot::AssetLink<T>{ resolved.entry.asset.key }.IsValid())
    {
        // An unresolved child in the descriptor's snapshot is not a malformed
        // representation, and must not retry against the current catalog.
        return fail(Status::Failed, Error::NotMounted);
    }
    if (!SupportedModelRepresentation(resolved)
        || resolved.entry.asset.kind != AssetDepot::AssetLink<T>::kKind
        || (!exactGeneration && (!catalog || catalog->ResolverRevision() != resolved.resolverRevision)))
    {
        return fail(Status::Failed, Error::InvalidLink);
    }
    if (!exactGeneration)
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetInvalidationDepth != 0u || epoch != m_assetPreparationEpoch || !m_cookedCatalog
            || m_cookedCatalog->ResolverRevision() != resolved.resolverRevision)
        {
            return fail(Status::Stale, Error::RevisionChanged);
        }
    }
    if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>
        || std::is_same_v<T, assets::ModelMeshDescriptor>)
    {
        if (exactGeneration && !skeleton.byteSource
            && skeleton.entry.asset.kind == model_cooked::CookedAssetKind::Skeleton
            && AssetDepot::AssetLink<assets::ModelSkeletonPayload>{ skeleton.entry.asset.key }.IsValid())
        {
            return fail(Status::Failed, Error::NotMounted);
        }
        if ((!HasNoHardDependencies(resolved) && !ValidateClipDependency(resolved, skeleton))
            || (std::is_same_v<T, assets::ModelAnimationPayload> && !skeleton.byteSource))
        {
            return fail(Status::Failed, Error::DependencyFailed);
        }
    }
    try
    {
        auto work = StartModelAssetWorkLocked<T>(resolved, catalog, exactGeneration,
            exactGeneration ? m_assetPreparationEpoch : epoch, skeleton);
        consumer->completion = work->completion;
        if (work->status == Status::Pending)
        {
            work->consumers.emplace_back(consumer);
        }
        else
        {
            NotifyModelConsumer(consumer, work->status, work->error, work->message, work->asset);
        }
    }
    catch (...)
    {
        return fail(Status::Failed, Error::SubmissionFailed);
    }
    return request;
}

AssetDepot::AssetRequest<assets::ModelGeometryPayload> DataSystem::RequestModelColliderGeometry(
    model_cooked::ResolvedAssetEntry resolved,
    AssetDepot::AssetRequest<assets::ModelAnimationDescriptor> descriptor, std::uint64_t epoch,
    assets::ModelColliderPreparationPolicy colliderPolicy)
{
    using Payload = assets::ModelGeometryPayload;
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<Payload>>();
    AssetDepot::AssetRequest<Payload> request(consumer);
    own::shared_owner<AssetDepot::ModelAssetWork<Payload>> work;
    const auto knownRoot = descriptor.Snapshot();
    std::lock_guard lock(m_assetPreparationMutex);
    const auto fail = [&](Status status, Error error)
    {
        NotifyModelConsumer(consumer, status, error, {});
        return request;
    };
    if (m_assetPreparationStopping)
    {
        return fail(Status::Cancelled, Error::ShuttingDown);
    }
    if (m_assetInvalidationDepth != 0u || epoch != m_assetPreparationEpoch ||
        resolved.resolverRevision != m_assetDepotRevision)
    {
        return fail(Status::Stale, Error::RevisionChanged);
    }
    if (!SupportedModelRepresentation(resolved) || resolved.entry.asset.kind != model_cooked::CookedAssetKind::Mesh)
    {
        return fail(Status::Failed, Error::UnsupportedRepresentation);
    }
    if (colliderPolicy == assets::ModelColliderPreparationPolicy::Disabled)
    {
        return fail(Status::Cancelled, Error::None);
    }
    if (knownRoot.status == Status::Ready && knownRoot.asset)
    {
        if (knownRoot.asset->origin.resolverRevision != resolved.resolverRevision)
        {
            return fail(Status::Stale, Error::RevisionChanged);
        }
        if (!assets::ShouldPrepareModelCollider(colliderPolicy, knownRoot.asset->summary.createMeshCollider) ||
            !knownRoot.asset->Instantiates(resolved.entry.asset.key.assetId))
        {
            // Resident root metadata can decline the optional request before
            // even creating a flight or pinning an existing geometry payload.
            return fail(Status::Cancelled, Error::None);
        }
    }
    try
    {
        if (m_modelAssets.nextRequestId == (std::numeric_limits<std::uint64_t>::max)())
        {
            return fail(Status::Failed, Error::SubmissionFailed);
        }
        work = own::make_shared<AssetDepot::ModelAssetWork<Payload>>();
        work->key = ModelKey<Payload>(resolved, true);
        work->resolved = std::move(resolved);
        work->epoch = epoch;
        work->requestId = m_modelAssets.nextRequestId++;
        work->dependencies.colliderOnly = true;
        work->dependencies.colliderDescriptor = std::move(descriptor);
        work->dependencies.colliderMesh = work->resolved.entry.asset.key.assetId;
        work->dependencies.colliderPolicy = colliderPolicy;
        work->dependencies.colliderConsumers.emplace_back(consumer);
        std::vector<job_handle> dependencies;
        const auto rootCompletion = work->dependencies.colliderDescriptor.Completion();
        if (rootCompletion.valid())
        {
            dependencies.push_back(rootCompletion);
        }
        auto& cache = m_modelAssets.geometry;
        auto& entry = cache.entries[work->key];
        if (entry.inFlight)
        {
            // Different model roots must keep distinct decisions and fixed
            // tokens. Serialize their conditional flights by content key and
            // keep the predecessor result for a zero-budget positive handoff.
            work->dependencies.predecessor = entry.inFlight;
            if (entry.inFlight->completion.valid())
            {
                dependencies.push_back(entry.inFlight->completion);
            }
        }
        entry.inFlight = work;
        entry.lastUse = ++m_modelAssets.clock;
        ModelSubmissionScope submitting(work->requestId);
        try
        {
            job_group jobs;
            jobs.add([this, work]() { RunModelAssetWork(work); });
            jobs.on_complete([this, work](std::exception_ptr failure)
            {
                const auto finish = [&]()
                {
                    CompleteModelAssetWorkLocked(work, Status::Failed,
                        failure ? Error::SubmissionFailed : Error::DecodeFailed);
                };
                if (ModelSubmittingRequestId == work->requestId)
                {
                    finish();
                }
                else
                {
                    std::lock_guard completionLock(m_assetPreparationMutex);
                    finish();
                }
            });
            work->completion = SubmitAssetWorkLocked(std::move(jobs), dependencies, true);
        }
        catch (...)
        {
            CompleteModelAssetWorkLocked(work, Status::Failed, Error::SubmissionFailed);
        }
        consumer->completion = work->completion;
    }
    catch (...)
    {
        return fail(Status::Failed, Error::SubmissionFailed);
    }
    return request;
}

template<class T>
own::shared_owner<AssetDepot::ModelAssetWork<T>> DataSystem::StartModelAssetWorkLocked(
    const model_cooked::ResolvedAssetEntry& resolved,
    const own::shared_owner<const model_cooked::CookedAssetCatalog>& catalog,
    bool exactGeneration, std::uint64_t epoch, const model_cooked::ResolvedAssetEntry& skeleton)
{
    auto& cache = ModelAssetCacheLocked<T>();
    const auto key = ModelKey<T>(resolved, exactGeneration);
    const auto previous = cache.entries.find(key);
    if (previous != cache.entries.end() && previous->second.inFlight)
    {
        previous->second.lastUse = ++m_modelAssets.clock;
        if constexpr (std::is_same_v<T, assets::ModelGeometryPayload>)
        {
            // A queued conditional gate need not delay an ordinary consumer
            // when an earlier flight has already published compatible data.
            auto resident = previous->second.retained ? previous->second.retained : previous->second.live.lock();
            if (resident)
            {
                auto ready = own::make_shared<AssetDepot::ModelAssetWork<T>>();
                ready->key = key;
                ready->status = Status::Ready;
                ready->asset = std::move(resident);
                return ready;
            }
            // This is ordinary descriptor/payload demand, never an optional
            // collider subscriber. Admission and the skip decision share a lock.
            previous->second.inFlight->dependencies.ordinaryDemand = true;
        }
        return previous->second.inFlight;
    }
    auto work = own::make_shared<AssetDepot::ModelAssetWork<T>>();
    work->key = key;
    work->resolved = resolved;
    work->catalog = catalog;
    work->epoch = epoch;
    if (previous != cache.entries.end())
    {
        work->asset = previous->second.retained ? previous->second.retained : previous->second.live.lock();
        previous->second.lastUse = ++m_modelAssets.clock;
    }
    if (work->asset)
    {
        work->status = Status::Ready;
        return work;
    }
    if (m_modelAssets.nextRequestId == (std::numeric_limits<std::uint64_t>::max)())
    {
        work->status = Status::Failed;
        work->error = Error::SubmissionFailed;
        return work;
    }
    work->requestId = m_modelAssets.nextRequestId++;
    std::vector<job_handle> dependencies;
    if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>
        || std::is_same_v<T, assets::ModelMeshDescriptor>)
    {
        if (skeleton.byteSource)
        {
            work->dependencies.skeletonWork = StartModelAssetWorkLocked<assets::ModelSkeletonPayload>(
                skeleton, catalog, exactGeneration, epoch, {});
            if (work->dependencies.skeletonWork->completion.valid())
            {
                dependencies.push_back(work->dependencies.skeletonWork->completion);
            }
        }
    }
    if constexpr (std::is_same_v<T, assets::ModelMeshDescriptor>)
    {
        // Accepted compatible raw work survives current-link invalidation. Each
        // bound descriptor still revalidates its own revision before publishing.
        work->dependencies.geometryWork = StartModelAssetWorkLocked<assets::ModelGeometryPayload>(
            resolved, {}, true, epoch, {});
        if (work->dependencies.geometryWork->completion.valid())
        {
            dependencies.push_back(work->dependencies.geometryWork->completion);
        }
    }
    // A dependency's inline terminal callback can trim expired parent entries.
    // Reacquire this node only after dependency submission; never retain an
    // iterator/reference across that callback boundary.
    auto& entry = cache.entries[key];
    entry.lastUse = ++m_modelAssets.clock;
    entry.inFlight = work;
    ModelSubmissionScope submitting(work->requestId);
    try
    {
        job_group jobs;
        jobs.add([this, work]() { RunModelAssetWork(work); });
        jobs.on_complete([this, work](std::exception_ptr failure)
        {
            const auto finish = [&]()
            {
                // Scheduler-skipped bodies also terminalize before their token
                // becomes ready. The body completion makes this a harmless no-op.
                CompleteModelAssetWorkLocked(work, Status::Failed,
                    failure ? Error::SubmissionFailed : Error::DecodeFailed);
            };
            if (ModelSubmittingRequestId == work->requestId)
            {
                finish();
            }
            else
            {
                std::lock_guard completionLock(m_assetPreparationMutex);
                finish();
            }
        });
        work->completion = SubmitAssetWorkLocked(std::move(jobs), dependencies, exactGeneration);
    }
    catch (...)
    {
        CompleteModelAssetWorkLocked(work, Status::Failed, Error::SubmissionFailed);
    }
    return work;
}

template<class T>
void DataSystem::RunModelAssetWork(own::shared_owner<AssetDepot::ModelAssetWork<T>> work)
{
    try
    {
        own::shared_owner<const assets::ModelSkeletonPayload> skeleton;
        own::shared_owner<const assets::ModelGeometryPayload> geometry;
        own::shared_owner<AssetDepot::ModelAssetWork<assets::ModelGeometryPayload>> predecessor;
        AssetDepot::AssetRequest<assets::ModelAnimationDescriptor> colliderRequest;
        AssetDepot::AssetRequestSnapshot<assets::ModelAnimationDescriptor> colliderRoot;
        {
            std::lock_guard lock(m_assetPreparationMutex);
            if (work->status != Status::Pending)
            {
                return;
            }
            if constexpr (std::is_same_v<T, assets::ModelGeometryPayload>)
            {
                auto& demand = work->dependencies;
                if (demand.colliderOnly)
                {
                    colliderRequest = std::move(demand.colliderDescriptor);
                    colliderRoot = colliderRequest.Snapshot();
                    demand.colliderGateEvaluated = true;
                    demand.colliderRequired = colliderRoot.status == Status::Ready && colliderRoot.asset &&
                        assets::ShouldPrepareModelCollider(
                            demand.colliderPolicy, colliderRoot.asset->summary.createMeshCollider) &&
                        colliderRoot.asset->Instantiates(demand.colliderMesh);
                    // All predecessor callbacks are complete. A queued later
                    // gate owns the handoff chain, including across a false
                    // intermediate gate. The tail releases it outside the lock.
                    predecessor = demand.predecessor;
                    const auto entry = m_modelAssets.geometry.entries.find(work->key);
                    if (entry != m_modelAssets.geometry.entries.end() && entry->second.inFlight &&
                        entry->second.inFlight->requestId == work->requestId)
                    {
                        predecessor = std::move(demand.predecessor);
                    }
                    if (!demand.ordinaryDemand && !demand.colliderRequired)
                    {
                        CompleteModelAssetWorkLocked(work, Status::Cancelled, Error::None);
                        return;
                    }
                    VisitModelFlights(predecessor, [&](const auto& earlier)
                    {
                        if (!geometry && earlier->status == Status::Ready)
                        {
                            geometry = earlier->asset;
                        }
                    });
                    if (!geometry)
                    {
                        const auto found = m_modelAssets.geometry.entries.find(work->key);
                        if (found != m_modelAssets.geometry.entries.end())
                        {
                            geometry = found->second.retained ? found->second.retained : found->second.live.lock();
                        }
                    }
                }
            }
            if (m_assetPreparationStopping
                || (!work->key.exactGeneration && (m_assetInvalidationDepth != 0u
                    || work->epoch != m_assetPreparationEpoch)))
            {
                CompleteModelAssetWorkLocked(work,
                    m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                    m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
                return;
            }
            if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>
                || std::is_same_v<T, assets::ModelMeshDescriptor>)
            {
                const auto& dependency = work->dependencies.skeletonWork;
                if (dependency)
                {
                    if (dependency->status != Status::Ready || !dependency->asset)
                    {
                        CompleteModelAssetWorkLocked(work, Status::Failed, Error::DependencyFailed);
                        return;
                    }
                    skeleton = dependency->asset;
                }
                else if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>)
                {
                    CompleteModelAssetWorkLocked(work, Status::Failed, Error::DependencyFailed);
                    return;
                }
            }
            if constexpr (std::is_same_v<T, assets::ModelMeshDescriptor>)
            {
                const auto& dependency = work->dependencies.geometryWork;
                if (!dependency || dependency->status != Status::Ready || !dependency->asset)
                {
                    CompleteModelAssetWorkLocked(work, Status::Failed,
                        dependency ? dependency->error : Error::DependencyFailed,
                        dependency ? dependency->message : std::string{});
                    return;
                }
                geometry = dependency->asset;
            }
        }
        std::string failure;
        Error error = Error::None;
        own::shared_owner<const T> candidate;
        if constexpr (std::is_same_v<T, assets::ModelMeshDescriptor>)
        {
            candidate = DescribeModelGeometry(work->resolved, *geometry, std::move(skeleton), error, failure);
        }
        else if constexpr (std::is_same_v<T, assets::ModelGeometryPayload>)
        {
            if (geometry)
            {
                candidate = geometry;
            }
            else
            {
                std::vector<std::byte> bytes;
                auto resolved = work->resolved;
                error = ReadModelBytes(resolved, bytes, failure);
                if (error == Error::None)
                {
                    candidate = DecodeModelGeometry(resolved, bytes, failure);
                }
            }
        }
        else
        {
            std::vector<std::byte> bytes;
            auto resolved = work->resolved;
            error = ReadModelBytes(resolved, bytes, failure);
            if (error == Error::None)
            {
                if constexpr (std::is_same_v<T, assets::ModelSkeletonPayload>)
                {
                    candidate = DecodeModelSkeleton(resolved, bytes, failure);
                }
                else if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>)
                {
                    candidate = DecodeModelAnimation(resolved, bytes, std::move(skeleton), failure);
                }
                else
                {
                    candidate = DecodeModelDescriptor(resolved, bytes, work->catalog, failure);
                }
            }
        }
        if (error == Error::None && !candidate)
        {
            error = Error::DecodeFailed;
        }
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteModelAssetWorkLocked(work, error == Error::None ? Status::Ready : Status::Failed,
            error, std::move(failure), candidate);
        // candidate keeps rejected/stale bulk alive until this lock is gone.
    }
    catch (...)
    {
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteModelAssetWorkLocked(work, Status::Failed, Error::DecodeFailed);
    }
}

template<class T>
void DataSystem::CompleteModelAssetWorkLocked(const own::shared_owner<AssetDepot::ModelAssetWork<T>>& work,
    Status status, Error error, std::string message, own::shared_owner<const T> asset)
{
    if (work->status != Status::Pending)
    {
        return;
    }
    auto& cache = ModelAssetCacheLocked<T>();
    const auto found = cache.entries.find(work->key);
    bool registered{};
    if (found != cache.entries.end())
    {
        VisitModelFlights(found->second.inFlight, [&](const auto& flight)
        {
            registered = registered || flight->requestId == work->requestId;
        });
    }
    bool current = true;
    if (!work->key.exactGeneration)
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        current = m_cookedCatalog && m_cookedCatalog->ResolverRevision() == work->key.resolverRevision
            && m_assetInvalidationDepth == 0u && m_assetPreparationEpoch == work->epoch;
    }
    if (m_assetPreparationStopping)
    {
        status = Status::Cancelled;
        error = Error::ShuttingDown;
        message.clear();
    }
    else if (!current || !registered)
    {
        status = Status::Stale;
        error = Error::RevisionChanged;
        message.clear();
    }
    if (status != Status::Ready)
    {
        asset.reset();
    }
    if (status == Status::Ready && !asset)
    {
        status = Status::Failed;
        error = Error::DecodeFailed;
    }
    bool retainCandidate = true;
    if constexpr (std::is_same_v<T, assets::ModelMeshDescriptor>
        || std::is_same_v<T, assets::ModelGeometryPayload>)
    {
        try
        {
            // Every allocation precedes work/cache publication. If retirement
            // staging cannot allocate, deliver an unretained ready result instead.
            const auto charge = asset ? asset->ByteSize() : 0u;
            if (charge <= cache.budgetBytes && cache.retainedBytes > cache.budgetBytes - charge)
            {
                work->retiredAssets.reserve(cache.entries.size());
            }
        }
        catch (...)
        {
            retainCandidate = false;
        }
    }
    work->status = status;
    work->error = error;
    work->message = std::move(message);
    work->asset = asset;
    if (registered)
    {
        auto& entry = found->second;
        if (entry.inFlight->requestId == work->requestId)
        {
            if constexpr (std::is_same_v<T, assets::ModelGeometryPayload>)
            {
                // Inline submission/dependency failure can terminalize a tail
                // before an older accepted flight. Restore that flight instead
                // of orphaning its ordinary consumers or marking it stale.
                own::shared_owner<AssetDepot::ModelAssetWork<T>> pending;
                VisitModelFlights(work->dependencies.predecessor, [&](const auto& earlier)
                {
                    if (!pending && earlier->status == Status::Pending)
                    {
                        pending = earlier;
                    }
                });
                entry.inFlight = std::move(pending);
            }
            else
            {
                entry.inFlight.reset();
            }
        }
        if (asset)
        {
            entry.live = asset;
            entry.lastUse = ++m_modelAssets.clock;
            const auto charge = asset->ByteSize();
            if (retainCandidate && charge <= cache.budgetBytes
                && charge <= (std::numeric_limits<std::size_t>::max)() - cache.retainedBytes)
            {
                cache.retainedBytes -= entry.retainedCharge;
                entry.retained = asset;
                entry.retainedCharge = charge;
                cache.retainedBytes += charge;
            }
        }
    }
    for (const auto& weak : work->consumers)
    {
        if (const auto consumer = weak.lock())
        {
            NotifyModelConsumer(consumer, status, error, work->message, asset);
        }
    }
    work->consumers.clear();
    if constexpr (std::is_same_v<T, assets::ModelGeometryPayload>)
    {
        for (const auto& weak : work->dependencies.colliderConsumers)
        {
            if (const auto consumer = weak.lock())
            {
                if (work->dependencies.colliderGateEvaluated && !work->dependencies.colliderRequired &&
                    status != Status::Stale && !(status == Status::Cancelled && error == Error::ShuttingDown))
                {
                    // A false policy/unused mesh does not pin an ordinary
                    // descriptor consumer's decoded result merely by joining.
                    NotifyModelConsumer(consumer, Status::Cancelled, Error::None, {});
                }
                else
                {
                    NotifyModelConsumer(consumer, status, error, work->message, asset);
                }
            }
        }
        work->dependencies.colliderConsumers.clear();
    }
    if constexpr (std::is_same_v<T, assets::ModelMeshDescriptor>
        || std::is_same_v<T, assets::ModelGeometryPayload>)
    {
        if (retainCandidate)
        {
            TrimModelCache(cache, &work->retiredAssets);
        }
    }
    TrimModelAssetsLocked();
}

AssetDepot::AssetRequest<assets::ModelGeometryPayload> DataSystem::RequestAsync(
    own::shared_owner<const assets::ModelMeshDescriptor> descriptor)
{
    if (!descriptor || descriptor->geometryKey != GeometryKey(descriptor->origin)
        || (descriptor->skinned && (!descriptor->skeleton
            || descriptor->requiredBoneCount != descriptor->skeleton->skeleton.bones.size()
            || descriptor->requiredSkinBindingSha256 != descriptor->skeleton->skinBindingSha256)))
    {
        auto consumer = own::make_shared<AssetDepot::AssetRequestState<assets::ModelGeometryPayload>>();
        NotifyModelConsumer(consumer, Status::Failed, Error::InvalidLink, "Invalid exact mesh descriptor.");
        return AssetDepot::AssetRequest<assets::ModelGeometryPayload>(std::move(consumer));
    }
    return RequestResolvedModelAssetAsync<assets::ModelGeometryPayload>(descriptor->origin, {}, true, 0u);
}

own::shared_owner<const assets::ModelGeometryPayload> DataSystem::TryAcquire(
    const own::shared_owner<const assets::ModelMeshDescriptor>& descriptor)
{
    own::shared_owner<const assets::ModelGeometryPayload> result;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (!descriptor || m_assetPreparationStopping)
        {
            return {};
        }
        result = TryAcquireResolvedModelAssetLocked<assets::ModelGeometryPayload>(descriptor->origin, true);
    }
    return result && result->Matches(*descriptor) ? result : own::shared_owner<const assets::ModelGeometryPayload>{};
}

AssetDepot::AssetRequest<assets::ModelMeshDescriptor> DataSystem::RequestAsync(
    own::shared_owner<const assets::ModelAnimationDescriptor> descriptor, std::size_t meshIndex)
{
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<assets::ModelMeshDescriptor>>();
    AssetDepot::AssetRequest<assets::ModelMeshDescriptor> request(consumer);
    if (!descriptor || meshIndex >= descriptor->meshes.size() || meshIndex >= descriptor->summary.meshes.size())
    {
        NotifyModelConsumer(consumer, Status::Failed, Error::InvalidLink, "Invalid selected mesh descriptor.");
        return request;
    }
    const auto& summary = descriptor->summary.meshes[meshIndex];
    auto selected = RequestResolvedModelAssetAsync<assets::ModelMeshDescriptor>(descriptor->meshes[meshIndex],
        {}, true, 0u, summary.skinned ? descriptor->skeleton : model_cooked::ResolvedAssetEntry{});
    // The selected child validates only after its exact decode dependency. Keep
    // both the child result and the root summary in this nonblocking continuation.
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping)
    {
        NotifyModelConsumer(consumer, Status::Cancelled, Error::ShuttingDown, {});
        return request;
    }
    try
    {
        job_group jobs;
        jobs.add([this, consumer, descriptor = std::move(descriptor), meshIndex, selected]()
        {
            auto result = selected.Snapshot();
            if (result.status == Status::Ready && result.asset)
            {
                const auto& expected = descriptor->summary.meshes[meshIndex];
                if (!MatchesModelMeshSummary(*result.asset, expected))
                {
                    NotifyModelConsumer(consumer, Status::Failed, Error::IntegrityFailed,
                        "Selected mesh does not match the model descriptor summary.");
                    return;
                }
                // Material/name selection remains in the model's summary. Return
                // the same cached mesh generation so its weak live entry also
                // survives zero-budget handoff; do not create a second wrapper.
            }
            std::lock_guard completionLock(m_assetPreparationMutex);
            NotifyModelConsumer(consumer,
                m_assetPreparationStopping ? Status::Cancelled : result.status,
                m_assetPreparationStopping ? Error::ShuttingDown : result.error,
                result.message, m_assetPreparationStopping ? own::shared_owner<const assets::ModelMeshDescriptor>{}
                    : result.asset);
        });
        jobs.on_complete([consumer](std::exception_ptr failure)
        {
            NotifyModelConsumer(consumer, Status::Failed,
                failure ? Error::SubmissionFailed : Error::DecodeFailed, {});
        });
        const auto completion = selected.Completion();
        consumer->completion = SubmitAssetWorkLocked(std::move(jobs),
            completion.valid() ? std::span<const job_handle>(&completion, 1u) : std::span<const job_handle>{}, true);
    }
    catch (...)
    {
        NotifyModelConsumer(consumer, Status::Failed, Error::SubmissionFailed, {});
    }
    return request;
}

own::shared_owner<const assets::ModelMeshDescriptor> DataSystem::TryAcquire(
    const own::shared_owner<const assets::ModelAnimationDescriptor>& descriptor, std::size_t meshIndex)
{
    std::lock_guard lock(m_assetPreparationMutex);
    if (!descriptor || meshIndex >= descriptor->meshes.size()
        || meshIndex >= descriptor->summary.meshes.size() || m_assetPreparationStopping)
    {
        return {};
    }
    auto result = TryAcquireResolvedModelAssetLocked<assets::ModelMeshDescriptor>(descriptor->meshes[meshIndex], true);
    return result && MatchesModelMeshSummary(*result, descriptor->summary.meshes[meshIndex])
        ? result : own::shared_owner<const assets::ModelMeshDescriptor>{};
}

bool DataSystem::HasModelMeshDescriptor(AssetDepot::AssetLink<assets::ModelMeshDescriptor> link) const
{
    if (!link.IsValid())
    {
        return false;
    }
    std::lock_guard lock(m_assetPreparationMutex);
    std::lock_guard catalogLock(m_cookedCatalogMutex);
    model_cooked::ResolvedAssetEntry resolved;
    if (!m_cookedCatalog)
    {
        return false;
    }
    const auto status = m_cookedCatalog->Find(link.ToReference(), resolved);
    // A mounted winner of the wrong kind is still an explicit v3 error, not
    // permission to load the legacy source model at the same serialized ID.
    return status == model_cooked::AssetLookupStatus::Found
        || status == model_cooked::AssetLookupStatus::TypeMismatch;
}

void DataSystem::SetModelGeometryCacheBudgets(std::size_t descriptors, std::size_t geometry)
{
    std::vector<own::shared_owner<const assets::ModelMeshDescriptor>> retiredMeshes;
    std::vector<own::shared_owner<const assets::ModelGeometryPayload>> retiredGeometry;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        retiredMeshes.reserve(m_modelAssets.meshes.entries.size());
        retiredGeometry.reserve(m_modelAssets.geometry.entries.size());
        m_modelAssets.meshes.budgetBytes = descriptors;
        m_modelAssets.geometry.budgetBytes = geometry;
        TrimModelCache(m_modelAssets.meshes, &retiredMeshes);
        TrimModelCache(m_modelAssets.geometry, &retiredGeometry);
    }
    // Potentially large arrays and exact backing are destroyed after the lock.
}

AssetDepot::AssetRequest<assets::ModelSkeletonPayload> DataSystem::RequestSkeletonAsync(
    own::shared_owner<const assets::ModelAnimationDescriptor> descriptor)
{
    return RequestResolvedModelAssetAsync<assets::ModelSkeletonPayload>(
        descriptor ? descriptor->skeleton : model_cooked::ResolvedAssetEntry{},
        {}, true, 0u);
}

AssetDepot::AssetRequest<assets::ModelAnimationPayload> DataSystem::RequestAnimationAsync(
    own::shared_owner<const assets::ModelAnimationDescriptor> descriptor, std::size_t clipIndex)
{
    const bool valid = descriptor && clipIndex < descriptor->clips.size();
    return RequestResolvedModelAssetAsync<assets::ModelAnimationPayload>(
        valid ? descriptor->clips[clipIndex] : model_cooked::ResolvedAssetEntry{},
        {}, true, 0u,
        valid ? descriptor->skeleton : model_cooked::ResolvedAssetEntry{});
}

own::shared_owner<const assets::ModelSkeletonPayload> DataSystem::TryAcquireSkeleton(
    const own::shared_owner<const assets::ModelAnimationDescriptor>& descriptor)
{
    std::lock_guard lock(m_assetPreparationMutex);
    return descriptor && !m_assetPreparationStopping
        ? TryAcquireResolvedModelAssetLocked<assets::ModelSkeletonPayload>(descriptor->skeleton, true)
        : own::shared_owner<const assets::ModelSkeletonPayload>{};
}

own::shared_owner<const assets::ModelAnimationPayload> DataSystem::TryAcquireAnimation(
    const own::shared_owner<const assets::ModelAnimationDescriptor>& descriptor, std::size_t clipIndex)
{
    std::lock_guard lock(m_assetPreparationMutex);
    return descriptor && clipIndex < descriptor->clips.size() && !m_assetPreparationStopping
        ? TryAcquireResolvedModelAssetLocked<assets::ModelAnimationPayload>(descriptor->clips[clipIndex], true)
        : own::shared_owner<const assets::ModelAnimationPayload>{};
}

bool DataSystem::HasModelAnimationDescriptor(AssetDepot::AssetLink<assets::ModelAnimationDescriptor> link) const
{
    if (!link.IsValid())
    {
        return false;
    }
    std::lock_guard lock(m_assetPreparationMutex);
    std::lock_guard catalogLock(m_cookedCatalogMutex);
    model_cooked::ResolvedAssetEntry resolved;
    // A mounted v3 definition stays on the v3 path even when unsupported or
    // temporarily stale; never recover a failed v3 request through CEMCv11 I/O.
    return m_cookedCatalog
        && m_cookedCatalog->Find(link.ToReference(), resolved) == model_cooked::AssetLookupStatus::Found;
}

void DataSystem::TrimModelAssetsLocked()
{
    TrimModelCache(m_modelAssets.descriptors);
    TrimModelCache(m_modelAssets.skeletons);
    TrimModelCache(m_modelAssets.animations);
    // Mesh/geometry retention is trimmed by its publishing work or the budget
    // setter, both with staged owners disposed outside this lock.
}

void DataSystem::StageModelAssetRetirementLocked(AssetDepot::ModelAssetRetiredEntries& retired)
{
    assert(retired.descriptors.empty() && retired.skeletons.empty() && retired.animations.empty()
        && retired.meshes.empty() && retired.geometry.empty());
    const auto stage = [&](const auto& cache, auto& consumers)
    {
        assert(consumers.empty());
        std::size_t count{};
        const auto visitConsumers = [&](const auto& flight, const auto& visit)
        {
            for (const auto& weak : flight->consumers)
            {
                visit(weak);
            }
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(cache)>,
                AssetDepot::ModelAssetCache<assets::ModelGeometryPayload>>)
            {
                for (const auto& weak : flight->dependencies.colliderConsumers)
                {
                    visit(weak);
                }
            }
        };
        for (const auto& [key, entry] : cache.entries)
        {
            if (!key.exactGeneration || m_assetPreparationStopping)
            {
                VisitModelFlights(entry.inFlight, [&](const auto& flight)
                {
                    if (flight->status == Status::Pending)
                    {
                        visitConsumers(flight, [&](const auto&)
                        {
                            if (count == consumers.max_size())
                            {
                                throw std::length_error("Model retirement consumer capacity exceeded.");
                            }
                            ++count;
                        });
                    }
                });
            }
        }
        consumers.reserve(count);
        for (const auto& [key, entry] : cache.entries)
        {
            if (!key.exactGeneration || m_assetPreparationStopping)
            {
                VisitModelFlights(entry.inFlight, [&](const auto& flight)
                {
                    if (flight->status == Status::Pending)
                    {
                        visitConsumers(flight, [&](const auto& weak)
                        {
                            if (auto consumer = weak.lock())
                            {
                                consumers.push_back(std::move(consumer));
                            }
                        });
                    }
                });
            }
        }
    };
    stage(m_modelAssets.descriptors, retired.descriptorConsumers);
    stage(m_modelAssets.skeletons, retired.skeletonConsumers);
    stage(m_modelAssets.animations, retired.animationConsumers);
    stage(m_modelAssets.meshes, retired.meshConsumers);
    stage(m_modelAssets.geometry, retired.geometryConsumers);
}

void DataSystem::InvalidateModelAssetsLocked(AssetDepot::ModelAssetRetiredEntries& retired) noexcept
{
    assert(retired.descriptors.empty() && retired.skeletons.empty() && retired.animations.empty()
        && retired.meshes.empty() && retired.geometry.empty());
    const auto status = m_assetPreparationStopping ? Status::Cancelled : Status::Stale;
    const auto error = m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged;
    const auto invalidate = [&](auto& cache, auto& detached, const auto& consumers)
    {
        for (auto item = cache.entries.begin(); item != cache.entries.end();)
        {
            if (item->first.exactGeneration && !m_assetPreparationStopping)
            {
                // Exact-owner requests survive remount from captured locators.
                ++item;
                continue;
            }
            auto node = cache.entries.extract(item++);
            auto& entry = node.mapped();
            cache.retainedBytes -= entry.retainedCharge;
            if (entry.retained)
            {
                ++cache.logicalEvictions;
            }
            VisitModelFlights(entry.inFlight, [&](const auto& flight)
            {
                if (flight->status == Status::Pending)
                {
                    flight->status = status;
                    flight->error = error;
                    flight->message.clear();
                }
            });
            // Node transfer between equal default allocators cannot allocate.
            // The preconstructed destination is empty and all source keys unique.
            const auto inserted = detached.insert(std::move(node));
            if (!inserted.inserted)
            {
                std::terminate();
            }
        }
        for (const auto& consumer : consumers)
        {
            NotifyModelConsumer(consumer, status, error, {});
        }
    };
    invalidate(m_modelAssets.descriptors, retired.descriptors, retired.descriptorConsumers);
    invalidate(m_modelAssets.skeletons, retired.skeletons, retired.skeletonConsumers);
    invalidate(m_modelAssets.animations, retired.animations, retired.animationConsumers);
    invalidate(m_modelAssets.meshes, retired.meshes, retired.meshConsumers);
    invalidate(m_modelAssets.geometry, retired.geometry, retired.geometryConsumers);
}

void DataSystem::SetModelAssetCacheBudgets(std::size_t descriptors, std::size_t skeletons, std::size_t clips)
{
    std::lock_guard lock(m_assetPreparationMutex);
    m_modelAssets.descriptors.budgetBytes = descriptors;
    m_modelAssets.skeletons.budgetBytes = skeletons;
    m_modelAssets.animations.budgetBytes = clips;
    TrimModelAssetsLocked();
}

AssetDepot::ModelAssetCacheSnapshot DataSystem::SnapshotModelAssetCache() const
{
    std::lock_guard lock(m_assetPreparationMutex);
    AssetDepot::ModelAssetCacheSnapshot result;
    const auto append = [&](const auto& cache)
    {
        result.entries += cache.entries.size();
        result.retainedChargeBytes = AddModelCharge(result.retainedChargeBytes, cache.retainedBytes);
        result.budgetBytes = AddModelCharge(result.budgetBytes, cache.budgetBytes);
        result.logicalEvictions += cache.logicalEvictions;
        for (const auto& [key, entry] : cache.entries)
        {
            result.liveEntries += entry.live.lock() ? 1u : 0u;
            result.retainedEntries += entry.retained ? 1u : 0u;
            VisitModelFlights(entry.inFlight, [&](const auto& flight)
            {
                result.inFlight += flight->status == Status::Pending ? 1u : 0u;
            });
        }
    };
    append(m_modelAssets.descriptors);
    append(m_modelAssets.skeletons);
    append(m_modelAssets.animations);
    append(m_modelAssets.meshes);
    append(m_modelAssets.geometry);
    result.meshDescriptorRetainedBytes = m_modelAssets.meshes.retainedBytes;
    result.geometryRetainedBytes = m_modelAssets.geometry.retainedBytes;
    result.geometryBudgetBytes = m_modelAssets.geometry.budgetBytes;
    for (const auto& [key, entry] : m_modelAssets.meshes.entries)
    {
        if (const auto owner = entry.live.lock())
        {
            result.meshDescriptorLiveBytes = AddModelCharge(result.meshDescriptorLiveBytes, owner->metadataBytes);
            if (owner->skeleton)
            {
                result.skeletonDependencyBytes = AddModelCharge(result.skeletonDependencyBytes,
                    owner->skeleton->ByteSize());
            }
        }
    }
    for (const auto& [key, entry] : m_modelAssets.geometry.entries)
    {
        if (const auto owner = entry.live.lock())
        {
            result.geometryLiveBytes = AddModelCharge(result.geometryLiveBytes, owner->ByteSize());
        }
        VisitModelFlights(entry.inFlight, [&](const auto& flight)
        {
            result.geometryInFlight += flight->status == Status::Pending ? 1u : 0u;
        });
    }
    return result;
}

// Current-link dispatch returns logical descriptors, never raw geometry.
#define INSTANTIATE_MODEL_ASSET(Type) \
    template own::shared_owner<const Type> DataSystem::TryAcquireCurrentModelAsset(AssetDepot::AssetLink<Type>); \
    template AssetDepot::AssetRequest<Type> DataSystem::RequestCurrentModelAssetAsync(AssetDepot::AssetLink<Type>); \
    template AssetDepot::AssetRequest<Type> DataSystem::RequestModelAssetFromSnapshot(AssetDepot::AssetLink<Type>, \
        own::shared_owner<const experiment::cooked::CookedAssetCatalog>, std::uint64_t);
INSTANTIATE_MODEL_ASSET(assets::ModelAnimationDescriptor)
INSTANTIATE_MODEL_ASSET(assets::ModelSkeletonPayload)
INSTANTIATE_MODEL_ASSET(assets::ModelAnimationPayload)
INSTANTIATE_MODEL_ASSET(assets::ModelMeshDescriptor)
#undef INSTANTIATE_MODEL_ASSET

// Full model scene preparation uses the same compatible raw work as descriptors.
template AssetDepot::AssetRequest<assets::ModelGeometryPayload> DataSystem::RequestResolvedModelAssetAsync(
    experiment::cooked::ResolvedAssetEntry, own::shared_owner<const experiment::cooked::CookedAssetCatalog>,
    bool, std::uint64_t, experiment::cooked::ResolvedAssetEntry);
