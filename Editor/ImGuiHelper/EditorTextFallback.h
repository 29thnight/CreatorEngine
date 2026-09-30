#pragma once
#include <imgui.h>
#include <imgui_freetype.h>

static_assert(sizeof(ImWchar) == 4, "Install the imgui[wchar32,freetype] manifest features before building.");
#ifndef IMGUI_ENABLE_FREETYPE
#error Install the imgui[freetype] manifest feature before building.
#endif

namespace editor::fonts
{
    // Reserve the About sample's BMP emoji and supplementary pictographs for
    // the color source. Other BMP characters retain normal text-font priority.
    inline constexpr ImWchar text_with_emoji_exclusions[]{
        0x2728, 0x2728, 0xE000, 0xF8FF, 0x1F000, 0x1FAFF, 0
    };

    inline bool merge_color_emoji_fallback(ImFontAtlas& atlas, const char* filename, float size)
    {
        if (atlas.Fonts.empty() || !filename || !*filename) return false;
        ImFontConfig config;
        config.Flags |= ImFontFlags_NoLoadError;
        // The manifest selects FreeType at atlas level. Its global lifecycle
        // callbacks prevent using it as a per-source FontLoader in ImGui 1.92.
        config.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LoadColor;
        // Probe before merging: an unrelated or missing font must leave text usable.
        ImFontAtlas probe;
        ImFont* candidate = probe.AddFontFromFileTTF(filename, size, &config);
        if (!candidate || !candidate->IsGlyphInFont(0x1F3B9) || !candidate->IsGlyphInFont(0x2728)) return false;

        static constexpr ImWchar iconExclusions[]{0xE000, 0xF8FF, 0};
        config.MergeMode = true;
        config.GlyphExcludeRanges = iconExclusions;
        return atlas.AddFontFromFileTTF(filename, size, &config) != nullptr;
    }

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
