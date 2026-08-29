#include "ExperimentParity/ExperimentTextureCookSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/TextureCookProducer.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <sstream>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ck = experiment::cooked;

        // 단정 하나. 실패해도 계속 진행한다 — 첫 실패에서 멈추면 "몇 개가
        // 깨졌는가"를 못 보고, 그러면 변이가 정확히 몇 건을 빨갛게 만드는지도
        // 셀 수 없다(이 저장소의 이빨 증명 규칙).
        struct Checker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& what)
            {
                if (condition) { ++passed; return; }
                ++failed;
                log += "    [실패] " + what + "\n";
            }
        };

        [[nodiscard]] std::filesystem::path MakeScratchRoot()
        {
            const auto ticks = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            std::ostringstream name;
            name << "cemc-texcook-" << std::hex
                << static_cast<unsigned int>(_getpid())
                << '-' << static_cast<std::uint64_t>(ticks);
            return std::filesystem::temp_directory_path() / name.str();
        }

        [[nodiscard]] bool WriteFile(const std::filesystem::path& path,
            const std::string& bytes)
        {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return false;

            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream) return false;
            if (!bytes.empty())
            {
                stream.write(bytes.data(),
                    static_cast<std::streamsize>(bytes.size()));
            }
            stream.flush();
            return static_cast<bool>(stream);
        }

        [[nodiscard]] bool ReadFileBytes(const std::filesystem::path& path,
            std::string& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff bytes = stream.tellg();
            if (bytes < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(bytes));
            if (!out.empty())
                stream.read(out.data(), static_cast<std::streamsize>(out.size()));
            return stream.good() || stream.eof();
        }

        // 최소한의 sidecar. 실제 importer 가 쓰는 형태와 같은 최상위 guid 한 줄.
        [[nodiscard]] std::string MetaYaml(const std::string& guid)
        {
            return "guid: " + guid + "\nimportSettings:\n  extension: .png\n";
        }

        [[nodiscard]] std::string BytesToString(
            const std::vector<std::byte>& bytes)
        {
            std::string text;
            text.resize(bytes.size());
            for (std::size_t index = 0u; index < bytes.size(); ++index)
                text[index] = static_cast<char>(bytes[index]);
            return text;
        }

        // 정상 자산 하나를 만든다. guid 는 canonical UUIDv4 여야 한다.
        struct Fixture final
        {
            std::filesystem::path assetRoot{};
            std::filesystem::path source{};
            std::string guid{};
            std::string payload{};
        };

        [[nodiscard]] Fixture MakeFixture(const std::filesystem::path& root,
            const std::string& relative, const std::string& guid,
            const std::string& payload, bool writeMeta = true,
            const std::string& metaGuid = {})
        {
            Fixture fixture;
            fixture.assetRoot = root;
            fixture.source = root / relative;
            fixture.guid = guid;
            fixture.payload = payload;
            (void)WriteFile(fixture.source, payload);
            if (writeMeta)
            {
                std::filesystem::path meta = fixture.source;
                meta += ".meta";
                (void)WriteFile(meta,
                    MetaYaml(metaGuid.empty() ? guid : metaGuid));
            }
            return fixture;
        }

        // ★ **어느 guard 가 걸었는지까지 본다.**
        //
        //   "거부됐다"만 보면 guard 를 지워도 초록일 수 있다 — 다른 guard 가
        //   우연히 같은 입력을 거부하기 때문이다. ShaderMeta producer 에서
        //   실제로 그 일이 났고(변이 둘이 통과), 거기서 죽은 guard 두 개가
        //   드러났다. 같은 결함이 여기 있을 이유가 없어서 함께 고친다.
        void ExpectRejected(Checker& check, const Fixture& fixture,
            const std::string& expectedContext, const std::string& what)
        {
            const ck::TextureCookProductResult result =
                ck::BuildTextureCookProduct({ fixture.source, fixture.assetRoot });
            check.Check(!result.Succeeded(), what + " — 거부해야 한다");
            // ★ 거부해 놓고 product 를 채워 주면 호출자가 그것을 게시한다.
            //   "실패했다"와 "아무것도 안 내놓았다"를 함께 봐야 한다.
            check.Check(!result.product.has_value(),
                what + " — 거부 시 product 가 없어야 한다");
            check.Check(!result.issues.empty(),
                what + " — 거부 사유가 있어야 한다");
            if (result.issues.empty()) return;

            check.Check(result.issues.front().context == expectedContext,
                what + " — 사유가 '" + expectedContext + "' 여야 한다(실제 '"
                + result.issues.front().context + "')");
        }
    }

    bool RunExperimentTextureCookSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.texcook] 합성 검사\n";

        const std::filesystem::path root = MakeScratchRoot();
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            outLog += "    [실패] 임시 asset root 를 만들 수 없다\n";
            return false;
        }

        // ── 1. allowlist 세 갈래를 모두 태운다 ──────────────────────────
        // 실자산은 .dds 를 하나도 통과시키지 못하므로 여기가 유일한 커버리지다.
        struct Case final { const char* relative; const char* guid; const char* ext; };
        const Case cases[] = {
            { "Tex/a.png", "11111111-1111-4111-8111-111111111111", ".png" },
            { "Tex/b.hdr", "22222222-2222-4222-9222-222222222222", ".hdr" },
            { "Tex/c.dds", "33333333-3333-4333-a333-333333333333", ".dds" },
        };

        for (const Case& item : cases)
        {
            const std::string payload =
                std::string("payload-") + item.ext + "-bytes";
            const Fixture fixture = MakeFixture(root, item.relative,
                item.guid, payload);
            const ck::TextureCookProductResult result =
                ck::BuildTextureCookProduct({ fixture.source, root });

            const std::string tag = std::string("확장자 ") + item.ext;
            check.Check(result.Succeeded(), tag + " 는 통과해야 한다");
            if (!result.Succeeded()) continue;

            const ck::TextureCookProduct& product = *result.product;
            check.Check(BytesToString(product.artifactBytes) == payload,
                tag + " artifact 가 원본 바이트와 같아야 한다");
            check.Check(product.sourceExtension == item.ext,
                tag + " sourceExtension");

            const std::string expectedPath = std::string("Derived/Textures/")
                + std::string(item.guid).substr(0u, 2u) + "/" + item.guid
                + item.ext;
            check.Check(product.artifactPath == expectedPath,
                tag + " artifactPath 가 GUID 주소여야 한다");

            const ck::CookedAssetManifestEntry& entry = product.manifestEntry;
            check.Check(entry.kind == ck::CookedAssetKind::Texture,
                tag + " manifest kind");
            check.Check(entry.formatVersion == ck::kTextureArtifactVersion,
                tag + " manifest formatVersion");
            check.Check(entry.byteSize == product.artifactBytes.size(),
                tag + " manifest byteSize");
            check.Check(entry.artifactPath == product.artifactPath,
                tag + " manifest artifactPath");
            // 텍스처는 잎이다. 의존이 붙으면 폐포 계산이 틀어진다.
            check.Check(entry.dependencies.empty(),
                tag + " 텍스처는 의존이 없어야 한다");

            // 해시가 내용에서 나오는가. 상수를 넣어 두고 통과하는 것을 막는다.
            ck::Sha256Digest expected{};
            std::string hashError;
            const bool hashed = ck::ComputeSha256(product.artifactBytes,
                expected, hashError);
            check.Check(hashed && entry.contentSha256 == expected,
                tag + " manifest contentSha256 가 내용 해시여야 한다");
        }

        // ── 2. 결정성 ──────────────────────────────────────────────────
        {
            const Fixture fixture = MakeFixture(root, "Det/x.png",
                "44444444-4444-4444-8444-444444444444", "deterministic-bytes");
            const ck::TextureCookProductResult first =
                ck::BuildTextureCookProduct({ fixture.source, root });
            const ck::TextureCookProductResult second =
                ck::BuildTextureCookProduct({ fixture.source, root });
            check.Check(first.Succeeded() && second.Succeeded(),
                "결정성 — 두 번 다 통과해야 한다");
            if (first.Succeeded() && second.Succeeded())
            {
                check.Check(first.product->artifactBytes
                    == second.product->artifactBytes, "결정성 — 같은 바이트");
                check.Check(first.product->manifestEntry.contentSha256
                    == second.product->manifestEntry.contentSha256,
                    "결정성 — 같은 해시");
                check.Check(first.product->artifactPath
                    == second.product->artifactPath, "결정성 — 같은 경로");
            }
        }

        // ── 3. fail-closed ─────────────────────────────────────────────
        ExpectRejected(check, MakeFixture(root, "Bad/unsupported.tga",
            "55555555-5555-4555-8555-555555555555", "tga"),
            "texture.extension", "지원하지 않는 확장자(.tga)");

        ExpectRejected(check, MakeFixture(root, "Bad/nometa.png",
            "66666666-6666-4666-8666-666666666666", "png", false),
            "texture.meta", ".meta 누락");

        ExpectRejected(check, MakeFixture(root, "Bad/brace.png",
            "77777777-7777-4777-8777-777777777777", "png", true,
            "{77777777-7777-4777-8777-777777777777}"),
            "texture.meta", "brace 표기 GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/upper.png",
            "88888888-8888-4888-8888-888888888888", "png", true,
            "88888888-8888-4888-8888-88888888888A"),
            "texture.meta", "대문자 GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/nil.png",
            "99999999-9999-4999-8999-999999999999", "png", true,
            "00000000-0000-0000-0000-000000000000"),
            "texture.meta", "nil GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/nonv4.png",
            "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "png", true,
            "aaaaaaaa-aaaa-1aaa-8aaa-aaaaaaaaaaaa"),
            "texture.meta", "UUIDv1 GUID");

        ExpectRejected(check, MakeFixture(root, "Bad/empty.png",
            "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", ""),
            "texture.read", "0바이트 source");

        {
            // asset root 밖. root 의 형제로 두어 `..` 로만 닿게 한다.
            const std::filesystem::path outsideRoot = root / "Inside";
            std::filesystem::create_directories(outsideRoot, error);
            Fixture outside = MakeFixture(root, "Outside/o.png",
                "cccccccc-cccc-4ccc-8ccc-cccccccccccc", "outside");
            outside.assetRoot = outsideRoot;
            ExpectRejected(check, outside, "request.sourcePath",
                "asset root 밖 source");
        }

        {
            Fixture missing = MakeFixture(root, "Bad/present.png",
                "dddddddd-dddd-4ddd-8ddd-dddddddddddd", "present");
            missing.source = root / "Bad" / "does-not-exist.png";
            ExpectRejected(check, missing, "request.sourcePath",
                "존재하지 않는 source");
        }

        {
            Fixture badRoot = MakeFixture(root, "Bad/root.png",
                "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", "root");
            badRoot.assetRoot = root / "no-such-directory";
            ExpectRejected(check, badRoot, "request.assetRoot",
                "디렉터리가 아닌 asset root");
        }

        // ── 4. 경로 헬퍼가 깨지는 표기를 막는가 ────────────────────────
        {
            experiment::AssetId id{};
            const bool parsed = experiment::TryParseCanonicalAssetId(
                "12345678-1234-4234-8234-123456789abc", id);
            check.Check(parsed, "경로 헬퍼 fixture GUID 파싱");

            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".png")
                == "Derived/Textures/12/12345678-1234-4234-8234-123456789abc.png",
                "경로 헬퍼 정상 표기");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, "png").empty(),
                "경로 헬퍼 — 점 없는 확장자 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, "").empty(),
                "경로 헬퍼 — 빈 확장자 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".").empty(),
                "경로 헬퍼 — 점만 있는 확장자 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".a/b").empty(),
                "경로 헬퍼 — 슬래시 포함 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(id, ".a.b").empty(),
                "경로 헬퍼 — 점 두 개 거부");
            check.Check(ck::MakeDerivedTextureArtifactPath(
                experiment::AssetId{}, ".png").empty(),
                "경로 헬퍼 — nil GUID 거부");
        }

        // ── 5. allowlist 술어 ──────────────────────────────────────────
        check.Check(ck::IsSupportedTextureExtension(".png"), "allowlist .png");
        check.Check(ck::IsSupportedTextureExtension(".hdr"), "allowlist .hdr");
        check.Check(ck::IsSupportedTextureExtension(".dds"), "allowlist .dds");
        check.Check(!ck::IsSupportedTextureExtension(".PNG"),
            "allowlist 는 소문자만 받는다");
        check.Check(!ck::IsSupportedTextureExtension(".tga"),
            "allowlist 에 없는 확장자");
        check.Check(!ck::IsSupportedTextureExtension(""), "allowlist 빈 문자열");

        std::filesystem::remove_all(root, error);

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentTextureCookReal(const std::string& assetRootPath,
        const std::string& texturePath, std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.texcook] 실자산: " + texturePath + "\n";

        const std::filesystem::path source(texturePath);
        const ck::TextureCookProductResult result =
            ck::BuildTextureCookProduct({ source, std::filesystem::path(assetRootPath) });

        check.Check(result.Succeeded(), "실자산 cook 이 통과해야 한다");
        if (!result.Succeeded())
        {
            for (const ck::TextureCookProductIssue& issue : result.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            outLog += "  실자산 단정 실패\n";
            return false;
        }

        const ck::TextureCookProduct& product = *result.product;
        std::string original;
        check.Check(ReadFileBytes(source, original), "원본을 읽을 수 있어야 한다");
        check.Check(BytesToString(product.artifactBytes) == original,
            "artifact 가 원본과 비트 단위로 같아야 한다");
        check.Check(product.manifestEntry.byteSize == original.size(),
            "manifest byteSize 가 원본 크기와 같아야 한다");

        ck::Sha256Digest expected{};
        std::string hashError;
        const bool hashed = ck::ComputeSha256(product.artifactBytes,
            expected, hashError);
        check.Check(hashed && product.manifestEntry.contentSha256 == expected,
            "manifest 해시가 내용 해시여야 한다");

        char summary[256]{};
        std::snprintf(summary, sizeof(summary),
            "  실자산 단정 %zu/%zu · %llu B · %s\n",
            check.passed, check.passed + check.failed,
            static_cast<unsigned long long>(product.artifactBytes.size()),
            product.artifactPath.c_str());
        outLog += summary;
        return check.failed == 0u;
    }
}
