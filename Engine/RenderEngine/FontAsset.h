#pragma once

#include "Ownership.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

class Texture;
enum class TextAlignment : std::uint8_t;

// A stable append-only page identity. Existing glyph texels and UVs never move.
// Static layouts follow its latest immutable Texture rather than pinning one
// full atlas version forever; frame sealing retains the returned actual Texture.
class TextAtlasPage final
{
public:
    own::shared_owner<const Texture> GetTexture() const
    {
        std::lock_guard lock(m_mutex);
        return m_texture;
    }

private:
    friend class FontAsset;
    void Publish(own::shared_owner<const Texture> texture)
    {
        std::lock_guard lock(m_mutex);
        // Retire the previous snapshot after releasing the page lock.
        m_texture.swap(texture);
    }
    mutable std::mutex m_mutex;
    own::shared_owner<const Texture> m_texture;
};

// Immutable GT-produced geometry. Atlas publication is separate and append-only.
struct TextGlyph
{
    float left{}, top{}, right{}, bottom{};
    float uvLeft{}, uvTop{}, uvRight{}, uvBottom{};
    std::shared_ptr<TextAtlasPage> atlasPage;
    // Synthetic CPU/RHI fixtures may supply a standalone immutable texture.
    own::shared_owner<const Texture> texture;
    own::shared_owner<const Texture> GetTexture() const
    {
        return atlasPage ? atlasPage->GetTexture() : texture;
    }
};

struct TextLayout
{
    std::vector<TextGlyph> glyphs;
    float width{}, height{}, lineHeight{};
    std::size_t fallbackCount{};
    std::size_t invalidUtf8Count{};
    bool truncated{};
    // Only transient snapshot pressure. Permanent glyph/text limits never set it.
    bool retryWhenAtlasAvailable{};
};

// CPU-only font owner. Load and BuildLayout belong to changed-text preparation,
// never render-thread submission. stb_truetype is not a hardened font parser:
// only trusted project/engine fonts are supported, not arbitrary network fonts.
class FontAsset final
{
public:
    static constexpr std::size_t kMaxFontBytes = 32u * 1024u * 1024u;
    static constexpr std::size_t kMaxTextBytes = 1024u * 1024u;
    static constexpr std::size_t kMaxLayoutCharacters = 16384u;
    static constexpr std::size_t kMaxCachedGlyphs = 4096u;
    static constexpr std::size_t kMaxAtlasPages = 8u;
    static constexpr std::size_t kMaxLiveAtlasSnapshots = 16u;
    static constexpr std::uint32_t kAtlasSize = 1024u;
    static constexpr float kRasterPixelHeight = 64.0f;
    static constexpr float kSdfEdge = 128.0f / 255.0f;

    static std::shared_ptr<FontAsset> Load(const std::filesystem::path& path, std::string& error);
    ~FontAsset();
    FontAsset(const FontAsset&) = delete;
    FontAsset& operator=(const FontAsset&) = delete;

    // One optional bundled fallback face. Called during loading, before sharing.
    bool AddFallback(const std::filesystem::path& path, std::string& error);
    std::shared_ptr<const TextLayout> BuildLayout(std::string_view utf8, float fontPixelSize,
        float maxWidth, TextAlignment alignment, std::string& error);
    // No I/O or rasterization; queried only for a pressure-truncated layout.
    bool CanRetryAtlas() const;
    const std::filesystem::path& GetPath() const noexcept { return m_path; }
    std::uint64_t GetRevision() const noexcept { return m_revision.load(std::memory_order_acquire); }

private:
    struct Impl;
    FontAsset();
    std::filesystem::path m_path;
    std::unique_ptr<Impl> m_impl;
    mutable std::mutex m_mutex;
    std::atomic<std::uint64_t> m_revision{ 1 };
};
