#pragma once

#include "EditorIcons.h"
#include <imgui.h>
#include <string_view>

namespace editor::fonts
{
    // Compare visible glyph centers relative to the shared text baseline. The
    // line box returned by CalcTextSize cannot reveal a merged font's bearings.
    inline ImFont* merge_aligned_icons(ImFontAtlas& atlas, const char* filename,
        float iconSize, float opticalOffset = 0.f)
    {
        if (atlas.Fonts.empty()) return nullptr;
        ImFont* destination = atlas.Fonts.back();
        const bool implicit = (destination->Flags & ImFontFlags_ImplicitRefSize) != 0;
        ImFontConfig config;
        config.MergeMode = true;
        config.Flags |= ImFontFlags_NoLoadError;

        // ImGui forbids a nonzero offset together with an implicit merge size.
        // Editor-owned text fonts (including the default fallback) use an
        // explicit size; preserve the valid zero-offset path for other callers.
        if (!implicit)
        {
            constexpr float sampleSize = 128.f;
            ImFontAtlas metricsAtlas;
            ImFontConfig metricsConfig;
            metricsConfig.Flags |= ImFontFlags_NoLoadError;
            ImFont* symbols = metricsAtlas.AddFontFromFileTTF(filename, sampleSize, &metricsConfig);
            if (!symbols) return nullptr;
            ImWchar reference = 0;
            for (const auto& role : EditorIcon::Roles)
                if (std::string_view(role.name) == "Menu") reference = static_cast<ImWchar>(role.codepoint);

            ImFontBaked* text = destination->GetFontBaked(sampleSize, 1.f);
            ImFontBaked* icons = symbols->GetFontBaked(sampleSize, 1.f);
            const ImFontGlyph* capital = text->FindGlyphNoFallback('H');
            const ImFontGlyph* symbol = icons->FindGlyphNoFallback(reference);
            if (capital && symbol)
            {
                const float textCenter = (capital->Y0 + capital->Y1) * 0.5f - text->Ascent;
                const float iconCenter = (symbol->Y0 + symbol->Y1) * 0.5f - icons->Ascent;
                const float textSize = destination->Sources[0]->SizePixels;
                config.GlyphOffset.y = (textCenter * textSize - iconCenter * iconSize) / sampleSize + opticalOffset;
            }
        }
        // AddFont discards the destination's temporary measurement bake. The
        // offset stays in reference pixels and scales with user size and DPI.
        return atlas.AddFontFromFileTTF(filename, implicit ? 0.f : iconSize, &config);
    }
}
