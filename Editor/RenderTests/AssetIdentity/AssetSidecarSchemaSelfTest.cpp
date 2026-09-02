#include "AssetIdentity/AssetSidecarSchemaSelfTest.h"

#include "Assets/AssetIdentityEpoch.h"
#include "Assets/AssetIdentityHex.h"
#include "Assets/AssetIdentityRegistry.h"
#include "Assets/ModelSidecarV2.h"
#include "Assets/ModelStableKeys.h"
#include "Experiment/Import/FbxImporter.h"
#include "Experiment/Import/GltfImporter.h"
#include "Experiment/Import/ImportedScene.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace im = experiment::importer;

        struct SidecarChecker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& label)
            {
                if (condition) { ++passed; return; }
                ++failed;
                log += "    [실패] " + label + "\n";
            }
        };

        // 결정적 authoring key 공장 — 검사 안에서 CSPRNG 대신 쓴다(재현 가능).
        std::uint32_t g_factoryCounter = 0;
        bool DeterministicFactory(std::array<std::uint8_t, assets::kAuthoringKeyBytes>& out,
            std::string& error)
        {
            error.clear();
            ++g_factoryCounter;
            for (std::size_t i = 0; i < out.size(); ++i)
                out[i] = static_cast<std::uint8_t>((g_factoryCounter * 7u + i * 13u) & 0xFFu);
            return true;
        }

        [[nodiscard]] std::string Fp(std::string_view seed)
        {
            std::vector<std::uint8_t> bytes(seed.begin(), seed.end());
            return assets::MakeSourceFingerprint(bytes);
        }

        [[nodiscard]] assets::StableKeyElement Element(assets::SubAssetKind kind, std::size_t index,
            std::string name, std::string fingerprintSeed, std::string persistentId = {})
        {
            assets::StableKeyElement e;
            e.kind = kind; e.index = index; e.name = std::move(name);
            e.binding = std::string(assets::ToKindName(kind)) + "/" + std::to_string(index);
            e.fingerprint = Fp(fingerprintSeed);
            e.persistentId = std::move(persistentId);
            return e;
        }

        [[nodiscard]] std::vector<assets::ModelSubAssetRecord> ToRecords(
            const assets::StableKeyResult& result)
        {
            std::vector<assets::ModelSubAssetRecord> records;
            for (const assets::StableKeyAssignment& a : result.assignments)
                records.push_back({ a.kind, a.stableKey, {}, a.binding, a.name, a.fingerprint });
            return records;
        }

        [[nodiscard]] bool HasIssue(const std::vector<assets::SidecarIssue>& issues,
            assets::SidecarIssueCode code)
        {
            return std::ranges::any_of(issues, [code](const assets::SidecarIssue& i) { return i.code == code; });
        }
        [[nodiscard]] bool HasIssue(const std::vector<assets::StableKeyIssue>& issues,
            assets::StableKeyIssueCode code)
        {
            return std::ranges::any_of(issues, [code](const assets::StableKeyIssue& i) { return i.code == code; });
        }
        [[nodiscard]] bool HasIssue(const std::vector<assets::EpochHeaderIssue>& issues,
            assets::EpochHeaderIssueCode code)
        {
            return std::ranges::any_of(issues, [code](const assets::EpochHeaderIssue& i) { return i.code == code; });
        }

        [[nodiscard]] assets::IdentityEpochHeader TestHeader()
        {
            assets::IdentityEpochHeader header;
            header.identityEpoch = "test-epoch";
            for (std::size_t i = 0; i < header.identityEpochSeed.size(); ++i)
                header.identityEpochSeed[i] = static_cast<std::uint8_t>(i);
            header.createdAt = "2026-09-02T00:00:00";
            return header;
        }

        [[nodiscard]] std::string Repeat(char c, std::size_t n) { return std::string(n, c); }

        [[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path,
            std::vector<std::uint8_t>& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(size));
            if (!out.empty()) stream.read(reinterpret_cast<char*>(out.data()), size);
            return stream.good() || stream.eof();
        }
    }

    bool RunAssetSidecarSchemaSelfTest(const std::string& assetRoot, std::string& outLog)
    {
        SidecarChecker check{ outLog };
        outLog += "[assets.sidecar] epoch header·stable key·sidecar schema v2 검사\n";
        g_factoryCounter = 0;

        // ── [1] epoch header ─────────────────────────────────────────────────
        outLog += "  [1] epoch header\n";
        {
            assets::IdentityEpochSeed a{}, b{};
            std::string error;
            check.Check(assets::CreateIdentityEpochSeed(a, error) && !assets::IsZeroSeed(a),
                "CSPRNG seed 발급 (" + error + ")");
            check.Check(assets::CreateIdentityEpochSeed(b, error) && a != b, "두 seed가 다르다");

            const assets::IdentityEpochHeader header = TestHeader();
            const std::string yaml = assets::WriteIdentityEpochHeader(header);
            check.Check(!yaml.empty() && yaml.find("identityProfile: ce.uuidv8.sha256.v1") != std::string::npos
                && yaml.find("identityEpochSeed: 000102030405") != std::string::npos,
                "header YAML 쓰기");
            assets::IdentityEpochHeader back;
            std::vector<assets::EpochHeaderIssue> issues;
            check.Check(assets::ReadIdentityEpochHeader(yaml, back, issues)
                && back.identityEpoch == header.identityEpoch
                && back.identityEpochSeed == header.identityEpochSeed
                && back.identityProfile == header.identityProfile, "header YAML 왕복");

            auto expectReject = [&](std::string text, assets::EpochHeaderIssueCode code, const char* label)
            {
                assets::IdentityEpochHeader h;
                std::vector<assets::EpochHeaderIssue> is;
                const bool ok = assets::ReadIdentityEpochHeader(text, h, is);
                check.Check(!ok && HasIssue(is, code), std::string("header 거부: ") + label);
            };
            std::string tampered = yaml;
            tampered.replace(tampered.find("sha256.v1"), 9, "sha256.v0");
            expectReject(tampered, assets::EpochHeaderIssueCode::ProfileMismatch, "프로필 v0");
            tampered = yaml;
            tampered.replace(tampered.find("000102030405"), 12, "00010203040");
            expectReject(tampered, assets::EpochHeaderIssueCode::InvalidSeed, "seed 63자");
            tampered = yaml;
            tampered.replace(tampered.find("000102030405"), 12, "0001020304AB");
            expectReject(tampered, assets::EpochHeaderIssueCode::InvalidSeed, "seed 대문자");
            assets::IdentityEpochHeader zero = header;
            zero.identityEpochSeed.fill(0u);
            check.Check(assets::WriteIdentityEpochHeader(zero).empty(), "0 seed는 쓰기 거부");
            expectReject("schemaVersion: 1\nidentityProfile: ce.uuidv8.sha256.v1\nidentityEpoch: e\nidentityEpochSeed: "
                + Repeat('0', 64) + "\n", assets::EpochHeaderIssueCode::ZeroSeed, "0 seed 읽기");
            tampered = yaml;
            tampered.replace(tampered.find("schemaVersion: 1"), 16, "schemaVersion: 2");
            expectReject(tampered, assets::EpochHeaderIssueCode::UnsupportedSchema, "schema 2");
            expectReject("schemaVersion: 1\nidentityProfile: ce.uuidv8.sha256.v1\nidentityEpochSeed: "
                + Repeat('a', 64) + "\n", assets::EpochHeaderIssueCode::MissingField, "epoch 이름 없음");
            expectReject("- a\n- b\n", assets::EpochHeaderIssueCode::InvalidDocument, "map 아님");
        }

        // ── [2] stable key 문법 ──────────────────────────────────────────────
        outLog += "  [2] stable key 문법\n";
        {
            assets::StableKeyOrigin origin{};
            std::string error;
            check.Check(assets::TryParseStableKey("name:MI_Hero", origin, error)
                && origin == assets::StableKeyOrigin::Semantic, "name: 수용");
            check.Check(assets::TryParseStableKey("exporter:dcc-0042", origin, error)
                && origin == assets::StableKeyOrigin::Exporter, "exporter: 수용");
            check.Check(assets::TryParseStableKey("authoring:" + Repeat('a', 64), origin, error)
                && origin == assets::StableKeyOrigin::Authoring, "authoring:<64hex> 수용");
            check.Check(assets::TryParseStableKey("name:\xEC\x9E\xAC\xEC\xA7\x88", origin, error), "name: 다바이트 NFC 수용");

            const char* rejected[] = {
                "gltf/material/0", "fbx/material/12", "3", "name:", "authoring:zz",
                "authoring:" , "Name:X", "exporter:", "name:e\xCC\x81", "",
            };
            for (const char* r : rejected)
            {
                check.Check(!assets::TryParseStableKey(r, origin, error),
                    std::string("거부 '") + r + "' (" + error + ")");
            }
            std::string authoringUpper = "authoring:" + Repeat('A', 64);
            check.Check(!assets::TryParseStableKey(authoringUpper, origin, error), "authoring 대문자 거부");
            check.Check(assets::IsForbiddenOrdinalKey("gltf/material/0")
                && assets::IsForbiddenOrdinalKey("7") && !assets::IsForbiddenOrdinalKey("name:7"),
                "ordinal 판정");
        }

        // ── [3] 규칙 엔진 ───────────────────────────────────────────────────
        outLog += "  [3] 규칙 엔진\n";
        using K = assets::SubAssetKind;
        {
            // a. 유일한 이름 → 전부 semantic
            std::vector<assets::StableKeyElement> named = {
                Element(K::Material, 0, "MatA", "fa"), Element(K::Material, 1, "MatB", "fb"),
                Element(K::Texture, 0, "TexA", "ta"), Element(K::Mesh, 0, "MeshA", "ma"),
                Element(K::Skeleton, 0, "Root", "sa"), Element(K::Animation, 0, "Idle", "aa"),
            };
            assets::StableKeyResult r = assets::DeriveModelStableKeys(named, {}, &DeterministicFactory);
            check.Check(r.Succeeded() && r.assignments.size() == 6u
                && r.CountOrigin(K::Material, assets::StableKeyOrigin::Semantic) == 2u
                && r.CountOrigin(K::Animation, assets::StableKeyOrigin::Semantic) == 1u,
                "유일 이름 → semantic 6/6");
            const auto matA = std::ranges::find_if(r.assignments,
                [](const auto& a) { return a.kind == K::Material && a.index == 0u; });
            check.Check(matA != r.assignments.end() && matA->stableKey == "name:MatA"
                && matA->binding == "material/0" && !matA->reboundFromPrior, "key 텍스트 name:MatA");

            // b. 이름 중복 → 중복분만 authoring, 유일분은 semantic. 같은 이름이 다른 kind면 무관.
            std::vector<assets::StableKeyElement> dup = {
                Element(K::Material, 0, "Mat", "f0"), Element(K::Material, 1, "Mat", "f1"),
                Element(K::Material, 2, "Only", "f2"), Element(K::Texture, 0, "Mat", "t0"),
            };
            r = assets::DeriveModelStableKeys(dup, {}, &DeterministicFactory);
            check.Check(r.Succeeded() && r.CountOrigin(K::Material, assets::StableKeyOrigin::Authoring) == 2u
                && r.CountOrigin(K::Material, assets::StableKeyOrigin::Semantic) == 1u
                && r.CountOrigin(K::Texture, assets::StableKeyOrigin::Semantic) == 1u,
                "중복 이름 → authoring 2 + semantic 1, 다른 kind 동명 무관");

            // c. 무명 → authoring, 지문으로 재결합(순서 바뀌어도 같은 key)
            std::vector<assets::StableKeyElement> nameless = {
                Element(K::Material, 0, "", "n0"), Element(K::Material, 1, "", "n1"), Element(K::Material, 2, "", "n2"),
            };
            const assets::StableKeyResult first = assets::DeriveModelStableKeys(nameless, {}, &DeterministicFactory);
            check.Check(first.Succeeded() && first.CountOrigin(K::Material, assets::StableKeyOrigin::Authoring) == 3u,
                "무명 3 → authoring 3");
            std::vector<assets::ModelSubAssetRecord> prior = ToRecords(first);
            std::vector<assets::StableKeyElement> reordered = {
                Element(K::Material, 0, "", "n2"), Element(K::Material, 1, "", "n0"), Element(K::Material, 2, "", "n1"),
            };
            const assets::StableKeyResult again = assets::DeriveModelStableKeys(reordered, prior, &DeterministicFactory);
            bool sameKeys = again.Succeeded() && again.assignments.size() == 3u;
            for (const auto& a : again.assignments)
            {
                const auto p = std::ranges::find_if(prior, [&](const auto& rec) { return rec.fingerprint == a.fingerprint; });
                sameKeys = sameKeys && p != prior.end() && p->stableKey == a.stableKey && a.reboundFromPrior;
            }
            check.Check(sameKeys, "재임포트(순서 변경) → 지문으로 같은 authoring key 재결합 3/3");

            // e. 내용이 바뀐 무명 요소 + prior → 증명 불가 → 오류
            std::vector<assets::StableKeyElement> changed = {
                Element(K::Material, 0, "", "n0"), Element(K::Material, 1, "", "n1"), Element(K::Material, 2, "", "CHANGED"),
            };
            const assets::StableKeyResult amb = assets::DeriveModelStableKeys(changed, prior, &DeterministicFactory);
            check.Check(!amb.Succeeded() && HasIssue(amb.issues, assets::StableKeyIssueCode::AuthoringRebindAmbiguous),
                "무명 요소 내용 변경 + prior → AuthoringRebindAmbiguous 오류");

            // f. 삭제 → 은퇴 경고, 성공
            std::vector<assets::StableKeyElement> fewer = {
                Element(K::Material, 0, "", "n0"), Element(K::Material, 1, "", "n1"),
            };
            const assets::StableKeyResult del = assets::DeriveModelStableKeys(fewer, prior, &DeterministicFactory);
            check.Check(del.Succeeded() && del.assignments.size() == 2u
                && HasIssue(del.issues, assets::StableKeyIssueCode::AuthoringKeyRetired),
                "무명 요소 삭제 → 2 재결합 + AuthoringKeyRetired 경고");

            // g. 추가 → 2 재결합 + 1 신규, 오류 없음
            std::vector<assets::StableKeyElement> more = nameless;
            more.push_back(Element(K::Material, 3, "", "n3"));
            const assets::StableKeyResult add = assets::DeriveModelStableKeys(more, prior, &DeterministicFactory);
            std::size_t rebound = 0;
            for (const auto& a : add.assignments) if (a.reboundFromPrior) ++rebound;
            check.Check(add.Succeeded() && add.assignments.size() == 4u && rebound == 3u,
                "무명 요소 추가 → 3 재결합 + 1 신규");

            // 같은 지문 둘 + prior 하나 → 어느 것인지 모름 → 오류
            std::vector<assets::StableKeyElement> twins = {
                Element(K::Material, 0, "", "same"), Element(K::Material, 1, "", "same"),
            };
            const assets::StableKeyResult twinFirst = assets::DeriveModelStableKeys(twins, {}, &DeterministicFactory);
            std::vector<assets::ModelSubAssetRecord> twinPrior = ToRecords(twinFirst);
            twinPrior.pop_back();
            const assets::StableKeyResult twinAgain = assets::DeriveModelStableKeys(twins, twinPrior, &DeterministicFactory);
            std::size_t twinRebound = 0;
            for (const auto& a : twinAgain.assignments) if (a.reboundFromPrior) ++twinRebound;
            check.Check(twinAgain.Succeeded() && twinAgain.assignments.size() == 2u && twinRebound == 1u
                && twinAgain.assignments[0].stableKey == twinPrior[0].stableKey,
                "같은 지문 무명 둘 + prior 하나 → binding 순 1 재결합 + 1 신규(동일 콘텐츠는 교환 불가시)");
            const std::vector<assets::ModelSubAssetRecord> twinPriorBoth = ToRecords(twinFirst);
            const assets::StableKeyResult twinBoth = assets::DeriveModelStableKeys(twins, twinPriorBoth, &DeterministicFactory);
            check.Check(twinBoth.Succeeded() && twinBoth.assignments.size() == 2u
                && twinBoth.assignments[0].stableKey == twinPriorBoth[0].stableKey
                && twinBoth.assignments[1].stableKey == twinPriorBoth[1].stableKey,
                "같은 지문 무명 둘 + prior 둘 → binding 순 짝짓기 결정적");
            std::vector<assets::StableKeyElement> swapped = {
                Element(K::Material, 0, "", "same"), Element(K::Material, 1, "", "different"),
            };
            const assets::StableKeyResult swappedAgain = assets::DeriveModelStableKeys(swapped, twinPriorBoth, &DeterministicFactory);
            check.Check(!swappedAgain.Succeeded()
                && HasIssue(swappedAgain.issues, assets::StableKeyIssueCode::AuthoringRebindAmbiguous),
                "지문 그룹 축소 + 새 지문 → 증명 불가 오류");

            // h. exporter id
            std::vector<assets::StableKeyElement> exported = {
                Element(K::Material, 0, "Mat", "e0", "dcc-1"), Element(K::Material, 1, "Mat", "e1", "dcc-2"),
            };
            r = assets::DeriveModelStableKeys(exported, {}, &DeterministicFactory);
            check.Check(r.Succeeded() && r.CountOrigin(K::Material, assets::StableKeyOrigin::Exporter) == 2u
                && r.assignments[0].stableKey == "exporter:dcc-1", "exporter id → exporter: 2/2 (이름 중복 무관)");
            std::vector<assets::StableKeyElement> dupExport = {
                Element(K::Material, 0, "A", "e0", "dcc-1"), Element(K::Material, 1, "B", "e1", "dcc-1"),
            };
            r = assets::DeriveModelStableKeys(dupExport, {}, &DeterministicFactory);
            check.Check(!r.Succeeded() && HasIssue(r.issues, assets::StableKeyIssueCode::DuplicatePersistentId),
                "exporter id 중복 → 오류");

            // 비NFC 이름 → authoring으로 강등(경고), 성공
            std::vector<assets::StableKeyElement> nfd = { Element(K::Material, 0, "e\xCC\x81", "d0") };
            r = assets::DeriveModelStableKeys(nfd, {}, &DeterministicFactory);
            check.Check(r.Succeeded() && r.CountOrigin(K::Material, assets::StableKeyOrigin::Authoring) == 1u
                && HasIssue(r.issues, assets::StableKeyIssueCode::NameNotNfc), "비NFC 이름 → authoring + 경고");
        }

        // ── [4] sidecar v2 코덱·폐포 ────────────────────────────────────────
        outLog += "  [4] sidecar v2\n";
        assets::ModelSidecarV2 doc;
        const assets::IdentityEpochHeader header = TestHeader();
        {
            std::vector<assets::StableKeyElement> named = {
                Element(K::Material, 0, "MatA", "fa"), Element(K::Material, 1, "", "fb"),
                Element(K::Texture, 0, "TexA", "ta"), Element(K::Mesh, 0, "MeshA", "ma"),
                Element(K::Skeleton, 0, "Root", "sa"), Element(K::Animation, 0, "Idle", "aa"),
            };
            const assets::StableKeyResult r = assets::DeriveModelStableKeys(named, {}, &DeterministicFactory);
            const std::string modelKey = "authoring:" + Repeat('a', 64);
            std::vector<assets::SidecarIssue> issues;
            check.Check(assets::BuildModelSidecarV2(header, modelKey, 1u, Fp("source-bytes"),
                r.assignments, doc, issues) && issues.empty(),
                "BuildModelSidecarV2 (" + std::to_string(issues.size()) + " issues)");
            check.Check(assets::IsUuidV8(doc.assetId) && doc.subAssets.size() == 6u, "모델 v8 + subasset 6");
            const assets::IdentityDerivation expectModel = assets::DeriveModelId(header.identityEpochSeed, modelKey);
            check.Check(expectModel.Succeeded() && expectModel.uuid == doc.assetId, "모델 id = DeriveModelId");

            const std::string existing =
                "guid: 68b21a01-958e-44ed-8820-a2b9aa289587\nimportSettings:\n  extension: .glb\n  timestamp: 1\n"
                "ModelImporter:\n  OptimizeMeshes: true\n";
            issues.clear();
            const std::string yaml = assets::WriteModelSidecarV2(doc, existing, issues);
            check.Check(!yaml.empty() && issues.empty(), "Write v2");
            check.Check(yaml.find("schemaVersion: 2") == 0u, "schemaVersion: 2가 첫 줄");
            check.Check(yaml.find("guid:") == std::string::npos, "legacy guid 제거");
            check.Check(yaml.find("importSettings:") != std::string::npos
                && yaml.find("OptimizeMeshes: true") != std::string::npos, "다른 최상위 키 보존");
            check.Check(yaml.find("stableKey: name:MatA") != std::string::npos
                && yaml.find("binding: material/0") != std::string::npos
                && yaml.find("fingerprint: sha256:") != std::string::npos, "subasset 필드 표기");
            outLog += "    --- v2 sample ---\n";
            {
                std::size_t lines = 0;
                std::string::size_type pos = 0;
                while (pos < yaml.size() && lines < 14u)
                {
                    const auto end = yaml.find('\n', pos);
                    outLog += "    | " + yaml.substr(pos, end == std::string::npos ? std::string::npos : end - pos) + "\n";
                    if (end == std::string::npos) break;
                    pos = end + 1u; ++lines;
                }
            }

            assets::ModelSidecarV2 back;
            issues.clear();
            check.Check(assets::ReadModelSidecarV2(yaml, back, issues) && issues.empty(), "Read v2");
            check.Check(back.assetId == doc.assetId && back.authoringKey == doc.authoringKey
                && back.identityEpoch == doc.identityEpoch && back.generation == 1u
                && back.sourceFingerprint == doc.sourceFingerprint && back.subAssets.size() == 6u
                && back.subAssets[1].stableKey == doc.subAssets[1].stableKey
                && back.subAssets[1].assetId == doc.subAssets[1].assetId
                && back.subAssets[1].fingerprint == doc.subAssets[1].fingerprint, "왕복 필드 일치");
            issues.clear();
            check.Check(assets::ValidateModelSidecarV2Closure(back, header, issues) && issues.empty(),
                "폐포 검증 통과");

            auto expectReject = [&](const std::string& text, assets::SidecarIssueCode code, const char* label)
            {
                assets::ModelSidecarV2 d;
                std::vector<assets::SidecarIssue> is;
                const bool ok = assets::ReadModelSidecarV2(text, d, is);
                check.Check(!ok && HasIssue(is, code), std::string("v2 읽기 거부: ") + label);
            };
            expectReject(existing, assets::SidecarIssueCode::LegacyGuidField, "legacy guid 필드");
            expectReject("guid: 935b883a-9d6d-44be-af0a-8ef7495f282a\nsubAssets:\n  schemaVersion: 1\n  materials:\n    - key: gltf/material/0\n      guid: 9d7a8405-5150-4628-8f93-33f92fa7f20e\n",
                assets::SidecarIssueCode::LegacySchema, "v1 sidecar");
            std::string tampered = yaml;
            tampered.replace(tampered.find("stableKey: name:MatA"), 20, "stableKey: gltf/material/0");
            expectReject(tampered, assets::SidecarIssueCode::InvalidStableKey, "ordinal stable key");
            tampered = yaml;
            tampered.replace(tampered.find("authoringKey: authoring:"), 24, "authoringKey: name:aaaaaaaaaa");
            expectReject(tampered, assets::SidecarIssueCode::InvalidModelKey, "model key name:");
            tampered = yaml;
            tampered.replace(tampered.find("generation: 1"), 13, "generation: 0");
            expectReject(tampered, assets::SidecarIssueCode::InvalidGeneration, "generation 0");
            tampered = yaml;
            tampered.replace(tampered.find("schemaVersion: 2"), 16, "schemaVersion: 3");
            expectReject(tampered, assets::SidecarIssueCode::UnsupportedSchema, "schema 3");
            tampered = yaml;
            {
                const std::string id = Uuid::ToString(doc.subAssets[0].assetId);
                std::string upper = id;
                for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                tampered.replace(tampered.find(id), id.size(), upper);
                expectReject(tampered, assets::SidecarIssueCode::InvalidAssetId, "대문자 assetId");
            }

            // 변조: 형식은 맞지만 재유도가 다른 assetId → 폐포 검증이 잡는다
            assets::ModelSidecarV2 forged = back;
            forged.subAssets[2].assetId = doc.subAssets[3].assetId; // 다른 요소의 id로 바꿈(중복)
            issues.clear();
            check.Check(!assets::ValidateModelSidecarV2Closure(forged, header, issues)
                && HasIssue(issues, assets::SidecarIssueCode::DuplicateAssetId), "중복 assetId → 거부");
            forged = back;
            forged.subAssets[2].assetId.data[15] ^= 0x01u;
            issues.clear();
            check.Check(!assets::ValidateModelSidecarV2Closure(forged, header, issues)
                && HasIssue(issues, assets::SidecarIssueCode::RecomputeMismatch), "subasset id 1비트 변조 → RecomputeMismatch");
            forged = back;
            forged.assetId.data[0] ^= 0x01u;
            issues.clear();
            check.Check(!assets::ValidateModelSidecarV2Closure(forged, header, issues)
                && HasIssue(issues, assets::SidecarIssueCode::RecomputeMismatch), "model id 변조 → RecomputeMismatch");
            assets::IdentityEpochHeader otherEpoch = header;
            otherEpoch.identityEpoch = "other";
            issues.clear();
            check.Check(!assets::ValidateModelSidecarV2Closure(back, otherEpoch, issues)
                && HasIssue(issues, assets::SidecarIssueCode::EpochMismatch), "epoch 이름 불일치 → EpochMismatch");
            assets::IdentityEpochHeader otherSeed = header;
            otherSeed.identityEpochSeed[0] ^= 0xFFu;
            issues.clear();
            check.Check(!assets::ValidateModelSidecarV2Closure(back, otherSeed, issues)
                && HasIssue(issues, assets::SidecarIssueCode::RecomputeMismatch), "같은 epoch 이름·다른 seed → RecomputeMismatch");
            forged = back;
            forged.subAssets[0].stableKey = forged.subAssets[2].stableKey;
            forged.subAssets[0].kind = forged.subAssets[2].kind;
            issues.clear();
            check.Check(!assets::ValidateModelSidecarV2Closure(forged, header, issues)
                && HasIssue(issues, assets::SidecarIssueCode::DuplicateStableKey), "kind 안 stable key 중복 → 거부");

            // 결정성: 같은 배정으로 다시 Build → 같은 문서
            assets::ModelSidecarV2 rebuilt;
            issues.clear();
            check.Check(assets::BuildModelSidecarV2(header, modelKey, 1u, Fp("source-bytes"), r.assignments, rebuilt, issues)
                && rebuilt.assetId == doc.assetId && rebuilt.subAssets.size() == doc.subAssets.size()
                && std::ranges::equal(rebuilt.subAssets, doc.subAssets,
                    [](const auto& x, const auto& y) { return x.assetId == y.assetId && x.stableKey == y.stableKey; }),
                "같은 입력 → 같은 문서");
        }

        // ── [5] 실자산 corpus ────────────────────────────────────────────────
        outLog += "  [5] 실자산 corpus\n";
        if (assetRoot.empty())
        {
            outLog += "    corpus 검사 건너뜀 — assetRoot 미지정 (실자산 축 없음)\n";
        }
        else
        {
            std::error_code ec;
            std::vector<std::filesystem::path> sources;
            for (std::filesystem::recursive_directory_iterator it(assetRoot, ec), end; it != end && !ec; it.increment(ec))
            {
                if (!it->is_regular_file(ec)) continue;
                std::string ext = it->path().extension().string();
                std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".glb" || ext == ".gltf" || ext == ".fbx") sources.push_back(it->path());
            }
            std::ranges::sort(sources);
            check.Check(!sources.empty(), "corpus에 모델이 있다: " + std::to_string(sources.size()));

            assets::IdentityRegistry global;
            std::size_t models = 0, ok = 0, totalSub = 0;
            for (const std::filesystem::path& source : sources)
            {
                ++models;
                im::GltfImporter gltf;
                im::FbxImporter fbx;
                im::IAssetImporter* importer = gltf.CanImport(source) ? static_cast<im::IAssetImporter*>(&gltf)
                    : fbx.CanImport(source) ? static_cast<im::IAssetImporter*>(&fbx) : nullptr;
                const std::string file = source.filename().string();
                if (!importer)
                {
                    check.Check(false, "임포터 없음: " + file);
                    continue;
                }
                im::ImportRequest request;
                request.sourcePath = source;
                const im::ImportResult imported = importer->Import(request);
                if (!imported.Succeeded())
                {
                    check.Check(false, "임포트 실패: " + file);
                    continue;
                }
                const im::ImportedScene& scene = *imported.scene;
                const std::vector<assets::StableKeyElement> elements = assets::CollectStableKeyElements(scene);
                const assets::StableKeyResult keys = assets::DeriveModelStableKeys(elements, {}, &DeterministicFactory);

                std::vector<std::uint8_t> bytes;
                const bool readOk = ReadFileBytes(source, bytes);
                std::string error;
                const std::string modelKey = assets::CreateModelAuthoringKey(error);
                assets::ModelSidecarV2 sidecar;
                std::vector<assets::SidecarIssue> issues;
                const bool built = readOk && !modelKey.empty() && keys.Succeeded()
                    && assets::BuildModelSidecarV2(header, modelKey, 1u, assets::MakeSourceFingerprint(bytes),
                        keys.assignments, sidecar, issues);

                // 재배정: 첫 결과를 prior로 같은 입력 → 같은 신원(authoring 재결합 포함)
                const std::vector<assets::ModelSubAssetRecord> prior = ToRecords(keys);
                const assets::StableKeyResult again = assets::DeriveModelStableKeys(elements, prior, &DeterministicFactory);
                assets::ModelSidecarV2 sidecarAgain;
                std::vector<assets::SidecarIssue> issuesAgain;
                const bool sameIdentity = built && again.Succeeded()
                    && assets::BuildModelSidecarV2(header, modelKey, 2u, sidecar.sourceFingerprint,
                        again.assignments, sidecarAgain, issuesAgain)
                    && std::ranges::equal(sidecar.subAssets, sidecarAgain.subAssets,
                        [](const auto& x, const auto& y) { return x.assetId == y.assetId && x.stableKey == y.stableKey; });

                // 전 corpus registry — 모델 간 충돌 0
                std::size_t collisions = 0;
                if (built)
                {
                    for (const assets::ModelSubAssetRecord& r : sidecar.subAssets)
                    {
                        assets::IdentityInput input;
                        input.domain = assets::kDomainSubAsset;
                        input.namespaceBytes = std::span<const std::uint8_t>(sidecar.assetId.data);
                        input.kind = assets::ToKindName(r.kind);
                        input.stableKey = r.stableKey;
                        if (!global.Register(input, file + "/" + r.stableKey, r.assetId).Succeeded()) ++collisions;
                    }
                }

                const bool modelOk = built && sameIdentity && collisions == 0u;
                if (modelOk) ++ok;
                totalSub += sidecar.subAssets.size();
                auto count = [&](K kind, assets::StableKeyOrigin origin) { return keys.CountOrigin(kind, origin); };
                char line[512];
                std::snprintf(line, sizeof(line),
                    "    sidecar %s ok=%d mat=%zu(sem %zu/auth %zu) tex=%zu(sem %zu/auth %zu) mesh=%zu(sem %zu/auth %zu) skel=%zu anim=%zu(sem %zu/auth %zu) issues=%zu\n",
                    file.c_str(), modelOk ? 1 : 0,
                    count(K::Material, assets::StableKeyOrigin::Semantic) + count(K::Material, assets::StableKeyOrigin::Authoring),
                    count(K::Material, assets::StableKeyOrigin::Semantic), count(K::Material, assets::StableKeyOrigin::Authoring),
                    count(K::Texture, assets::StableKeyOrigin::Semantic) + count(K::Texture, assets::StableKeyOrigin::Authoring),
                    count(K::Texture, assets::StableKeyOrigin::Semantic), count(K::Texture, assets::StableKeyOrigin::Authoring),
                    count(K::Mesh, assets::StableKeyOrigin::Semantic) + count(K::Mesh, assets::StableKeyOrigin::Authoring),
                    count(K::Mesh, assets::StableKeyOrigin::Semantic), count(K::Mesh, assets::StableKeyOrigin::Authoring),
                    count(K::Skeleton, assets::StableKeyOrigin::Semantic) + count(K::Skeleton, assets::StableKeyOrigin::Authoring),
                    count(K::Animation, assets::StableKeyOrigin::Semantic) + count(K::Animation, assets::StableKeyOrigin::Authoring),
                    count(K::Animation, assets::StableKeyOrigin::Semantic), count(K::Animation, assets::StableKeyOrigin::Authoring),
                    keys.issues.size());
                outLog += line;
                for (const assets::StableKeyIssue& issue : keys.issues)
                    outLog += "      - " + issue.context + ": " + issue.message + "\n";
                for (const assets::SidecarIssue& issue : issues)
                    outLog += "      - sidecar " + issue.context + ": " + issue.message + "\n";
                check.Check(modelOk, "corpus 모델 신원 폐포: " + file);
            }
            std::vector<std::string> bijection;
            check.Check(global.VerifyBijection(bijection), "전 corpus registry bijection ("
                + std::to_string(bijection.size()) + " issues)");
            outLog += "    corpus models=" + std::to_string(models) + " ok=" + std::to_string(ok)
                + " subassets=" + std::to_string(totalSub) + " registry=" + std::to_string(global.Size()) + "\n";
        }

        outLog += "  단정 " + std::to_string(check.passed + check.failed) + "건 중 통과 "
            + std::to_string(check.passed) + " · 실패 " + std::to_string(check.failed) + "\n";
        return check.failed == 0;
    }
}
