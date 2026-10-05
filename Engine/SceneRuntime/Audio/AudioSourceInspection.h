#pragma once

#include <filesystem>
#include <string>

#include "AudioValues.h"

namespace wave
{
    // Fully validates an encoded source using bounded scratch memory. Callers
    // performing import must pass their immutable source snapshot path.
    [[nodiscard]] bool InspectAudioSource(const std::filesystem::path& source,
        ClipInfo& decoded, std::string& error);
}
