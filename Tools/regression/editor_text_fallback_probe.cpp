#include "EditorTextFallback.h"
#include "EditorIconAlignment.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <crtdbg.h>
#include <cstdlib>

int main(int argc, char** argv)
{
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    if (argc != 5) return 2;
    ImGui::CreateContext();
    try
    {
        int checks = 0;
        const auto check = [&](bool value, const char* message) {
            ++checks;
            if (!value) throw std::runtime_error(message);
        };
        ImFontAtlas atlas;
        check(!editor::fonts::merge_korean_fallback(atlas, argv[2], 16.f), "Empty atlas must be safe");
        static constexpr ImWchar exclusions[]{0xE000,0xF8FF,0};
        ImFontConfig config;
        config.GlyphExcludeRanges = exclusions;
        auto* body = atlas.AddFontFromFileTTF(argv[1], 16.f, &config);
        check(body != nullptr, "Primary font loaded");
        check(!body->IsGlyphInFont(0xAC00), "Reproduce Inter's missing Hangul");
        const float latinAdvance = body->GetFontBaked(16.f, 1.f)->FindGlyphNoFallback('W')->AdvanceX;
        check(!editor::fonts::merge_korean_fallback(atlas, argv[1], 16.f), "Reject a font without Hangul");
        check(editor::fonts::merge_korean_fallback(atlas, argv[2], 16.f), "Merge Korean fallback");
        check(atlas.Fonts.Size == 1, "Fallback must join the existing UI font");
        check(std::abs(body->GetFontBaked(16.f, 1.f)->FindGlyphNoFallback('W')->AdvanceX-latinAdvance)<.001f,
            "Keep primary Latin metrics");
        // IsGlyphInFont checks raw source coverage and ignores GlyphExcludeRanges.
        // Check the baked glyph that the UI would actually render instead.
        for (const auto& role : EditorIcon::Roles)
            check(body->GetFontBaked(16.f, 1.f)->FindGlyphNoFallback(static_cast<ImWchar>(role.codepoint)) == nullptr,
                "Korean source must not claim icon slots");
        check(editor::fonts::merge_aligned_icons(atlas, argv[3], 16.f) != nullptr, "Merge icons after fallback");
        // The actual SerializeField DisplayName in Bobber: 가로 흔들기.
        constexpr std::array<ImWchar,7> syllables{0xAC00,0xB85C,0xD754,0xB4E4,0xAE30,0xD7A3,0x3131};
        for (const float size : {12.f,16.f,24.f,32.f})
        {
            auto* baked = body->GetFontBaked(size, 1.f);
            for (const auto codepoint : syllables)
            {
                const auto* glyph = baked->FindGlyphNoFallback(codepoint);
                check(glyph && glyph->Codepoint == codepoint && glyph->Visible, "Render real Hangul instead of question marks");
            }
            for (const auto& role : EditorIcon::Roles)
                check(baked->FindGlyphNoFallback(static_cast<ImWchar>(role.codepoint)) != nullptr, "Icons survive Korean merge");
        }
        // The About heading shares the same merged body font. Exercise a BMP
        // overlap (Malgun's sparkles), an astral character, and the default face.
        for (const char* primary : std::array<const char*, 3>{argv[1], argv[2], nullptr})
        {
            ImFontAtlas emojiAtlas;
            check(!editor::fonts::merge_color_emoji_fallback(emojiAtlas, argv[4], 16.f), "Empty emoji atlas must be safe");
            ImFontConfig emojiTextConfig;
            emojiTextConfig.SizePixels = 16.f;
            emojiTextConfig.GlyphExcludeRanges = editor::fonts::text_with_emoji_exclusions;
            auto* text = primary ? emojiAtlas.AddFontFromFileTTF(primary, 16.f, &emojiTextConfig)
                : emojiAtlas.AddFontDefault(&emojiTextConfig);
            check(text != nullptr, "Emoji destination loaded");
            const float beforeMerge = text->GetFontBaked(16.f, 1.f)->FindGlyphNoFallback('W')->AdvanceX;
            check(!editor::fonts::merge_color_emoji_fallback(emojiAtlas, "missing-emoji.ttf", 16.f), "Missing emoji font is optional");
            check(!editor::fonts::merge_color_emoji_fallback(emojiAtlas, argv[1], 16.f), "Reject text-only emoji candidate");
            check(editor::fonts::merge_color_emoji_fallback(emojiAtlas, argv[4], 16.f), "Merge Segoe UI Emoji");
            check(emojiAtlas.Fonts.Size == 1, "Color emoji joins the existing face");
            check(editor::fonts::merge_korean_fallback(emojiAtlas, argv[2], 16.f), "Korean follows color emoji");
            check(editor::fonts::merge_aligned_icons(emojiAtlas, argv[3], 16.f) != nullptr, "Icons follow color emoji");
            check(std::abs(text->GetFontBaked(16.f, 1.f)->FindGlyphNoFallback('W')->AdvanceX - beforeMerge) < .001f,
                "Emoji merge preserves Latin metrics");
            for (const float size : {12.f,16.f,24.f,32.f})
            {
                auto* baked = text->GetFontBaked(size, 1.f);
                for (const ImWchar codepoint : {ImWchar{0x1F3B9}, ImWchar{0x2728}})
                {
                    const auto* glyph = baked->FindGlyphNoFallback(codepoint);
                    check(glyph && glyph->Codepoint == codepoint && glyph->Visible && glyph->Colored,
                        "About sample must render actual color glyphs");
                }
                const auto* sparkle = baked->FindGlyphNoFallback(0x2728);
                auto* texture = emojiAtlas.TexData;
                check(texture && texture->BytesPerPixel == 4, "Color atlas uses RGBA pixels");
                bool hasColor = false;
                const int x0 = static_cast<int>(std::lround(sparkle->U0 * texture->Width));
                const int y0 = static_cast<int>(std::lround(sparkle->V0 * texture->Height));
                const int x1 = static_cast<int>(std::lround(sparkle->U1 * texture->Width));
                const int y1 = static_cast<int>(std::lround(sparkle->V1 * texture->Height));
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x)
                    {
                        const auto* pixel = static_cast<const unsigned char*>(texture->GetPixelsAt(x, y));
                        hasColor |= pixel[3] != 0 && (pixel[0] != pixel[1] || pixel[1] != pixel[2]);
                    }
                check(hasColor, "Sparkles has actual non-grayscale color pixels");
                check(baked->FindGlyphNoFallback(0xAC00) != nullptr, "Hangul survives emoji merge");
                for (const auto& role : EditorIcon::Roles)
                    check(baked->FindGlyphNoFallback(static_cast<ImWchar>(role.codepoint)) != nullptr,
                        "Material Symbols survives emoji merge");
            }
        }
        std::cout << "TEXT_FALLBACK_OK checks=" << checks << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << "TEXT_FALLBACK_FAILED: " << error.what() << '\n';
        ImGui::DestroyContext();
        return 1;
    }
    ImGui::DestroyContext();
}
