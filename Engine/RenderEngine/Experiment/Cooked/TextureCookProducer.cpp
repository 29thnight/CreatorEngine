#include "TextureCookProducer.h"

#include "CookSupport.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        void AddIssue(TextureCookProductResult& result,
            std::string context, std::string message)
        {
            result.issues.push_back({ std::move(context), std::move(message) });
        }

        // 확장자만 소문자로 접는다. ASCII 로 충분하다 — 확장자에 비ASCII 를
        // 허용하지 않는다(아래 allowlist 가 어차피 거른다).
        [[nodiscard]] std::string LowercaseAscii(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char ch)
            {
                return static_cast<char>(
                    ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            });
            return text;
        }

        inline constexpr std::array<std::string_view, 4> kSupportedExtensions{
            ".png", ".hdr", ".dds", ".jpg"
        };

        [[nodiscard]] bool StartsWith(std::span<const std::byte> bytes,
            std::span<const std::uint8_t> magic) noexcept
        {
            if (bytes.size() < magic.size()) return false;
            for (std::size_t index = 0u; index < magic.size(); ++index)
            {
                if (static_cast<std::uint8_t>(bytes[index]) != magic[index])
                    return false;
            }
            return true;
        }
    }

    std::string_view SniffTextureExtension(
        std::span<const std::byte> bytes) noexcept
    {
        static constexpr std::uint8_t kPng[]{ 0x89u, 0x50u, 0x4Eu, 0x47u,
            0x0Du, 0x0Au, 0x1Au, 0x0Au };
        // JPEG 는 SOI(FFD8) 뒤에 마커 하나가 더 온다. FFD8 만 보면 두 바이트
        // 우연에 걸리므로 세 번째까지 본다.
        static constexpr std::uint8_t kJpeg[]{ 0xFFu, 0xD8u, 0xFFu };
        static constexpr std::uint8_t kDds[]{ 0x44u, 0x44u, 0x53u, 0x20u };  // "DDS "
        // Radiance HDR. "#?RADIANCE" 와 "#?RGBE" 두 서명이 모두 쓰인다.
        static constexpr std::uint8_t kRadiance[]{ 0x23u, 0x3Fu };           // "#?"

        if (StartsWith(bytes, kPng)) return ".png";
        if (StartsWith(bytes, kJpeg)) return ".jpg";
        if (StartsWith(bytes, kDds)) return ".dds";
        if (StartsWith(bytes, kRadiance)) return ".hdr";
        return {};
    }

    bool IsSupportedTextureExtension(
        std::string_view lowercaseExtension) noexcept
    {
        return std::ranges::find(kSupportedExtensions, lowercaseExtension)
            != kSupportedExtensions.end();
    }

    TextureCookProductResult BuildTextureCookProduct(
        const TextureCookProductRequest& request)
    {
        TextureCookProductResult result;
        std::error_code error;

        const std::filesystem::path assetRoot =
            std::filesystem::weakly_canonical(request.assetRoot, error);
        if (error || assetRoot.empty()
            || !std::filesystem::is_directory(assetRoot, error))
        {
            AddIssue(result, "request.assetRoot",
                "asset root가 유효한 디렉터리가 아니다.");
            return result;
        }

        error.clear();
        const std::filesystem::path source =
            std::filesystem::weakly_canonical(request.sourcePath, error);
        if (error || source.empty()
            || !std::filesystem::is_regular_file(source, error))
        {
            AddIssue(result, "request.sourcePath",
                "source texture가 유효한 파일이 아니다.");
            return result;
        }
        if (!IsContainedPath(assetRoot, source))
        {
            AddIssue(result, "request.sourcePath",
                "source texture가 asset root 밖에 있다.");
            return result;
        }

        const std::string extension =
            LowercaseAscii(source.extension().string());
        if (!IsSupportedTextureExtension(extension))
        {
            // ★ 조용히 건너뛰지 않는다. 건너뛰면 그 텍스처를 참조하는 재질이
            //   D5-b2c-5 폐포 검사에서 "없는 의존"으로 터지는데, 그때는 원인이
            //   여기서 멀어져 있다.
            AddIssue(result, "texture.extension",
                "지원하지 않는 texture 확장자다: '" + extension
                + "' (허용: .png .hdr .dds .jpg)");
            return result;
        }

        std::filesystem::path metaPath = source;
        metaPath += ".meta";
        AssetId textureAssetId{};
        std::string metaFailure;
        if (!ReadMetaAssetId(metaPath, textureAssetId, metaFailure))
        {
            AddIssue(result, "texture.meta", std::move(metaFailure));
            return result;
        }
        if (!IsAssetIdV4(textureAssetId))
        {
            AddIssue(result, "texture.meta",
                "texture meta GUID가 canonical UUIDv4가 아니다.");
            return result;
        }

        std::vector<std::byte> bytes;
        if (!ReadBinaryFile(source, bytes))
        {
            AddIssue(result, "texture.read",
                "source texture를 읽을 수 없다: " + source.string());
            return result;
        }
        if (bytes.empty())
        {
            // 0바이트는 디코더가 못 읽는다. 해시와 크기는 멀쩡히 계산되므로
            // manifest 검증만으로는 안 걸린다 — 여기서 막아야 한다.
            AddIssue(result, "texture.read",
                "source texture가 비어 있다: " + source.string());
            return result;
        }

        const std::string artifactPath =
            MakeDerivedTextureArtifactPath(textureAssetId, extension);
        if (artifactPath.empty())
        {
            AddIssue(result, "texture.artifactPath",
                "texture GUID가 Derived 경로를 만들지 못했다.");
            return result;
        }

        Sha256Digest digest{};
        std::string hashError;
        if (!ComputeSha256(bytes, digest, hashError))
        {
            AddIssue(result, "texture.sha256", std::move(hashError));
            return result;
        }

        TextureCookProduct product;
        product.textureAssetId = textureAssetId;
        product.artifactPath = artifactPath;
        product.sourceExtension = extension;
        product.artifactBytes = std::move(bytes);

        CookedAssetManifestEntry entry;
        entry.assetId = textureAssetId;
        entry.kind = CookedAssetKind::Texture;
        entry.formatVersion = kTextureArtifactVersion;
        entry.byteSize = product.artifactBytes.size();
        entry.contentSha256 = digest;
        entry.artifactPath = artifactPath;
        // 텍스처는 잎이다. 의존이 없다.
        product.manifestEntry = std::move(entry);

        result.product = std::move(product);
        return result;
    }
}
