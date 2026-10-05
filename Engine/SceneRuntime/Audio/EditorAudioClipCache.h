#pragma once

#include "AudioValues.h"
#include "../../RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"

namespace wave
{
    // Import-time adapter only. The runtime receives an immutable CEAC byte
    // source rooted in a content-addressed editor cache, never the mutable
    // authoring file. All I/O/validation runs outside the device callback.
    [[nodiscard]] bool ImportEditorAudioClip(const std::filesystem::path& source,
        const std::filesystem::path& cacheRoot, const ClipKey& id,
        std::string_view loadMode, std::string_view spatialKind,
        experiment::cooked::CookedAudioClipSource& out, std::string& error);
}
