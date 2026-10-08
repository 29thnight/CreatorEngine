// Unrun source fixture for a dedicated engine host. No standalone main/target.
// Suspend scheduler dispatch (not worker-side waits) around Begin and Replace;
// resume normal dispatch and tick Poll/ PollReplacement on the host thread.
// Neither the fixture nor production acquisition waits on a job token.
#include "../../Engine/RenderEngine/DataSystem.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedModelSubAssetCodec.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace model_animation_storage_probe
{
    namespace ck = experiment::cooked;
    using Status = AssetDepot::AssetRequestStatus;
    using Error = AssetDepot::AssetRequestError;
    using Skeleton = assets::ModelSkeletonPayload;
    using Clip = assets::ModelAnimationPayload;
    using Descriptor = assets::ModelAnimationDescriptor;

    static_assert(!std::is_copy_constructible_v<Skeleton> && !std::is_move_constructible_v<Skeleton>);
    static_assert(!std::is_copy_constructible_v<Clip> && !std::is_move_constructible_v<Clip>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Skeleton>().skeleton)>>);
    static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Clip>().clip)>>);

    void Require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    experiment::AssetId Id(std::uint8_t seed)
    {
        experiment::AssetId id;
        id.value.data[0] = seed;
        id.value.data[6] = 0x80u;
        id.value.data[8] = 0x80u;
        return id;
    }

    template<class T>
    AssetDepot::AssetLink<T> Link(std::uint8_t seed) { return { { Id(seed), {} } }; }

    void VerifyStorageKeyCompatibility()
    {
        ck::AssetBlobRecord blob;
        blob.contentSha256[0] = 1u;
        blob.byteSize = 4096u;
        blob.kind = ck::CookedAssetKind::Skeleton;
        blob.representation = ck::kSkeletonRepresentation;
        blob.schemaVersion = ck::kSkeletonArtifactVersion;
        blob.targetPlatform = "probe-platform";
        blob.targetAbi = "probe-abi";
        blob.artifactPath = "Derived/first";
        const auto key = AssetDepot::MakeModelStorageKey(blob);
        auto changed = blob;
        changed.artifactPath = "Derived/second";
        Require(AssetDepot::MakeModelStorageKey(changed) == key, "Source locator entered compatible decode key");
        changed = blob; changed.contentSha256[0] ^= 1u;
        Require(AssetDepot::MakeModelStorageKey(changed) != key, "Content identity ignored");
        changed = blob; ++changed.byteSize;
        Require(AssetDepot::MakeModelStorageKey(changed) != key, "Encoded size ignored");
        changed = blob; changed.kind = ck::CookedAssetKind::AnimationClip;
        Require(AssetDepot::MakeModelStorageKey(changed) != key, "Typed decode kind ignored");
        changed = blob; ++changed.representation;
        Require(AssetDepot::MakeModelStorageKey(changed) != key, "Representation ignored");
        changed = blob; ++changed.schemaVersion;
        Require(AssetDepot::MakeModelStorageKey(changed) != key, "Schema ignored");
        changed = blob; changed.targetPlatform += "-other";
        Require(AssetDepot::MakeModelStorageKey(changed) != key, "Platform ignored");
        changed = blob; changed.targetAbi += "-other";
        Require(AssetDepot::MakeModelStorageKey(changed) != key, "ABI ignored");
        Require(AssetDepot::MakeModelStorageKey(blob, 2u) != key, "Decode recipe ignored");
    }

    class CountingSource final : public ck::ArtifactByteSource
    {
    public:
        // Freeze files before mounting; counters alone remain mutable.
        std::map<std::string, std::vector<std::byte>, std::less<>> files;
        DataSystem* dataSystem{}; // Fixture host outlives mounted source and jobs.
        mutable std::atomic<std::size_t> skeletonReads{};
        mutable std::atomic<std::size_t> clipReads{};
        mutable std::atomic<std::size_t> observedInputStaging{};

        bool Size(std::string_view path, std::uint64_t& size, std::string& failure) const override
        {
            const auto found = files.find(path);
            if (found == files.end()) { failure = "Missing probe artifact"; return false; }
            size = found->second.size();
            return true;
        }

        bool ReadAt(std::string_view path, std::uint64_t offset,
            std::span<std::byte> output, std::string& failure) const override
        {
            const auto found = files.find(path);
            if (found == files.end() || offset > found->second.size()
                || output.size() > found->second.size() - offset)
            {
                failure = "Invalid probe read";
                return false;
            }
            if (path.starts_with("Derived/skeleton"))
            {
                ++skeletonReads;
            }
            if (path.starts_with("Derived/clip"))
            {
                ++clipReads;
            }
            // Reentrant read-only snapshot also proves that read/decode is not
            // running under DataSystem's preparation lock.
            const auto snapshot = dataSystem->SnapshotModelAssetCache();
            Require(snapshot.inputStagingBytes >= output.size(), "Active read buffer was not accounted");
            observedInputStaging.store(snapshot.inputStagingBytes);
            std::copy_n(found->second.begin() + static_cast<std::size_t>(offset), output.size(), output.begin());
            return true;
        }
    };

    struct Fixture final
    {
        own::shared_owner<CountingSource> source;
        std::vector<std::byte> manifest;
        ck::AssetSetMountOptions options;
    };

    Fixture MakeFixture(DataSystem& dataSystem, bool changedPose)
    {
        Fixture result;
        result.source = own::make_shared<CountingSource>();
        result.source->dataSystem = &dataSystem;
        // Dedicated host may substitute its actual target strings here.
        result.options = { "probe-platform", "probe-abi", {} };
        ck::AssetSetManifest manifest;
        manifest.assetSetId = Id(30u);
        manifest.revision = changedPose ? 2u : 1u;
        manifest.targetPlatform = result.options.expectedTargetPlatform;
        manifest.targetAbi = result.options.expectedTargetAbi;
        std::string failure;
        ck::SkeletonArtifact skeleton;
        skeleton.skeleton.rootBone = experiment::BoneIndex(0u);
        skeleton.skeleton.bones = { { "Root", {}, math::matrix4x4::identity() } };
        if (changedPose)
        {
            skeleton.skeleton.rootTransform.m[3][0] = 4.0f;
        }
        Require(ck::ComputeBoneLayoutDigest(skeleton.skeleton, skeleton.boneLayoutSha256, failure), "Skeleton layout");
        ck::AnimationClipArtifact clip;
        clip.skeletonAssetId = Id(1u);
        clip.requiredBoneLayoutSha256 = skeleton.boneLayoutSha256;
        clip.requiredBoneCount = 1u;
        clip.clip.name = "Shared";
        clip.clip.durationTicks = 1.0;
        clip.clip.ticksPerSecond = 1.0;
        experiment::AnimationChannel channel;
        channel.bone = experiment::BoneIndex(0u);
        channel.translations = { { 0.0, {} }, { 1.0, { 1.0f, 0.0f, 0.0f } } };
        clip.clip.channels.push_back(channel);
        std::vector<std::byte> skeletonBytes, clipBytes, differentClipBytes, descriptorBytes;
        Require(ck::WriteSkeletonArtifact(skeleton, skeletonBytes, failure), "Skeleton encode");
        Require(ck::WriteAnimationClipArtifact(clip, clipBytes, failure), "Clip encode");
        clip.clip.channels[0].translations[1].value.x = 2.0f;
        Require(ck::WriteAnimationClipArtifact(clip, differentClipBytes, failure), "Different clip encode");
        ck::ModelDescriptorArtifact descriptor;
        descriptor.modelAssetId = Id(10u);
        descriptor.skeletonAssetId = Id(1u);
        descriptor.name = "Storage probe";
        descriptor.clips = { { Id(3u), "Shared", 1.0, 1.0, true }, { Id(4u), "Shared", 1.0, 1.0, true } };
        Require(ck::WriteModelDescriptorArtifact(descriptor, descriptorBytes, failure), "Descriptor encode");
        const auto add = [&](std::uint8_t id, ck::CookedAssetKind kind, std::string path,
            const std::vector<std::byte>& bytes, std::uint32_t representation, std::uint32_t schema,
            std::vector<ck::AssetDependency> dependencies)
        {
            ck::AssetBlobRecord blob;
            blob.kind = kind;
            blob.representation = representation;
            blob.schemaVersion = schema;
            blob.targetPlatform = manifest.targetPlatform;
            blob.targetAbi = manifest.targetAbi;
            blob.artifactPath = path;
            blob.byteSize = bytes.size();
            Require(ck::ComputeSha256(bytes, blob.contentSha256, failure), "Artifact hash");
            const ck::TypedAssetReference asset{ { Id(id), {} }, kind };
            manifest.entries.push_back({ asset, static_cast<std::uint32_t>(manifest.blobs.size()), std::move(dependencies) });
            manifest.roots.push_back(asset);
            manifest.blobs.push_back(std::move(blob));
            result.source->files.emplace(std::move(path), bytes);
        };
        const auto hard = [](std::uint8_t skeletonId)
        {
            return std::vector<ck::AssetDependency>{ { Link<Skeleton>(skeletonId).ToReference(),
                ck::AssetDependencyKind::Hard, ck::AssetDependencyScope::Internal } };
        };
        add(1u, ck::CookedAssetKind::Skeleton, "Derived/skeleton-a", skeletonBytes,
            ck::kSkeletonRepresentation, ck::kSkeletonArtifactVersion, {});
        add(2u, ck::CookedAssetKind::Skeleton, "Derived/skeleton-b", skeletonBytes,
            ck::kSkeletonRepresentation, ck::kSkeletonArtifactVersion, {});
        add(3u, ck::CookedAssetKind::AnimationClip, "Derived/clip-a", clipBytes,
            ck::kAnimationClipRepresentation, ck::kAnimationClipArtifactVersion, hard(1u));
        add(4u, ck::CookedAssetKind::AnimationClip, "Derived/clip-b", clipBytes,
            ck::kAnimationClipRepresentation, ck::kAnimationClipArtifactVersion, hard(1u));
        // Same bytes, but wrong logical hard identity despite compatible bones.
        add(5u, ck::CookedAssetKind::AnimationClip, "Derived/clip-bad-binding", clipBytes,
            ck::kAnimationClipRepresentation, ck::kAnimationClipArtifactVersion, hard(2u));
        add(6u, ck::CookedAssetKind::AnimationClip, "Derived/clip-different", differentClipBytes,
            ck::kAnimationClipRepresentation, ck::kAnimationClipArtifactVersion, hard(1u));
        add(10u, ck::CookedAssetKind::Model, "Derived/model", descriptorBytes,
            ck::kModelDescriptorRepresentation, ck::kModelDescriptorVersion,
            { { Link<Skeleton>(1u).ToReference(), ck::AssetDependencyKind::Loadable, ck::AssetDependencyScope::Internal },
              { Link<Clip>(3u).ToReference(), ck::AssetDependencyKind::Loadable, ck::AssetDependencyScope::Internal },
              { Link<Clip>(4u).ToReference(), ck::AssetDependencyKind::Loadable, ck::AssetDependencyScope::Internal } });
        auto encoded = ck::WriteAssetSetManifest(manifest);
        Require(encoded.Succeeded(), "Manifest encode");
        result.manifest = std::move(encoded.bytes);
        return result;
    }

    template<class T>
    bool Finished(const AssetDepot::AssetRequest<T>& request)
    {
        const auto token = request.Completion();
        return request.Snapshot().status != Status::Pending && (!token.valid() || token.is_complete());
    }

    class Driver final
    {
    public:
        void Begin(DataSystem& dataSystem)
        {
            VerifyStorageKeyCompatibility();
            m_data = &dataSystem;
            m_original = MakeFixture(dataSystem, false);
            std::vector<ck::AssetManifestIssue> issues;
            m_mount = dataSystem.MountAssetSet(m_original.manifest, m_original.source, m_original.options, issues);
            Require(m_mount.IsValid(), "Initial mount");
            m_cancelled = dataSystem.RequestAsync(Link<Clip>(3u));
            m_cancelledToken = m_cancelled.Completion();
            m_cancelled.Cancel();
            Require(m_cancelled.Snapshot().status == Status::Cancelled, "Begin requires dispatch suspension");
            m_skeletonA = dataSystem.RequestAsync(Link<Skeleton>(1u));
            m_skeletonB = dataSystem.RequestAsync(Link<Skeleton>(2u));
            m_clipA = dataSystem.RequestAsync(Link<Clip>(3u));
            m_clipB = dataSystem.RequestAsync(Link<Clip>(4u));
            m_bad = dataSystem.RequestAsync(Link<Clip>(5u));
            m_different = dataSystem.RequestAsync(Link<Clip>(6u));
            m_descriptor = dataSystem.RequestAsync(Link<Descriptor>(10u));
        }

        bool Poll()
        {
            if (!Finished(m_skeletonA) || !Finished(m_skeletonB) || !Finished(m_clipA)
                || !Finished(m_clipB) || !Finished(m_bad) || !Finished(m_different)
                || !Finished(m_descriptor) || !m_cancelledToken.is_complete())
            {
                return false;
            }
            const auto a = m_skeletonA.Snapshot().asset;
            const auto b = m_skeletonB.Snapshot().asset;
            const auto ca = m_clipA.Snapshot().asset;
            const auto cb = m_clipB.Snapshot().asset;
            const auto different = m_different.Snapshot().asset;
            Require(a && b && ca && cb && different, "Ready sibling missing");
            Require(&*a != &*b && &*a->storage == &*b->storage && a->skeleton.skeletonId.IsNil(), "Skeleton sharing boundary");
            Require(a->origin.entry.asset == Link<Skeleton>(1u).ToReference()
                && b->origin.entry.asset == Link<Skeleton>(2u).ToReference()
                && a->origin.blob.artifactPath != b->origin.blob.artifactPath, "Skeleton exact origins merged");
            Require(&*ca != &*cb && &*ca->storage == &*cb->storage && &*ca->storage != &*different->storage, "Clip blob sharing boundary");
            Require(ca->origin.entry.asset == Link<Clip>(3u).ToReference()
                && cb->origin.entry.asset == Link<Clip>(4u).ToReference(), "Clip logical identities merged");
            Require(ca->skeleton->origin.entry.asset == Link<Skeleton>(1u).ToReference()
                && ca->tracks[0] == &ca->clip.tracks[0] && cb->tracks[0] == ca->tracks[0], "Track table ownership");
            Require(m_bad.Snapshot().status == Status::Failed && m_bad.Snapshot().error == Error::DependencyFailed,
                "Shared decode bypassed alias-specific hard binding validation");
            Require(m_cancelled.Snapshot().status == Status::Cancelled, "Subscriber cancellation was overwritten");
            Require(m_original.source->skeletonReads == 1u && m_original.source->clipReads == 2u,
                "Compatible aliases read/decode separately or distinct content was merged");
            m_pinnedDescriptor = m_descriptor.Snapshot().asset;
            Require(m_pinnedDescriptor != nullptr, "Descriptor not ready");
            m_data->SetModelAssetCacheBudgets(0u, 0u, 0u);
            m_data->SetModelAnimationStorageCacheBudgets(0u, 0u);
            const auto snapshot = m_data->SnapshotModelAssetCache();
            Require(snapshot.skeletonStorageRetainedBytes == 0u && snapshot.animationStorageRetainedBytes == 0u
                && snapshot.skeletonStorageLiveBytes == a->storage->ByteSize()
                && snapshot.animationStorageLiveBytes == ca->storage->ByteSize() + different->storage->ByteSize(),
                "Logical eviction was reported as physical release or aliases were double-counted");
            Require(snapshot.inputStagingBytes == 0u && snapshot.decodeStagingChargeBytes == 0u
                && snapshot.completedWorkResultPinBytes == 0u && m_original.source->observedInputStaging > 0u,
                "Staging/result diagnostics did not drain");
            return true;
        }

        void Replace()
        {
            // Poll must have completed and returned. Drop every payload/request
            // pin while retaining only the old metadata descriptor. Dispatch is
            // suspended around this transaction; it is resumed before polling.
            m_skeletonA = {}; m_skeletonB = {}; m_clipA = {}; m_clipB = {};
            m_bad = {}; m_different = {}; m_descriptor = {}; m_cancelled = {};
            const auto empty = m_data->SnapshotModelAssetCache();
            Require(empty.skeletonStorageLiveBytes == 0u && empty.animationStorageLiveBytes == 0u, "Payloads still pinned");
            m_stale = m_data->RequestAsync(Link<Clip>(3u));
            m_staleToken = m_stale.Completion();
            std::vector<ck::AssetManifestIssue> issues;
            Require(m_data->UnmountAssetSet(m_mount, issues), "Unmount");
            Require(m_stale.Snapshot().status == Status::Stale, "Current request did not terminalize Stale");
            Require(!m_data->TryAcquire(Link<Clip>(3u)), "Unmount admitted current lookup");
            m_replacement = MakeFixture(*m_data, true);
            m_mount = m_data->MountAssetSet(m_replacement.manifest, m_replacement.source, m_replacement.options, issues);
            Require(m_mount.IsValid(), "Replacement mount");
            m_oldExact = m_data->RequestAnimationAsync(m_pinnedDescriptor, 0u);
            m_newCurrent = m_data->RequestAsync(Link<Clip>(3u));
        }

        bool PollReplacement()
        {
            if (!Finished(m_oldExact) || !Finished(m_newCurrent) || !m_staleToken.is_complete())
            {
                return false;
            }
            const auto old = m_oldExact.Snapshot().asset;
            const auto current = m_newCurrent.Snapshot().asset;
            Require(old && current && &*old->storage == &*current->storage, "Compatible cross-revision clip storage was not shared");
            Require(old->origin.resolverRevision == m_pinnedDescriptor->origin.resolverRevision
                && old->origin.resolverRevision != current->origin.resolverRevision, "Old wrapper resolved the latest generation");
            Require(old->skeleton->skeleton.rootTransform.m[3][0] == 0.0f
                && current->skeleton->skeleton.rootTransform.m[3][0] == 4.0f, "Hard skeleton generations were conflated");
            Require(m_stale.Snapshot().status == Status::Stale && !m_stale.Snapshot().asset,
                "Shared raw success overwrote terminal Stale");
            Require(m_original.source->clipReads + m_replacement.source->clipReads == 3u
                && m_original.source->skeletonReads + m_replacement.source->skeletonReads == 3u,
                "Exact rehydration or compatible pending-flight sharing failed");
            return true;
        }

    private:
        DataSystem* m_data{};
        Fixture m_original, m_replacement;
        AssetDepot::AssetMountId m_mount;
        AssetDepot::AssetRequest<Skeleton> m_skeletonA, m_skeletonB;
        AssetDepot::AssetRequest<Clip> m_cancelled, m_clipA, m_clipB, m_bad, m_different;
        AssetDepot::AssetRequest<Clip> m_stale, m_oldExact, m_newCurrent;
        AssetDepot::AssetRequest<Descriptor> m_descriptor;
        own::shared_owner<const Descriptor> m_pinnedDescriptor;
        job_handle m_cancelledToken, m_staleToken;
    };
}
