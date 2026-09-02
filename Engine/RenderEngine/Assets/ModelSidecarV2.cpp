#include "ModelSidecarV2.h"
#include "AssetIdentityHex.h"
#include "AssetIdentityRegistry.h"

#include "Sha256.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <charconv>
#include <set>
#include <utility>

namespace assets
{
    namespace
    {
        constexpr const char* kIdentityKeys[] = {
            "schemaVersion", "identityProfile", "identityEpoch", "authoringKey",
            "assetId", "generation", "sourceFingerprint", "subAssets",
        };

        void AddIssue(std::vector<SidecarIssue>& issues, SidecarIssueCode code,
            std::string context, std::string message)
        {
            issues.push_back({ code, std::move(context), std::move(message) });
        }

        [[nodiscard]] bool ReadScalar(const YAML::Node& parent, const char* key,
            const std::string& context, std::string& out, std::vector<SidecarIssue>& issues,
            bool required = true)
        {
            const YAML::Node node = parent[key];
            if (!node)
            {
                if (required)
                {
                    AddIssue(issues, SidecarIssueCode::MissingField, context + "." + key,
                        "필수 scalar가 없다.");
                }
                return !required;
            }
            if (!node.IsScalar())
            {
                AddIssue(issues, SidecarIssueCode::InvalidDocument, context + "." + key,
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
                AddIssue(issues, SidecarIssueCode::InvalidDocument, context + "." + key,
                    exception.what());
                return false;
            }
        }

        [[nodiscard]] bool ReadV8(const YAML::Node& parent, const char* key,
            const std::string& context, Uuid::Uuid16& out, std::vector<SidecarIssue>& issues)
        {
            std::string text;
            if (!ReadScalar(parent, key, context, text, issues)) return false;
            if (!TryParseCanonicalUuidV8(text, out))
            {
                AddIssue(issues, SidecarIssueCode::InvalidAssetId, context + "." + key,
                    "소문자 canonical UUIDv8이어야 한다: " + text);
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ValidateModelKeyText(std::string_view text, std::string& error) noexcept
        {
            StableKeyOrigin origin{};
            if (!TryParseStableKey(text, origin, error)) return false;
            if (origin == StableKeyOrigin::Semantic)
            {
                error = "model authoringKey는 exporter:|authoring: 만 허용한다(name: 불가): "
                    + std::string(text);
                return false;
            }
            return true;
        }

        // 문서 자체의 형식 규칙 — Read와 Write·Build가 같은 함수를 쓴다.
        [[nodiscard]] bool CheckDocumentShape(const ModelSidecarV2& doc,
            std::vector<SidecarIssue>& issues)
        {
            const std::size_t before = issues.size();
            std::string error;
            if (doc.identityProfile != kIdentityProfile)
            {
                AddIssue(issues, SidecarIssueCode::ProfileMismatch, "identityProfile",
                    "이 빌드의 프로필은 " + std::string(kIdentityProfile) + "이다: " + doc.identityProfile);
            }
            if (doc.identityEpoch.empty())
            {
                AddIssue(issues, SidecarIssueCode::MissingField, "identityEpoch", "비어 있다.");
            }
            if (!ValidateModelKeyText(doc.authoringKey, error))
            {
                AddIssue(issues, SidecarIssueCode::InvalidModelKey, "authoringKey", error);
            }
            if (!IsUuidV8(doc.assetId))
            {
                AddIssue(issues, SidecarIssueCode::InvalidAssetId, "assetId", "UUIDv8이 아니다.");
            }
            if (doc.generation == 0u)
            {
                AddIssue(issues, SidecarIssueCode::InvalidGeneration, "generation",
                    "generation은 1 이상이다(0 = 게시되지 않음).");
            }
            if (!IsFingerprintText(doc.sourceFingerprint))
            {
                AddIssue(issues, SidecarIssueCode::InvalidFingerprint, "sourceFingerprint",
                    "sha256:<64 소문자 hex>여야 한다.");
            }

            std::set<std::pair<SubAssetKind, std::string>> keys;
            std::set<Uuid::Uuid16> ids{ doc.assetId };
            for (std::size_t i = 0; i < doc.subAssets.size(); ++i)
            {
                const ModelSubAssetRecord& r = doc.subAssets[i];
                const std::string ctx = "subAssets[" + std::to_string(i) + "]";
                StableKeyOrigin origin{};
                if (!TryParseStableKey(r.stableKey, origin, error))
                {
                    AddIssue(issues, SidecarIssueCode::InvalidStableKey, ctx + ".stableKey", error);
                }
                else if (!keys.emplace(r.kind, r.stableKey).second)
                {
                    AddIssue(issues, SidecarIssueCode::DuplicateStableKey, ctx + ".stableKey",
                        "같은 kind 안에서 stable key가 중복됐다: " + r.stableKey);
                }
                if (!IsUuidV8(r.assetId))
                {
                    AddIssue(issues, SidecarIssueCode::InvalidAssetId, ctx + ".assetId", "UUIDv8이 아니다.");
                }
                else if (!ids.insert(r.assetId).second)
                {
                    AddIssue(issues, SidecarIssueCode::DuplicateAssetId, ctx + ".assetId",
                        "assetId가 sidecar 안에서 중복됐다: " + Uuid::ToString(r.assetId));
                }
                if (!r.fingerprint.empty() && !IsFingerprintText(r.fingerprint))
                {
                    AddIssue(issues, SidecarIssueCode::InvalidFingerprint, ctx + ".fingerprint",
                        "sha256:<64 소문자 hex>여야 한다.");
                }
            }
            return issues.size() == before;
        }
    }

    std::string_view ToString(SidecarIssueCode code) noexcept
    {
        switch (code)
        {
        case SidecarIssueCode::InvalidDocument:    return "invalid-document";
        case SidecarIssueCode::MissingField:       return "missing-field";
        case SidecarIssueCode::LegacySchema:       return "legacy-schema";
        case SidecarIssueCode::UnsupportedSchema:  return "unsupported-schema";
        case SidecarIssueCode::LegacyGuidField:    return "legacy-guid-field";
        case SidecarIssueCode::ProfileMismatch:    return "profile-mismatch";
        case SidecarIssueCode::EpochMismatch:      return "epoch-mismatch";
        case SidecarIssueCode::InvalidAssetId:     return "invalid-asset-id";
        case SidecarIssueCode::InvalidStableKey:   return "invalid-stable-key";
        case SidecarIssueCode::InvalidModelKey:    return "invalid-model-key";
        case SidecarIssueCode::InvalidKind:        return "invalid-kind";
        case SidecarIssueCode::InvalidGeneration:  return "invalid-generation";
        case SidecarIssueCode::InvalidFingerprint: return "invalid-fingerprint";
        case SidecarIssueCode::DuplicateStableKey: return "duplicate-stable-key";
        case SidecarIssueCode::DuplicateAssetId:   return "duplicate-asset-id";
        case SidecarIssueCode::RecomputeMismatch:  return "recompute-mismatch";
        case SidecarIssueCode::Collision:          return "collision";
        }
        return "unknown";
    }

    bool IsFingerprintText(std::string_view text) noexcept
    {
        if (!text.starts_with(kFingerprintPrefix)) return false;
        std::vector<std::uint8_t> bytes;
        return TryParseLowerHex(text.substr(kFingerprintPrefix.size()), bytes, 32u);
    }

    std::string MakeSourceFingerprint(std::span<const std::uint8_t> bytes)
    {
        return std::string(kFingerprintPrefix)
            + Hash::ToHex(Hash::Sha256::Compute(bytes.data(), bytes.size()));
    }

    std::string CreateModelAuthoringKey(std::string& outError)
    {
        std::array<std::uint8_t, kAuthoringKeyBytes> bytes{};
        if (!CreateAuthoringKeyBytes(bytes, outError)) return {};
        return MakeAuthoringStableKey(bytes);
    }

    bool ReadModelSidecarV2(std::string_view yaml, ModelSidecarV2& out,
        std::vector<SidecarIssue>& outIssues)
    {
        const std::size_t before = outIssues.size();
        ModelSidecarV2 doc;
        try
        {
            const YAML::Node root = YAML::Load(std::string(yaml));
            if (!root || !root.IsMap())
            {
                AddIssue(outIssues, SidecarIssueCode::InvalidDocument, "root", "map document여야 한다.");
                return false;
            }
            if (root["guid"])
            {
                AddIssue(outIssues, SidecarIssueCode::LegacyGuidField, "guid",
                    "legacy 최상위 guid는 v2 sidecar에 없다(alias 금지 §8.1).");
            }

            const YAML::Node schema = root["schemaVersion"];
            if (!schema)
            {
                const YAML::Node legacy = root["subAssets"];
                const bool isV1 = legacy && legacy.IsMap() && legacy["schemaVersion"];
                AddIssue(outIssues, isV1 ? SidecarIssueCode::LegacySchema : SidecarIssueCode::MissingField,
                    "schemaVersion", isV1
                    ? "v1 sidecar(subAssets.schemaVersion)다 — MBC4 offline migrator의 입력이며 v2 reader는 거부한다."
                    : "최상위 schemaVersion이 없다.");
                return false;
            }
            std::string schemaText;
            if (ReadScalar(root, "schemaVersion", "root", schemaText, outIssues)
                && schemaText != std::to_string(kModelSidecarSchemaVersion))
            {
                AddIssue(outIssues, SidecarIssueCode::UnsupportedSchema, "schemaVersion",
                    "지원하는 model sidecar schemaVersion은 2다: " + schemaText);
                return false;
            }

            (void)ReadScalar(root, "identityProfile", "root", doc.identityProfile, outIssues);
            (void)ReadScalar(root, "identityEpoch", "root", doc.identityEpoch, outIssues);
            (void)ReadScalar(root, "authoringKey", "root", doc.authoringKey, outIssues);
            (void)ReadV8(root, "assetId", "root", doc.assetId, outIssues);
            (void)ReadScalar(root, "sourceFingerprint", "root", doc.sourceFingerprint, outIssues);

            std::string generationText;
            if (ReadScalar(root, "generation", "root", generationText, outIssues))
            {
                std::uint64_t value{};
                const auto* first = generationText.data();
                const auto* last = first + generationText.size();
                const auto parsed = std::from_chars(first, last, value);
                if (parsed.ec != std::errc{} || parsed.ptr != last)
                {
                    AddIssue(outIssues, SidecarIssueCode::InvalidGeneration, "generation",
                        "부호 없는 정수여야 한다: " + generationText);
                }
                else
                {
                    doc.generation = value;
                }
            }

            const YAML::Node subAssets = root["subAssets"];
            if (!subAssets || !subAssets.IsSequence())
            {
                AddIssue(outIssues, subAssets ? SidecarIssueCode::InvalidDocument
                    : SidecarIssueCode::MissingField, "subAssets", "subasset sequence가 필요하다(비어도 sequence).");
            }
            else
            {
                for (std::size_t i = 0; i < subAssets.size(); ++i)
                {
                    const YAML::Node node = subAssets[i];
                    const std::string ctx = "subAssets[" + std::to_string(i) + "]";
                    if (!node.IsMap())
                    {
                        AddIssue(outIssues, SidecarIssueCode::InvalidDocument, ctx, "map이어야 한다.");
                        continue;
                    }
                    ModelSubAssetRecord record;
                    std::string kindText;
                    if (ReadScalar(node, "kind", ctx, kindText, outIssues)
                        && !TryParseKindName(kindText, record.kind))
                    {
                        AddIssue(outIssues, SidecarIssueCode::InvalidKind, ctx + ".kind",
                            "kind는 mesh|material|texture|skeleton|animation이다: " + kindText);
                    }
                    (void)ReadScalar(node, "stableKey", ctx, record.stableKey, outIssues);
                    (void)ReadV8(node, "assetId", ctx, record.assetId, outIssues);
                    (void)ReadScalar(node, "binding", ctx, record.binding, outIssues, false);
                    (void)ReadScalar(node, "name", ctx, record.name, outIssues, false);
                    (void)ReadScalar(node, "fingerprint", ctx, record.fingerprint, outIssues, false);
                    doc.subAssets.push_back(std::move(record));
                }
            }
        }
        catch (const YAML::Exception& exception)
        {
            AddIssue(outIssues, SidecarIssueCode::InvalidDocument, "root", exception.what());
            return false;
        }

        if (outIssues.size() != before) return false;
        if (!CheckDocumentShape(doc, outIssues)) return false;
        out = std::move(doc);
        return true;
    }

    std::string WriteModelSidecarV2(const ModelSidecarV2& document,
        std::string_view existingYaml, std::vector<SidecarIssue>& outIssues)
    {
        if (!CheckDocumentShape(document, outIssues)) return {};

        try
        {
            YAML::Node root;
            if (!existingYaml.empty())
            {
                root = YAML::Load(std::string(existingYaml));
                if (!root.IsMap()) root = YAML::Node(YAML::NodeType::Map);
                // legacy 신원 키는 v2 문서에서 사라진다. 신원 키는 아래에서 전부 덮는다.
                root.remove("guid");
                for (const char* key : kIdentityKeys) root.remove(key);
            }
            else
            {
                root = YAML::Node(YAML::NodeType::Map);
            }

            // 신원 키를 문서 앞에 두려면 새 맵에 먼저 넣고 나머지를 뒤에 붙인다.
            YAML::Node ordered(YAML::NodeType::Map);
            ordered["schemaVersion"] = kModelSidecarSchemaVersion;
            ordered["identityProfile"] = document.identityProfile;
            ordered["identityEpoch"] = document.identityEpoch;
            ordered["authoringKey"] = document.authoringKey;
            ordered["assetId"] = Uuid::ToString(document.assetId);
            ordered["generation"] = document.generation;
            ordered["sourceFingerprint"] = document.sourceFingerprint;
            YAML::Node subAssets(YAML::NodeType::Sequence);
            for (const ModelSubAssetRecord& r : document.subAssets)
            {
                YAML::Node entry(YAML::NodeType::Map);
                entry["kind"] = std::string(ToKindName(r.kind));
                entry["stableKey"] = r.stableKey;
                entry["assetId"] = Uuid::ToString(r.assetId);
                if (!r.binding.empty()) entry["binding"] = r.binding;
                if (!r.name.empty()) entry["name"] = r.name;
                if (!r.fingerprint.empty()) entry["fingerprint"] = r.fingerprint;
                subAssets.push_back(entry);
            }
            ordered["subAssets"] = subAssets;
            for (const auto& kv : root) ordered[kv.first] = kv.second;

            YAML::Emitter emitter;
            emitter.SetIndent(2);
            emitter << ordered;
            if (!emitter.good())
            {
                AddIssue(outIssues, SidecarIssueCode::InvalidDocument, "root", "YAML emit 실패.");
                return {};
            }
            std::string out(emitter.c_str(), emitter.size());
            out.push_back('\n');
            return out;
        }
        catch (const YAML::Exception& exception)
        {
            AddIssue(outIssues, SidecarIssueCode::InvalidDocument, "root", exception.what());
            return {};
        }
    }

    bool ValidateModelSidecarV2Closure(const ModelSidecarV2& document,
        const IdentityEpochHeader& header, std::vector<SidecarIssue>& outIssues)
    {
        const std::size_t before = outIssues.size();
        if (!CheckDocumentShape(document, outIssues)) return false;

        std::vector<EpochHeaderIssue> headerIssues;
        if (!ValidateIdentityEpochHeader(header, headerIssues))
        {
            AddIssue(outIssues, SidecarIssueCode::EpochMismatch, "header",
                "epoch header가 유효하지 않다.");
            return false;
        }
        if (document.identityEpoch != header.identityEpoch)
        {
            AddIssue(outIssues, SidecarIssueCode::EpochMismatch, "identityEpoch",
                "sidecar epoch '" + document.identityEpoch + "' ≠ header epoch '"
                + header.identityEpoch + "'.");
            return false;
        }

        IdentityRegistry registry;
        {
            IdentityInput input;
            input.domain = kDomainModel;
            input.namespaceBytes = std::span<const std::uint8_t>(header.identityEpochSeed);
            input.kind = kKindModel;
            input.stableKey = document.authoringKey;
            const IdentityRegisterResult r = registry.Register(input, "model", document.assetId);
            if (!r.Succeeded())
            {
                AddIssue(outIssues, r.outcome == IdentityRegisterOutcome::RecomputeMismatch
                    ? SidecarIssueCode::RecomputeMismatch : SidecarIssueCode::InvalidDocument,
                    "assetId", r.message);
                return false; // 모델 id가 틀리면 subasset 재유도는 의미가 없다
            }
        }
        for (std::size_t i = 0; i < document.subAssets.size(); ++i)
        {
            const ModelSubAssetRecord& r = document.subAssets[i];
            const std::string ctx = "subAssets[" + std::to_string(i) + "]";
            IdentityInput input;
            input.domain = kDomainSubAsset;
            input.namespaceBytes = std::span<const std::uint8_t>(document.assetId.data);
            input.kind = ToKindName(r.kind);
            input.stableKey = r.stableKey;
            const IdentityRegisterResult result = registry.Register(input, ctx, r.assetId);
            switch (result.outcome)
            {
            case IdentityRegisterOutcome::Registered: break;
            case IdentityRegisterOutcome::RecomputeMismatch:
                AddIssue(outIssues, SidecarIssueCode::RecomputeMismatch, ctx + ".assetId", result.message);
                break;
            case IdentityRegisterOutcome::DuplicateTuple:
                AddIssue(outIssues, SidecarIssueCode::DuplicateStableKey, ctx + ".stableKey", result.message);
                break;
            case IdentityRegisterOutcome::UuidCollision:
                AddIssue(outIssues, SidecarIssueCode::Collision, ctx + ".assetId", result.message);
                break;
            case IdentityRegisterOutcome::InvalidInput:
                AddIssue(outIssues, SidecarIssueCode::InvalidStableKey, ctx + ".stableKey", result.message);
                break;
            }
        }
        std::vector<std::string> bijection;
        if (!registry.VerifyBijection(bijection))
        {
            for (const std::string& line : bijection)
                AddIssue(outIssues, SidecarIssueCode::Collision, "registry", line);
        }
        return outIssues.size() == before;
    }

    bool BuildModelSidecarV2(const IdentityEpochHeader& header, std::string_view modelAuthoringKey,
        std::uint64_t generation, std::string_view sourceFingerprint,
        std::span<const StableKeyAssignment> assignments, ModelSidecarV2& out,
        std::vector<SidecarIssue>& outIssues)
    {
        const std::size_t before = outIssues.size();
        ModelSidecarV2 doc;
        doc.identityProfile = std::string(kIdentityProfile);
        doc.identityEpoch = header.identityEpoch;
        doc.authoringKey = std::string(modelAuthoringKey);
        doc.generation = generation;
        doc.sourceFingerprint = std::string(sourceFingerprint);

        const IdentityDerivation model = DeriveModelId(header.identityEpochSeed, modelAuthoringKey);
        if (!model.Succeeded())
        {
            AddIssue(outIssues, SidecarIssueCode::InvalidModelKey, "authoringKey",
                std::string(ToString(model.issue)) + " @" + model.context);
            return false;
        }
        doc.assetId = model.uuid;

        for (const StableKeyAssignment& a : assignments)
        {
            const IdentityDerivation sub = DeriveSubAssetId(model.uuid, a.kind, a.stableKey);
            if (!sub.Succeeded())
            {
                AddIssue(outIssues, SidecarIssueCode::InvalidStableKey,
                    std::string(ToKindName(a.kind)) + "/" + a.stableKey,
                    std::string(ToString(sub.issue)) + " @" + sub.context);
                continue;
            }
            doc.subAssets.push_back({ a.kind, a.stableKey, sub.uuid, a.binding, a.name, a.fingerprint });
        }
        if (outIssues.size() != before) return false;
        if (!ValidateModelSidecarV2Closure(doc, header, outIssues)) return false;
        out = std::move(doc);
        return true;
    }
}
