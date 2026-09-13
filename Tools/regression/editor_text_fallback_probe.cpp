#include "EditorTextFallback.h"
#include "EditorIconAlignment.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
    if (argc != 4) return 2;
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
