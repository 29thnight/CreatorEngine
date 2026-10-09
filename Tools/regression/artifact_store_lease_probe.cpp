// Unrun regression source. Requires the same catalog/manifest objects as
// asset_set_catalog_probe.cpp. This probe never deletes/repackages real content;
// all mutation is confined to its newly-created temporary fixture directory.
#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "Experiment/Cooked/ArtifactByteSource.h"
#if defined(_WIN32)
#include "Experiment/Cooked/PakAudioClipByteSource.h"
#else
#include <csignal>
#include <sys/wait.h>
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    namespace Cooked = experiment::cooked;
    using AssetDepot::ArtifactStoreAccess;
    using AssetDepot::ArtifactStoreGuard;

    void Require(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void Write(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.close();
        Require(stream.good(), "fixture write failed");
    }

    struct Scratch final
    {
        std::filesystem::path path;
        Scratch()
        {
            path = std::filesystem::temp_directory_path() / ("artifact-store-lease-probe-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            Require(std::filesystem::create_directory(path), "fixture directory already exists");
        }
        ~Scratch()
        {
            // Fixture-only teardown, after every guard/source/process is gone.
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    void PublishLoose(const std::filesystem::path& root)
    {
        ArtifactStoreGuard publication;
        std::string failure;
        Require(ArtifactStoreGuard::BeginPublication(root, publication, failure), failure);
        Write(root / "Derived/root.bin", "root");
        Write(root / "Derived/lazy.bin", "lazy");
        Require(publication.CommitPublication(failure), failure);
    }

    void ExpectBusy(const std::filesystem::path& root)
    {
        ArtifactStoreGuard collection;
        std::string failure;
        Require(ArtifactStoreGuard::TryAcquireCollection(root, collection, failure) == ArtifactStoreAccess::Busy,
            "live source/snapshot did not exclude physical collection");
        Require(!collection.RevalidateCollection(failure), "failed acquisition produced a usable capability");
    }

    class ChildProcess final
    {
    public:
        ChildProcess(const std::filesystem::path& executable, const char* mode,
            const std::filesystem::path& root, const std::filesystem::path& ready = {})
        {
#if defined(_WIN32)
            const std::wstring modeWide(mode, mode + std::char_traits<char>::length(mode));
            std::wstring command = L"\"" + executable.wstring() + L"\" " + modeWide
                + L" \"" + root.wstring() + L"\" \"" + ready.wstring() + L"\"";
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                0u, nullptr, nullptr, &startup, &process) != FALSE, "child process could not start");
            CloseHandle(process.hThread);
            m_process = process.hProcess;
#else
            m_process = ::fork();
            Require(m_process >= 0, "child fork failed");
            if (m_process == 0)
            {
                ::execl(executable.c_str(), executable.c_str(), mode, root.c_str(), ready.c_str(), nullptr);
                ::_exit(127);
            }
#endif
        }
        ChildProcess(const ChildProcess&) = delete;
        ChildProcess& operator=(const ChildProcess&) = delete;
        ~ChildProcess()
        {
            Stop();
        }
        void ExpectSuccess()
        {
#if defined(_WIN32)
            Require(WaitForSingleObject(m_process, 10000u) == WAIT_OBJECT_0, "child completion timed out");
            DWORD code{};
            Require(GetExitCodeProcess(m_process, &code) != FALSE && code == 0u, "child assertion failed");
            CloseHandle(m_process);
            m_process = nullptr;
#else
            int status{};
            bool done{};
            for (unsigned attempt = 0u; attempt < 200u; ++attempt)
            {
                const auto result = ::waitpid(m_process, &status, WNOHANG);
                if (result == m_process)
                {
                    done = true;
                    m_process = -1;
                    break;
                }
                Require(result == 0, "child wait failed");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            Require(done && WIFEXITED(status) && WEXITSTATUS(status) == 0, "child assertion failed or timed out");
#endif
        }
        void Stop()
        {
#if defined(_WIN32)
            if (m_process)
            {
                TerminateProcess(m_process, 0u);
                WaitForSingleObject(m_process, INFINITE);
                CloseHandle(m_process);
                m_process = nullptr;
            }
#else
            if (m_process > 0)
            {
                ::kill(m_process, SIGKILL);
                int ignored{};
                while (::waitpid(m_process, &ignored, 0) < 0 && errno == EINTR)
                {
                }
                m_process = -1;
            }
#endif
        }
    private:
#if defined(_WIN32)
        HANDLE m_process{};
#else
        pid_t m_process{ -1 };
#endif
    };

    experiment::AssetId Id(std::uint8_t seed)
    {
        experiment::AssetId id;
        id.value.data[6] = 0x40u;
        id.value.data[8] = 0x80u;
        id.value.data[15] = seed;
        return id;
    }

    Cooked::AssetSetManifest Manifest()
    {
        Cooked::AssetSetManifest manifest;
        manifest.assetSetId = Id(10u);
        manifest.revision = 1u;
        manifest.targetPlatform = "probe-platform";
        manifest.targetAbi = "probe-abi";
        for (std::uint8_t index = 1u; index <= 2u; ++index)
        {
            Cooked::TypedAssetReference reference{ { Id(index), {} }, Cooked::CookedAssetKind::Texture };
            Cooked::AssetBlobRecord blob;
            blob.contentSha256.fill(index);
            blob.byteSize = 4u;
            blob.kind = reference.kind;
            blob.representation = 1u;
            blob.schemaVersion = 1u;
            blob.targetPlatform = manifest.targetPlatform;
            blob.targetAbi = manifest.targetAbi;
            blob.artifactPath = index == 1u ? "Derived/root.bin" : "Derived/lazy.bin";
            manifest.blobs.push_back(std::move(blob));
            manifest.entries.push_back({ reference, static_cast<std::uint32_t>(index - 1u), {} });
        }
        manifest.roots.push_back(manifest.entries.front().asset);
        manifest.entries.front().dependencies.push_back({ manifest.entries.back().asset,
            Cooked::AssetDependencyKind::Loadable, Cooked::AssetDependencyScope::Internal });
        return manifest;
    }

#if defined(__linux__)
    std::size_t OpenFileCount(const std::filesystem::path& path)
    {
        struct stat expected{};
        Require(::stat(path.c_str(), &expected) == 0, "file descriptor fixture stat failed");
        std::size_t count{};
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
        {
            struct stat opened{};
            if (::stat(entry.path().c_str(), &opened) == 0
                && opened.st_dev == expected.st_dev && opened.st_ino == expected.st_ino)
            {
                ++count;
            }
        }
        return count;
    }
#endif

    void VerifySnapshotsAndLazyOrigins(const std::filesystem::path& root,
        const std::filesystem::path& executable)
    {
        const auto manifest = Manifest();
        own::shared_owner<const Cooked::ArtifactByteSource> source =
            own::make_shared<Cooked::LooseArtifactByteSource>(root);
        own::weak_owner<const Cooked::ArtifactByteSource> weakSource(source);
        Cooked::CookedAssetCatalog empty;
        Cooked::CookedAssetCatalog mounted;
        std::vector<Cooked::AssetManifestIssue> issues;
        Require(empty.WithMountedAssetSet(manifest, source, { 1u }, 1u,
            { "probe-platform", "probe-abi", {} }, mounted, issues), "fixture mount failed");
        const auto roots = mounted.ListRoots({ 1u }, Cooked::CookedAssetKind::Texture);
        Require(roots.size() == 1u, "metadata root enumeration failed");
        // Catalog and descriptor origins have opened no artifact FILE here.
#if defined(__linux__)
        Require(OpenFileCount(root / "Derived/root.bin") == 0u
            && OpenFileCount(root / "Derived/lazy.bin") == 0u,
            "metadata-only source/mount eagerly opened artifact files");
#endif
        Cooked::ResolvedAssetEntry lazy;
        Require(mounted.Find(manifest.entries.back().asset, lazy) == Cooked::AssetLookupStatus::Found,
            "lazy descriptor source was not resolved");
        ExpectBusy(root);
        ChildProcess denied(executable, "--expect-busy", root);
        denied.ExpectSuccess();
        Cooked::CookedAssetCatalog unmounted;
        Require(mounted.WithoutMountedAssetSet({ 1u }, 2u, unmounted, issues), "logical unmount failed");
        source.reset();
        ExpectBusy(root); // Retired snapshot still retains a shared root guard.
        mounted = {};
        ExpectBusy(root); // Only the never-read lazy descriptor now owns backing.
        std::string failure;
        Require(Cooked::CaptureArtifactSource(lazy.byteSource, lazy.blob.artifactPath, failure),
            "old lazy descriptor could not capture its exact child after unmount");
        Require(weakSource.expired(), "narrowed child retained mount source or sibling file descriptors");
        ExpectBusy(root); // Narrowed source retains the guard, not the root source.
#if defined(__linux__)
        Require(OpenFileCount(root / "Derived/root.bin") == 0u
            && OpenFileCount(root / "Derived/lazy.bin") == 1u,
            "narrowed child opened or retained sibling file descriptors");
#endif
        std::byte bytes[4]{};
        Require(lazy.byteSource->ReadAt(lazy.blob.artifactPath, 0u, bytes, failure)
            && bytes[0] == std::byte{ 'l' }, "exact lazy bytes did not survive logical unmount");
        lazy = {};
        ArtifactStoreGuard collection;
        Require(ArtifactStoreGuard::TryAcquireCollection(root, collection, failure) == ArtifactStoreAccess::Acquired
            && collection.RevalidateCollection(failure), "final owner did not release collection exclusion");
        ArtifactStoreGuard competingReader;
        Require(ArtifactStoreGuard::OpenShared(root, competingReader, failure) == ArtifactStoreAccess::Busy,
            "new source raced through a held collection capability");
        // No actual deletion: the held capability alone is the tested result.
    }

    void VerifyCrossProcessExit(const std::filesystem::path& root,
        const std::filesystem::path& executable, const std::filesystem::path& ready)
    {
        ChildProcess child(executable, "--hold", root, ready);
        for (unsigned attempt = 0u; attempt < 200u && !std::filesystem::exists(ready); ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        Require(std::filesystem::exists(ready), "child did not acquire its shared guard");
        ExpectBusy(root);
        child.Stop(); // OS cleanup must release without C++ destructors/age checks.
        ArtifactStoreGuard collection;
        std::string failure;
        Require(ArtifactStoreGuard::TryAcquireCollection(root, collection, failure) == ArtifactStoreAccess::Acquired,
            "terminated process left a false live lease");
        Require(collection.RevalidateCollection(failure), "post-exit exact capability failed revalidation");
    }

    void VerifyFailClosed(const std::filesystem::path& scratch)
    {
        std::string failure;
        const auto legacy = scratch / "legacy";
        std::filesystem::create_directory(legacy);
        ArtifactStoreGuard legacyReader;
        Require(ArtifactStoreGuard::OpenShared(legacy, legacyReader, failure) == ArtifactStoreAccess::Unmanaged
            && !legacyReader.IsManaged(), "legacy source was silently enrolled");
        Write(legacy / "unrelated-added-file.bin", "legacy");
        Require(legacyReader.ValidateBacking(failure), "unmanaged root metadata changes invalidated legacy reading");
        ArtifactStoreGuard collection;
        Require(ArtifactStoreGuard::TryAcquireCollection(legacy, collection, failure) == ArtifactStoreAccess::Unmanaged
            && !collection.RevalidateCollection(failure), "unenrolled backing was collectible");
        Require(!ArtifactStoreGuard::BeginPublication(legacy, collection, failure),
            "existing legacy backing was retroactively enrolled");
        const auto root = scratch / "replaced";
        PublishLoose(root);
        Require(ArtifactStoreGuard::TryAcquireCollection(root, collection, failure) == ArtifactStoreAccess::Acquired,
            "replacement fixture permit failed");
        std::filesystem::rename(root, scratch / "retired-exact-root");
        std::filesystem::create_directory(root);
        Require(!collection.RevalidateCollection(failure), "exclusive capability accepted a replacement directory");
        collection = {};
        Require(ArtifactStoreGuard::TryAcquireCollection(root, collection, failure) == ArtifactStoreAccess::Invalid,
            "guard record accepted a different backing identity");
        const auto malformed = scratch / "malformed";
        std::filesystem::create_directory(malformed);
        Write(std::filesystem::path(malformed.string() + ".asset-store.guard"), "partial");
        Require(ArtifactStoreGuard::TryAcquireCollection(malformed, collection, failure) == ArtifactStoreAccess::Invalid,
            "incomplete guard record was treated as an expired lease");
#if !defined(_WIN32)
        const auto changedMetadata = scratch / "changed-metadata";
        PublishLoose(changedMetadata);
        Require(ArtifactStoreGuard::TryAcquireCollection(changedMetadata, collection, failure)
            == ArtifactStoreAccess::Acquired, "incarnation fixture permit failed");
        // Ensure even a filesystem with second-resolution ctime sees the change.
        // This changes metadata on the SAME inode, isolating incarnation checks.
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        std::filesystem::last_write_time(changedMetadata,
            std::filesystem::last_write_time(changedMetadata) + std::chrono::seconds(1));
        Require(!collection.RevalidateCollection(failure), "capability ignored changed backing incarnation");
        collection = {};
        Require(ArtifactStoreGuard::TryAcquireCollection(changedMetadata, collection, failure)
            == ArtifactStoreAccess::Invalid, "persistent guard ignored changed backing incarnation");
        const auto alias = scratch / "alias";
        std::filesystem::create_directory_symlink(legacy, alias);
        Require(ArtifactStoreGuard::TryAcquireCollection(alias, collection, failure) == ArtifactStoreAccess::Invalid,
            "symlink backing was accepted");
#endif
    }

#if defined(_WIN32)
    void VerifyPakArchiveOwner(const std::filesystem::path& pak)
    {
        std::string failure;
        {
            ArtifactStoreGuard publication;
            Require(ArtifactStoreGuard::BeginPublication(pak, publication, failure), failure);
            Pak::Builder builder(pak, Pak::BuildOptions{ .encrypt = false, .compress = false });
            const std::array<std::byte, 1u> bytes{ std::byte{ 0x5a } };
            builder.addMemory("Assets/Derived/test.bin", bytes);
            builder.finish();
            Require(publication.CommitPublication(failure), failure);
        }
        auto archive = std::make_shared<Pak::Archive>(pak);
        {
            const auto source = own::make_shared<Cooked::PakAudioClipByteSource>(archive);
            ExpectBusy(pak);
        }
        ExpectBusy(pak); // Archive ownership alone still excludes repack/delete.
        archive.reset();
        ArtifactStoreGuard collection;
        Require(ArtifactStoreGuard::TryAcquireCollection(pak, collection, failure) == ArtifactStoreAccess::Acquired
            && collection.RevalidateCollection(failure), "last archive owner did not release pak backing guard");
    }
#endif

    int Run(const std::filesystem::path& executable, std::string_view mode,
        const std::filesystem::path& root, const std::filesystem::path& ready)
    {
        try
        {
            if (mode == "--expect-busy")
            {
                ExpectBusy(root);
                return 0;
            }
            if (mode == "--hold")
            {
                ArtifactStoreGuard shared;
                std::string failure;
                Require(ArtifactStoreGuard::OpenShared(root, shared, failure) == ArtifactStoreAccess::Acquired,
                    "child shared acquisition failed");
                Write(ready, "ready");
                for (;;)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
            Scratch scratch;
            const auto release = scratch.path / "release";
            PublishLoose(release);
            VerifySnapshotsAndLazyOrigins(release, executable);
            VerifyCrossProcessExit(release, executable, scratch.path / "child.ready");
            VerifyFailClosed(scratch.path);
#if defined(_WIN32)
            VerifyPakArchiveOwner(scratch.path / "immutable.pak");
#endif
            std::cout << "artifact-store lease probe passed\n";
            return 0;
        }
        catch (const std::exception& error)
        {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
}

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    const std::wstring mode = argc > 1 ? argv[1] : L"";
    return Run(std::filesystem::absolute(argv[0]), std::string(mode.begin(), mode.end()),
        argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path{},
        argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path{});
}
#else
int main(int argc, char** argv)
{
    return Run(std::filesystem::absolute(argv[0]), argc > 1 ? argv[1] : "",
        argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path{},
        argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path{});
}
#endif
