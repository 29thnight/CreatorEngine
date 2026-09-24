#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace audio_cook
{
    struct DecodedSource final
    {
        std::uint32_t channels{};
        std::uint32_t sampleRate{};
        std::uint64_t frameCount{};
    };

    // Validates container extent and drains the pinned decoder to EOF.
    // The result is written only after every check succeeds.
    [[nodiscard]] bool ValidateEncodedSource(const std::filesystem::path& source,
        DecodedSource& out, std::string& failure);
}
