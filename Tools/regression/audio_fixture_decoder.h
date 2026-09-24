#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

// Local probe contract only. The implementation lives in MiniaudioBackend.cpp,
// so the vendor header remains confined to that single translation unit.
namespace wave::probe
{
    bool DecodePcmS16Mono48k(const std::filesystem::path& source,
        std::size_t wantedFrames, std::vector<std::int16_t>& samples,
        std::uint64_t& length);
}
