#include "EditorIconAlignment.h"
#include <cmath>
#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    ImGui::CreateContext();
    int checks = 0, failures = 0;
    float maxError = 0.f;
    for (int face = 2; face <= argc; ++face)
    {
        for (float referenceSize : {10.f, 12.f, 16.f})
        {
            ImFontAtlas atlas;
            ImFontConfig textConfig;
            textConfig.SizePixels = referenceSize;
            const ImWchar exclude[]{0xe000, 0xf8ff, 0};
            textConfig.GlyphExcludeRanges = exclude;
            ImFont* font = face == argc ? atlas.AddFontDefault(&textConfig)
                : atlas.AddFontFromFileTTF(argv[face], referenceSize, &textConfig);
            if (!font || !editor::fonts::merge_aligned_icons(atlas, argv[1], referenceSize)) return 3;
            for (float scale : {1.f, 1.25f, 1.5f, 2.f, 2.25f, 3.f})
            {
                ImFontBaked* baked = font->GetFontBaked(referenceSize * scale, 1.f);
                const ImFontGlyph* capital = baked->FindGlyphNoFallback('H');
                if (!capital) return 4;
                const float center = (capital->Y0 + capital->Y1) * 0.5f;
                for (const auto& role : EditorIcon::Roles)
                {
                    const std::string_view name(role.name);
                    if (name != "Scene" && name != "Game" && name != "Menu" && name != "Lit") continue;
                    const ImFontGlyph* glyph = baked->FindGlyphNoFallback(static_cast<ImWchar>(role.codepoint));
                    if (!glyph) return 5;
                    const float error = std::fabs((glyph->Y0 + glyph->Y1) * 0.5f - center);
                    maxError = error > maxError ? error : maxError;
                    ++checks;
                    // Independent glyph raster rounding may differ by one pixel.
                    if (error > 1.5f)
                    {
                        ++failures;
                        std::printf("FAIL face=%d size=%.2f role=%s center error=%.3f\n",
                            face, referenceSize * scale, role.name, error);
                    }
                }
            }
        }
    }
    // Preserve ImGui's supported implicit-size merge path for external callers.
    {
        ImFontAtlas atlas;
        atlas.AddFontDefault();
        if (!editor::fonts::merge_aligned_icons(atlas, argv[1], 16.f)) return 6;
        ++checks;
    }
    ImGui::DestroyContext();
    std::printf("Icon alignment: %d checks, %d failures, max center error %.3f px\n", checks, failures, maxError);
    return failures ? 1 : 0;
}
