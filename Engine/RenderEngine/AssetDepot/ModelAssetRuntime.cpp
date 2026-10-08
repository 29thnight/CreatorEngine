#include "ModelAssetRuntime.h"
#include "../DataSystem.h"
#include "../Assets/ModelAnimationSampler.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <exception>
#include <limits>
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

    AssetDepot::ModelAssetKey ModelKey(const model_cooked::ResolvedAssetEntry& resolved, bool exact)
    {
        return { resolved.entry.asset, resolved.blob, resolved.resolverRevision, exact };
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

    own::shared_owner<const assets::ModelAnimationDescriptor> DecodeModelDescriptor(
        const model_cooked::ResolvedAssetEntry& resolved, std::span<const std::byte> bytes,
        own::shared_owner<const model_cooked::CookedAssetCatalog> catalog, std::string& failure)
    {
        auto result = own::make_shared<assets::ModelAnimationDescriptor>();
        if (!catalog || !model_cooked::ReadModelDescriptorArtifact(bytes, result->summary, failure))
        {
            return {};
        }
        if (result->summary.modelAssetId != resolved.entry.asset.key.assetId)
        {
            failure = "Model descriptor identity differs from its manifest entry.";
            return {};
        }
        result->origin = resolved;
        result->metadataBytes = AddModelCharge(sizeof(assets::ModelAnimationDescriptor),
            ModelOriginDynamicCharge(result->origin));
        result->metadataBytes = AddModelCharge(result->metadataBytes, result->summary.name.capacity());
        result->metadataBytes = AddModelCharge(result->metadataBytes,
            result->summary.clips.capacity() * sizeof(model_cooked::ModelClipSummary));
        for (const auto& clip : result->summary.clips)
        {
            result->metadataBytes = AddModelCharge(result->metadataBytes, clip.name.capacity());
        }
        if (!result->summary.skeletonAssetId.IsValid())
        {
            // Static model metadata can enumerate zero clips without fabricating
            // a skeleton payload. An explicit skeleton request then fails closed.
            return result;
        }
        const model_cooked::TypedAssetReference skeletonReference{
            { result->summary.skeletonAssetId, {} }, model_cooked::CookedAssetKind::Skeleton };
        const auto hasLoadable = [&](const auto& reference)
        {
            return std::any_of(resolved.entry.dependencies.begin(), resolved.entry.dependencies.end(),
                [&](const auto& dependency)
                {
                    return dependency.kind == model_cooked::AssetDependencyKind::Loadable
                        && dependency.target == reference;
                });
        };
        if (!hasLoadable(skeletonReference)
            || !ResolveModelLoadable(*catalog, skeletonReference, result->skeleton, failure)
            || (result->skeleton.byteSource && !HasNoHardDependencies(result->skeleton)))
        {
            failure = "Model descriptor skeleton metadata is incompatible or not a declared loadable dependency.";
            return {};
        }
        result->clips.reserve(result->summary.clips.size());
        for (const auto& clip : result->summary.clips)
        {
            model_cooked::ResolvedAssetEntry child;
            const model_cooked::TypedAssetReference reference{
                { clip.clipAssetId, {} }, model_cooked::CookedAssetKind::AnimationClip };
            if (!hasLoadable(reference)
                || !ResolveModelLoadable(*catalog, reference, child, failure)
                || (child.byteSource && !HasClipSkeletonDependency(child, skeletonReference)))
            {
                failure = "Model descriptor clip metadata has an invalid declared skeleton dependency.";
                return {};
            }
            result->clips.push_back(std::move(child));
        }
        result->metadataBytes = AddModelCharge(result->metadataBytes,
            result->clips.capacity() * sizeof(model_cooked::ResolvedAssetEntry));
        result->metadataBytes = AddModelCharge(result->metadataBytes, ModelOriginDynamicCharge(result->skeleton));
        for (const auto& clip : result->clips)
        {
            result->metadataBytes = AddModelCharge(result->metadataBytes, ModelOriginDynamicCharge(clip));
        }
        return result;
    }

    template<class T>
    void TrimModelCache(AssetDepot::ModelAssetCache<T>& cache)
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
            oldest->second.retained.reset();
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
    else
    {
        return m_modelAssets.animations;
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
    const auto found = cache.entries.find(ModelKey(resolved, exactGeneration));
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
    if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>)
    {
        if (exactGeneration && !skeleton.byteSource
            && skeleton.entry.asset.kind == model_cooked::CookedAssetKind::Skeleton
            && AssetDepot::AssetLink<assets::ModelSkeletonPayload>{ skeleton.entry.asset.key }.IsValid())
        {
            return fail(Status::Failed, Error::NotMounted);
        }
        if (!ValidateClipDependency(resolved, skeleton))
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

template<class T>
own::shared_owner<AssetDepot::ModelAssetWork<T>> DataSystem::StartModelAssetWorkLocked(
    const model_cooked::ResolvedAssetEntry& resolved,
    const own::shared_owner<const model_cooked::CookedAssetCatalog>& catalog,
    bool exactGeneration, std::uint64_t epoch, const model_cooked::ResolvedAssetEntry& skeleton)
{
    auto& cache = ModelAssetCacheLocked<T>();
    const auto key = ModelKey(resolved, exactGeneration);
    const auto previous = cache.entries.find(key);
    if (previous != cache.entries.end() && previous->second.inFlight)
    {
        previous->second.lastUse = ++m_modelAssets.clock;
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
    if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>)
    {
        work->dependencies.skeletonWork = StartModelAssetWorkLocked<assets::ModelSkeletonPayload>(
            skeleton, catalog, exactGeneration, epoch, {});
        if (work->dependencies.skeletonWork->completion.valid())
        {
            dependencies.push_back(work->dependencies.skeletonWork->completion);
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
        {
            std::lock_guard lock(m_assetPreparationMutex);
            if (work->status != Status::Pending)
            {
                return;
            }
            if (m_assetPreparationStopping
                || (!work->key.exactGeneration && (m_assetInvalidationDepth != 0u
                    || work->epoch != m_assetPreparationEpoch)))
            {
                CompleteModelAssetWorkLocked(work, Status::Cancelled, Error::ShuttingDown);
                return;
            }
            if constexpr (std::is_same_v<T, assets::ModelAnimationPayload>)
            {
                const auto& dependency = work->dependencies.skeletonWork;
                if (!dependency || dependency->status != Status::Ready || !dependency->asset)
                {
                    CompleteModelAssetWorkLocked(work, Status::Failed, Error::DependencyFailed);
                    return;
                }
                skeleton = dependency->asset;
            }
        }
        std::vector<std::byte> bytes;
        std::string failure;
        auto resolved = work->resolved;
        Error error = ReadModelBytes(resolved, bytes, failure);
        own::shared_owner<const T> candidate;
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
            if (!candidate)
            {
                error = Error::DecodeFailed;
            }
        }
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteModelAssetWorkLocked(work, error == Error::None ? Status::Ready : Status::Failed,
            error, std::move(failure), std::move(candidate));
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
    else if (!current || found == cache.entries.end() || !found->second.inFlight
        || found->second.inFlight->requestId != work->requestId)
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
    work->status = status;
    work->error = error;
    work->message = std::move(message);
    work->asset = asset;
    if (found != cache.entries.end() && found->second.inFlight
        && found->second.inFlight->requestId == work->requestId)
    {
        auto& entry = found->second;
        entry.inFlight.reset();
        if (asset)
        {
            entry.live = asset;
            entry.lastUse = ++m_modelAssets.clock;
            const auto charge = asset->ByteSize();
            if (charge <= cache.budgetBytes
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
    TrimModelAssetsLocked();
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
}

void DataSystem::StageModelAssetRetirementLocked(AssetDepot::ModelAssetRetiredEntries& retired)
{
    assert(retired.descriptors.empty() && retired.skeletons.empty() && retired.animations.empty());
    const auto stage = [&](const auto& cache, auto& consumers)
    {
        assert(consumers.empty());
        std::size_t count{};
        for (const auto& [key, entry] : cache.entries)
        {
            if ((!key.exactGeneration || m_assetPreparationStopping)
                && entry.inFlight && entry.inFlight->status == Status::Pending)
            {
                if (entry.inFlight->consumers.size() > consumers.max_size() - count)
                {
                    throw std::length_error("Model retirement consumer capacity exceeded.");
                }
                count += entry.inFlight->consumers.size();
            }
        }
        consumers.reserve(count);
        for (const auto& [key, entry] : cache.entries)
        {
            if ((!key.exactGeneration || m_assetPreparationStopping)
                && entry.inFlight && entry.inFlight->status == Status::Pending)
            {
                for (const auto& weak : entry.inFlight->consumers)
                {
                    if (auto consumer = weak.lock())
                    {
                        consumers.push_back(std::move(consumer));
                    }
                }
            }
        }
    };
    stage(m_modelAssets.descriptors, retired.descriptorConsumers);
    stage(m_modelAssets.skeletons, retired.skeletonConsumers);
    stage(m_modelAssets.animations, retired.animationConsumers);
}

void DataSystem::InvalidateModelAssetsLocked(AssetDepot::ModelAssetRetiredEntries& retired) noexcept
{
    assert(retired.descriptors.empty() && retired.skeletons.empty() && retired.animations.empty());
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
            if (entry.inFlight && entry.inFlight->status == Status::Pending)
            {
                entry.inFlight->status = status;
                entry.inFlight->error = error;
                entry.inFlight->message.clear();
            }
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
            result.inFlight += entry.inFlight ? 1u : 0u;
        }
    };
    append(m_modelAssets.descriptors);
    append(m_modelAssets.skeletons);
    append(m_modelAssets.animations);
    return result;
}

// Public template dispatch is instantiated only for the three concrete types.
#define INSTANTIATE_MODEL_ASSET(Type) \
    template own::shared_owner<const Type> DataSystem::TryAcquireCurrentModelAsset(AssetDepot::AssetLink<Type>); \
    template AssetDepot::AssetRequest<Type> DataSystem::RequestCurrentModelAssetAsync(AssetDepot::AssetLink<Type>);
INSTANTIATE_MODEL_ASSET(assets::ModelAnimationDescriptor)
INSTANTIATE_MODEL_ASSET(assets::ModelSkeletonPayload)
INSTANTIATE_MODEL_ASSET(assets::ModelAnimationPayload)
#undef INSTANTIATE_MODEL_ASSET
