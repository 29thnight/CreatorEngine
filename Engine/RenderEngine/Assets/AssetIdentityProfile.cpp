// RenderEngine 자신은 프로젝트 루트를 include 경로에 두지 않는다(소비자 쪽이
// `$(SolutionDir)Engine\RenderEngine\`를 더한다). 그래서 계층 안에서는 상대
// include, 밖(RenderTests·AssetCooker)에서는 `Assets/...`다 — Experiment와 같은 규칙.
#include "AssetIdentityProfile.h"

#include "Sha256.h"

#include <Windows.h>

#include <cstring>
#include <limits>

#pragma comment(lib, "normaliz.lib")

namespace assets
{
    namespace
    {
        constexpr std::string_view kKindNames[kSubAssetKindCount] = {
            "mesh", "material", "texture", "skeleton", "animation",
        };

        void AppendU32BE(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        }

        // 길이 접두 + 바이트. U32BE 범위를 넘으면 false.
        [[nodiscard]] bool AppendField(std::vector<std::uint8_t>& out,
            std::span<const std::uint8_t> bytes)
        {
            if (bytes.size() > static_cast<std::size_t>(
                (std::numeric_limits<std::uint32_t>::max)()))
            {
                return false;
            }
            AppendU32BE(out, static_cast<std::uint32_t>(bytes.size()));
            out.insert(out.end(), bytes.begin(), bytes.end());
            return true;
        }

        [[nodiscard]] std::span<const std::uint8_t> AsBytes(std::string_view text) noexcept
        {
            return { reinterpret_cast<const std::uint8_t*>(text.data()), text.size() };
        }

        // 문자열 필드 하나의 규약 검사: 비어 있지 않다 · 잘 형성된 UTF-8 · NFC.
        [[nodiscard]] bool ValidateTextField(std::string_view text,
            IdentityIssue emptyIssue, const char* fieldName,
            IdentityIssue& outIssue, std::string& outContext) noexcept
        {
            if (text.empty())
            {
                outIssue = emptyIssue;
                outContext = fieldName;
                return false;
            }
            if (!IsWellFormedUtf8(text))
            {
                outIssue = IdentityIssue::InvalidUtf8;
                outContext = fieldName;
                return false;
            }
            IdentityIssue nfcIssue = IdentityIssue::None;
            if (!IsUtf8Nfc(text, nfcIssue))
            {
                outIssue = nfcIssue;
                outContext = fieldName;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool BuildWithProfile(std::string_view profile,
            const IdentityInput& input, std::vector<std::uint8_t>& out,
            IdentityIssue& outIssue, std::string& outContext) noexcept
        {
            out.clear();
            outIssue = IdentityIssue::None;
            outContext.clear();

            if (profile.empty())
            {
                outIssue = IdentityIssue::EmptyProfile;
                outContext = "profile";
                return false;
            }
            if (!ValidateTextField(input.domain, IdentityIssue::EmptyDomain,
                "domain", outIssue, outContext))
            {
                return false;
            }
            if (input.namespaceBytes.empty())
            {
                outIssue = IdentityIssue::EmptyNamespace;
                outContext = "namespace";
                return false;
            }
            if (!ValidateTextField(input.kind, IdentityIssue::EmptyKind,
                "kind", outIssue, outContext))
            {
                return false;
            }
            if (!ValidateTextField(input.stableKey, IdentityIssue::EmptyStableKey,
                "stableKey", outIssue, outContext))
            {
                return false;
            }

            try
            {
                out.reserve(profile.size() + 1u + 4u * 4u + input.domain.size()
                    + input.namespaceBytes.size() + input.kind.size()
                    + input.stableKey.size());
                const std::span<const std::uint8_t> profileBytes = AsBytes(profile);
                out.insert(out.end(), profileBytes.begin(), profileBytes.end());
                out.push_back(0x00u);
                if (!AppendField(out, AsBytes(input.domain)))
                {
                    outIssue = IdentityIssue::FieldTooLong; outContext = "domain";
                    out.clear(); return false;
                }
                if (!AppendField(out, input.namespaceBytes))
                {
                    outIssue = IdentityIssue::FieldTooLong; outContext = "namespace";
                    out.clear(); return false;
                }
                if (!AppendField(out, AsBytes(input.kind)))
                {
                    outIssue = IdentityIssue::FieldTooLong; outContext = "kind";
                    out.clear(); return false;
                }
                if (!AppendField(out, AsBytes(input.stableKey)))
                {
                    outIssue = IdentityIssue::FieldTooLong; outContext = "stableKey";
                    out.clear(); return false;
                }
            }
            catch (...)
            {
                // std::bad_alloc — 신원을 "대충" 내는 것보다 실패가 낫다.
                outIssue = IdentityIssue::FieldTooLong;
                outContext = "allocation";
                out.clear();
                return false;
            }
            return true;
        }

        [[nodiscard]] IdentityDerivation DeriveWithProfile(std::string_view profile,
            const IdentityInput& input) noexcept
        {
            IdentityDerivation result;
            std::vector<std::uint8_t> bytes;
            if (!BuildWithProfile(profile, input, bytes, result.issue, result.context))
            {
                return result;
            }
            result.uuid = DeriveUuidFromIdentityInputBytes(bytes);
            return result;
        }

        // UTF-8 → UTF-16. 잘 형성됐다는 전제(IsWellFormedUtf8 뒤에 부른다).
        [[nodiscard]] bool ToWide(std::string_view text, std::wstring& out) noexcept
        {
            if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                return false;
            const int needed = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (needed <= 0) return false;
            try
            {
                out.assign(static_cast<std::size_t>(needed), L'\0');
            }
            catch (...) { return false; }
            const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                text.data(), static_cast<int>(text.size()), out.data(), needed);
            return written == needed;
        }

        [[nodiscard]] bool IsAscii(std::string_view text) noexcept
        {
            for (unsigned char c : text)
            {
                if (c >= 0x80u) return false;
            }
            return true;
        }
    }

    std::string_view ToKindName(SubAssetKind kind) noexcept
    {
        const auto index = static_cast<std::size_t>(kind);
        return index < kSubAssetKindCount ? kKindNames[index] : std::string_view{};
    }

    bool TryParseKindName(std::string_view name, SubAssetKind& outKind) noexcept
    {
        for (std::size_t index = 0; index < kSubAssetKindCount; ++index)
        {
            if (kKindNames[index] == name)
            {
                outKind = static_cast<SubAssetKind>(index);
                return true;
            }
        }
        return false;
    }

    std::string_view ToString(IdentityIssue issue) noexcept
    {
        switch (issue)
        {
        case IdentityIssue::None:                return "none";
        case IdentityIssue::EmptyDomain:         return "empty-domain";
        case IdentityIssue::EmptyNamespace:      return "empty-namespace";
        case IdentityIssue::EmptyKind:           return "empty-kind";
        case IdentityIssue::EmptyStableKey:      return "empty-stable-key";
        case IdentityIssue::InvalidUtf8:         return "invalid-utf8";
        case IdentityIssue::NotNfc:              return "not-nfc";
        case IdentityIssue::NormalizationFailed: return "normalization-failed";
        case IdentityIssue::FieldTooLong:        return "field-too-long";
        case IdentityIssue::NamespaceLength:     return "namespace-length";
        case IdentityIssue::NamespaceNotV8:      return "namespace-not-v8";
        case IdentityIssue::EmptyProfile:        return "empty-profile";
        }
        return "unknown";
    }

    bool BuildIdentityInputBytes(const IdentityInput& input,
        std::vector<std::uint8_t>& out, IdentityIssue& outIssue,
        std::string& outContext) noexcept
    {
        return BuildWithProfile(kIdentityProfile, input, out, outIssue, outContext);
    }

    Uuid::Uuid16 DeriveUuidFromIdentityInputBytes(
        std::span<const std::uint8_t> canonicalInput) noexcept
    {
        const Hash::Sha256Digest digest =
            Hash::Sha256::Compute(canonicalInput.data(), canonicalInput.size());

        Uuid::Uuid16 uuid;
        std::memcpy(uuid.data.data(), digest.data(), kUuidBytes);
        uuid.data[6] = static_cast<std::uint8_t>((uuid.data[6] & 0x0Fu) | 0x80u);
        uuid.data[8] = static_cast<std::uint8_t>((uuid.data[8] & 0x3Fu) | 0x80u);
        return uuid;
    }

    IdentityDerivation DeriveIdentity(const IdentityInput& input) noexcept
    {
        return DeriveWithProfile(kIdentityProfile, input);
    }

    IdentityDerivation DeriveIdentityWithProfile(std::string_view profile,
        const IdentityInput& input) noexcept
    {
        return DeriveWithProfile(profile, input);
    }

    IdentityDerivation DeriveModelId(const IdentityEpochSeed& epochSeed,
        std::string_view modelAuthoringKey) noexcept
    {
        IdentityInput input;
        input.domain = kDomainModel;
        input.namespaceBytes = std::span<const std::uint8_t>(epochSeed);
        input.kind = kKindModel;
        input.stableKey = modelAuthoringKey;
        return DeriveIdentity(input);
    }

    IdentityDerivation DeriveSubAssetId(const Uuid::Uuid16& modelId,
        SubAssetKind kind, std::string_view stableKey) noexcept
    {
        IdentityDerivation result;
        if (!IsUuidV8(modelId))
        {
            result.issue = IdentityIssue::NamespaceNotV8;
            result.context = "modelId";
            return result;
        }
        const std::string_view kindName = ToKindName(kind);
        if (kindName.empty())
        {
            result.issue = IdentityIssue::EmptyKind;
            result.context = "kind";
            return result;
        }
        IdentityInput input;
        input.domain = kDomainSubAsset;
        input.namespaceBytes = std::span<const std::uint8_t>(modelId.data);
        input.kind = kindName;
        input.stableKey = stableKey;
        return DeriveIdentity(input);
    }

    bool IsUuidV8(const Uuid::Uuid16& value) noexcept
    {
        return !value.IsNil()
            && (value.data[6] & 0xF0u) == 0x80u
            && (value.data[8] & 0xC0u) == 0x80u;
    }

    bool TryParseCanonicalUuidV8(std::string_view text, Uuid::Uuid16& out) noexcept
    {
        Uuid::Uuid16 parsed{};
        if (!Uuid::TryParse(text, parsed)) return false;
        if (!IsUuidV8(parsed)) return false;
        // 소문자 8-4-4-4-12만. Uuid::TryParse가 받는 대문자·brace·무하이픈은 여기서
        // 걸린다(재표기가 원문과 다르다).
        if (Uuid::ToString(parsed) != text) return false;
        out = parsed;
        return true;
    }

    bool IsWellFormedUtf8(std::string_view text) noexcept
    {
        // RFC 3629 §4 문법 그대로. overlong·surrogate·>U+10FFFF·절단을 거부한다.
        const auto* p = reinterpret_cast<const unsigned char*>(text.data());
        const std::size_t n = text.size();
        std::size_t i = 0;
        while (i < n)
        {
            const unsigned char c = p[i];
            if (c < 0x80u) { ++i; continue; }

            std::size_t length = 0;
            unsigned char secondMin = 0x80u, secondMax = 0xBFu;
            if (c >= 0xC2u && c <= 0xDFu) { length = 2; }
            else if (c == 0xE0u) { length = 3; secondMin = 0xA0u; }
            else if (c >= 0xE1u && c <= 0xECu) { length = 3; }
            else if (c == 0xEDu) { length = 3; secondMax = 0x9Fu; } // surrogate 제외
            else if (c >= 0xEEu && c <= 0xEFu) { length = 3; }
            else if (c == 0xF0u) { length = 4; secondMin = 0x90u; }
            else if (c >= 0xF1u && c <= 0xF3u) { length = 4; }
            else if (c == 0xF4u) { length = 4; secondMax = 0x8Fu; }  // > U+10FFFF 제외
            else return false; // 0x80~0xC1, 0xF5~0xFF

            if (i + length > n) return false;
            const unsigned char second = p[i + 1];
            if (second < secondMin || second > secondMax) return false;
            for (std::size_t k = 2; k < length; ++k)
            {
                const unsigned char tail = p[i + k];
                if (tail < 0x80u || tail > 0xBFu) return false;
            }
            i += length;
        }
        return true;
    }

    bool IsUtf8Nfc(std::string_view text, IdentityIssue& outIssue) noexcept
    {
        outIssue = IdentityIssue::None;
        if (IsAscii(text)) return true; // ASCII는 정의상 NFC

        std::wstring wide;
        if (!ToWide(text, wide))
        {
            outIssue = IdentityIssue::InvalidUtf8;
            return false;
        }
        if (wide.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            outIssue = IdentityIssue::FieldTooLong;
            return false;
        }

        ::SetLastError(ERROR_SUCCESS);
        const BOOL normalized = ::IsNormalizedString(NormalizationC, wide.c_str(),
            static_cast<int>(wide.size()));
        if (normalized) return true;

        // FALSE는 "정규화 안 됨"과 "질의 실패" 둘 다다. GetLastError로 가른다 —
        // 실패를 "정규화됨"으로도 "안 됨"으로도 읽지 않고 별도 사유로 낸다.
        outIssue = (::GetLastError() == ERROR_SUCCESS)
            ? IdentityIssue::NotNfc : IdentityIssue::NormalizationFailed;
        return false;
    }

    bool NormalizeUtf8Nfc(std::string_view text, std::string& out, std::string& outError)
    {
        out.clear();
        outError.clear();
        if (!IsWellFormedUtf8(text))
        {
            outError = "invalid UTF-8";
            return false;
        }
        if (IsAscii(text))
        {
            out.assign(text);
            return true;
        }

        std::wstring wide;
        if (!ToWide(text, wide))
        {
            outError = "UTF-8 -> UTF-16 conversion failed";
            return false;
        }

        // NormalizeString은 추정 길이를 내고, 부족하면 음수로 필요 길이를 되돌린다.
        int estimate = ::NormalizeString(NormalizationC, wide.c_str(),
            static_cast<int>(wide.size()), nullptr, 0);
        if (estimate <= 0)
        {
            outError = "NormalizeString(size) failed";
            return false;
        }
        std::wstring normalized;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            normalized.assign(static_cast<std::size_t>(estimate), L'\0');
            ::SetLastError(ERROR_SUCCESS);
            const int written = ::NormalizeString(NormalizationC, wide.c_str(),
                static_cast<int>(wide.size()), normalized.data(), estimate);
            if (written > 0)
            {
                normalized.resize(static_cast<std::size_t>(written));
                break;
            }
            const DWORD error = ::GetLastError();
            if (error == ERROR_INSUFFICIENT_BUFFER && written < 0)
            {
                estimate = -written;
                continue;
            }
            outError = "NormalizeString failed (" + std::to_string(error) + ")";
            return false;
        }

        const int needed = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            normalized.c_str(), static_cast<int>(normalized.size()), nullptr, 0,
            nullptr, nullptr);
        if (needed <= 0)
        {
            outError = "UTF-16 -> UTF-8 conversion failed";
            return false;
        }
        out.assign(static_cast<std::size_t>(needed), '\0');
        const int written = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            normalized.c_str(), static_cast<int>(normalized.size()), out.data(),
            needed, nullptr, nullptr);
        if (written != needed)
        {
            out.clear();
            outError = "UTF-16 -> UTF-8 conversion truncated";
            return false;
        }
        return true;
    }
}
