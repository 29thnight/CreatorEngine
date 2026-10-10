#include "TextureCookProducer.h"

#include "CookSupport.h"
#include "ModelCookIdentity.h"
#include "CookedTexture.h"
#include "TextureImportSettings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <utility>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace experiment::cooked
{
    static_assert(kTextureArtifactVersion == kCookedTextureSchemaVersion);

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

        inline constexpr std::array<std::string_view, 5> kSupportedExtensions{
            ".png", ".hdr", ".dds", ".jpg", ".jpeg"
        };

        [[nodiscard]] bool StartsWith(std::span<const std::byte> bytes,
            std::span<const std::uint8_t> magic) noexcept
        {
            if (bytes.size() < magic.size())
            {
                return false;
            }
            for (std::size_t index = 0u; index < magic.size(); ++index)
            {
                if (static_cast<std::uint8_t>(bytes[index]) != magic[index])
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool ReadInput(const std::filesystem::path& path,
            std::uint64_t limit, std::vector<std::byte>& bytes, std::string& failure)
        {
#ifdef _WIN32
            // Editor rename/save must remain possible during both initial
            // capture and the independent disk recheck after cooking.
            const auto handle = CreateFileW(path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                failure = "Cannot open texture input: " + path.string();
                return false;
            }
            struct CloseInput final
            {
                HANDLE handle;
                ~CloseInput() { CloseHandle(handle); }
            } close{ handle };
            LARGE_INTEGER size{};
            if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0
                || static_cast<std::uint64_t>(size.QuadPart) > limit
                || static_cast<std::uint64_t>(size.QuadPart) > MAXDWORD)
            {
                failure = "Texture input exceeds its byte budget: " + path.string();
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size.QuadPart));
            DWORD read{};
            LARGE_INTEGER after{};
            if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)
                || read != bytes.size() || !GetFileSizeEx(handle, &after) || after.QuadPart != size.QuadPart)
            {
                failure = "Texture input changed size or could not be read: " + path.string();
                return false;
            }
            return true;
#else
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                failure = "Cannot open texture input: " + path.string();
                return false;
            }
            const auto size = stream.tellg();
            if (size < 0 || static_cast<std::uint64_t>(size) > limit)
            {
                failure = "Texture input exceeds its byte budget: " + path.string();
                return false;
            }
            bytes.resize(static_cast<std::size_t>(size));
            stream.seekg(0, std::ios::beg);
            if (!bytes.empty())
            {
                stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            }
            if (!stream || stream.peek() != std::char_traits<char>::eof())
            {
                failure = "Texture input changed size or could not be read: " + path.string();
                return false;
            }
            return true;
#endif
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

        if (StartsWith(bytes, kPng))
        {
            return ".png";
        }
        if (StartsWith(bytes, kJpeg))
        {
            return ".jpg";
        }
        if (StartsWith(bytes, kDds))
        {
            return ".dds";
        }
        if (StartsWith(bytes, kRadiance))
        {
            return ".hdr";
        }
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
                + "' (허용: .png .hdr .dds .jpg .jpeg)");
            return result;
        }

        std::filesystem::path metaPath = source;
        metaPath += ".meta";
        error.clear();
        metaPath = std::filesystem::weakly_canonical(metaPath, error);
        if (error || !IsContainedPath(assetRoot, metaPath))
        {
            AddIssue(result, "texture.meta", "Texture sidecar escapes the asset root.");
            return result;
        }
        constexpr std::uint64_t kMaxSourceBytes = 512ull * 1024ull * 1024ull;
        constexpr std::uint64_t kMaxMetaBytes = 16ull * 1024ull * 1024ull;
        std::string failure;
        const auto capture = [&](const std::filesystem::path& path, std::uint64_t limit,
            std::vector<std::byte>& out)
        {
            const bool read = request.captureSource
                ? request.captureSource(path, out, failure) : ReadInput(path, limit, out, failure);
            if (read && out.size() > limit)
            {
                failure = "Captured texture input exceeds its byte budget: " + path.string();
                return false;
            }
            return read;
        };
        std::vector<std::byte> metaBytes;
        if (!capture(metaPath, kMaxMetaBytes, metaBytes))
        {
            AddIssue(result, "texture.meta", std::move(failure));
            return result;
        }
        const std::string_view metaText(reinterpret_cast<const char*>(metaBytes.data()), metaBytes.size());
        AssetId textureAssetId{};
        std::vector<ModelIdentityIssue> identityIssues;
        if (!ReadAssetIdFromMeta(metaText, textureAssetId, identityIssues) || !IsAssetIdV4(textureAssetId))
        {
            AddIssue(result, "texture.meta", "Texture sidecar must contain a canonical UUIDv4 GUID.");
            return result;
        }
        TextureImportSettings settings;
        if (!ParseTextureImportSettings(metaText, settings, failure))
        {
            AddIssue(result, "texture.settings", std::move(failure));
            return result;
        }
        std::vector<std::byte> bytes;
        if (!capture(source, kMaxSourceBytes, bytes) || bytes.empty())
        {
            AddIssue(result, "texture.read", failure.empty() ? "Source texture is empty." : std::move(failure));
            return result;
        }
        if (SniffTextureExtension(bytes) != (extension == ".jpeg" ? ".jpg" : extension))
        {
            AddIssue(result, "texture.decode", "Source image signature does not match its declared extension.");
            return result;
        }
        TextureCookProduct product;
        if (!ComputeSha256(bytes, product.sourceContentSha256, failure) ||
            !ComputeSha256(metaBytes, product.sourceMetaSha256, failure))
        {
            AddIssue(result, "texture.sha256", std::move(failure));
            return result;
        }
        if (!CookTexture(bytes, settings, product.artifactBytes, failure))
        {
            AddIssue(result, "texture.decode", std::move(failure));
            return result;
        }
        // Compare the captured inputs to the actual producer inputs, never to
        // the cooked bytes: cooking necessarily changes the representation.
        std::vector<std::byte> currentSource;
        std::vector<std::byte> currentMeta;
        Sha256Digest sourceDigest{};
        Sha256Digest metaDigest{};
        if (!ReadInput(source, kMaxSourceBytes, currentSource, failure) ||
            !ReadInput(metaPath, kMaxMetaBytes, currentMeta, failure) ||
            !ComputeSha256(currentSource, sourceDigest, failure) ||
            !ComputeSha256(currentMeta, metaDigest, failure) ||
            sourceDigest != product.sourceContentSha256 || metaDigest != product.sourceMetaSha256)
        {
            AddIssue(result, "texture.snapshot", "Texture source or sidecar changed during cooking: " + failure);
            return result;
        }

        const std::string artifactPath =
            MakeDerivedTextureArtifactPath(textureAssetId, ".cetex");
        if (artifactPath.empty())
        {
            AddIssue(result, "texture.artifactPath",
                "texture GUID가 Derived 경로를 만들지 못했다.");
            return result;
        }

        Sha256Digest digest{};
        std::string hashError;
        if (!ComputeSha256(product.artifactBytes, digest, hashError))
        {
            AddIssue(result, "texture.sha256", std::move(hashError));
            return result;
        }

        product.textureAssetId = textureAssetId;
        product.artifactPath = artifactPath;
        product.sourceExtension = extension;
        product.importRecipe = TextureImportRecipe(settings);

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
