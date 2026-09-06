#include "AssetIdentity/AssetIdentitySelfTest.h"
#include "AssetIdentity/AssetIdentityTestVectors.inl"

#include "Assets/AssetIdentityProfile.h"
#include "Assets/AssetIdentityRegistry.h"
#include "Experiment/AssetIdentity.h"                 // IsAssetIdV4 — legacy 판정과의 배타성
#include "Experiment/Cooked/CookedAssetManifest.h"   // BCrypt ComputeSha256 — 독립 대조군
#include "Sha256.h"
#include "Uuid.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace vec = identity_vectors;

        struct IdentityChecker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& label)
            {
                if (condition)
                {
                    ++passed;
                    return;
                }
                ++failed;
                log += "    [실패] " + label + "\n";
            }
        };

        [[nodiscard]] std::vector<std::uint8_t> FromHex(std::string_view hex)
        {
            std::vector<std::uint8_t> out;
            out.reserve(hex.size() / 2u);
            auto nibble = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
            {
                const int hi = nibble(hex[i]);
                const int lo = nibble(hex[i + 1]);
                if (hi < 0 || lo < 0) return {};
                out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
            }
            return out;
        }

        [[nodiscard]] const vec::ProfileVector* FindVector(std::string_view name)
        {
            for (const vec::ProfileVector& v : vec::kProfileVectors)
            {
                if (name == v.name) return &v;
            }
            return nullptr;
        }

        [[nodiscard]] assets::IdentityInput MakeInput(const vec::ProfileVector& v,
            const std::vector<std::uint8_t>& namespaceBytes)
        {
            assets::IdentityInput input;
            input.domain = v.domain;
            input.namespaceBytes = namespaceBytes;
            input.kind = v.kind;
            input.stableKey = v.stableKey;
            return input;
        }

        [[nodiscard]] bool BcryptSha256(std::span<const std::uint8_t> bytes,
            Hash::Sha256Digest& out, std::string& error)
        {
            experiment::cooked::Sha256Digest digest{};
            const std::span<const std::byte> view{
                reinterpret_cast<const std::byte*>(bytes.data()), bytes.size() };
            if (!experiment::cooked::ComputeSha256(view, digest, error)) return false;
            out = digest;
            return true;
        }
    }

    bool RunAssetIdentitySelfTest(std::string& outLog, AssetIdentityReport* report)
    {
        if (report) *report = {};
        IdentityChecker check{ outLog };
        outLog += "[assets.identity] ce.uuidv8.sha256.v1 프로필·충돌 registry 합성 검사\n";

        check.Check(assets::kIdentityProfile == "ce.uuidv8.sha256.v1",
            "프로필 문자열이 계획서 §2.1과 같다");

        // ── 1. SHA-256: FIPS 공표 벡터 + BCrypt 독립 대조 ────────────────────
        outLog += "  [1] SHA-256 KAT·BCrypt 대조\n";
        for (const vec::ShaVector& v : vec::kShaVectors)
        {
            const std::vector<std::uint8_t> input = FromHex(v.inputHex);
            const Hash::Sha256Digest digest =
                Hash::Sha256::Compute(input.data(), input.size());
            check.Check(Hash::ToHex(digest) == v.sha256Hex,
                std::string("KAT ") + v.name);
        }
        {
            // million 'a' — 블록 경계를 15,625번 넘는다. 스트리밍 Update로 넣어
            // 버퍼 이월 경로도 태운다.
            Hash::Sha256 sha;
            const std::string chunk(4099, 'a'); // 64의 배수가 아닌 조각
            std::size_t remaining = 1'000'000u;
            while (remaining > 0)
            {
                const std::size_t take = remaining < chunk.size() ? remaining : chunk.size();
                sha.Update(chunk.data(), take);
                remaining -= take;
            }
            check.Check(Hash::ToHex(sha.Finish()) == vec::kMillionASha256,
                "KAT million 'a' (스트리밍)");
        }
        {
            std::size_t agreed = 0, total = 0;
            std::string bcryptError;
            bool bcryptAvailable = true;
            for (std::size_t length = 0; length <= 300u && bcryptAvailable; length += 7u)
            {
                std::vector<std::uint8_t> buffer(length);
                for (std::size_t i = 0; i < length; ++i)
                    buffer[i] = static_cast<std::uint8_t>((i * 31u + length * 7u + 3u) & 0xFFu);
                Hash::Sha256Digest reference{};
                if (!BcryptSha256(buffer, reference, bcryptError))
                {
                    bcryptAvailable = false;
                    break;
                }
                ++total;
                if (Hash::Sha256::Compute(buffer.data(), buffer.size()) == reference)
                    ++agreed;
            }
            check.Check(bcryptAvailable, "BCrypt 대조군 사용 가능 (" + bcryptError + ")");
            check.Check(total >= 43u && agreed == total,
                "BCrypt 대조 " + std::to_string(agreed) + "/" + std::to_string(total));
            if (report) { report->bcryptMatched = agreed; report->bcryptTotal = total; }
            outLog += "    bcrypt agreement: " + std::to_string(agreed) + "/"
                + std::to_string(total) + "\n";
        }

        // ── 2. 프로필 벡터(Python 독립 유도) ─────────────────────────────────
        outLog += "  [2] 프로필 벡터\n";
        std::size_t vectorCount = 0;
        std::set<std::string> seenUuids;
        std::set<std::string> seenInputs;
        for (const vec::ProfileVector& v : vec::kProfileVectors)
        {
            ++vectorCount;
            const std::vector<std::uint8_t> ns = FromHex(v.namespaceHex);
            const assets::IdentityInput input = MakeInput(v, ns);

            // 바이트 배치 자체를 단정한다 — UUID만 맞추면 "우연히 같은 값"과
            // "같은 배치"를 못 가른다.
            std::vector<std::uint8_t> bytes;
            assets::IdentityIssue issue{};
            std::string context;
            const bool built = (std::string_view(v.profile) == assets::kIdentityProfile)
                ? assets::BuildIdentityInputBytes(input, bytes, issue, context)
                : true;
            if (std::string_view(v.profile) == assets::kIdentityProfile)
            {
                check.Check(built && bytes == FromHex(v.inputHex),
                    std::string("입력 바이트열 ") + v.name);
                const Hash::Sha256Digest digest =
                    Hash::Sha256::Compute(bytes.data(), bytes.size());
                check.Check(Hash::ToHex(digest) == v.sha256Hex,
                    std::string("입력 SHA-256 ") + v.name);
                const assets::IdentityDerivation derived = assets::DeriveIdentity(input);
                check.Check(derived.Succeeded()
                    && Uuid::ToString(derived.uuid) == v.uuid,
                    std::string("DeriveIdentity ") + v.name + " = " + v.uuid);
            }
            const assets::IdentityDerivation withProfile =
                assets::DeriveIdentityWithProfile(v.profile, input);
            check.Check(withProfile.Succeeded()
                && Uuid::ToString(withProfile.uuid) == v.uuid,
                std::string("DeriveIdentityWithProfile ") + v.name);
            check.Check(assets::IsUuidV8(withProfile.uuid),
                std::string("version 8·variant 10 ") + v.name);

            Uuid::Uuid16 parsed{};
            check.Check(assets::TryParseCanonicalUuidV8(v.uuid, parsed)
                && parsed == withProfile.uuid,
                std::string("canonical 왕복 ") + v.name);

            // 결정성 — 세 번 유도해 같다.
            const assets::IdentityDerivation again =
                assets::DeriveIdentityWithProfile(v.profile, input);
            const assets::IdentityDerivation third =
                assets::DeriveIdentityWithProfile(v.profile, input);
            check.Check(again.uuid == withProfile.uuid && third.uuid == withProfile.uuid,
                std::string("결정성 ") + v.name);

            if (report) report->vectors.push_back({v.name, Uuid::ToString(withProfile.uuid)});
            seenUuids.insert(v.uuid);
            seenInputs.insert(v.inputHex);
            outLog += std::string("    vector ") + v.name + " = " + v.uuid + "\n";
        }
        check.Check(vectorCount >= 15u, "벡터 수 " + std::to_string(vectorCount) + " ≥ 15");
        check.Check(seenUuids.size() == vectorCount, "벡터 UUID 전부 서로 다름");
        check.Check(seenInputs.size() == vectorCount, "벡터 입력 바이트열 전부 서로 다름");

        // ── 3. 관계 단정 — 무엇이 신원을 가르는가 ────────────────────────────
        outLog += "  [3] 구분 관계\n";
        {
            const auto* alpha = FindVector("model.alpha");
            const auto* altEpoch = FindVector("model.alpha.alt-epoch");
            const auto* ns16 = FindVector("namespace.16");
            const auto* v0 = FindVector("mutation.profile-v0");
            const auto* abC = FindVector("prefix.ab-c");
            const auto* aBc = FindVector("prefix.a-bc");
            const auto* lead = FindVector("space.leading");
            const auto* trail = FindVector("space.trailing");
            check.Check(alpha && altEpoch && ns16 && v0 && abC && aBc && lead && trail,
                "관계 벡터 8종 존재");
            if (alpha && altEpoch && ns16 && v0 && abC && aBc && lead && trail)
            {
                check.Check(std::string_view(alpha->uuid) != altEpoch->uuid,
                    "epoch가 다르면 같은 key도 다른 ModelId");
                check.Check(std::string_view(alpha->uuid) != ns16->uuid,
                    "namespace 길이(32 vs 16)가 신원을 가른다");
                check.Check(std::string_view(alpha->uuid) != v0->uuid
                    && std::string_view(alpha->inputHex) != v0->inputHex,
                    "프로필 문자열 한 글자 변이 → 다른 신원(변이 검출 발화)");
                check.Check(std::string_view(abC->uuid) != aBc->uuid,
                    "길이 접두가 kind|key 경계를 가른다 (ab|c ≠ a|bc)");
                check.Check(std::string_view(lead->uuid) != trail->uuid,
                    "공백을 자르지 않는다 (' x' ≠ 'x ')");

                // 제품 상수로 v0 입력을 유도하면 alpha와 같아야 한다 — 벡터가
                // "입력 같음·프로필만 다름"을 실제로 표현하는지 확인.
                // ★ namespace 바이트는 span으로 참조된다 — 임시 vector를 바로 넘기면
                //   표현식이 끝나는 순간 dangling이다(첫 실행에서 registry 구간 5건이
                //   정확히 그렇게 붉었다). 항상 지역 변수에 담아 넘긴다.
                const std::vector<std::uint8_t> v0Ns = FromHex(v0->namespaceHex);
                const assets::IdentityDerivation v0AsV1 =
                    assets::DeriveIdentity(MakeInput(*v0, v0Ns));
                check.Check(v0AsV1.Succeeded()
                    && Uuid::ToString(v0AsV1.uuid) == alpha->uuid,
                    "v0 벡터 입력을 v1 프로필로 유도 = model.alpha");
            }
        }

        // ── 4. §2.3 계층 유도 ────────────────────────────────────────────────
        outLog += "  [4] 계층 유도\n";
        {
            assets::IdentityEpochSeed seed{};
            for (std::size_t i = 0; i < seed.size(); ++i)
                seed[i] = static_cast<std::uint8_t>(i);

            const assets::IdentityDerivation model =
                assets::DeriveModelId(seed, "authoring/alpha");
            const auto* alpha = FindVector("model.alpha");
            check.Check(model.Succeeded() && alpha
                && Uuid::ToString(model.uuid) == alpha->uuid,
                "DeriveModelId = model.alpha 벡터");

            struct KindCase final
            {
                assets::SubAssetKind kind;
                const char* vectorName;
                const char* key;
            };
            const KindCase cases[] = {
                { assets::SubAssetKind::Material, "subasset.material", "material/MI_Hero_GU_F_Mythic" },
                { assets::SubAssetKind::Texture, "subasset.texture", "image/Hero_GU_F_Mythic_D" },
                { assets::SubAssetKind::Mesh, "subasset.mesh", "mesh/SK_Hero_GU_F_Mythic/0" },
                { assets::SubAssetKind::Skeleton, "subasset.skeleton", "skeleton/root" },
                { assets::SubAssetKind::Animation, "subasset.animation", "animation/Idle" },
            };
            for (const KindCase& c : cases)
            {
                const auto* v = FindVector(c.vectorName);
                const assets::IdentityDerivation sub =
                    assets::DeriveSubAssetId(model.uuid, c.kind, c.key);
                check.Check(v && sub.Succeeded() && Uuid::ToString(sub.uuid) == v->uuid,
                    std::string("DeriveSubAssetId(") + assets::ToKindName(c.kind).data()
                    + ") = " + c.vectorName);
            }

            // kind 어휘 왕복 — 문자열이 계약이다.
            for (std::size_t i = 0; i < assets::kSubAssetKindCount; ++i)
            {
                const auto kind = static_cast<assets::SubAssetKind>(i);
                assets::SubAssetKind parsed{};
                check.Check(assets::TryParseKindName(assets::ToKindName(kind), parsed)
                    && parsed == kind, "kind 이름 왕복 " + std::string(assets::ToKindName(kind)));
            }
            assets::SubAssetKind notKind{};
            check.Check(!assets::TryParseKindName("model", notKind),
                "'model'은 subasset kind가 아니다");

            // legacy v4 GUID를 namespace로 넘기면 값에서 막힌다.
            Uuid::Uuid16 legacyV4 = Uuid::Parse("68b21a01-958e-44ed-8820-a2b9aa289587");
            const assets::IdentityDerivation fromV4 = assets::DeriveSubAssetId(
                legacyV4, assets::SubAssetKind::Material, "material/x");
            check.Check(!fromV4.Succeeded()
                && fromV4.issue == assets::IdentityIssue::NamespaceNotV8,
                "v4 GUID namespace → NamespaceNotV8");
            const assets::IdentityDerivation emptyKey = assets::DeriveModelId(seed, "");
            check.Check(!emptyKey.Succeeded()
                && emptyKey.issue == assets::IdentityIssue::EmptyStableKey,
                "빈 authoring key → EmptyStableKey");
        }

        // ── 5. fail-closed ───────────────────────────────────────────────────
        outLog += "  [5] fail-closed\n";
        {
            const std::vector<std::uint8_t> ns(16, 0x11u);
            auto expectIssue = [&](assets::IdentityInput input,
                assets::IdentityIssue expected, const char* label)
            {
                const assets::IdentityDerivation d = assets::DeriveIdentity(input);
                check.Check(!d.Succeeded() && d.issue == expected && d.uuid.IsNil(),
                    std::string(label) + " → " + std::string(assets::ToString(expected))
                    + " (실제 " + std::string(assets::ToString(d.issue)) + ")");
            };
            assets::IdentityInput base;
            base.domain = "subasset"; base.namespaceBytes = ns;
            base.kind = "material"; base.stableKey = "k";

            { auto i = base; i.domain = "";     expectIssue(i, assets::IdentityIssue::EmptyDomain, "빈 domain"); }
            { auto i = base; i.namespaceBytes = {}; expectIssue(i, assets::IdentityIssue::EmptyNamespace, "빈 namespace"); }
            { auto i = base; i.kind = "";       expectIssue(i, assets::IdentityIssue::EmptyKind, "빈 kind"); }
            { auto i = base; i.stableKey = "";  expectIssue(i, assets::IdentityIssue::EmptyStableKey, "빈 stableKey"); }

            const char* badUtf8[] = {
                "\xC0\x80",          // overlong NUL
                "\xED\xA0\x80",      // UTF-16 surrogate D800
                "\xF4\x90\x80\x80",  // > U+10FFFF
                "\xE2\x82",          // 절단
                "\x80",              // 홀로 선 continuation
                "a\xFFz",            // 0xFF
            };
            for (const char* bad : badUtf8)
            {
                auto i = base; i.stableKey = bad;
                expectIssue(i, assets::IdentityIssue::InvalidUtf8, "비정형 UTF-8 key");
                check.Check(!assets::IsWellFormedUtf8(bad), "IsWellFormedUtf8 거부");
            }
            check.Check(assets::IsWellFormedUtf8("\xF0\x9F\x8E\xA8 \xEA\xB8\xB0 \xC3\xA9"),
                "IsWellFormedUtf8 수용(4·3·2바이트)");

            // NFC: e + U+0301(결합 악센트)는 NFD — 거부. U+00E9는 NFC — 수용.
            {
                auto i = base; i.stableKey = "e\xCC\x81";
                expectIssue(i, assets::IdentityIssue::NotNfc, "NFD key(e+U+0301)");
                auto j = base; j.stableKey = "\xC3\xA9";
                check.Check(assets::DeriveIdentity(j).Succeeded(), "NFC key(U+00E9) 수용");
                std::string normalized, error;
                check.Check(assets::NormalizeUtf8Nfc("e\xCC\x81", normalized, error)
                    && normalized == "\xC3\xA9",
                    "NormalizeUtf8Nfc(e+U+0301) = U+00E9 (" + error + ")");
                // 한글 자모 분해형(ᄒ ᅡ ᆫ) → 음절 '한'(U+D55C)
                auto k = base; k.stableKey = "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB";
                expectIssue(k, assets::IdentityIssue::NotNfc, "한글 분해형 key");
                check.Check(assets::NormalizeUtf8Nfc("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB",
                    normalized, error) && normalized == "\xED\x95\x9C",
                    "NormalizeUtf8Nfc(한) = 한");
                // 정규화 도구는 비정형 입력을 고치지 않는다.
                check.Check(!assets::NormalizeUtf8Nfc("\xC0\x80", normalized, error),
                    "NormalizeUtf8Nfc가 비정형 UTF-8을 거부");
            }
        }

        // ── 6. legacy 표기·판정과의 배타성 ────────────────────────────────────
        outLog += "  [6] legacy 배타\n";
        {
            Uuid::Uuid16 out{};
            check.Check(!assets::TryParseCanonicalUuidV8(
                "68b21a01-958e-44ed-8820-a2b9aa289587", out), "v4 표기 거부");
            const Uuid::Uuid16 v5 = Uuid::FromName(
                Uuid::Parse("68b21a01-958e-44ed-8820-a2b9aa289587"), "gltf/material/0");
            check.Check(!assets::TryParseCanonicalUuidV8(Uuid::ToString(v5), out),
                "v5(SHA-1) 표기 거부");
            // pseudo v5-as-v4(현 ModelIdentityRefresher 방식)도 v8이 아니다.
            Uuid::Uuid16 pseudo = v5;
            pseudo.data[6] = static_cast<std::uint8_t>((pseudo.data[6] & 0x0Fu) | 0x40u);
            check.Check(!assets::IsUuidV8(pseudo) && experiment::IsAssetIdV4({ pseudo }),
                "pseudo v5-as-v4는 v8이 아니다(legacy 판정에는 걸린다)");

            const auto* alpha = FindVector("model.alpha");
            if (alpha)
            {
                std::string upper(alpha->uuid);
                for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                check.Check(!assets::TryParseCanonicalUuidV8(upper, out), "대문자 거부");
                check.Check(!assets::TryParseCanonicalUuidV8(
                    "{" + std::string(alpha->uuid) + "}", out), "brace 거부");
                std::string noHyphen;
                for (char c : std::string_view(alpha->uuid)) if (c != '-') noHyphen.push_back(c);
                check.Check(!assets::TryParseCanonicalUuidV8(noHyphen, out), "무하이픈 거부");
                check.Check(!assets::TryParseCanonicalUuidV8(
                    std::string(alpha->uuid) + " ", out), "꼬리 공백 거부");

                Uuid::Uuid16 v8 = Uuid::Parse(alpha->uuid);
                check.Check(!experiment::IsAssetIdV4({ v8 }),
                    "v8은 legacy IsAssetIdV4에 걸리지 않는다(두 세계가 겹치지 않음)");
                Uuid::Uuid16 badVariant = v8;
                badVariant.data[8] = static_cast<std::uint8_t>(badVariant.data[8] & 0x3Fu);
                check.Check(!assets::IsUuidV8(badVariant), "variant 비트 훼손 → v8 아님");
                Uuid::Uuid16 badVersion = v8;
                badVersion.data[6] = static_cast<std::uint8_t>((badVersion.data[6] & 0x0Fu) | 0x40u);
                check.Check(!assets::IsUuidV8(badVersion), "version nibble 훼손 → v8 아님");
            }
            check.Check(!assets::IsUuidV8(Uuid::Nil()), "nil은 v8이 아니다");
        }

        // ── 7. bit 설정 — digest와의 관계를 256건에서 단정 ────────────────────
        outLog += "  [7] bit 설정\n";
        {
            std::size_t ok = 0;
            const std::vector<std::uint8_t> ns(32, 0x5Au);
            for (int i = 0; i < 256; ++i)
            {
                const std::string key = "k/" + std::to_string(i);
                assets::IdentityInput input;
                input.domain = "model"; input.namespaceBytes = ns;
                input.kind = "model"; input.stableKey = key;
                std::vector<std::uint8_t> bytes;
                assets::IdentityIssue issue{};
                std::string context;
                if (!assets::BuildIdentityInputBytes(input, bytes, issue, context)) continue;
                const Hash::Sha256Digest digest =
                    Hash::Sha256::Compute(bytes.data(), bytes.size());
                const Uuid::Uuid16 uuid = assets::DeriveUuidFromIdentityInputBytes(bytes);
                bool same = assets::IsUuidV8(uuid);
                for (std::size_t b = 0; b < 16; ++b)
                {
                    if (b == 6) same = same && (uuid.data[6] & 0x0Fu) == (digest[6] & 0x0Fu);
                    else if (b == 8) same = same && (uuid.data[8] & 0x3Fu) == (digest[8] & 0x3Fu);
                    else same = same && uuid.data[b] == digest[b];
                }
                if (same) ++ok;
            }
            check.Check(ok == 256u, "digest[0..15] 보존·version/variant 6비트만 고정 "
                + std::to_string(ok) + "/256");
        }

        // ── 8. 충돌 registry ─────────────────────────────────────────────────
        outLog += "  [8] registry\n";
        {
            assets::IdentityRegistry registry;
            std::size_t registered = 0;
            std::size_t duplicates = 0;
            std::string firstDuplicate;
            for (const vec::ProfileVector& v : vec::kProfileVectors)
            {
                if (std::string_view(v.profile) != assets::kIdentityProfile) continue;
                const std::vector<std::uint8_t> ns = FromHex(v.namespaceHex);
                const assets::IdentityInput input = MakeInput(v, ns);
                const assets::IdentityRegisterResult r =
                    registry.Register(input, v.name, Uuid::Parse(v.uuid));
                if (r.Succeeded()) ++registered;
                else if (r.outcome == assets::IdentityRegisterOutcome::DuplicateTuple)
                {
                    ++duplicates;
                    if (firstDuplicate.empty()) firstDuplicate = r.message;
                }
            }
            check.Check(registered == 14u && duplicates == 0u,
                "v1 벡터 14건 등록 (" + std::to_string(registered) + ", 중복 "
                + std::to_string(duplicates) + ")");
            check.Check(registry.Size() == 14u, "registry size 14");

            // v0 변이 벡터의 입력은 model.alpha와 tuple이 같다 → DuplicateTuple.
            const auto* v0 = FindVector("mutation.profile-v0");
            if (v0)
            {
                const std::vector<std::uint8_t> v0Ns = FromHex(v0->namespaceHex);
                const assets::IdentityRegisterResult r = registry.Register(
                    MakeInput(*v0, v0Ns), "mutation.profile-v0");
                check.Check(r.outcome == assets::IdentityRegisterOutcome::DuplicateTuple
                    && r.conflictingContext == "model.alpha",
                    "같은 tuple 재등록 → DuplicateTuple(model.alpha)");
                check.Check(registry.Size() == 14u, "거부는 등록하지 않는다");
            }

            // claimed가 유도값과 다르면 RecomputeMismatch.
            const auto* beta = FindVector("model.beta");
            const auto* alpha = FindVector("model.alpha");
            if (beta && alpha)
            {
                assets::IdentityRegistry fresh;
                const std::vector<std::uint8_t> betaNs = FromHex(beta->namespaceHex);
                const std::vector<std::uint8_t> alphaNs = FromHex(alpha->namespaceHex);
                const assets::IdentityRegisterResult r = fresh.Register(
                    MakeInput(*beta, betaNs), "beta-with-alpha-uuid",
                    Uuid::Parse(alpha->uuid));
                check.Check(r.outcome == assets::IdentityRegisterOutcome::RecomputeMismatch
                    && Uuid::ToString(r.uuid) == beta->uuid && fresh.Size() == 0u,
                    "sidecar 주장값 ≠ 재유도 → RecomputeMismatch, 미등록");

                const assets::IdentityRegisterResult r2 = fresh.RegisterDerived(
                    FromHex(beta->inputHex), Uuid::Parse(alpha->uuid), "derived-mismatch");
                check.Check(r2.outcome == assets::IdentityRegisterOutcome::RecomputeMismatch,
                    "RegisterDerived도 재유도 대조를 한다");
                const assets::IdentityRegisterResult r3 = fresh.RegisterDerived(
                    FromHex(beta->inputHex), Uuid::Parse(beta->uuid), "derived-ok");
                check.Check(r3.Succeeded() && fresh.Size() == 1u
                    && fresh.Contains(Uuid::Parse(beta->uuid)),
                    "RegisterDerived 정상 등록");
                const assets::IdentityRegistryEntry* found = fresh.Find(Uuid::Parse(beta->uuid));
                check.Check(found && found->context == "derived-ok", "Find가 context를 되돌린다");
                check.Check(!fresh.Contains(Uuid::Parse(alpha->uuid)), "미등록 UUID는 Contains false");

                // UUID 충돌 — 실제 SHA-256 충돌은 만들 수 없으므로 test seam으로
                // 조립한다: 다른 바이트열에 alpha의 UUID를 붙여 두고 alpha를 등록.
                assets::IdentityRegistry forged;
                forged.InsertUncheckedForTest(FromHex(beta->inputHex),
                    Uuid::Parse(alpha->uuid), "forged-collision");
                const assets::IdentityRegisterResult r4 = forged.Register(
                    MakeInput(*alpha, alphaNs), "model.alpha");
                check.Check(r4.outcome == assets::IdentityRegisterOutcome::UuidCollision
                    && r4.conflictingContext == "forged-collision",
                    "다른 tuple·같은 UUID → UuidCollision(forged-collision)");
                std::vector<std::string> issues;
                check.Check(!forged.VerifyBijection(issues) && !issues.empty(),
                    "위조 항목이 있는 registry는 VerifyBijection 실패");
                bool sawMismatch = false;
                for (const std::string& s : issues)
                    if (s.rfind("recompute mismatch", 0) == 0) sawMismatch = true;
                check.Check(sawMismatch, "VerifyBijection이 재유도 불일치를 보고");
            }

            std::vector<std::string> issues;
            check.Check(registry.VerifyBijection(issues) && issues.empty(),
                "정상 registry의 VerifyBijection 통과 (" + std::to_string(issues.size()) + " issues)");

            // 잘못된 입력은 InvalidInput + 사유.
            assets::IdentityInput bad;
            bad.domain = "model"; bad.kind = "model"; bad.stableKey = "e\xCC\x81";
            const std::vector<std::uint8_t> ns(32, 0x01u);
            bad.namespaceBytes = ns;
            const assets::IdentityRegisterResult rBad = registry.Register(bad, "bad");
            check.Check(rBad.outcome == assets::IdentityRegisterOutcome::InvalidInput
                && rBad.issue == assets::IdentityIssue::NotNfc && registry.Size() == 14u,
                "비NFC 입력 → InvalidInput(not-nfc), 미등록");

            registry.Clear();
            check.Check(registry.Size() == 0u, "Clear");
        }

        outLog += "  단정 " + std::to_string(check.passed + check.failed) + "건 중 통과 "
            + std::to_string(check.passed) + " · 실패 " + std::to_string(check.failed) + "\n";
        if (report) { report->passed = check.passed; report->failed = check.failed; }
        return check.failed == 0;
    }
}
