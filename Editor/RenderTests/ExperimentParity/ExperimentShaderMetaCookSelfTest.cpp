#include "ExperimentParity/ExperimentShaderMetaCookSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/ShaderMetaCookProducer.h"
#include "ShaderMeta.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringNodeEquality.h"
#include "AuthoringParsedDocument.h"

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
            name << "cemc-smcook-" << std::hex
                << static_cast<unsigned int>(_getpid())
                << '-' << static_cast<std::uint64_t>(ticks);
            return std::filesystem::temp_directory_path() / name.str();
        }

        [[nodiscard]] bool WriteTextFile(const std::filesystem::path& path,
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

        [[nodiscard]] std::string MetaYaml(const std::string& guid)
        {
            return "guid: " + guid + "\n";
        }

        // 실자산과 같은 모양의 최소 문서. 정본 파서가 받는 형태여야 한다.
        [[nodiscard]] std::string ShaderMetaYaml(const std::string& sourceRelative)
        {
            return
                "schema: 1\n"
                "name: GateProbe\n"
                "source: " + sourceRelative + "\n"
                "properties:\n"
                "  - { name: baseColor, label: Base Color, type: float4, "
                "default: [1.0, 1.0, 1.0, 1.0] }\n"
                "  - { name: baseColorMap, label: Base Color Map, type: texture2d }\n"
                "keywords:\n"
                "  - { axis: SHADING_QUALITY, values: [full, reduced] }\n"
                "passes:\n"
                "  - name: Forward\n"
                "    vs: { entry: VSMain }\n"
                "    ps: { entry: PSMain }\n"
                "    state:\n"
                "      fill: solid\n"
                "      cull: back\n"
                "      blend: alpha\n"
                "      depthWrite: false\n"
                "      depthTest: less\n"
                "      topology: triangle\n"
                "    queue: transparent\n";
        }

        struct Fixture final
        {
            std::filesystem::path assetRoot{};
            std::filesystem::path shaderMeta{};
        };

        struct FixtureSpec final
        {
            std::string folder{ "Probe" };
            std::string stem{ "probe" };
            std::string metaGuid{ "11111111-1111-4111-8111-111111111111" };
            std::string shaderGuid{ "22222222-2222-4222-8222-222222222222" };
            std::string sourceRelative{ "probe.hlsl" };
            std::string documentOverride{};
            std::string extension{ ".shadermeta" };
            bool writeSidecar{ true };
            bool writeShaderSource{ true };
            bool writeShaderSidecar{ true };
        };

        [[nodiscard]] Fixture MakeFixture(const std::filesystem::path& root,
            const FixtureSpec& spec)
        {
            Fixture fixture;
            fixture.assetRoot = root;
            fixture.shaderMeta =
                root / spec.folder / (spec.stem + spec.extension);

            const std::string document = spec.documentOverride.empty()
                ? ShaderMetaYaml(spec.sourceRelative)
                : spec.documentOverride;
            (void)WriteTextFile(fixture.shaderMeta, document);

            if (spec.writeSidecar)
            {
                std::filesystem::path sidecar = fixture.shaderMeta;
                sidecar += ".meta";
                (void)WriteTextFile(sidecar, MetaYaml(spec.metaGuid));
            }

            if (spec.writeShaderSource)
            {
                const std::filesystem::path shader =
                    (fixture.shaderMeta.parent_path() / spec.sourceRelative)
                        .lexically_normal();
                (void)WriteTextFile(shader, "// probe hlsl\n");
                if (spec.writeShaderSidecar)
                {
                    std::filesystem::path sidecar = shader;
                    sidecar += ".meta";
                    (void)WriteTextFile(sidecar, MetaYaml(spec.shaderGuid));
                }
            }
            return fixture;
        }

        // ★ **어느 guard 가 걸었는지까지 본다.**
        //
        //   "거부됐다"만 보면 guard 를 지워도 초록이다 — 다른 guard 가 우연히
        //   같은 입력을 거부하기 때문이다. 실제로 그랬다: source 존재 검사를
        //   지워도 그 다음 `.meta` 판독이 거부했고, schema 검사를 무력화해도
        //   빈 `source` 가 경로 해소에서 거부됐다. 변이 둘이 그대로 통과했다.
        //
        //   그래서 기대하는 issue context 를 함께 받는다. 이제 guard 를 지우면
        //   거부는 되더라도 **사유가 달라져서** 빨개진다.
        void ExpectRejected(Checker& check, const Fixture& fixture,
            const std::string& expectedContext, const std::string& what)
        {
            const ck::ShaderMetaCookProductResult result =
                ck::BuildShaderMetaCookProduct(
                    { fixture.shaderMeta, fixture.assetRoot });
            check.Check(!result.Succeeded(), what + " — 거부해야 한다");
            check.Check(!result.product.has_value(),
                what + " — 거부 시 product 가 없어야 한다");
            check.Check(!result.issues.empty(),
                what + " — 거부 사유가 있어야 한다");
            if (result.issues.empty()) return;

            const bool matched = result.issues.front().context == expectedContext;
            check.Check(matched, what + " — 사유가 '" + expectedContext
                + "' 여야 한다(실제 '" + result.issues.front().context + "')");
        }
    }

    bool RunExperimentShaderMetaCookSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.smcook] 합성 검사\n";

        const std::filesystem::path root = MakeScratchRoot();
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            outLog += "    [실패] 임시 asset root 를 만들 수 없다\n";
            return false;
        }

        // ── 1. 정상 경로 ───────────────────────────────────────────────
        {
            FixtureSpec spec;
            const Fixture fixture = MakeFixture(root, spec);
            const ck::ShaderMetaCookProductResult result =
                ck::BuildShaderMetaCookProduct({ fixture.shaderMeta, root });

            check.Check(result.Succeeded(), "정상 .shadermeta 는 통과해야 한다");
            if (result.Succeeded())
            {
                const ck::ShaderMetaCookProduct& product = *result.product;

                std::string original;
                check.Check(ReadFileBytes(fixture.shaderMeta, original),
                    "원본을 읽을 수 있어야 한다");
                check.Check(Authoring::IsCookedDocument(product.artifactBytes),
                    "artifact 가 CEDO binary document여야 한다");
                std::string sourceError;
                std::string cookedError;
                const Authoring::ParsedDocument sourceDocument =
                    Authoring::ParsedDocument::ParseText(original, sourceError);
                const Authoring::ParsedDocument cookedDocument =
                    Authoring::ParsedDocument::ParseCooked(
                        product.artifactBytes, cookedError);
                check.Check(sourceDocument && cookedDocument
                    && Authoring::NodesEqual(
                        sourceDocument.Root(), cookedDocument.Root()),
                    "fixture authoring/CEDO 구조가 같아야 한다");

                const std::string expectedPath = "Derived/ShaderMeta/"
                    + spec.metaGuid.substr(0u, 2u) + "/" + spec.metaGuid
                    + ".shadermeta";
                check.Check(product.artifactPath == expectedPath,
                    "artifactPath 가 GUID 주소여야 한다");
                check.Check(product.name == "GateProbe", "파싱된 name");
                check.Check(product.propertyCount == 2u, "property 수");
                check.Check(product.keywordAxisCount == 1u, "keyword 축 수");
                check.Check(product.passCount == 1u, "pass 수");

                experiment::AssetId expectedShaderId{};
                check.Check(experiment::TryParseCanonicalAssetId(
                    spec.shaderGuid, expectedShaderId), "fixture 셰이더 GUID 파싱");
                // ★ source 간선은 manifest 에 안 들어가지만, 해소 자체는
                //   증명돼야 한다. 이게 비면 "검증한다"는 주석만 남는다.
                check.Check(product.sourceShaderAssetId == expectedShaderId,
                    "source 셰이더 GUID 가 .hlsl.meta 와 같아야 한다");

                const ck::CookedAssetManifestEntry& entry = product.manifestEntry;
                check.Check(entry.kind == ck::CookedAssetKind::ShaderMeta,
                    "manifest kind");
                // ★ 2026-09-06 교정. 이 줄은 `== ShaderMeta::kSchemaVersion` 이었고
                //   **늘 실패하고 있었다.** 프로듀서는
                //   `(kSchemaVersion << 16) | Authoring::kCookedDocumentVersion` 을
                //   넣는다(`ShaderMetaCookProducer.cpp` — "schema 정본에서 유도한다.
                //   손으로 올리는 숫자가 아니다").
                //
                //   같은 파일의 **실자산 절반이 이미 합성 값을 단정하고 있었다.**
                //   즉 옳은 단정과 낡은 단정이 한 파일에 같이 있었고, 낡은 쪽만
                //   돌고 있었다 — 실자산 절반은 CLI 인자 둘을 요구하는데 그것을
                //   주는 스크립트가 없다. 아무도 안 도는 절반에 정답이 있으면
                //   드리프트는 영원히 산다.
                check.Check(entry.formatVersion
                    == ((ShaderMeta::kSchemaVersion << 16u)
                        | Authoring::kCookedDocumentVersion),
                    "manifest formatVersion 이 schema/CEDO 정본에서 나와야 한다");
                check.Check(entry.byteSize == product.artifactBytes.size(),
                    "manifest byteSize");
                check.Check(entry.artifactPath == product.artifactPath,
                    "manifest artifactPath");
                // HLSL 은 Derived artifact 가 아니다(B2/B3 소유). 여기에
                // 간선을 그리면 해소 불가능한 dependency 가 된다.
                check.Check(entry.dependencies.empty(),
                    "manifest dependency 는 비어야 한다");

                ck::Sha256Digest expected{};
                std::string hashError;
                const bool hashed = ck::ComputeSha256(product.artifactBytes,
                    expected, hashError);
                check.Check(hashed && entry.contentSha256 == expected,
                    "manifest contentSha256 가 내용 해시여야 한다");
            }
        }

        // ── 2. 결정성 ──────────────────────────────────────────────────
        {
            FixtureSpec spec;
            spec.folder = "Det";
            spec.metaGuid = "33333333-3333-4333-8333-333333333333";
            spec.shaderGuid = "44444444-4444-4444-8444-444444444444";
            const Fixture fixture = MakeFixture(root, spec);
            const ck::ShaderMetaCookProductResult first =
                ck::BuildShaderMetaCookProduct({ fixture.shaderMeta, root });
            const ck::ShaderMetaCookProductResult second =
                ck::BuildShaderMetaCookProduct({ fixture.shaderMeta, root });
            check.Check(first.Succeeded() && second.Succeeded(),
                "결정성 — 두 번 다 통과해야 한다");
            if (first.Succeeded() && second.Succeeded())
            {
                check.Check(first.product->artifactBytes
                    == second.product->artifactBytes, "결정성 — 같은 바이트");
                check.Check(first.product->manifestEntry.contentSha256
                    == second.product->manifestEntry.contentSha256,
                    "결정성 — 같은 해시");
            }
        }

        // ── 3. fail-closed ─────────────────────────────────────────────
        {
            FixtureSpec spec;
            spec.folder = "BadExt";
            spec.extension = ".yaml";
            spec.metaGuid = "55555555-5555-4555-8555-555555555555";
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.extension", "확장자가 .shadermeta 가 아님");
        }
        {
            FixtureSpec spec;
            spec.folder = "NoSidecar";
            spec.writeSidecar = false;
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.meta", ".meta 누락");
        }
        {
            FixtureSpec spec;
            spec.folder = "Brace";
            spec.metaGuid = "{66666666-6666-4666-8666-666666666666}";
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.meta", "brace 표기 GUID");
        }
        {
            FixtureSpec spec;
            spec.folder = "Nil";
            spec.metaGuid = "00000000-0000-0000-0000-000000000000";
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.meta", "nil GUID");
        }
        {
            FixtureSpec spec;
            spec.folder = "Empty";
            spec.metaGuid = "77777777-7777-4777-8777-777777777777";
            spec.documentOverride = " ";  // 공백 한 칸 — 파서가 걸러야 한다
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.schema", "내용 없는 문서");
        }
        {
            // ★ 정본 파서가 실제로 도는지. schema 를 어긴 문서다.
            FixtureSpec spec;
            spec.folder = "BadSchema";
            spec.metaGuid = "88888888-8888-4888-8888-888888888888";
            spec.documentOverride =
                "schema: 1\nname: Broken\nsource: probe.hlsl\n"
                "passes: not-a-sequence\n";
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.schema", "schema 위반");
        }
        {
            FixtureSpec spec;
            spec.folder = "NoShader";
            spec.metaGuid = "99999999-9999-4999-8999-999999999999";
            spec.writeShaderSource = false;
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.schema", "source 셰이더 누락(정본 파서가 거부)");
        }
        {
            FixtureSpec spec;
            spec.folder = "NoShaderMeta";
            spec.metaGuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
            spec.writeShaderSidecar = false;
            ExpectRejected(check, MakeFixture(root, spec),
                "shadermeta.source.meta", "source 셰이더 .meta 누락");
        }
        {
            // asset root 를 하위 폴더로 잡아 source 가 밖에 놓이게 한다.
            FixtureSpec spec;
            spec.folder = "Escape";
            spec.metaGuid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
            spec.shaderGuid = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
            spec.sourceRelative = "../escaped.hlsl";
            Fixture fixture = MakeFixture(root, spec);
            fixture.assetRoot = root / "Escape";
            ExpectRejected(check, fixture, "shadermeta.schema",
                "source 상위이동(정본 파서가 거부)");
        }
        {
            FixtureSpec spec;
            spec.folder = "BadRoot";
            spec.metaGuid = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
            Fixture fixture = MakeFixture(root, spec);
            fixture.assetRoot = root / "no-such-directory";
            ExpectRejected(check, fixture, "request.assetRoot",
                "디렉터리가 아닌 asset root");
        }
        {
            FixtureSpec spec;
            spec.folder = "Missing";
            spec.metaGuid = "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee";
            Fixture fixture = MakeFixture(root, spec);
            fixture.shaderMeta = root / "Missing" / "absent.shadermeta";
            ExpectRejected(check, fixture, "request.sourcePath",
                "존재하지 않는 source");
        }

        // ── 4. 경로 헬퍼 ───────────────────────────────────────────────
        {
            experiment::AssetId id{};
            const bool parsed = experiment::TryParseCanonicalAssetId(
                "12345678-1234-4234-8234-123456789abc", id);
            check.Check(parsed, "경로 헬퍼 fixture GUID 파싱");
            check.Check(ck::MakeDerivedShaderMetaArtifactPath(id)
                == "Derived/ShaderMeta/12/"
                   "12345678-1234-4234-8234-123456789abc.shadermeta",
                "경로 헬퍼 정상 표기");
            check.Check(ck::MakeDerivedShaderMetaArtifactPath(
                experiment::AssetId{}).empty(), "경로 헬퍼 — nil GUID 거부");
        }

        std::filesystem::remove_all(root, error);

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentShaderMetaCookReal(const std::string& assetRootPath,
        const std::string& shaderMetaPath, std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.smcook] 실자산: " + shaderMetaPath + "\n";

        const std::filesystem::path source(shaderMetaPath);
        const ck::ShaderMetaCookProductResult result =
            ck::BuildShaderMetaCookProduct(
                { source, std::filesystem::path(assetRootPath) });

        check.Check(result.Succeeded(), "실자산 cook 이 통과해야 한다");
        if (!result.Succeeded())
        {
            for (const ck::ShaderMetaCookProductIssue& issue : result.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            outLog += "  실자산 단정 실패\n";
            return false;
        }

        const ck::ShaderMetaCookProduct& product = *result.product;
        std::string original;
        check.Check(ReadFileBytes(source, original), "원본을 읽을 수 있어야 한다");
        check.Check(Authoring::IsCookedDocument(product.artifactBytes),
            "artifact 가 CEDO binary document여야 한다");
        std::string sourceError;
        std::string cookedError;
        const Authoring::ParsedDocument sourceDocument =
            Authoring::ParsedDocument::ParseText(original, sourceError);
        const Authoring::ParsedDocument cookedDocument =
            Authoring::ParsedDocument::ParseCooked(
                product.artifactBytes, cookedError);
        check.Check(sourceDocument && cookedDocument
            && Authoring::NodesEqual(
                sourceDocument.Root(), cookedDocument.Root()),
            "실자산 authoring/CEDO 구조가 같아야 한다");
        check.Check(product.manifestEntry.formatVersion
            == ((ShaderMeta::kSchemaVersion << 16u)
                | Authoring::kCookedDocumentVersion),
            "formatVersion 이 schema/CEDO 정본과 같아야 한다");
        check.Check(experiment::IsAssetIdV4(product.sourceShaderAssetId),
            "source 셰이더 GUID 가 canonical UUIDv4 여야 한다");
        check.Check(product.passCount > 0u, "pass 가 하나 이상이어야 한다");

        ck::Sha256Digest expected{};
        std::string hashError;
        const bool hashed = ck::ComputeSha256(product.artifactBytes,
            expected, hashError);
        check.Check(hashed && product.manifestEntry.contentSha256 == expected,
            "manifest 해시가 내용 해시여야 한다");

        char summary[320]{};
        std::snprintf(summary, sizeof(summary),
            "  실자산 단정 %zu/%zu · %s · property %zu · pass %zu · %s\n",
            check.passed, check.passed + check.failed,
            product.name.c_str(), product.propertyCount, product.passCount,
            product.artifactPath.c_str());
        outLog += summary;
        return check.failed == 0u;
    }
}
