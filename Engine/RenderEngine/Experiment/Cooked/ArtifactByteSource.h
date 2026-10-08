#pragma once

#include "../../../Utility_Framework/Ownership.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace experiment::cooked
{
    // Paths are normalized virtual paths under Derived/. Implementations may
    // address a loose cooked tree or an entry inside a mounted pak. Holding the
    // source owner retains its backing; calling CaptureArtifact/Size/ReadAt is
    // explicit I/O and must not occur during metadata-only mount/root lookup.
    class ArtifactByteSource
    {
    public:
        virtual ~ArtifactByteSource() = default;

        // Capture before a multi-call read or before publishing a durable exact
        // locator. A nonempty result narrows ownership to this artifact. Success
        // with an empty result means this source already retains immutable
        // backing (for example a pak or memory image): retain the caller's
        // existing source owner instead. This default is not valid for mutable
        // pathname-only sources. Failure must never fall back to the original.
        // Capture alone reads no payload. It does not validate a typed digest.
        // Pass a separate output; use CaptureArtifactSource below when replacing
        // an existing engine owner so the empty-result fallback stays owned.
        [[nodiscard]] virtual bool CaptureArtifact(std::string_view,
            own::shared_owner<const ArtifactByteSource>& narrowed,
            std::string& failure) const
        {
            failure.clear();
            narrowed.reset();
            return true;
        }
        [[nodiscard]] virtual bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const = 0;
        [[nodiscard]] virtual bool ReadAt(std::string_view path,
            std::uint64_t offset, std::span<std::byte> out,
            std::string& failure) const = 0;
    };

    // Keep the default immutable-source fallback in one place for durable
    // engine-owned locators. The replacement is complete before the root source
    // owner is dropped, and failed capture leaves that existing owner intact.
    [[nodiscard]] inline bool CaptureArtifactSource(
        own::shared_owner<const ArtifactByteSource>& source,
        std::string_view path, std::string& failure)
    {
        if (!source)
        {
            failure = "cooked artifact source owner is absent";
            return false;
        }
        own::shared_owner<const ArtifactByteSource> narrowed;
        if (!source->CaptureArtifact(path, narrowed, failure))
        {
            return false;
        }
        if (narrowed)
        {
            source = std::move(narrowed);
        }
        return true;
    }

    // Generic byte sources validate location, not a particular codec suffix.
    // Typed readers still validate kind/schema and the artifact's own format.
    [[nodiscard]] inline bool IsArtifactVirtualPath(std::string_view path) noexcept
    {
        if (path.size() > 4096u || !path.starts_with("Derived/") || path.size() <= 8u
            || path.back() == '/' || path.find('\\') != std::string_view::npos
            || path.find(':') != std::string_view::npos)
        {
            return false;
        }
        std::size_t begin = 0u;
        while (begin < path.size())
        {
            const std::size_t end = path.find('/', begin);
            const std::string_view segment = path.substr(begin,
                end == std::string_view::npos ? path.size() - begin : end - begin);
            if (segment.empty() || segment == "." || segment == "..")
            {
                return false;
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            begin = end + 1u;
        }
        for (std::size_t index = 0u; index < path.size();)
        {
            const auto first = static_cast<unsigned char>(path[index++]);
            if (first < 0x80u)
            {
                if (first < 0x20u || first == 0x7fu)
                {
                    return false;
                }
                continue;
            }
            std::size_t trailing{};
            std::uint32_t codepoint{};
            std::uint32_t minimum{};
            if (first >= 0xc2u && first <= 0xdfu)
            {
                trailing = 1u;
                codepoint = first & 0x1fu;
                minimum = 0x80u;
            }
            else if (first >= 0xe0u && first <= 0xefu)
            {
                trailing = 2u;
                codepoint = first & 0x0fu;
                minimum = 0x800u;
            }
            else if (first >= 0xf0u && first <= 0xf4u)
            {
                trailing = 3u;
                codepoint = first & 0x07u;
                minimum = 0x10000u;
            }
            else
            {
                return false;
            }
            if (trailing > path.size() - index)
            {
                return false;
            }
            for (std::size_t byte = 0u; byte < trailing; ++byte)
            {
                const auto next = static_cast<unsigned char>(path[index++]);
                if ((next & 0xc0u) != 0x80u)
                {
                    return false;
                }
                codepoint = (codepoint << 6u) | (next & 0x3fu);
            }
            if (codepoint < minimum || codepoint > 0x10ffffu
                || (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            {
                return false;
            }
        }
        return true;
    }

    class LooseArtifactByteSource final : public ArtifactByteSource
    {
    private:
        struct PinnedFile
        {
            std::FILE* file{};
            std::mutex mutex;

            ~PinnedFile()
            {
                if (file)
                {
                    std::fclose(file);
                }
            }
        };

        // The caller holds this file's mutex. Size and type refer to the opened
        // descriptor, never a pathname that may have been replaced meanwhile.
        [[nodiscard]] static bool FileSizeLocked(const PinnedFile& pinned,
            std::uint64_t& out, std::string& failure)
        {
#if defined(_WIN32)
            struct _stat64 information{};
            if (::_fstat64(::_fileno(pinned.file), &information) != 0
                || (information.st_mode & _S_IFMT) != _S_IFREG || information.st_size < 0)
#else
            struct stat information{};
            if (::fstat(::fileno(pinned.file), &information) != 0
                || !S_ISREG(information.st_mode) || information.st_size < 0)
#endif
            {
                failure = "pinned cooked artifact is not a readable regular file";
                return false;
            }
            out = static_cast<std::uint64_t>(information.st_size);
            return true;
        }

        [[nodiscard]] static bool FileSize(const own::shared_owner<PinnedFile>& pinned,
            std::uint64_t& out, std::string& failure)
        {
            std::lock_guard lock(pinned->mutex);
            return FileSizeLocked(*pinned, out, failure);
        }

        [[nodiscard]] static bool ReadFile(const own::shared_owner<PinnedFile>& pinned,
            std::uint64_t offset, std::span<std::byte> out, std::string& failure)
        {
            std::lock_guard lock(pinned->mutex);
            std::uint64_t size{};
            if (!FileSizeLocked(*pinned, size, failure))
            {
                return false;
            }
            if (offset > size || out.size() > size - offset
                || offset > static_cast<std::uint64_t>(
                    (std::numeric_limits<std::streamoff>::max)())
                || out.size() > static_cast<std::size_t>(
                    (std::numeric_limits<std::streamsize>::max)()))
            {
                failure = "cooked artifact read exceeds file extent";
                return false;
            }
            if (out.empty())
            {
                return true;
            }
#if defined(_WIN32)
            const bool positioned = _fseeki64(pinned->file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
            const bool positioned = offset <= static_cast<std::uint64_t>((std::numeric_limits<off_t>::max)())
                && ::fseeko(pinned->file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
            if (!positioned || std::fread(out.data(), 1u, out.size(), pinned->file) != out.size())
            {
                failure = "pinned cooked artifact range cannot be read";
                return false;
            }
            return true;
        }

        // An exact locator owns only its physical file, not the mount source or
        // sibling files. Recapture keeps this already-narrow source owner;
        // no path lookup is ever performed again.
        class SingleArtifactSource final : public ArtifactByteSource
        {
        public:
            SingleArtifactSource(std::string path, own::shared_owner<PinnedFile> file)
                : path_(std::move(path)), file_(std::move(file)) {}

            [[nodiscard]] bool CaptureArtifact(std::string_view path,
                own::shared_owner<const ArtifactByteSource>& narrowed,
                std::string& failure) const override
            {
                if (!Matches(path, failure))
                {
                    narrowed.reset();
                    return false;
                }
                failure.clear();
                narrowed.reset();
                return true;
            }

            [[nodiscard]] bool Size(std::string_view path, std::uint64_t& out,
                std::string& failure) const override
            {
                return Matches(path, failure) && FileSize(file_, out, failure);
            }

            [[nodiscard]] bool ReadAt(std::string_view path, std::uint64_t offset,
                std::span<std::byte> out, std::string& failure) const override
            {
                return Matches(path, failure) && ReadFile(file_, offset, out, failure);
            }

        private:
            [[nodiscard]] bool Matches(std::string_view path, std::string& failure) const
            {
                if (path != path_)
                {
                    failure = "exact cooked source does not contain this artifact";
                    return false;
                }
                return true;
            }

            std::string path_;
            own::shared_owner<PinnedFile> file_;
        };

    public:
        // Replaced manifests must use immutable CAS paths or a fresh source
        // snapshot. A live path pin in this snapshot keeps its old file identity.
        explicit LooseArtifactByteSource(std::filesystem::path root)
            : root_(std::move(root)) {}

        [[nodiscard]] bool CaptureArtifact(std::string_view path,
            own::shared_owner<const ArtifactByteSource>& narrowed,
            std::string& failure) const override
        {
            auto pinned = Pin(path, failure);
            if (!pinned)
            {
                narrowed.reset();
                return false;
            }
            auto candidate = own::make_shared<SingleArtifactSource>(std::string(path), std::move(pinned));
            failure.clear();
            narrowed = std::move(candidate);
            return true;
        }

        // Direct calls are safe one-shot reads, not durable file-identity pins.
        // A caller spanning multiple reads must retain CaptureArtifact's owner.
        [[nodiscard]] bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const override
        {
            const auto pinned = Pin(path, failure);
            return pinned && FileSize(pinned, out, failure);
        }

        [[nodiscard]] bool ReadAt(std::string_view path,
            std::uint64_t offset, std::span<std::byte> out,
            std::string& failure) const override
        {
            const auto pinned = Pin(path, failure);
            return pinned && ReadFile(pinned, offset, out, failure);
        }

    private:
        // Windows denies writes; POSIX retains the opened inode across
        // rename/unlink. In-place external edits remain a caller immutability
        // violation and are rejected when a typed reader rechecks its digest.
        // Mount/root enumeration never calls Pin or opens artifact files.
        [[nodiscard]] own::shared_owner<PinnedFile> Pin(std::string_view path,
            std::string& failure) const
        {
            std::lock_guard lock(filesMutex_);
            // Weak lookup must not accumulate a control block/path for every
            // artifact ever touched. Amortized pruning retains at most one
            // small interval of expired records between new captures.
            if (++pinsSincePrune_ == 64u)
            {
                std::erase_if(files_, [](const auto& item) { return item.second.expired(); });
                pinsSincePrune_ = 0u;
            }
            const std::string key(path);
            if (const auto existing = files_.find(key); existing != files_.end())
            {
                if (auto pinned = existing->second.lock())
                {
                    return pinned;
                }
                files_.erase(existing);
            }
            std::filesystem::path resolved;
            if (!Resolve(path, resolved, failure))
            {
                return {};
            }
            auto pinned = own::make_shared<PinnedFile>();
#if defined(_WIN32)
            pinned->file = _wfsopen(resolved.c_str(), L"rb", _SH_DENYWR);
#else
            // Reject a hostile FIFO/device without blocking before its type can
            // be checked. Validate the opened descriptor, not a pathname stat.
            int flags = O_RDONLY | O_CLOEXEC | O_NONBLOCK;
#if defined(O_NOFOLLOW)
            flags |= O_NOFOLLOW;
#endif
            const int descriptor = ::open(resolved.c_str(), flags);
            if (descriptor < 0)
            {
                failure = "cooked artifact backing cannot be opened";
                return {};
            }
            struct stat information{};
            if (::fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode))
            {
                ::close(descriptor);
                failure = "cooked artifact backing is not a regular file";
                return {};
            }
            pinned->file = ::fdopen(descriptor, "rb");
            if (!pinned->file)
            {
                ::close(descriptor);
            }
#endif
            if (!pinned->file)
            {
                failure = "cooked artifact backing cannot be opened and pinned";
                return {};
            }
            std::uint64_t ignoredSize{};
            if (!FileSize(pinned, ignoredSize, failure))
            {
                return {};
            }
            files_.emplace(key, pinned);
            return pinned;
        }

        mutable std::mutex filesMutex_;
        mutable std::unordered_map<std::string, own::weak_owner<PinnedFile>> files_;
        mutable std::size_t pinsSincePrune_{};
        [[nodiscard]] bool Resolve(std::string_view virtualPath,
            std::filesystem::path& out, std::string& failure) const
        {
            if (!IsArtifactVirtualPath(virtualPath))
            {
                failure = "cooked artifact virtual path is invalid";
                return false;
            }
            std::error_code error;
            const std::filesystem::path root =
                std::filesystem::weakly_canonical(root_, error);
            if (error || root.empty())
            {
                failure = "cooked root cannot be resolved";
                return false;
            }
            const auto* first = reinterpret_cast<const char8_t*>(virtualPath.data());
            const std::filesystem::path relative = std::filesystem::path(
                std::u8string(first, first + virtualPath.size()));
            const std::filesystem::path candidate =
                std::filesystem::weakly_canonical(root / relative, error);
            if (error || candidate.empty())
            {
                failure = "cooked artifact path cannot be resolved";
                return false;
            }
            const std::filesystem::path under = candidate.lexically_relative(root);
            if (under.empty() || under.is_absolute())
            {
                failure = "cooked artifact escapes root";
                return false;
            }
            for (const auto& segment : under)
            {
                if (segment == "..")
                {
                    failure = "cooked artifact escapes root";
                    return false;
                }
            }
            out = candidate;
            return true;
        }

        std::filesystem::path root_{};
    };

}
