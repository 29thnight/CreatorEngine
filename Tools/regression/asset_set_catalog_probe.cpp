#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "Experiment/Cooked/ArtifactByteSource.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace
{
    namespace Cooked = experiment::cooked;

    struct SourceCounters final
    {
        unsigned captureCalls{};
        unsigned sizeCalls{};
        unsigned readCalls{};
        unsigned destructions{};
    };

    class CountingByteSource final : public Cooked::ArtifactByteSource
    {
    public:
        explicit CountingByteSource(own::shared_owner<SourceCounters> counters)
            : m_counters(std::move(counters))
        {
        }

        ~CountingByteSource() override { ++m_counters->destructions; }

        [[nodiscard]] bool CaptureArtifact(std::string_view path,
            own::shared_owner<const Cooked::ArtifactByteSource>& narrowed,
            std::string& failure) const override
        {
            ++m_counters->captureCalls;
            return ArtifactByteSource::CaptureArtifact(path, narrowed, failure);
        }

        [[nodiscard]] bool Size(std::string_view, std::uint64_t& out, std::string& failure) const override
        {
            ++m_counters->sizeCalls;
            out = 1u;
            failure.clear();
            return true;
        }

        [[nodiscard]] bool ReadAt(std::string_view, std::uint64_t, std::span<std::byte> out,
            std::string& failure) const override
        {
            ++m_counters->readCalls;
            std::ranges::fill(out, std::byte{ 0x5a });
            failure.clear();
            return true;
        }

    private:
        own::shared_owner<SourceCounters> m_counters;
    };

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    [[nodiscard]] experiment::AssetId MakeId(std::uint16_t seed)
    {
        experiment::AssetId id;
        id.value.data[0] = static_cast<std::uint8_t>(seed >> 8u);
        id.value.data[6] = 0x40u;
        id.value.data[8] = 0x80u;
        id.value.data[15] = static_cast<std::uint8_t>(seed);
        return id;
    }

    [[nodiscard]] Cooked::TypedAssetReference Link(std::uint16_t seed,
        Cooked::CookedAssetKind kind = Cooked::CookedAssetKind::Texture)
    {
        return { { MakeId(seed), {} }, kind };
    }

    [[nodiscard]] Cooked::AssetSetMountOptions Options()
    {
        return { "probe-platform", "probe-abi", {} };
    }

    [[nodiscard]] Cooked::AssetSetManifest MakeManifest(std::uint16_t setId,
        Cooked::TypedAssetReference asset, std::uint8_t content,
        std::vector<Cooked::AssetDependency> dependencies = {})
    {
        Cooked::AssetSetManifest manifest;
        manifest.assetSetId = MakeId(setId);
        manifest.revision = 1u;
        manifest.targetPlatform = "probe-platform";
        manifest.targetAbi = "probe-abi";
        Cooked::AssetBlobRecord blob;
        blob.contentSha256.fill(content);
        blob.byteSize = 1u;
        blob.kind = asset.kind;
        blob.representation = 1u;
        blob.schemaVersion = 1u;
        blob.targetPlatform = manifest.targetPlatform;
        blob.targetAbi = manifest.targetAbi;
        blob.artifactPath = "Derived/Probe/" + std::to_string(setId) + ".bin";
        manifest.blobs.push_back(std::move(blob));
        manifest.entries.push_back({ asset, 0u, std::move(dependencies) });
        manifest.roots.push_back(asset);
        return manifest;
    }

    [[nodiscard]] Cooked::CookedAssetCatalog Mount(const Cooked::CookedAssetCatalog& previous,
        const Cooked::AssetSetManifest& manifest, own::shared_owner<const Cooked::ArtifactByteSource> source,
        std::uint64_t mountId, std::uint64_t revision, const Cooked::AssetSetMountOptions& options = Options())
    {
        Cooked::CookedAssetCatalog next;
        std::vector<Cooked::AssetManifestIssue> issues;
        Require(previous.WithMountedAssetSet(manifest, std::move(source), { mountId }, revision, options, next, issues),
            issues.empty() ? "mount failed" : issues.front().message.c_str());
        return next;
    }

    [[nodiscard]] Cooked::CookedAssetCatalog Unmount(const Cooked::CookedAssetCatalog& previous,
        std::uint64_t mountId, std::uint64_t revision)
    {
        Cooked::CookedAssetCatalog next;
        std::vector<Cooked::AssetManifestIssue> issues;
        Require(previous.WithoutMountedAssetSet({ mountId }, revision, next, issues), "unmount failed");
        return next;
    }

    [[nodiscard]] std::vector<Cooked::ResolvedAssetEntry> Closure(const Cooked::CookedAssetCatalog& catalog,
        const Cooked::TypedAssetReference& root)
    {
        std::vector<Cooked::ResolvedAssetEntry> result;
        Cooked::AssetCatalogLookupIssue issue;
        Require(catalog.CollectHardClosure(root, result, issue) == Cooked::AssetLookupStatus::Found,
            "hard closure failed");
        return result;
    }

    void VerifyTransactionsAndClosures()
    {
        const auto counters = own::make_shared<SourceCounters>();
        const own::shared_owner<const Cooked::ArtifactByteSource> source =
            own::make_shared<CountingByteSource>(counters);
        const auto a = Link(1u, Cooked::CookedAssetKind::Material);
        const auto b = Link(2u);
        const auto lazy = Link(3u);
        const auto bManifest = MakeManifest(101u, b, 11u);
        const auto aManifest = MakeManifest(102u, a, 12u,
            { { b, Cooked::AssetDependencyKind::Hard, Cooked::AssetDependencyScope::External },
                { lazy, Cooked::AssetDependencyKind::Loadable, Cooked::AssetDependencyScope::External } });
        const Cooked::CookedAssetCatalog empty;
        const auto onlyB = Mount(empty, bManifest, source, 10u, 1u);
        const auto original = Mount(onlyB, aManifest, source, 20u, 2u);
        Require(empty.MountCount() == 0u && onlyB.MountCount() == 1u && original.MountCount() == 2u,
            "candidate mount mutated an older snapshot");
        Require(original.IsEmpty() && original.Size() == 0u && original.Find(a.key.assetId) == nullptr,
            "v3 mount changed the legacy v2 lookup surface");
        Require(original.ListRoots({ 20u }, a.kind) == std::vector{ a }, "typed roots differ");
        Require(original.ListRoots({ 20u }, b.kind).empty(), "root listing ignored expected kind");
        Require(original.ListRoots({ 999u }, a.kind).empty(), "unknown mount listed roots");
        auto originalClosure = Closure(original, a);
        Require(originalClosure.size() == 2u && originalClosure[0].entry.asset == b
            && originalClosure[1].entry.asset == a, "hard closure loaded a lazy edge or ordered the root first");
        Require(originalClosure[0].resolverRevision == 2u && originalClosure[1].resolverRevision == 2u,
            "hard closure mixed resolver revisions");

        Cooked::CookedAssetCatalog unchanged = original;
        std::vector<Cooked::AssetManifestIssue> issues;
        Require(!empty.WithMountedAssetSet(aManifest, source, { 1u }, 1u, Options(), unchanged, issues),
            "missing external hard dependency mounted");
        Require(unchanged.ResolverRevision() == 2u && unchanged.MountCount() == 2u,
            "failed candidate replaced the output catalog");
        Require(!original.WithMountedAssetSet(bManifest, source, { 30u }, 2u, Options(), unchanged, issues),
            "non-advancing resolver revision accepted");
        Require(!original.WithMountedAssetSet(bManifest, source, { 10u }, 3u, Options(), unchanged, issues),
            "duplicate mount ID accepted");
        Require(!original.WithMountedAssetSet(bManifest, {}, { 30u }, 3u, Options(), unchanged, issues),
            "mount accepted no source owner");
        auto wrongTarget = Options();
        wrongTarget.expectedTargetAbi = "other-abi";
        Require(!original.WithMountedAssetSet(bManifest, source, { 30u }, 3u, wrongTarget, unchanged, issues),
            "manifest approved its own incompatible target ABI");
        Require(!unchanged.WithMountedAssetSet(bManifest, source, { 30u }, 3u, Options(), unchanged, issues),
            "immutable candidate accepted in-place publication");

        auto wrongType = aManifest;
        wrongType.entries[0].dependencies[0].target.kind = Cooked::CookedAssetKind::Mesh;
        Require(!onlyB.WithMountedAssetSet(wrongType, source, { 20u }, 2u, Options(), unchanged, issues),
            "external hard edge ignored expected type");
        auto wrongLoadableType = aManifest;
        wrongLoadableType.entries[0].dependencies[0].kind = Cooked::AssetDependencyKind::Loadable;
        wrongLoadableType.entries[0].dependencies[0].target.kind = Cooked::CookedAssetKind::Mesh;
        Require(!onlyB.WithMountedAssetSet(wrongLoadableType, source, { 20u }, 2u,
            Options(), unchanged, issues), "mounted loadable edge ignored a known wrong target type");
        Cooked::ResolvedAssetEntry resolved;
        Require(original.Find(Link(2u, Cooked::CookedAssetKind::Mesh), resolved)
            == Cooked::AssetLookupStatus::TypeMismatch
            && !resolved.byteSource, "typed lookup returned mismatched data or retained an old result");

        auto changedSchema = bManifest;
        changedSchema.blobs[0].schemaVersion = 2u;
        Require(!original.WithMountedAssetSet(changedSchema, source, { 30u }, 3u,
            Options(), unchanged, issues), "same digest merged an incompatible schema");
        auto changedRepresentation = bManifest;
        changedRepresentation.blobs[0].representation = 2u;
        Require(!original.WithMountedAssetSet(changedRepresentation, source, { 30u }, 3u,
            Options(), unchanged, issues), "same digest merged an incompatible representation");
        auto changedDependencies = bManifest;
        changedDependencies.entries[0].dependencies.push_back(
            { lazy, Cooked::AssetDependencyKind::Loadable, Cooked::AssetDependencyScope::External });
        Require(!original.WithMountedAssetSet(changedDependencies, source, { 30u }, 3u,
            Options(), unchanged, issues), "same bytes merged different declared dependencies");
        auto changedB = MakeManifest(103u, b, 99u);
        Require(!original.WithMountedAssetSet(changedB, source, { 30u }, 3u, Options(), unchanged, issues),
            "conflicting definition silently used the latest mount");
        Require(!issues.empty() && issues[0].message.find("Existing:") != std::string::npos
            && issues[0].message.find("incoming:") != std::string::npos, "conflict omitted one source locator");
        auto overrideB = Options();
        overrideB.overrideIdentities.push_back(b.key);
        const auto updated = Mount(original, changedB, source, 30u, 3u, overrideB);
        const auto updatedClosure = Closure(updated, a);
        Require(updatedClosure[0].blob.contentSha256[0] == 99u
            && originalClosure[0].blob.contentSha256[0] == 11u
            && updatedClosure[1].blob.contentSha256 == originalClosure[1].blob.contentSha256
            && updatedClosure[1].resolverRevision != originalClosure[1].resolverRevision,
            "unchanged A bytes reused its old B binding after override");
        Require(Closure(original, a)[0].blob.contentSha256[0] == 11u, "override mutated the old snapshot");

        auto cycleB = MakeManifest(104u, b, 55u,
            { { a, Cooked::AssetDependencyKind::Hard, Cooked::AssetDependencyScope::External } });
        Require(!original.WithMountedAssetSet(cycleB, source, { 40u }, 3u, overrideB, unchanged, issues),
            "cross-mount hard ownership cycle accepted");
        Require(!issues.empty() && issues[0].message.find("cycle") != std::string::npos
            && issues[0].message.find(" -> ") != std::string::npos, "cycle diagnostic omitted its path");

        const auto removedB = Unmount(original, 10u, 4u);
        Require(removedB.ListRoots({ 20u }, a.kind) == std::vector{ a }, "unmount erased a dependent mount's roots");
        Cooked::AssetCatalogLookupIssue lookupIssue;
        Require(removedB.CollectHardClosure(a, originalClosure, lookupIssue) == Cooked::AssetLookupStatus::NotMounted
            && originalClosure.empty() && lookupIssue.dependencyPath == std::vector{ a, b },
            "new closure implicitly borrowed the unmounted dependency or exposed a partial result");
        Require(Closure(original, a).size() == 2u, "logical removal invalidated an older closure");
        Require(!removedB.WithMountedAssetSet(MakeManifest(105u, lazy, 88u), source, { 50u }, 5u,
            Options(), unchanged, issues), "new mount skipped strict validation of the current union");
        Require(counters->captureCalls == 0u && counters->sizeCalls == 0u && counters->readCalls == 0u, "mount/list/closure read artifact bulk");
    }

    void VerifyMergedOverrideAndValueIndices()
    {
        const auto counters = own::make_shared<SourceCounters>();
        const own::shared_owner<const Cooked::ArtifactByteSource> source =
            own::make_shared<CountingByteSource>(counters);
        const auto asset = Link(9u);
        const auto baseManifest = MakeManifest(201u, asset, 1u);
        Cooked::CookedAssetCatalog empty;
        const auto base = Mount(empty, baseManifest, source, 1u, 1u);
        auto duplicate = baseManifest;
        duplicate.assetSetId = MakeId(202u);
        duplicate.blobs[0].artifactPath = "Derived/Elsewhere/identical.bin";
        const auto merged = Mount(base, duplicate, source, 2u, 2u);
        Require(merged.MountCount() == 2u && merged.MountedAssetCount() == 1u, "identical definitions did not merge");

        auto replacement = MakeManifest(203u, asset, 2u);
        auto options = Options();
        options.overrideIdentities.push_back(asset.key);
        const auto overridden = Mount(merged, replacement, source, 3u, 3u, options);
        replacement.assetSetId = MakeId(204u);
        replacement.blobs[0].artifactPath = "Derived/Elsewhere/replacement.bin";
        const auto mergedOverride = Mount(overridden, replacement, source, 4u, 4u);
        const auto removedOverrideSource = Unmount(mergedOverride, 3u, 5u);
        Cooked::ResolvedAssetEntry resolved;
        Require(removedOverrideSource.Find(asset, resolved) == Cooked::AssetLookupStatus::Found
            && resolved.mountId.value == 4u && resolved.blob.contentSha256[0] == 2u,
            "merged override lost precedence when its original source was unmounted");
        auto restored = Unmount(removedOverrideSource, 4u, 6u);
        Require(restored.Find(asset, resolved) == Cooked::AssetLookupStatus::Found
            && resolved.blob.contentSha256[0] == 1u, "removing all overrides did not restore the prior declaration");

        // Reallocation, snapshot copy, move, and removal all preserve index-based
        // resolution. No stored entry pointer may refer into an earlier vector.
        for (std::uint16_t index = 0u; index < 64u; ++index)
        {
            const auto next = Mount(restored, MakeManifest(static_cast<std::uint16_t>(1000u + index),
                Link(static_cast<std::uint16_t>(2000u + index)), 3u), source, 100u + index, 7u + index);
            restored = next;
        }
        Cooked::CookedAssetCatalog copied = restored;
        Cooked::CookedAssetCatalog moved = std::move(copied);
        Require(moved.Find(asset, resolved) == Cooked::AssetLookupStatus::Found
            && resolved.blob.contentSha256[0] == 1u, "copy/move/reallocation invalidated a mounted entry");
        Require(counters->captureCalls == 0u && counters->sizeCalls == 0u && counters->readCalls == 0u, "index rebuild performed bulk I/O");
    }

    void VerifyAbsentModelLoadableRemainsMetadataOnly()
    {
        const auto counters = own::make_shared<SourceCounters>();
        const own::shared_owner<const Cooked::ArtifactByteSource> source =
            own::make_shared<CountingByteSource>(counters);
        const auto model = Link(301u, Cooked::CookedAssetKind::Model);
        const auto skeleton = Link(302u, Cooked::CookedAssetKind::Skeleton);
        const auto present = Link(303u, Cooked::CookedAssetKind::AnimationClip);
        const auto absent = Link(304u, Cooked::CookedAssetKind::AnimationClip);
        const Cooked::CookedAssetCatalog empty;
        const auto withSkeleton = Mount(empty, MakeManifest(601u, skeleton, 21u), source, 1u, 1u);
        const auto withClip = Mount(withSkeleton, MakeManifest(602u, present, 22u,
            { { skeleton, Cooked::AssetDependencyKind::Hard, Cooked::AssetDependencyScope::External } }),
            source, 2u, 2u);
        auto descriptorManifest = MakeManifest(603u, model, 23u,
            { { skeleton, Cooked::AssetDependencyKind::Loadable, Cooked::AssetDependencyScope::External },
                { present, Cooked::AssetDependencyKind::Loadable, Cooked::AssetDependencyScope::External },
                { absent, Cooked::AssetDependencyKind::Loadable, Cooked::AssetDependencyScope::External } });
        descriptorManifest.blobs[0].representation = 2u; // Model descriptor, not the legacy full model.
        const auto mounted = Mount(withClip, descriptorManifest, source, 3u, 3u);
        Require(mounted.ListRoots({ 3u }, model.kind) == std::vector{ model }
            && Closure(mounted, model).size() == 1u,
            "absent optional clip prevented model root enumeration or entered its hard closure");
        Cooked::ResolvedAssetEntry child;
        Require(mounted.Find(present, child) == Cooked::AssetLookupStatus::Found && child.byteSource,
            "present model Loadable lost its snapshot metadata/source owner");
        Require(mounted.Find(absent, child) == Cooked::AssetLookupStatus::NotMounted && !child.byteSource,
            "missing model Loadable was not reported as metadata absence");
        const auto removedSkeleton = Unmount(mounted, 1u, 4u);
        Require(Closure(removedSkeleton, model).size() == 1u,
            "missing optional child's hard skeleton blocked the model metadata root");
        Require(removedSkeleton.Find(present, child) == Cooked::AssetLookupStatus::Found,
            "logical skeleton removal erased an independently mounted clip's metadata");
        std::vector<Cooked::ResolvedAssetEntry> selectedClosure;
        Cooked::AssetCatalogLookupIssue issue;
        Require(removedSkeleton.CollectHardClosure(present, selectedClosure, issue)
            == Cooked::AssetLookupStatus::NotMounted && selectedClosure.empty(),
            "selected clip bypassed its absent hard skeleton dependency");
        Require(counters->captureCalls == 0u && counters->sizeCalls == 0u && counters->readCalls == 0u,
            "unselected model Loadable metadata enumeration captured or read child files");
    }

    void VerifyGenericArtifactPathsAndLooseRanges()
    {
        Require(Cooked::IsArtifactVirtualPath("Derived/AssetBlobs/compat/content.png"),
            "generic source rejected an AssetSet content-addressed texture path");
        for (const std::string_view path : { "Derived/", "Derived//a.png", "Derived/../a.png",
            "Derived/./a.png", "/Derived/a.png", "Derived/a:stream.png", "Derived/a\\b.png" })
        {
            Require(!Cooked::IsArtifactVirtualPath(path), "generic source accepted an unsafe artifact path");
        }
        Require(!Cooked::IsArtifactVirtualPath(std::string("Derived/a\0b.png", 15u)),
            "generic source accepted an embedded NUL");
        Require(!Cooked::IsArtifactVirtualPath("Derived/\xc0\xaf.png"),
            "generic source accepted overlong UTF-8");
        Require(!Cooked::IsArtifactVirtualPath("Derived/\xed\xa0\x80.png"),
            "generic source accepted a UTF-8 surrogate");

        struct ScratchDirectory final
        {
            std::filesystem::path path;
            bool created{};
            ~ScratchDirectory()
            {
                if (created)
                {
                    std::error_code error;
                    std::filesystem::remove_all(path, error);
                }
            }
        };
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        ScratchDirectory scratch{ std::filesystem::temp_directory_path()
            / ("asset-set-source-probe-" + std::to_string(nonce)) };
        scratch.created = std::filesystem::create_directory(scratch.path);
        Require(scratch.created, "loose source fixture directory already exists");
        const std::string virtualPath = "Derived/AssetBlobs/compat/content.png";
        const auto file = scratch.path / virtualPath;
        std::filesystem::create_directories(file.parent_path());
        {
            std::ofstream output(file, std::ios::binary);
            const char bytes[] = { 0x11, 0x22, 0x33, 0x44 };
            output.write(bytes, sizeof(bytes));
            Require(output.good(), "loose source fixture was not written");
        }
        const std::string siblingPath = "Derived/AssetBlobs/compat/sibling.png";
        const auto siblingFile = scratch.path / siblingPath;
        std::filesystem::copy_file(file, siblingFile);
        // Declared after scratch, so all OS handles close before cleanup.
        own::shared_owner<const Cooked::ArtifactByteSource> source =
            own::make_shared<Cooked::LooseArtifactByteSource>(scratch.path);
        own::weak_owner<const Cooked::ArtifactByteSource> weakMount(source);
        std::uint64_t size{};
        std::string failure;
        Require(source->Size(virtualPath, size, failure) && size == 4u, "generic loose source size failed");
        std::byte range[2]{};
        Require(source->ReadAt(virtualPath, 1u, range, failure)
            && range[0] == std::byte{ 0x22 } && range[1] == std::byte{ 0x33 }, "generic loose range read failed");
        Require(!source->ReadAt(virtualPath, 3u, range, failure), "loose source allowed an out-of-bounds read");
        Require(!source->Size("Derived/../outside.png", size, failure), "loose source allowed traversal");
        own::shared_owner<const Cooked::ArtifactByteSource> exact;
        own::shared_owner<const Cooked::ArtifactByteSource> recaptured;
        own::shared_owner<const Cooked::ArtifactByteSource> sibling;
        Require(source->CaptureArtifact(virtualPath, exact, failure) && exact,
            "loose source did not return an exact artifact owner");
        Require(source->CaptureArtifact(virtualPath, recaptured, failure) && recaptured,
            "same artifact could not be independently captured");
        Require(source->CaptureArtifact(siblingPath, sibling, failure) && sibling,
            "sibling artifact could not be independently captured");
        Require(Cooked::CaptureArtifactSource(exact, virtualPath, failure) && exact,
            "recapturing a narrowed source lost its owner");
        Require(!Cooked::CaptureArtifactSource(exact, siblingPath, failure) && exact,
            "exact source allowed capture of a different artifact or lost its existing owner");
        Require(!exact->Size(siblingPath, size, failure)
            && !exact->ReadAt(siblingPath, 0u, range, failure), "exact source read a sibling artifact");
#if defined(__linux__)
        struct stat originalInformation{};
        struct stat siblingInformation{};
        Require(::stat(file.c_str(), &originalInformation) == 0
            && ::stat(siblingFile.c_str(), &siblingInformation) == 0, "file identity fixture stat failed");
        const auto openReferences = [](const struct stat& identity)
        {
            std::size_t count{};
            for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
            {
                struct stat opened{};
                if (::stat(entry.path().c_str(), &opened) == 0
                    && opened.st_dev == identity.st_dev && opened.st_ino == identity.st_ino)
                {
                    ++count;
                }
            }
            return count;
        };
        Require(openReferences(originalInformation) == 1u && openReferences(siblingInformation) == 1u,
            "recapture opened duplicate native handles or lost an independent file");
#endif
#if defined(_WIN32)
        {
            std::fstream writer(file, std::ios::binary | std::ios::in | std::ios::out);
            Require(!writer.is_open(), "captured loose source lost its deny-writer file pin");
        }
#else
        const auto retiredFile = file.string() + ".retired";
        std::filesystem::rename(file, retiredFile);
        {
            std::ofstream replacement(file, std::ios::binary);
            const char bytes[] = { 0x55, 0x66, 0x77, 0x00 };
            replacement.write(bytes, sizeof(bytes));
            Require(replacement.good(), "replacement path fixture was not written");
        }
        Require(exact->ReadAt(virtualPath, 1u, range, failure)
            && range[0] == std::byte{ 0x22 } && range[1] == std::byte{ 0x33 },
            "exact source reopened the replacement path instead of its pinned file");
        std::filesystem::remove(retiredFile);
        Require(recaptured->ReadAt(virtualPath, 1u, range, failure)
            && range[0] == std::byte{ 0x22 } && range[1] == std::byte{ 0x33 },
            "POSIX source lost its pinned inode after unlink");
        const auto pipePath = scratch.path / "Derived/not-an-artifact.pipe";
        Require(::mkfifo(pipePath.c_str(), 0600) == 0, "FIFO rejection fixture was not created");
        Require(!source->Size("Derived/not-an-artifact.pipe", size, failure),
            "loose source accepted a FIFO as an artifact");
        own::shared_owner<const Cooked::ArtifactByteSource> invalid;
        Require(!source->CaptureArtifact("Derived/not-an-artifact.pipe", invalid, failure) && !invalid,
            "loose capture accepted a FIFO as an artifact");
#endif
        exact.reset();
        Require(recaptured->ReadAt(virtualPath, 1u, range, failure)
            && range[0] == std::byte{ 0x22 } && range[1] == std::byte{ 0x33 },
            "dropping one artifact owner invalidated another owner of that same file");
        recaptured.reset();
#if defined(__linux__)
        Require(openReferences(originalInformation) == 0u && openReferences(siblingInformation) == 1u,
            "final artifact owner did not release exactly its native handle while sibling/mount survived");
#endif
#if defined(_WIN32)
        {
            std::fstream writer(file, std::ios::binary | std::ios::in | std::ios::out);
            std::fstream siblingWriter(siblingFile, std::ios::binary | std::ios::in | std::ios::out);
            Require(writer.is_open() && !siblingWriter.is_open(),
                "last artifact owner did not release exactly its file while sibling/mount survived");
        }
#else
        Require(source->ReadAt(virtualPath, 1u, range, failure)
            && range[0] == std::byte{ 0x66 } && range[1] == std::byte{ 0x77 },
            "expired weak lookup reused the retired file instead of a fresh current-path capture");
#endif
        source.reset();
        Require(weakMount.expired(), "single-artifact owner unexpectedly retained the entire mount source");
        Require(sibling->ReadAt(siblingPath, 1u, range, failure)
            && range[0] == std::byte{ 0x22 } && range[1] == std::byte{ 0x33 },
            "independent sibling did not survive mount and other artifact release");
        sibling.reset();
#if defined(__linux__)
        Require(openReferences(siblingInformation) == 0u, "last sibling owner did not close its native handle");
#endif
#if defined(_WIN32)
        std::fstream siblingWriter(siblingFile, std::ios::binary | std::ios::in | std::ios::out);
        Require(siblingWriter.is_open(), "last sibling owner did not release its deny-writer handle");
#endif
    }

    void VerifySourceOwnerSurvivesLogicalRemoval()
    {
        const auto counters = own::make_shared<SourceCounters>();
        Cooked::ResolvedAssetEntry exact;
        const auto asset = Link(50u);
        {
            own::shared_owner<const Cooked::ArtifactByteSource> source = own::make_shared<CountingByteSource>(counters);
            Cooked::CookedAssetCatalog empty;
            auto mounted = Mount(empty, MakeManifest(501u, asset, 1u), source, 1u, 1u);
            Require(mounted.Find(asset, exact) == Cooked::AssetLookupStatus::Found, "exact source was not resolved");
            std::string failure;
            Require(Cooked::CaptureArtifactSource(exact.byteSource, exact.blob.artifactPath, failure)
                && exact.byteSource, "immutable source capture fallback lost its existing owner");
            const auto removed = Unmount(mounted, 1u, 2u);
            source.reset();
            mounted = {};
            Require(removed.MountCount() == 0u && counters->destructions == 0u,
                "unmount destroyed a source still retained by an exact result");
        }
        std::byte byte{};
        std::string failure;
        Require(exact.byteSource->ReadAt(exact.blob.artifactPath, 0u, std::span{ &byte, 1u }, failure)
            && byte == std::byte{ 0x5a }, "old exact source could not finish after logical unmount");
        exact = {};
        Require(counters->destructions == 1u, "retired source was not released after its final owner");
    }
}

int main()
{
    try
    {
        VerifyTransactionsAndClosures();
        VerifyMergedOverrideAndValueIndices();
        VerifySourceOwnerSurvivesLogicalRemoval();
        VerifyAbsentModelLoadableRemainsMetadataOnly();
        VerifyGenericArtifactPathsAndLooseRanges();
        std::cout << "ASSET_SET_CATALOG_OK\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
