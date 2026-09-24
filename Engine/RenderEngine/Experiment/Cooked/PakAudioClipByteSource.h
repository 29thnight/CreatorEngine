#pragma once

#include "CookedAudioClipSource.h"
#include "../../../Utility_Framework/Paklib.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace experiment::cooked
{
    // The pak owns its index and is kept alive by every opened clip source.
    // Pak::Archive::readRange decompresses only intersecting chunks.
    class PakAudioClipByteSource final : public ArtifactByteSource
    {
    public:
        explicit PakAudioClipByteSource(std::shared_ptr<const Pak::Archive> archive)
            : archive_(std::move(archive)) {}

        [[nodiscard]] bool Size(std::string_view path,
            std::uint64_t& out, std::string& failure) const override
        {
            if (!archive_ || !IsAudioArtifactVirtualPath(path))
            {
                failure = "pak audio virtual path is invalid";
                return false;
            }
            const std::string virtualPath = "Assets/" + std::string(path);
            const auto size = archive_->sizeOf(virtualPath);
            if (!size)
            {
                failure = "audio artifact is missing from pak";
                return false;
            }
            out = *size;
            return true;
        }

        [[nodiscard]] bool ReadAt(std::string_view path,
            std::uint64_t offset, std::span<std::byte> out,
            std::string& failure) const override
        {
            std::uint64_t size = 0u;
            if (!Size(path, size, failure)) return false;
            if (offset > size || out.size() > size - offset)
            {
                failure = "pak audio read exceeds entry extent";
                return false;
            }
            try
            {
                archive_->readRange("Assets/" + std::string(path), offset,
                    std::span(reinterpret_cast<Pak::u8*>(out.data()), out.size()));
            }
            catch (const std::exception& error)
            {
                failure = std::string("pak audio range read failed: ") + error.what();
                return false;
            }
            return true;
        }

    private:
        std::shared_ptr<const Pak::Archive> archive_{};
    };
}
