#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#if defined(_WIN32)
#include <cstdio>
#include <io.h>
#include <share.h>
#include <mutex>
#include <unordered_map>
#endif

namespace experiment::cooked
{
    // Paths are normalized virtual paths under Derived/. Implementations may
    // address a loose cooked tree or an entry inside a mounted pak. Holding the
    // source owner retains its backing; calling Size/ReadAt remains explicit I/O.
    class ArtifactByteSource
    {
    public:
        virtual ~ArtifactByteSource() = default;
        [[nodiscard]] virtual bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const = 0;
        [[nodiscard]] virtual bool ReadAt(std::string_view path,
            std::uint64_t offset, std::span<std::byte> out,
            std::string& failure) const = 0;
    };

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
    public:
        explicit LooseArtifactByteSource(std::filesystem::path root)
            : root_(std::move(root)) {}

        [[nodiscard]] bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const override
        {
#if defined(_WIN32)
            const auto pinned = Pin(path, failure);
            if (!pinned)
            {
                return false;
            }
            std::lock_guard lock(pinned->mutex);
            const auto size = _filelengthi64(_fileno(pinned->file));
            if (size < 0)
            {
                failure = "pinned cooked artifact size cannot be read";
                return false;
            }
            out = static_cast<std::uint64_t>(size);
            return true;
#else
            std::filesystem::path file;
            if (!Resolve(path, file, failure))
            {
                return false;
            }
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error) || error)
            {
                failure = "cooked artifact file is missing";
                return false;
            }
            out = std::filesystem::file_size(file, error);
            if (error)
            {
                failure = "cooked artifact size cannot be read";
                return false;
            }
            return true;
#endif
        }

        [[nodiscard]] bool ReadAt(std::string_view path,
            std::uint64_t offset, std::span<std::byte> out,
            std::string& failure) const override
        {
            std::uint64_t size = 0u;
            if (!Size(path, size, failure))
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
            const auto pinned = Pin(path, failure);
            if (!pinned)
            {
                return false;
            }
            std::lock_guard lock(pinned->mutex);
            if (_fseeki64(pinned->file, static_cast<__int64>(offset), SEEK_SET) != 0 ||
                std::fread(out.data(), 1, out.size(), pinned->file) != out.size())
            {
                failure = "pinned cooked artifact range cannot be read";
                return false;
            }
            return true;
#else
            std::filesystem::path file;
            if (!Resolve(path, file, failure))
            {
                return false;
            }
            std::ifstream input(file, std::ios::binary);
            input.seekg(static_cast<std::streamoff>(offset));
            input.read(reinterpret_cast<char*>(out.data()),
                static_cast<std::streamsize>(out.size()));
            if (!input || input.gcount() != static_cast<std::streamsize>(out.size()))
            {
                failure = "cooked artifact range cannot be read";
                return false;
            }
            return true;
#endif
        }

    private:
#if defined(_WIN32)
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

        // An opened artifact retains this byte source. Denying writers pins its
        // validated file identity/bytes until the final source owner is released.
        // Mount/root enumeration never calls Pin or opens artifact files.
        [[nodiscard]] std::shared_ptr<PinnedFile> Pin(std::string_view path,
            std::string& failure) const
        {
            std::lock_guard lock(filesMutex_);
            const std::string key(path);
            if (const auto existing = files_.find(key); existing != files_.end())
            {
                return existing->second;
            }
            std::filesystem::path resolved;
            if (!Resolve(path, resolved, failure))
            {
                return {};
            }
            auto pinned = std::make_shared<PinnedFile>();
            pinned->file = _wfsopen(resolved.c_str(), L"rb", _SH_DENYWR);
            if (!pinned->file)
            {
                failure = "cooked artifact cannot be pinned against writes";
                return {};
            }
            files_.emplace(key, pinned);
            return pinned;
        }

        mutable std::mutex filesMutex_;
        mutable std::unordered_map<std::string, std::shared_ptr<PinnedFile>> files_;
#endif
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
