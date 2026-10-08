#include "FontAsset.h"
#include "Texture.h"
#include "Interfaces/Navigation.h"
#include "RHI/RHIFormat.h"

// Private symbols avoid ImGui's separate stb implementation. This translation
// unit is excluded from unity builds so these macros cannot leak to other files.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#undef STB_TRUETYPE_IMPLEMENTATION
#undef STBTT_STATIC

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <exception>
#include <limits>
#include <unordered_map>
#include <utility>

namespace
{
    constexpr int kSdfPadding = 8;
    constexpr unsigned char kSdfOnEdge = 128;
    constexpr float kSdfDistanceScale = 16.0f;
    constexpr int kMaxGlyphExtent = 256;
    constexpr std::uint32_t kReplacement = 0xfffdu;
    constexpr std::size_t kNoPage = std::numeric_limits<std::size_t>::max();

    std::uint16_t read_be16(const unsigned char* value)
    {
        return static_cast<std::uint16_t>((value[0] << 8u) | value[1]);
    }

    std::uint32_t read_be32(const unsigned char* value)
    {
        return (std::uint32_t(value[0]) << 24u) | (std::uint32_t(value[1]) << 16u)
            | (std::uint32_t(value[2]) << 8u) | std::uint32_t(value[3]);
    }

    bool validate_sfnt(const std::vector<unsigned char>& bytes, std::string& error)
    {
        // Reject collections and compressed containers. Their extra offset graph is
        // intentionally outside the supported TTF/OTF project-asset surface.
        if (bytes.size() < 12u || (read_be32(bytes.data()) != 0x00010000u
            && read_be32(bytes.data()) != 0x4f54544fu && read_be32(bytes.data()) != 0x74727565u))
        {
            error = "Expected a standalone TrueType/OpenType font (TTF/OTF).";
            return false;
        }
        const auto count = read_be16(bytes.data() + 4);
        if (count == 0u || count > 256u || 12u + std::size_t(count) * 16u > bytes.size())
        {
            error = "Font SFNT table directory is truncated or exceeds its limit.";
            return false;
        }
        bool hasCmap = false, hasHead = false, hasHhea = false, hasHmtx = false, hasMaxp = false;
        bool hasGlyf = false, hasLoca = false, hasCff = false;
        for (std::size_t index = 0; index < count; ++index)
        {
            const auto* table = bytes.data() + 12u + index * 16u;
            const auto offset = read_be32(table + 8);
            const auto length = read_be32(table + 12);
            if (offset > bytes.size() || length > bytes.size() - offset)
            {
                error = "Font SFNT table points outside the file.";
                return false;
            }
            switch (read_be32(table))
            {
            case 0x636d6170u: hasCmap = length >= 4u; break;
            case 0x68656164u: hasHead = length >= 54u; break;
            case 0x68686561u: hasHhea = length >= 36u; break;
            case 0x686d7478u: hasHmtx = length >= 4u; break;
            case 0x6d617870u: hasMaxp = length >= 6u; break;
            case 0x676c7966u: hasGlyf = length != 0u; break;
            case 0x6c6f6361u: hasLoca = length >= 4u; break;
            case 0x43464620u: hasCff = length >= 4u; break;
            default: break;
            }
        }
        if (!hasCmap || !hasHead || !hasHhea || !hasHmtx || !hasMaxp || (!hasCff && (!hasGlyf || !hasLoca)))
        {
            error = "Font is missing required SFNT tables.";
            return false;
        }
        return true;
    }

    std::uint32_t decode_utf8(std::string_view text, std::size_t& cursor, std::size_t& invalidCount)
    {
        const auto first = static_cast<unsigned char>(text[cursor++]);
        if (first < 0x80u)
        {
            return first;
        }
        unsigned count = 0;
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2u && first <= 0xdfu)
        {
            count = 1;
            value = first & 0x1fu;
            minimum = 0x80u;
        }
        else if (first >= 0xe0u && first <= 0xefu)
        {
            count = 2;
            value = first & 0x0fu;
            minimum = 0x800u;
        }
        else if (first >= 0xf0u && first <= 0xf4u)
        {
            count = 3;
            value = first & 0x07u;
            minimum = 0x10000u;
        }
        else
        {
            ++invalidCount;
            return kReplacement;
        }
        for (unsigned index = 0; index < count; ++index)
        {
            if (cursor == text.size() || (static_cast<unsigned char>(text[cursor]) & 0xc0u) != 0x80u)
            {
                ++invalidCount;
                return kReplacement;
            }
            value = (value << 6u) | (static_cast<unsigned char>(text[cursor++]) & 0x3fu);
        }
        if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu))
        {
            // One replacement for the rejected sequence; no surrogate or overlong
            // encoding can masquerade as a newline, ASCII glyph or valid scalar.
            ++invalidCount;
            return kReplacement;
        }
        return value;
    }
}

struct FontAsset::Impl
{
    struct Face
    {
        std::filesystem::path path;
        std::vector<unsigned char> bytes;
        stbtt_fontinfo info{};
        float scale{};
        float ascent{};
        float lineHeight{};
    };
    struct CachedGlyph
    {
        std::size_t page{ kNoPage };
        int x{}, y{}, width{}, height{}, offsetX{}, offsetY{};
        float advance{};
    };
    struct Page
    {
        // 1 MiB staging + 4 MiB immutable RGBA snapshot per page (8 page cap).
        // RGBA uses the existing neutral upload formats; all channels hold SDF.
        std::vector<unsigned char> pixels = std::vector<unsigned char>(kAtlasSize * kAtlasSize, 0u);
        int cursorX{ 1 }, cursorY{ 1 }, rowHeight{};
        bool dirty{ true };
        std::shared_ptr<TextAtlasPage> owner = std::make_shared<TextAtlasPage>();
    };
    std::vector<std::unique_ptr<Face>> faces;
    std::unordered_map<std::uint64_t, CachedGlyph> glyphs;
    std::vector<Page> pages;
    // Weak bookkeeping imposes a live resource bound without retaining retired
    // owners. In-flight frames may hold old pages; they are never overwritten.
    std::vector<std::weak_ptr<Texture>> snapshots;
    bool snapshotAdmissionDenied{};

    Impl()
    {
        // Publication itself cannot fail to record a newly published owner.
        // Expired entries are pruned before each batch of at most eight pages.
        snapshots.reserve(kMaxLiveAtlasSnapshots + kMaxAtlasPages);
    }

    static std::unique_ptr<Face> LoadFace(const std::filesystem::path& path, std::string& error)
    {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            error = "Cannot open font: " + path.string();
            return {};
        }
        const auto length = input.tellg();
        if (length <= 0 || length > static_cast<std::streamoff>(kMaxFontBytes))
        {
            error = "Font is empty or exceeds the 32 MiB file budget: " + path.string();
            return {};
        }
        auto face = std::make_unique<Face>();
        face->path = path;
        face->bytes.resize(static_cast<std::size_t>(length));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(face->bytes.data()), static_cast<std::streamsize>(face->bytes.size()));
        if (!input || !validate_sfnt(face->bytes, error))
        {
            if (error.empty())
            {
                error = "Cannot read font: " + path.string();
            }
            return {};
        }
        if (!stbtt_InitFont(&face->info, face->bytes.data(), 0))
        {
            error = "stb_truetype rejected font: " + path.string();
            return {};
        }
        int ascent = 0, descent = 0, lineGap = 0;
        stbtt_GetFontVMetrics(&face->info, &ascent, &descent, &lineGap);
        if (ascent <= descent || face->info.numGlyphs <= 0)
        {
            error = "Font has invalid vertical metrics or no glyphs.";
            return {};
        }
        face->scale = stbtt_ScaleForPixelHeight(&face->info, kRasterPixelHeight);
        face->ascent = ascent * face->scale;
        face->lineHeight = std::max(kRasterPixelHeight, (ascent - descent + lineGap) * face->scale);
        return face;
    }

    std::pair<std::size_t, int> ResolveGlyph(std::uint32_t codepoint, bool& fallback) const
    {
        for (std::size_t face = 0; face < faces.size(); ++face)
        {
            const int glyph = stbtt_FindGlyphIndex(&faces[face]->info, static_cast<int>(codepoint));
            if (glyph != 0)
            {
                return { face, glyph };
            }
        }
        fallback = true;
        int glyph = stbtt_FindGlyphIndex(&faces[0]->info, static_cast<int>(kReplacement));
        if (glyph == 0)
        {
            glyph = stbtt_FindGlyphIndex(&faces[0]->info, '?');
        }
        return { 0, glyph };
    }

    bool CanDirtyPage(const Page* target)
    {
        std::erase_if(snapshots, [](const auto& snapshot) { return snapshot.expired(); });
        // Count every prospective version conservatively. An RT reader can
        // acquire the old atomic Texture concurrently, so use_count subtraction
        // would race and undercount. At most one publication batch is in flight.
        std::size_t required = snapshots.size();
        for (const auto& page : pages)
        {
            if (page.dirty || &page == target)
            {
                ++required;
            }
        }
        if (!target)
        {
            ++required;
        }
        if (required > kMaxLiveAtlasSnapshots)
        {
            snapshotAdmissionDenied = true;
            return false;
        }
        return true;
    }

    const CachedGlyph* CacheGlyph(std::size_t faceIndex, int glyphIndex)
    {
        const auto key = (std::uint64_t(faceIndex) << 32u) | static_cast<std::uint32_t>(glyphIndex);
        if (const auto found = glyphs.find(key); found != glyphs.end())
        {
            return &found->second;
        }
        if (glyphs.size() >= kMaxCachedGlyphs)
        {
            return nullptr;
        }
        const auto& face = *faces[faceIndex];
        CachedGlyph glyph;
        int advance = 0, leftBearing = 0;
        stbtt_GetGlyphHMetrics(&face.info, glyphIndex, &advance, &leftBearing);
        glyph.advance = advance * face.scale;
        if (stbtt_IsGlyphEmpty(&face.info, glyphIndex))
        {
            return &glyphs.emplace(key, glyph).first->second;
        }
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&face.info, glyphIndex, face.scale, face.scale, &x0, &y0, &x1, &y1);
        if (x1 - x0 + 2 * kSdfPadding > kMaxGlyphExtent || y1 - y0 + 2 * kSdfPadding > kMaxGlyphExtent
            || x1 < x0 || y1 < y0)
        {
            return nullptr;
        }
        // In-flight pressure should not rerasterize thousands of uncached
        // characters merely to reject all their atlas writes afterward.
        if (!CanDirtyPage(pages.empty() ? nullptr : &pages.back()))
        {
            return nullptr;
        }
        unsigned char* bitmap = stbtt_GetGlyphSDF(&face.info, face.scale, glyphIndex, kSdfPadding,
            kSdfOnEdge, kSdfDistanceScale, &glyph.width, &glyph.height, &glyph.offsetX, &glyph.offsetY);
        if (!bitmap)
        {
            return nullptr;
        }
        const std::unique_ptr<unsigned char, void(*)(unsigned char*)> bitmapOwner(bitmap,
            [](unsigned char* value) { stbtt_FreeSDF(value, nullptr); });
        if (glyph.width <= 0 || glyph.height <= 0 || glyph.width > kMaxGlyphExtent || glyph.height > kMaxGlyphExtent)
        {
            return nullptr;
        }
        if (pages.empty())
        {
            if (!CanDirtyPage(nullptr))
            {
                return nullptr;
            }
            pages.emplace_back();
        }
        auto fits = [&glyph](Page& page)
        {
            if (page.cursorX + glyph.width + 1 > static_cast<int>(kAtlasSize))
            {
                page.cursorX = 1;
                page.cursorY += page.rowHeight + 1;
                page.rowHeight = 0;
            }
            return page.cursorY + glyph.height + 1 <= static_cast<int>(kAtlasSize);
        };
        if (!fits(pages.back()))
        {
            if (pages.size() >= kMaxAtlasPages || !CanDirtyPage(nullptr))
            {
                return nullptr;
            }
            pages.emplace_back();
        }
        else if (!CanDirtyPage(&pages.back()))
        {
            // Preserve the old published page and use the cached replacement.
            // No atlas generation is silently dropped while a frame uses it.
            return nullptr;
        }
        auto& page = pages.back();
        glyph.page = pages.size() - 1u;
        glyph.x = page.cursorX;
        glyph.y = page.cursorY;
        for (int row = 0; row < glyph.height; ++row)
        {
            std::copy_n(bitmap + row * glyph.width, glyph.width,
                page.pixels.data() + (glyph.y + row) * kAtlasSize + glyph.x);
        }
        page.cursorX += glyph.width + 1;
        page.rowHeight = std::max(page.rowHeight, glyph.height);
        page.dirty = true;
        return &glyphs.emplace(key, glyph).first->second;
    }

    bool PublishPages(std::string& error)
    {
        std::erase_if(snapshots, [](const auto& snapshot) { return snapshot.expired(); });
        std::size_t additional = 0;
        for (const auto& page : pages)
        {
            if (page.dirty)
            {
                ++additional;
            }
        }
        if (snapshots.size() + additional > kMaxLiveAtlasSnapshots)
        {
            error = "SDF atlas snapshot budget reached while older layouts/frames are still retained.";
            return false;
        }
        for (auto& page : pages)
        {
            if (!page.dirty)
            {
                continue;
            }
            std::vector<unsigned char> rgba(std::size_t(kAtlasSize) * kAtlasSize * 4u);
            for (std::size_t index = 0; index < page.pixels.size(); ++index)
            {
                std::fill_n(rgba.data() + index * 4u, 4u, page.pixels[index]);
            }
            std::shared_ptr<Texture> texture(Texture::CreateFromPixels(kAtlasSize, kAtlasSize,
                "SDF font atlas", RHIFormat::RGBA8Unorm, rgba.data(), std::size_t(kAtlasSize) * 4u));
            if (!texture)
            {
                error = "Cannot create CPU SDF atlas texture.";
                return false;
            }
            // Never modify a Texture after it has escaped to a layout/frame.
            snapshots.emplace_back(texture);
            page.owner->Publish(std::move(texture));
            page.dirty = false;
        }
        return true;
    }
};

FontAsset::FontAsset() : m_impl(std::make_unique<Impl>()) {}
FontAsset::~FontAsset() = default;

std::shared_ptr<FontAsset> FontAsset::Load(const std::filesystem::path& path, std::string& error)
try
{
    error.clear();
    auto face = Impl::LoadFace(path, error);
    if (!face)
    {
        return {};
    }
    auto font = std::shared_ptr<FontAsset>(new FontAsset());
    font->m_path = path;
    font->m_impl->faces.push_back(std::move(face));
    return font;
}
catch (const std::exception& exception)
{
    error = std::string("Font load failed: ") + exception.what();
    return {};
}

bool FontAsset::AddFallback(const std::filesystem::path& path, std::string& error)
try
{
    error.clear();
    if (path == m_path)
    {
        return true;
    }
    auto face = Impl::LoadFace(path, error);
    if (!face)
    {
        return false;
    }
    std::lock_guard lock(m_mutex);
    if (m_impl->faces.size() >= 2u)
    {
        error = "Only one fallback font face is supported.";
        return false;
    }
    m_impl->faces.push_back(std::move(face));
    m_revision.fetch_add(1, std::memory_order_release);
    return true;
}
catch (const std::exception& exception)
{
    error = std::string("Font fallback load failed: ") + exception.what();
    return false;
}

std::shared_ptr<const TextLayout> FontAsset::BuildLayout(std::string_view utf8, float fontPixelSize,
    float maxWidth, TextAlignment alignment, std::string& error)
try
{
    error.clear();
    if (!std::isfinite(fontPixelSize) || fontPixelSize <= 0.0f || fontPixelSize > 4096.0f
        || !std::isfinite(maxWidth) || maxWidth < 0.0f || maxWidth > 1000000.0f)
    {
        error = "Text requires a finite font size in (0, 4096] and width in [0, 1000000].";
        return {};
    }
    std::lock_guard lock(m_mutex);
    m_impl->snapshotAdmissionDenied = false;
    auto layout = std::make_shared<TextLayout>();
    if (utf8.empty())
    {
        return layout;
    }
    if (utf8.size() > kMaxTextBytes)
    {
        utf8 = utf8.substr(0, kMaxTextBytes);
        layout->truncated = true;
    }
    const float scale = fontPixelSize / kRasterPixelHeight;
    float ascent = 0.0f;
    for (const auto& face : m_impl->faces)
    {
        ascent = std::max(ascent, face->ascent * scale);
        layout->lineHeight = std::max(layout->lineHeight, face->lineHeight * scale);
    }
    struct PendingGlyph { Impl::CachedGlyph glyph; float x; std::size_t line; };
    std::vector<PendingGlyph> pending;
    pending.reserve(std::min(utf8.size(), kMaxLayoutCharacters));
    std::vector<float> lineWidths;
    float x = 0.0f;
    int previousGlyph = 0;
    std::size_t previousFace = kNoPage;
    auto newLine = [&]
    {
        lineWidths.push_back(x);
        x = 0.0f;
        previousGlyph = 0;
        previousFace = kNoPage;
    };
    bool fallback = false;
    const auto replacement = m_impl->ResolveGlyph(kReplacement, fallback);
    // Reserve a replacement glyph before arbitrary text can consume the budget.
    (void)m_impl->CacheGlyph(replacement.first, replacement.second);
    const auto& primary = *m_impl->faces.front();
    int spaceAdvance = 0, spaceBearing = 0;
    stbtt_GetCodepointHMetrics(&primary.info, ' ', &spaceAdvance, &spaceBearing);
    const float tabWidth = std::max(1.0f, spaceAdvance * primary.scale * scale * 4.0f);
    std::size_t cursor = 0, characters = 0;
    while (cursor < utf8.size() && characters < kMaxLayoutCharacters)
    {
        ++characters;
        const auto codepoint = decode_utf8(utf8, cursor, layout->invalidUtf8Count);
        if (codepoint == '\r' || codepoint == '\n')
        {
            if (codepoint == '\r' && cursor < utf8.size() && utf8[cursor] == '\n')
            {
                ++cursor;
            }
            newLine();
            continue;
        }
        if (codepoint == '\t')
        {
            float next = (std::floor(x / tabWidth) + 1.0f) * tabWidth;
            if (maxWidth > 0.0f && next > maxWidth && x > 0.0f)
            {
                newLine();
                next = tabWidth;
            }
            x = next;
            previousFace = kNoPage;
            continue;
        }
        if (codepoint < 0x20u || codepoint == 0x7fu)
        {
            continue;
        }
        fallback = false;
        auto [faceIndex, glyphIndex] = m_impl->ResolveGlyph(codepoint, fallback);
        const auto* glyph = m_impl->CacheGlyph(faceIndex, glyphIndex);
        if (!glyph)
        {
            fallback = true;
            layout->truncated = true;
            faceIndex = replacement.first;
            glyphIndex = replacement.second;
            glyph = m_impl->CacheGlyph(faceIndex, glyphIndex);
        }
        if (fallback)
        {
            ++layout->fallbackCount;
        }
        if (!glyph)
        {
            continue;
        }
        const auto& face = *m_impl->faces[faceIndex];
        float kerning = previousFace == faceIndex
            ? stbtt_GetGlyphKernAdvance(&face.info, previousGlyph, glyphIndex) * face.scale * scale : 0.0f;
        const float advance = glyph->advance * scale;
        if (maxWidth > 0.0f && x > 0.0f && x + kerning + advance > maxWidth)
        {
            newLine();
            kerning = 0.0f;
        }
        x += kerning;
        if (glyph->page != kNoPage)
        {
            pending.push_back({ *glyph, x, lineWidths.size() });
        }
        x += advance;
        previousGlyph = glyphIndex;
        previousFace = faceIndex;
    }
    if (cursor < utf8.size())
    {
        layout->truncated = true;
    }
    newLine();
    layout->width = maxWidth > 0.0f ? maxWidth : *std::max_element(lineWidths.begin(), lineWidths.end());
    layout->height = static_cast<float>(lineWidths.size()) * layout->lineHeight;
    if (!m_impl->PublishPages(error))
    {
        return {};
    }
    layout->glyphs.reserve(pending.size());
    const float alignmentFactor = alignment == TextAlignment::Center ? 0.5f
        : alignment == TextAlignment::Right ? 1.0f : 0.0f;
    for (const auto& placed : pending)
    {
        const auto& glyph = placed.glyph;
        const float lineOffset = (layout->width - lineWidths[placed.line]) * alignmentFactor;
        TextGlyph output;
        output.left = placed.x + glyph.offsetX * scale + lineOffset;
        output.top = static_cast<float>(placed.line) * layout->lineHeight + ascent + glyph.offsetY * scale;
        output.right = output.left + glyph.width * scale;
        output.bottom = output.top + glyph.height * scale;
        output.uvLeft = static_cast<float>(glyph.x) / kAtlasSize;
        output.uvTop = static_cast<float>(glyph.y) / kAtlasSize;
        output.uvRight = static_cast<float>(glyph.x + glyph.width) / kAtlasSize;
        output.uvBottom = static_cast<float>(glyph.y + glyph.height) / kAtlasSize;
        output.atlasPage = m_impl->pages[glyph.page].owner;
        layout->glyphs.push_back(std::move(output));
    }
    layout->retryWhenAtlasAvailable = m_impl->snapshotAdmissionDenied;
    if (layout->truncated)
    {
        error = "Text or font atlas budget reached; output uses replacement glyphs or is truncated.";
    }
    return layout;
}
catch (const std::exception& exception)
{
    error = std::string("Text layout failed: ") + exception.what();
    return {};
}

bool FontAsset::CanRetryAtlas() const
{
    std::lock_guard lock(m_mutex);
    std::erase_if(m_impl->snapshots, [](const auto& snapshot) { return snapshot.expired(); });
    std::size_t required = m_impl->snapshots.size();
    for (const auto& page : m_impl->pages)
    {
        if (page.dirty)
        {
            ++required;
        }
    }
    if (m_impl->pages.empty() || !m_impl->pages.back().dirty)
    {
        ++required;
    }
    return required <= kMaxLiveAtlasSnapshots;
}
