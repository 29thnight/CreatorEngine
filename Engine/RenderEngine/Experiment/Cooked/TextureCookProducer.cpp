#include "TextureCookProducer.h"

#include "CookSupport.h"

#include <algorithm>
#include <array>
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

        inline constexpr std::array<std::string_view, 3> kSupportedExtensions{
            ".png", ".hdr", ".dds"
        };
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
                + "' (허용: .png .hdr .dds)");
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
