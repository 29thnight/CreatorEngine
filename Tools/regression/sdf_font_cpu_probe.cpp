// Source-only regression fixture. Build/run explicitly after engine execution is
// authorized; no graphics device or editor process is required.
#include "FontAsset.h"
#include "Texture.h"
#include "Interfaces/Navigation.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    struct Checker
    {
        int failed{};
        void Check(bool condition, const char* label)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
            if (!condition)
            {
                ++failed;
            }
        }
    };

    bool near(float left, float right)
    {
        return std::abs(left - right) < 0.01f;
    }

    std::vector<std::byte> copy_pixels(const std::shared_ptr<Texture>& texture)
    {
        if (!texture)
        {
            return {};
        }
        const auto view = texture->GetImageView();
        const auto* image = view.At(0);
        if (!image || !image->pixels)
        {
            return {};
        }
        return { image->pixels, image->pixels + image->slicePitch };
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "Usage: SdfFontCpuProbe <bundled-font-directory> <scratch-directory>\n");
        return 2;
    }
    const std::filesystem::path fontRoot(argv[1]);
    const std::filesystem::path scratchRoot(argv[2]);
    Checker checker;
    std::string error;
    auto font = FontAsset::Load(fontRoot / L"Inter-Regular.ttf", error);
    checker.Check(!!font, "load bundled Inter TTF");
    if (!font)
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    checker.Check(font->AddFallback(fontRoot / L"NanumGothic-Regular.ttf", error), "load Korean fallback TTF");
    auto build = [&](std::string_view text, float width = 0.0f, TextAlignment alignment = TextAlignment::Left)
    {
        return font->BuildLayout(text, 32.0f, width, alignment, error);
    };
    const auto empty = build("");
    checker.Check(empty && empty->glyphs.empty() && empty->width == 0.0f && empty->height == 0.0f, "empty text");
    checker.Check(!font->BuildLayout("A", std::numeric_limits<float>::quiet_NaN(), 0.0f, TextAlignment::Left, error),
        "reject NaN font size");
    checker.Check(!font->BuildLayout("A", 32.0f, std::numeric_limits<float>::infinity(), TextAlignment::Left, error),
        "reject infinite width");
    checker.Check(!font->BuildLayout("A", 0.0f, 0.0f, TextAlignment::Left, error), "reject zero font size");

    const auto single = build("A");
    if (!single || single->glyphs.size() != 1u)
    {
        checker.Check(false, "rasterize initial glyph");
        return 1;
    }
    auto oldTexture = single->glyphs.front().GetTexture();
    const auto oldPixels = copy_pixels(oldTexture);
    const auto added = build("B");
    checker.Check(added && !oldPixels.empty() && oldPixels == copy_pixels(oldTexture), "old frame atlas bytes immutable");
    checker.Check(single->glyphs.front().GetTexture() != oldTexture, "static layout observes append-only latest atlas");
    checker.Check(single->glyphs.front().atlasPage && !single->glyphs.front().texture,
        "real glyph layout retains stable page rather than a stale Texture");
    oldTexture.reset();

    const auto lf = build("A\nB");
    const auto crlf = build("A\r\nB");
    checker.Check(lf && crlf && near(lf->height, crlf->height) && near(lf->height, lf->lineHeight * 2.0f),
        "CRLF is exactly one newline");
    const auto plain = build("AB");
    const auto tab = build("A\tB");
    checker.Check(plain && tab && plain->glyphs.size() == 2u && tab->glyphs.size() == 2u && tab->glyphs[1].left > plain->glyphs[1].left,
        "tab advances to four-space stops");
    const auto a = build("A");
    const auto v = build("V");
    const auto av = build("AV");
    checker.Check(a && v && av && av->width < a->width + v->width, "Inter AV kerning");
    const auto left = build("AV", 200.0f, TextAlignment::Left);
    const auto center = build("AV", 200.0f, TextAlignment::Center);
    const auto right = build("AV", 200.0f, TextAlignment::Right);
    checker.Check(left && center && right && av && left->glyphs.size() == 2u
        && center->glyphs.size() == 2u && right->glyphs.size() == 2u && near(left->width, 200.0f)
        && near(center->glyphs[0].left - left->glyphs[0].left, (200.0f - av->width) * 0.5f)
        && near(right->glyphs[0].left - left->glyphs[0].left, 200.0f - av->width), "left/center/right alignment box");
    const auto wrapped = build("AAAA", a ? a->width + 0.1f : 1.0f);
    checker.Check(wrapped && near(wrapped->height, wrapped->lineHeight * 4.0f), "character wrapping");

    for (const std::string invalid : { std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
        std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82"), std::string("\x80") })
    {
        const auto layout = build(invalid);
        checker.Check(layout && layout->invalidUtf8Count != 0u && !layout->glyphs.empty(), "strict UTF-8 replacement");
    }
    const auto korean = build("\xed\x95\x9c\xea\xb8\x80"); // 한글
    checker.Check(korean && korean->glyphs.size() == 2u && korean->fallbackCount == 0u, "Hangul fallback coverage");
    const auto unsupported = build("\xf4\x8f\xbf\xbf"); // U+10FFFF, valid scalar but absent glyph.
    checker.Check(unsupported && unsupported->fallbackCount == 1u && unsupported->invalidUtf8Count == 0u,
        "missing scalar uses visible replacement");
    const auto bounded = build(std::string(FontAsset::kMaxLayoutCharacters + 1u, 'A'));
    checker.Check(bounded && bounded->truncated && bounded->glyphs.size() == FontAsset::kMaxLayoutCharacters
        && !bounded->retryWhenAtlasAvailable,
        "bounded layout character count");

    // Retain many separate immutable layouts while each introduces a new glyph.
    // They must follow the stable page instead of exhausting 16 texture versions.
    auto manyFont = FontAsset::Load(fontRoot / L"Inter-Regular.ttf", error);
    checker.Check(!!manyFont, "load isolated static-label fixture font");
    if (manyFont)
    {
        std::vector<std::shared_ptr<const TextLayout>> labels;
        bool allRendered = true;
        for (char character = '!'; character <= '~'; ++character)
        {
            auto label = manyFont->BuildLayout(std::string(1, character), 32.0f, 0.0f, TextAlignment::Left, error);
            allRendered = allRendered && label && !label->truncated && label->fallbackCount == 0u;
            labels.push_back(std::move(label));
        }
        checker.Check(allRendered && labels.size() > FontAsset::kMaxLiveAtlasSnapshots,
            "94 persistent distinct labels do not exhaust atlas snapshot budget");
        checker.Check(labels.front() && labels.back() && !labels.front()->glyphs.empty() && !labels.back()->glyphs.empty()
            && labels.front()->glyphs.front().GetTexture() == labels.back()->glyphs.front().GetTexture(),
            "old and new labels resolve the same latest page snapshot");
    }

    // Explicitly retained frame snapshots do count toward the resource cap. Once
    // released, a subsequent changed-text build must be able to recover.
    auto pressureFont = FontAsset::Load(fontRoot / L"Inter-Regular.ttf", error);
    bool sawPressure = false;
    char blockedCharacter = 0;
    if (pressureFont)
    {
        std::vector<std::shared_ptr<Texture>> frames;
        for (char character = '!'; character <= '~'; ++character)
        {
            const auto label = pressureFont->BuildLayout(std::string(1, character), 32.0f, 0.0f, TextAlignment::Left, error);
            if (label && label->truncated)
            {
                checker.Check(label->retryWhenAtlasAvailable, "snapshot pressure exposes transient retry flag");
                sawPressure = true;
                blockedCharacter = character;
                break;
            }
            if (label && !label->glyphs.empty())
            {
                frames.push_back(label->glyphs.front().GetTexture());
            }
        }
        checker.Check(sawPressure && frames.size() <= FontAsset::kMaxLiveAtlasSnapshots,
            "retained in-flight texture snapshots enforce the resource budget");
        frames.clear();
        checker.Check(pressureFont->CanRetryAtlas(), "retired frame owners reopen snapshot admission");
        const auto recovered = pressureFont->BuildLayout(std::string(1, blockedCharacter), 32.0f, 0.0f,
            TextAlignment::Left, error);
        checker.Check(recovered && !recovered->truncated && recovered->fallbackCount == 0u,
            "snapshot budget recovers after old frame owners release");
    }
    else
    {
        checker.Check(false, "load snapshot-pressure fixture font");
    }

    std::error_code fileError;
    std::filesystem::create_directories(scratchRoot, fileError);
    const auto malformedPath = scratchRoot / ("sdf-malformed-" + FileGuid::CreateRandomV4().ToString() + ".ttf");
    std::array<unsigned char, 28> malformed{};
    malformed[1] = 1; // SFNT TrueType signature.
    malformed[5] = 1; // One table.
    malformed[12] = 'c'; malformed[13] = 'm'; malformed[14] = 'a'; malformed[15] = 'p';
    malformed[20] = 0x7f; // Offset far outside the tiny file.
    malformed[27] = 4;
    {
        std::ofstream output(malformedPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(malformed.data()), malformed.size());
        checker.Check(!!output, "write isolated malformed-font fixture");
    }
    checker.Check(!FontAsset::Load(malformedPath, error) && error.find("outside") != std::string::npos,
        "reject out-of-file SFNT table before stb parser");
    std::filesystem::remove(malformedPath, fileError);
    std::printf("SDF CPU fixture: %d failure(s)\n", checker.failed);
    return checker.failed == 0 ? 0 : 1;
}
