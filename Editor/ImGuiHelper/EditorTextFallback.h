#pragma once
#include <imgui.h>

namespace editor::fonts
{
    // Append only missing characters: the primary face keeps its Latin metrics,
    // and Material Symbols retains every private-use slot.
    inline bool merge_korean_fallback(ImFontAtlas& atlas, const char* filename, float size)
    {
        if (atlas.Fonts.empty() || !filename || !*filename) return false;
        ImFontAtlas probe;
        ImFontConfig probeConfig;
        probeConfig.Flags |= ImFontFlags_NoLoadError;
        auto* candidate = probe.AddFontFromFileTTF(filename, size, &probeConfig);
        if (!candidate || !candidate->IsGlyphInFont(0xAC00) || !candidate->IsGlyphInFont(0xD7A3)) return false;

        static constexpr ImWchar iconExclusions[]{0xE000, 0xF8FF, 0};
        ImFontConfig config;
        config.MergeMode = true;
        config.Flags |= ImFontFlags_NoLoadError;
        config.GlyphExcludeRanges = iconExclusions;
        return atlas.AddFontFromFileTTF(filename, size, &config) != nullptr;
    }
}
