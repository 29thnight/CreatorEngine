#include "EditorAudioClipCache.h"
#include "AudioSourceInspection.h"
#include "../../RenderEngine/Assets/AudioClipSourceMetadata.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#if defined(_WIN32)
#include <Windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wave
{
    namespace
    {
        namespace ck = experiment::cooked;
        constexpr std::uint64_t kMaximumEncodedBytes = 1024ull * 1024ull * 1024ull;
        std::atomic<std::uint64_t> g_importSequence{ 0u };

        // Directory-relative operations prevent a project-controlled symlink
        // from redirecting publication outside the selected cache hierarchy.
        class CacheDirectory final : public std::enable_shared_from_this<CacheDirectory>
        {
        public:
            ~CacheDirectory()
            {
                Close();
            }
            void Close() noexcept
            {
#if defined(_WIN32)
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(m_handle);
                    m_handle = INVALID_HANDLE_VALUE;
                }
#else
                if (m_handle >= 0)
                {
                    close(m_handle);
                    m_handle = -1;
                }
#endif
            }
            static std::shared_ptr<CacheDirectory> OpenRoot(const std::filesystem::path& requested, std::string& error)
            {
                std::error_code code;
                const auto absolute = std::filesystem::absolute(requested, code).lexically_normal();
                if (code || absolute.empty())
                {
                    error = "Invalid editor audio cache root";
                    return {};
                }
                auto root = std::make_shared<CacheDirectory>();
                root->m_path = absolute.root_path();
#if defined(_WIN32)
                root->m_handle = OpenWindowsDirectory(root->m_path);
                if (root->m_handle == INVALID_HANDLE_VALUE)
#else
                root->m_handle = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                if (root->m_handle < 0)
#endif
                {
                    error = "Cannot pin editor audio cache filesystem root";
                    return {};
                }
                for (const auto& part : absolute.relative_path())
                {
                    root = root->Child(part, true, false, error);
                    if (!root)
                    {
                        return {};
                    }
                }
                return root;
            }
            std::shared_ptr<CacheDirectory> Child(const std::filesystem::path& name,
                bool create, bool exclusive, std::string& error)
            {
                if (name.empty() || name.has_parent_path() || name == "." || name == "..")
                {
                    error = "Invalid audio cache path component";
                    return {};
                }
                auto child = std::make_shared<CacheDirectory>();
                child->m_path = m_path / name;
                child->m_parent = shared_from_this();
#if defined(_WIN32)
                if (create && !CreateDirectoryW(child->m_path.c_str(), nullptr))
                {
                    if (GetLastError() != ERROR_ALREADY_EXISTS || exclusive)
                    {
                        error = "Cannot exclusively create audio cache directory";
                        return {};
                    }
                }
                child->m_handle = OpenWindowsDirectory(child->m_path);
                if (child->m_handle == INVALID_HANDLE_VALUE)
#else
                if (create && mkdirat(m_handle, name.c_str(), 0700) != 0 && (errno != EEXIST || exclusive))
                {
                    error = "Cannot exclusively create audio cache directory";
                    return {};
                }
                child->m_handle = openat(m_handle, name.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                if (child->m_handle < 0)
#endif
                {
                    error = "Audio cache hierarchy contains a link or inaccessible directory";
                    return {};
                }
                return child;
            }
            std::filesystem::path PinnedPath(const std::filesystem::path& leaf) const
            {
#if defined(_WIN32)
                return m_path / leaf;
#else
                return std::filesystem::path("/proc/self/fd") / std::to_string(m_handle) / leaf;
#endif
            }
            bool Publish(const CacheDirectory& staging, const std::filesystem::path& source,
                const std::filesystem::path& destination, std::string& error) const
            {
#if defined(_WIN32)
                // No MOVEFILE_REPLACE_EXISTING: a competing publisher cannot be clobbered.
                if (MoveFileExW((staging.m_path / source).c_str(), (m_path / destination).c_str(), MOVEFILE_WRITE_THROUGH))
                {
                    return true;
                }
                const auto code = GetLastError();
                if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION)
                {
                    // An existing pinned reader can cause either status. Only
                    // try reading/verifying the existing expected artifact;
                    // never retry a write or weaken hierarchy verification.
                    error = "No-replace audio cache publication failed (Win32 " + std::to_string(code) + ")";
                    return true;
                }
                if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS)
#else
                // linkat is atomic and fails with EEXIST; POSIX rename is not no-clobber.
                if (linkat(staging.m_handle, source.c_str(), m_handle, destination.c_str(), 0) == 0)
                {
                    return true;
                }
                if (errno == EEXIST)
#endif
                {
                    // The caller must verify the winning artifact before accepting it.
                    return true;
                }
                error = "Cannot publish immutable editor audio cache artifact";
                return false;
            }
            void RemoveFile(const std::filesystem::path& leaf) const noexcept
            {
#if defined(_WIN32)
                DeleteFileW((m_path / leaf).c_str());
#else
                unlinkat(m_handle, leaf.c_str(), 0);
#endif
            }
            void RemoveChildDirectory(const std::filesystem::path& leaf) const noexcept
            {
#if defined(_WIN32)
                ::RemoveDirectoryW((m_path / leaf).c_str());
#else
                unlinkat(m_handle, leaf.c_str(), AT_REMOVEDIR);
#endif
            }
#if defined(_WIN32)
            static HANDLE OpenWindowsDirectory(const std::filesystem::path& path)
            {
                const auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                BY_HANDLE_FILE_INFORMATION info{};
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return INVALID_HANDLE_VALUE;
                }
                if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                {
                    CloseHandle(handle);
                    return INVALID_HANDLE_VALUE;
                }
                return handle;
            }
            HANDLE m_handle{ INVALID_HANDLE_VALUE };
#else
            int m_handle{ -1 };
#endif
            std::filesystem::path m_path;
            std::shared_ptr<CacheDirectory> m_parent;
            // Windows path calls are safe only while every ancestor is pinned
            // without FILE_SHARE_DELETE. The importer retains this whole chain.
        };

        class CacheFile final : public ck::ArtifactByteSource
        {
        public:
            ~CacheFile() override
            {
#if defined(_WIN32)
                if (m_handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(m_handle);
                }
#else
                if (m_handle >= 0)
                {
                    close(m_handle);
                }
#endif
            }
            static std::shared_ptr<CacheFile> Open(const CacheDirectory& directory,
                const std::filesystem::path& leaf, bool create, std::string path, std::string& error)
            {
                auto file = std::make_shared<CacheFile>();
                file->m_virtualPath = std::move(path);
#if defined(_WIN32)
                file->m_handle = CreateFileW(directory.PinnedPath(leaf).c_str(), create ? GENERIC_WRITE : GENERIC_READ,
                    FILE_SHARE_READ, nullptr, create ? CREATE_NEW : OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                BY_HANDLE_FILE_INFORMATION info{};
                if (file->m_handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file->m_handle, &info)
                    || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
#else
                file->m_handle = openat(directory.m_handle, leaf.c_str(),
                    (create ? O_WRONLY | O_CREAT | O_EXCL : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW, 0600);
                struct stat info{};
                if (file->m_handle < 0 || fstat(file->m_handle, &info) != 0 || !S_ISREG(info.st_mode))
#endif
                {
                    error = "Audio cache artifact is a link, invalid file, or cannot be exclusively created";
                    return {};
                }
                return file;
            }
            bool Write(std::span<const std::byte> bytes, std::string& error)
            {
                while (!bytes.empty())
                {
#if defined(_WIN32)
                    DWORD count{};
                    if (!WriteFile(m_handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) || count == 0u)
#else
                    const auto count = write(m_handle, bytes.data(), bytes.size());
                    if (count <= 0)
#endif
                    {
                        error = "Cannot write editor audio cache artifact";
                        return false;
                    }
                    bytes = bytes.subspan(static_cast<std::size_t>(count));
                }
                return true;
            }
            bool Flush(std::string& error)
            {
#if defined(_WIN32)
                const bool ok = FlushFileBuffers(m_handle) != 0;
#else
                const bool ok = fsync(m_handle) == 0;
#endif
                if (!ok)
                {
                    error = "Cannot flush editor audio cache artifact";
                }
                return ok;
            }
            bool Size(std::string_view path, std::uint64_t& out, std::string& error) const override
            {
                if (path != m_virtualPath)
                {
                    error = "Audio cache virtual path mismatch";
                    return false;
                }
#if defined(_WIN32)
                LARGE_INTEGER size{};
                if (!GetFileSizeEx(m_handle, &size) || size.QuadPart < 0)
#else
                struct stat size{};
                if (fstat(m_handle, &size) != 0 || size.st_size < 0)
#endif
                {
                    error = "Cannot inspect pinned audio cache artifact";
                    return false;
                }
#if defined(_WIN32)
                out = static_cast<std::uint64_t>(size.QuadPart);
#else
                out = static_cast<std::uint64_t>(size.st_size);
#endif
                return true;
            }
            bool ReadAt(std::string_view path, std::uint64_t offset,
                std::span<std::byte> out, std::string& error) const override
            {
                std::uint64_t size{};
                if (!Size(path, size, error) || offset > size || out.size() > size - offset)
                {
                    error = "Pinned audio cache read exceeds extent";
                    return false;
                }
#if defined(_WIN32)
                std::lock_guard lock(m_readMutex);
                LARGE_INTEGER position{};
                position.QuadPart = static_cast<LONGLONG>(offset);
                if (!SetFilePointerEx(m_handle, position, nullptr, FILE_BEGIN))
                {
                    error = "Cannot seek pinned audio cache artifact";
                    return false;
                }
#endif
                while (!out.empty())
                {
#if defined(_WIN32)
                    DWORD count{};
                    if (!ReadFile(m_handle, out.data(), static_cast<DWORD>(out.size()), &count, nullptr) || count == 0u)
#else
                    const auto count = pread(m_handle, out.data(), out.size(), static_cast<off_t>(offset));
                    if (count <= 0)
#endif
                    {
                        error = "Cannot read pinned audio cache artifact";
                        return false;
                    }
                    out = out.subspan(static_cast<std::size_t>(count));
                    offset += static_cast<std::uint64_t>(count);
                }
                return true;
            }
        private:
#if defined(_WIN32)
            HANDLE m_handle{ INVALID_HANDLE_VALUE };
            mutable std::mutex m_readMutex;
#else
            int m_handle{ -1 };
#endif
            std::string m_virtualPath;
        };

        struct ImportCleanup final
        {
            std::shared_ptr<CacheDirectory> root;
            std::shared_ptr<CacheDirectory> staging;
            std::filesystem::path name;
            std::filesystem::path snapshot;
            ~ImportCleanup()
            {
                staging->RemoveFile(snapshot);
                staging->RemoveFile("artifact.ceac");
                staging->Close();
                staging.reset();
                root->RemoveChildDirectory(name);
            }
        };

        bool CopyBounded(const std::filesystem::path& source, CacheFile& output,
            std::uint64_t size, const Hash::Sha256Digest& expected, std::string& error)
        {
            std::ifstream input(source, std::ios::binary);
            Hash::Sha256 hash;
            std::array<std::byte, 64u * 1024u> block{};
            std::uint64_t remaining = size;
            while (input && remaining != 0u)
            {
                const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, block.size()));
                input.read(reinterpret_cast<char*>(block.data()), count);
                if (input.gcount() != count || !output.Write(std::span(block.data(), static_cast<std::size_t>(count)), error))
                {
                    break;
                }
                hash.Update(block.data(), static_cast<std::size_t>(count));
                remaining -= static_cast<std::uint64_t>(count);
            }
            if (remaining != 0u || input.peek() != std::char_traits<char>::eof()
                || !output.Flush(error) || hash.Finish() != expected)
            {
                error = "Audio source changed during immutable cache snapshot";
                return false;
            }
            return true;
        }
    }

    bool ImportEditorAudioClip(const std::filesystem::path& source,
        const std::filesystem::path& cacheRoot, const ClipKey& key,
        std::string_view loadMode, std::string_view spatialKind,
        ck::CookedAudioClipSource& out, std::string& error)
    {
        error.clear();
        experiment::AssetId id;
        if (cacheRoot.empty() || !key.IsGuid() || !experiment::TryParseCanonicalAssetId(key.Text(), id)
            || !assets::IsAudioLoadMode(loadMode) || !assets::IsAudioSpatialKind(spatialKind))
        {
            error = "Editor audio import identity, cache root or settings are invalid";
            return false;
        }
        assets::AudioClipSourceMetadata original;
        if (!assets::InspectAudioClipSource(source, original, error) || original.payloadSize > kMaximumEncodedBytes)
        {
            error = "Editor audio source is invalid or exceeds 1 GiB: " + error;
            return false;
        }
        std::error_code code;
        const auto sourceTime = std::filesystem::last_write_time(source, code);
        if (code)
        {
            error = "Cannot inspect audio source timestamp: " + code.message();
            return false;
        }
        const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
            + "-" + std::to_string(g_importSequence.fetch_add(1u));
        auto cache = CacheDirectory::OpenRoot(cacheRoot, error);
        if (!cache)
        {
            return false;
        }
        const auto stagingName = std::filesystem::path(".import-" + token);
        auto staging = cache->Child(stagingName, true, true, error);
        if (!staging)
        {
            return false;
        }
        auto snapshotName = std::filesystem::path("source");
        snapshotName += source.extension();
        ImportCleanup cleanup{ cache, staging, stagingName, snapshotName };
        auto snapshotFile = CacheFile::Open(*staging, snapshotName, true, {}, error);
        if (!snapshotFile || !CopyBounded(source, *snapshotFile, original.payloadSize, original.sourceContentHash, error))
        {
            return false;
        }
        snapshotFile.reset();
        const auto snapshot = staging->PinnedPath(snapshotName);
        ClipInfo decoded;
        if (!InspectAudioSource(snapshot, decoded, error))
        {
            return false;
        }
        if (spatialKind == "PointMono" && decoded.channels != 1u)
        {
            error = "PointMono audio import requires a mono source";
            return false;
        }
        assets::AudioClipSourceMetadata current;
        if (!assets::InspectAudioClipSource(source, current, error)
            || current.payloadSize != original.payloadSize || current.sourceContentHash != original.sourceContentHash
            || std::filesystem::last_write_time(source, code) != sourceTime || code)
        {
            error = "Audio source changed during cache decode; retry after the source is saved";
            return false;
        }
        ck::CookedAudioClipHeader metadata;
        metadata.codec = original.codec;
        metadata.loadMode = loadMode == "Stream" ? ck::AudioLoadMode::Stream
            : loadMode == "Resident" ? ck::AudioLoadMode::Resident : ck::AudioLoadMode::Auto;
        metadata.spatialKind = spatialKind == "PointMono" ? ck::AudioSpatialKind::PointMono : ck::AudioSpatialKind::NonSpatial;
        metadata.channels = static_cast<std::uint8_t>(decoded.channels);
        metadata.sampleRate = decoded.sampleRate;
        metadata.frameCount = decoded.frameCount;
        metadata.payloadBytes = original.payloadSize;
        metadata.payloadSha256 = original.sourceContentHash;
        const auto header = ck::WriteAudioClipHeader(metadata);
        ck::CookedAudioClipHeader check;
        if (!ck::ReadAudioClipHeader(header, header.size() + original.payloadSize, check))
        {
            error = "Editor audio import produced invalid CEAC metadata";
            return false;
        }
        // Content and import settings select a new immutable generation. Old
        // playbacks keep their own byte-source root across source edits.
        const auto generationName = Hash::ToHex(original.sourceContentHash)
            + "-" + std::string(loadMode) + "-" + std::string(spatialKind) + "-v1";
        auto destinationDirectory = cache->Child(generationName, true, false, error);
        for (const auto& segment : { std::string("Derived"), std::string("Audio"), key.Text().substr(0u, 2u) })
        {
            if (!destinationDirectory)
            {
                return false;
            }
            destinationDirectory = destinationDirectory->Child(segment, true, false, error);
        }
        if (!destinationDirectory)
        {
            return false;
        }
        ck::CookedAssetManifestEntry entry;
        entry.assetId = id;
        entry.kind = ck::CookedAssetKind::AudioClip;
        entry.formatVersion = ck::kAudioClipArtifactVersion;
        entry.artifactPath = ck::MakeDerivedAudioClipArtifactPath(id);
        entry.byteSize = header.size() + original.payloadSize;
        std::ifstream input(snapshot, std::ios::binary);
        auto output = CacheFile::Open(*staging, "artifact.ceac", true, {}, error);
        if (!output || !output->Write(header, error))
        {
            return false;
        }
        Hash::Sha256 artifactHash;
        artifactHash.Update(header.data(), header.size());
        std::array<std::byte, 64u * 1024u> block{};
        std::uint64_t remaining = original.payloadSize;
        while (input && remaining != 0u)
        {
            const auto count = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, block.size()));
            input.read(reinterpret_cast<char*>(block.data()), count);
            if (input.gcount() != count)
            {
                break;
            }
            if (!output->Write(std::span(block.data(), static_cast<std::size_t>(count)), error))
            {
                return false;
            }
            artifactHash.Update(block.data(), static_cast<std::size_t>(count));
            remaining -= static_cast<std::uint64_t>(count);
        }
        if (remaining != 0u || !output->Flush(error))
        {
            error = "Editor CEAC cache write failed";
            return false;
        }
        output.reset();
        entry.contentSha256 = artifactHash.Finish();
        const auto filename = std::filesystem::path(key.Text() + ".ceac");
        if (!destinationDirectory->Publish(*staging, "artifact.ceac", filename, error))
        {
            return false;
        }
        const auto publicationError = error;
        auto bytes = CacheFile::Open(*destinationDirectory, filename, false, entry.artifactPath, error);
        if (!bytes)
        {
            if (!publicationError.empty())
            {
                error = publicationError + "; existing artifact verification failed: " + error;
            }
            return false;
        }
        const bool accepted = ck::OpenCookedAudioClipEntry(entry, std::move(bytes), out, error);
        if (!accepted && !publicationError.empty())
        {
            error = publicationError + "; existing artifact verification failed: " + error;
        }
        return accepted;
    }
}
