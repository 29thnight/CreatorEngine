#include "AssetIdentityEpoch.h"
#include "AssetIdentityHex.h"

#include <Windows.h>
#include <bcrypt.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace assets
{
    namespace
    {
        void AddIssue(std::vector<EpochHeaderIssue>& issues, EpochHeaderIssueCode code,
            std::string context, std::string message)
        {
            issues.push_back({ code, std::move(context), std::move(message) });
        }

        [[nodiscard]] bool ReadScalar(const YAML::Node& parent, const char* key,
            std::string& out, std::vector<EpochHeaderIssue>& issues, bool required = true)
        {
            const YAML::Node node = parent[key];
            if (!node)
            {
                if (required)
                {
                    AddIssue(issues, EpochHeaderIssueCode::MissingField, key,
                        "필수 scalar가 없다.");
                }
                return !required;
            }
            if (!node.IsScalar())
            {
                AddIssue(issues, EpochHeaderIssueCode::InvalidDocument, key,
                    "scalar여야 한다.");
                return false;
            }
            try
            {
                out = node.as<std::string>();
                return true;
            }
            catch (const YAML::Exception& exception)
            {
                AddIssue(issues, EpochHeaderIssueCode::InvalidDocument, key,
                    exception.what());
                return false;
            }
        }
    }

    bool IsZeroSeed(const IdentityEpochSeed& seed) noexcept
    {
        return std::ranges::all_of(seed, [](std::uint8_t b) { return b == 0u; });
    }

    bool CreateIdentityEpochSeed(IdentityEpochSeed& out, std::string& outError) noexcept
    {
        outError.clear();
        IdentityEpochSeed seed{};
        const NTSTATUS status = ::BCryptGenRandom(nullptr, seed.data(),
            static_cast<ULONG>(seed.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0)
        {
            outError = "BCryptGenRandom failed";
            return false;
        }
        if (IsZeroSeed(seed))
        {
            // 2^-256 확률이지만 "0 = 미발급"이라는 규약과 충돌하므로 거부한다.
            outError = "generated seed is zero";
            return false;
        }
        out = seed;
        return true;
    }

    bool ValidateIdentityEpochHeader(const IdentityEpochHeader& header,
        std::vector<EpochHeaderIssue>& outIssues)
    {
        const std::size_t before = outIssues.size();
        if (header.identityProfile != kIdentityProfile)
        {
            AddIssue(outIssues, EpochHeaderIssueCode::ProfileMismatch, "identityProfile",
                "이 빌드의 프로필은 " + std::string(kIdentityProfile) + "이다: "
                + header.identityProfile);
        }
        if (header.identityEpoch.empty() || !IsWellFormedUtf8(header.identityEpoch))
        {
            AddIssue(outIssues, EpochHeaderIssueCode::InvalidEpochName, "identityEpoch",
                "비어 있지 않은 잘 형성된 UTF-8이어야 한다.");
        }
        else
        {
            IdentityIssue nfc{};
            if (!IsUtf8Nfc(header.identityEpoch, nfc))
            {
                AddIssue(outIssues, EpochHeaderIssueCode::InvalidEpochName, "identityEpoch",
                    std::string("NFC가 아니다(") + std::string(ToString(nfc)) + ").");
            }
        }
        if (IsZeroSeed(header.identityEpochSeed))
        {
            AddIssue(outIssues, EpochHeaderIssueCode::ZeroSeed, "identityEpochSeed",
                "seed가 0이다 — 발급되지 않았다.");
        }
        return outIssues.size() == before;
    }

    std::string WriteIdentityEpochHeader(const IdentityEpochHeader& header)
    {
        std::vector<EpochHeaderIssue> issues;
        if (!ValidateIdentityEpochHeader(header, issues)) return {};

        YAML::Node root(YAML::NodeType::Map);
        root["schemaVersion"] = kIdentityEpochHeaderSchemaVersion;
        root["identityProfile"] = header.identityProfile;
        root["identityEpoch"] = header.identityEpoch;
        root["identityEpochSeed"] = ToLowerHex(header.identityEpochSeed);
        if (!header.createdAt.empty()) root["createdAt"] = header.createdAt;

        YAML::Emitter emitter;
        emitter.SetIndent(2);
        emitter << root;
        if (!emitter.good()) return {};
        std::string out(emitter.c_str(), emitter.size());
        out.push_back('\n');
        return out;
    }

    bool ReadIdentityEpochHeader(std::string_view yaml, IdentityEpochHeader& out,
        std::vector<EpochHeaderIssue>& outIssues)
    {
        const std::size_t before = outIssues.size();
        IdentityEpochHeader parsed;
        try
        {
            const YAML::Node root = YAML::Load(std::string(yaml));
            if (!root || !root.IsMap())
            {
                AddIssue(outIssues, EpochHeaderIssueCode::InvalidDocument, "root",
                    "map document여야 한다.");
                return false;
            }

            std::string schema;
            if (ReadScalar(root, "schemaVersion", schema, outIssues)
                && schema != std::to_string(kIdentityEpochHeaderSchemaVersion))
            {
                AddIssue(outIssues, EpochHeaderIssueCode::UnsupportedSchema, "schemaVersion",
                    "지원하는 epoch header schemaVersion은 "
                    + std::to_string(kIdentityEpochHeaderSchemaVersion) + "이다: " + schema);
            }
            (void)ReadScalar(root, "identityProfile", parsed.identityProfile, outIssues);
            (void)ReadScalar(root, "identityEpoch", parsed.identityEpoch, outIssues);
            (void)ReadScalar(root, "createdAt", parsed.createdAt, outIssues, false);

            std::string seedHex;
            if (ReadScalar(root, "identityEpochSeed", seedHex, outIssues))
            {
                std::vector<std::uint8_t> seedBytes;
                if (!TryParseLowerHex(seedHex, seedBytes, kEpochSeedBytes))
                {
                    AddIssue(outIssues, EpochHeaderIssueCode::InvalidSeed, "identityEpochSeed",
                        "64자 소문자 16진(32바이트)이어야 한다.");
                }
                else
                {
                    std::copy(seedBytes.begin(), seedBytes.end(),
                        parsed.identityEpochSeed.begin());
                }
            }
        }
        catch (const YAML::Exception& exception)
        {
            AddIssue(outIssues, EpochHeaderIssueCode::InvalidDocument, "root",
                exception.what());
            return false;
        }

        if (outIssues.size() != before) return false;
        if (!ValidateIdentityEpochHeader(parsed, outIssues)) return false;
        out = std::move(parsed);
        return true;
    }
}
