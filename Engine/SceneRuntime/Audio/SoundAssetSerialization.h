#pragma once

#include "SoundGraph.h"

#include <cstddef>
#include <span>
#include <string_view>

namespace wave
{
    // Authoring is YAML; cooked data uses the engine's bounded CEDO envelope.
    // Readers never change the output on failure. Asset identities are GUIDs,
    // not filesystem paths or legacy filename aliases.
    [[nodiscard]] bool ReadSoundGraph(std::string_view text, SoundGraphDefinition& out, std::string& error);
    [[nodiscard]] std::string WriteSoundGraph(const SoundGraphDefinition& graph);
    [[nodiscard]] bool ReadSoundPreset(std::string_view text, SoundPreset& out, std::string& error);
    [[nodiscard]] std::string WriteSoundPreset(const SoundPreset& preset);
    [[nodiscard]] bool CookSoundGraph(const SoundGraphDefinition& graph, std::vector<std::byte>& out, std::string& error);
    [[nodiscard]] bool CookSoundPreset(const SoundPreset& preset, std::vector<std::byte>& out, std::string& error);
    [[nodiscard]] bool ReadCookedSoundGraph(std::span<const std::byte> bytes, SoundGraphDefinition& out, std::string& error);
    [[nodiscard]] bool ReadCookedSoundPreset(std::span<const std::byte> bytes, SoundPreset& out, std::string& error);
}
