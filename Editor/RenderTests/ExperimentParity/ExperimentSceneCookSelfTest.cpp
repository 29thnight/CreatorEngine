#include "ExperimentParity/ExperimentSceneCookSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/SceneCookProducer.h"

#include <algorithm>
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
            name << "cemc-scenecook-" << std::hex
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

        [[nodiscard]] std::string BytesToString(
            const std::vector<std::byte>& bytes)
        {
            std::string text;
            text.resize(bytes.size());
            for (std::size_t index = 0u; index < bytes.size(); ++index)
                text[index] = static_cast<char>(bytes[index]);
            return text;
        }

        inline constexpr const char* kNil =
            "00000000-0000-0000-0000-000000000000";
        inline constexpr const char* kSelfGuid =
            "11111111-1111-4111-8111-111111111111";
        inline constexpr const char* kModelGuid =
            "22222222-2222-4222-8222-222222222222";
        inline constexpr const char* kPrefabGuid =
            "33333333-3333-4333-8333-333333333333";
        inline constexpr const char* kTextureGuid =
            "44444444-4444-4444-8444-444444444444";
        inline constexpr const char* kBtGuid =
            "55555555-5555-4555-8555-555555555555";

        struct SceneSpec final
        {
            std::string folder{ "Scene" };
            std::string stem{ "probe" };
            std::string extension{ ".creator" };
            std::string metaGuid{ kSelfGuid };
            std::string documentOverride{};
            bool writeSidecar{ true };
        };

        // 실자산과 같은 중첩 모양. 자기 참조·nil·중복 간선·legacy 이름 참조·
        // producer 없는 GUID 를 한 문서에 모은다.
        [[nodiscard]] std::string SceneYaml()
        {
            return
                std::string("m_Entities:\n")
                + "  - Entity: 1\n"
                  "    m_prefabFileGuid: " + kSelfGuid + "\n"      // 자기 참조
                  "    m_components:\n"
                  "      - MeshRenderer: 2\n"
                  "        m_Material:\n"
                  "          m_baseColorTexName: Probe_BaseColor.png\n"
                  "          m_normalTexName: \"\"\n"
                  "          m_fileGuid: " + kModelGuid + "\n"     // 모델 간선
                  "          m_propertyValues:\n"
                  "            - m_name: baseColorMap\n"
                  "              m_textureGuid: " + kTextureGuid + "\n"
                  "            - m_name: normalMap\n"
                  "              m_textureGuid: " + kTextureGuid + "\n"  // 중복
                  "            - m_name: ormMap\n"
                  "              m_textureGuid: " + kNil + "\n"     // nil
                  "        m_Mesh:\n"
                  "          m_name: Probe_Mesh\n"
                  "      - BehaviorTreeComponent: 3\n"
                  "        m_BehaviorTreeGuid: " + kBtGuid + "\n"   // producer 없음
                  "  - Entity: 4\n"
                  "    m_prefabFileGuid: " + kPrefabGuid + "\n"     // 프리팹 간선
                  "    m_components: []\n";
        }

        struct Fixture final
        {
            std::filesystem::path assetRoot{};
            std::filesystem::path source{};
        };

        [[nodiscard]] Fixture MakeFixture(const std::filesystem::path& root,
            const SceneSpec& spec)
        {
            Fixture fixture;
            fixture.assetRoot = root;
            fixture.source = root / spec.folder / (spec.stem + spec.extension);
            (void)WriteTextFile(fixture.source, spec.documentOverride.empty()
                ? SceneYaml() : spec.documentOverride);
            if (spec.writeSidecar)
            {
                std::filesystem::path sidecar = fixture.source;
                sidecar += ".meta";
                (void)WriteTextFile(sidecar, "guid: " + spec.metaGuid + "\n");
            }
            return fixture;
        }

        void ExpectRejected(Checker& check, const Fixture& fixture,
            const std::string& expectedContext, const std::string& what)
        {
            const ck::SceneCookProductResult result =
                ck::BuildSceneCookProduct({ fixture.source, fixture.assetRoot });
            check.Check(!result.Succeeded(), what + " — 거부해야 한다");
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

    bool RunExperimentSceneCookSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.scenecook] 합성 검사\n";

        const std::filesystem::path root = MakeScratchRoot();
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (error)
        {
            outLog += "    [실패] 임시 asset root 를 만들 수 없다\n";
            return false;
        }

        experiment::AssetId self{}, model{}, prefab{}, texture{};
        check.Check(experiment::TryParseCanonicalAssetId(kSelfGuid, self)
            && experiment::TryParseCanonicalAssetId(kModelGuid, model)
            && experiment::TryParseCanonicalAssetId(kPrefabGuid, prefab)
            && experiment::TryParseCanonicalAssetId(kTextureGuid, texture),
            "fixture GUID 파싱");

        // ── 1. scene 정상 경로 ─────────────────────────────────────────
        {
            SceneSpec spec;
            const Fixture fixture = MakeFixture(root, spec);
            const ck::SceneCookProductResult result =
                ck::BuildSceneCookProduct({ fixture.source, root });
            check.Check(result.Succeeded(), "정상 .creator 는 통과해야 한다");
            if (result.Succeeded())
            {
                const ck::SceneCookProduct& product = *result.product;

                std::string original;
                check.Check(ReadFileBytes(fixture.source, original),
                    "원본을 읽을 수 있어야 한다");
                check.Check(BytesToString(product.artifactBytes) == original,
                    "artifact 가 원본과 비트 단위로 같아야 한다");
                check.Check(product.kind == ck::CookedAssetKind::Scene,
                    ".creator 는 Scene kind");
                check.Check(product.artifactPath == std::string("Derived/Scenes/")
                    + std::string(kSelfGuid).substr(0u, 2u) + "/" + kSelfGuid
                    + ".creator", "scene artifactPath");

                const auto& dependencies = product.manifestEntry.dependencies;
                // 모델 1 + 프리팹 1 + 텍스처 1. 자기 참조·nil·중복은 빠진다.
                check.Check(dependencies.size() == 3u,
                    "간선은 모델 1 + 프리팹 1 + 텍스처 1 이어야 한다");
                check.Check(std::ranges::find(dependencies, model)
                    != dependencies.end(), "모델 간선");
                check.Check(std::ranges::find(dependencies, prefab)
                    != dependencies.end(), "프리팹 간선");
                check.Check(std::ranges::find(dependencies, texture)
                    != dependencies.end(), "텍스처 간선");
                // ★ 자기 자신은 정체성이지 의존이 아니다. 그리면 manifest 가
                //   self-dependency 로 거부한다(실자산 프리팹이 그 모양이다).
                check.Check(std::ranges::find(dependencies, self)
                    == dependencies.end(), "자기 참조는 간선이 아니다");
                check.Check(std::ranges::find(dependencies,
                    experiment::AssetId{}) == dependencies.end(),
                    "nil 은 간선이 아니다");

                check.Check(product.modelEdges == 1u, "모델 간선 수");
                check.Check(product.prefabEdges == 1u, "프리팹 간선 수");
                check.Check(product.textureEdges == 2u,
                    "텍스처 참조 2건(중복 포함)을 센다");
                // ★ 그리지 못한 것들을 실제로 세는가.
                check.Check(product.legacyTextureNameReferences == 1u,
                    "빈 문자열을 뺀 legacy 이름 참조 1건");
                check.Check(product.unproducedGuidReferences == 1u,
                    "producer 없는 GUID 참조 1건(BT)");

                check.Check(product.manifestEntry.formatVersion
                    == ck::kSceneArtifactVersion, "manifest formatVersion");
                ck::Sha256Digest expected{};
                std::string hashError;
                const bool hashed = ck::ComputeSha256(product.artifactBytes,
                    expected, hashError);
                check.Check(hashed
                    && product.manifestEntry.contentSha256 == expected,
                    "manifest contentSha256 가 내용 해시여야 한다");
            }
        }

        // ── 2. prefab kind ─────────────────────────────────────────────
        {
            SceneSpec spec;
            spec.folder = "Pre";
            spec.extension = ".prefab";
            spec.metaGuid = "66666666-6666-4666-8666-666666666666";
            const Fixture fixture = MakeFixture(root, spec);
            const ck::SceneCookProductResult result =
                ck::BuildSceneCookProduct({ fixture.source, root });
            check.Check(result.Succeeded(), "정상 .prefab 는 통과해야 한다");
            if (result.Succeeded())
            {
                check.Check(result.product->kind == ck::CookedAssetKind::Prefab,
                    ".prefab 는 Prefab kind");
                check.Check(result.product->artifactPath.starts_with(
                    "Derived/Prefabs/"), "prefab artifactPath");
                // 이 문서의 자기 GUID 는 kSelfGuid 라 여기서는 자기 참조가
                // 아니다 — 그러므로 프리팹 간선이 하나 더 잡혀야 한다.
                check.Check(result.product->prefabEdges == 2u,
                    "자기 GUID 가 다르면 prefab 간선이 둘이다");
            }
        }

        // ── 3. 결정성 ──────────────────────────────────────────────────
        {
            SceneSpec spec;
            spec.folder = "Det";
            spec.metaGuid = "77777777-7777-4777-8777-777777777777";
            const Fixture fixture = MakeFixture(root, spec);
            const ck::SceneCookProductResult first =
                ck::BuildSceneCookProduct({ fixture.source, root });
            const ck::SceneCookProductResult second =
                ck::BuildSceneCookProduct({ fixture.source, root });
            check.Check(first.Succeeded() && second.Succeeded(),
                "결정성 — 두 번 다 통과");
            if (first.Succeeded() && second.Succeeded())
            {
                check.Check(first.product->manifestEntry.contentSha256
                    == second.product->manifestEntry.contentSha256,
                    "결정성 — 같은 해시");
                check.Check(first.product->manifestEntry.dependencies
                    == second.product->manifestEntry.dependencies,
                    "결정성 — 같은 간선 순서");
            }
        }

        // ── 4. fail-closed ─────────────────────────────────────────────
        {
            SceneSpec spec;
            spec.folder = "BadExt";
            spec.extension = ".yaml";
            spec.metaGuid = "88888888-8888-4888-8888-888888888888";
            ExpectRejected(check, MakeFixture(root, spec),
                "scene.extension", "확장자가 .creator/.prefab 이 아님");
        }
        {
            SceneSpec spec;
            spec.folder = "NoSidecar";
            spec.writeSidecar = false;
            ExpectRejected(check, MakeFixture(root, spec),
                "scene.meta", ".meta 누락");
        }
        {
            SceneSpec spec;
            spec.folder = "Brace";
            spec.metaGuid = "{99999999-9999-4999-8999-999999999999}";
            ExpectRejected(check, MakeFixture(root, spec),
                "scene.meta", "brace 표기 GUID");
        }
        {
            SceneSpec spec;
            spec.folder = "BadRef";
            spec.metaGuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
            spec.documentOverride =
                "m_Entities:\n  - Entity: 1\n"
                "    m_prefabFileGuid: not-a-guid\n";
            ExpectRejected(check, MakeFixture(root, spec),
                "scene.reference", "비정규 참조 GUID");
        }
        {
            // ★ brace 표기는 YAML flow mapping 이라 스칼라가 아니다.
            //   b2c-3 에서 같은 형태가 간선을 조용히 없앤 전례가 있다.
            SceneSpec spec;
            spec.folder = "NonScalarRef";
            spec.metaGuid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
            spec.documentOverride =
                "m_Entities:\n  - Entity: 1\n"
                "    m_fileGuid: {22222222-2222-4222-8222-222222222222}\n";
            ExpectRejected(check, MakeFixture(root, spec),
                "scene.reference.kind", "비스칼라 참조");
        }
        {
            SceneSpec spec;
            spec.folder = "BadYaml";
            spec.metaGuid = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
            spec.documentOverride = "m_Entities: [unclosed\n";
            ExpectRejected(check, MakeFixture(root, spec),
                "scene.yaml", "깨진 YAML");
        }
        {
            SceneSpec spec;
            spec.folder = "BadRoot";
            spec.metaGuid = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
            Fixture fixture = MakeFixture(root, spec);
            fixture.assetRoot = root / "no-such-directory";
            ExpectRejected(check, fixture,
                "request.assetRoot", "디렉터리가 아닌 asset root");
        }

        // ── 5. 경로 헬퍼 ───────────────────────────────────────────────
        {
            experiment::AssetId id{};
            check.Check(experiment::TryParseCanonicalAssetId(
                "12345678-1234-4234-8234-123456789abc", id), "헬퍼 GUID 파싱");
            check.Check(ck::MakeDerivedSceneArtifactPath(id)
                == "Derived/Scenes/12/"
                   "12345678-1234-4234-8234-123456789abc.creator", "scene 경로");
            check.Check(ck::MakeDerivedPrefabArtifactPath(id)
                == "Derived/Prefabs/12/"
                   "12345678-1234-4234-8234-123456789abc.prefab", "prefab 경로");
            check.Check(ck::MakeDerivedSceneArtifactPath(
                experiment::AssetId{}).empty(), "scene 경로 — nil 거부");
            check.Check(ck::MakeDerivedPrefabArtifactPath(
                experiment::AssetId{}).empty(), "prefab 경로 — nil 거부");
        }

        std::filesystem::remove_all(root, error);

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentSceneCookReal(const std::string& assetRootPath,
        const std::string& scenePath, std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.scenecook] 실자산: " + scenePath + "\n";

        const std::filesystem::path source(scenePath);
        const ck::SceneCookProductResult result = ck::BuildSceneCookProduct(
            { source, std::filesystem::path(assetRootPath) });
        check.Check(result.Succeeded(), "실자산 cook 이 통과해야 한다");
        if (!result.Succeeded())
        {
            for (const ck::SceneCookProductIssue& issue : result.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }

        const ck::SceneCookProduct& product = *result.product;
        std::string original;
        check.Check(ReadFileBytes(source, original), "원본을 읽을 수 있어야 한다");
        check.Check(BytesToString(product.artifactBytes) == original,
            "artifact 가 원본과 비트 단위로 같아야 한다");
        // 실자산 프리팹은 자기 GUID 를 자기 안에 적어 둔다. 그것이 간선으로
        // 새어 나가면 manifest 가 self-dependency 로 거부한다.
        check.Check(std::ranges::find(product.manifestEntry.dependencies,
            product.sceneAssetId) == product.manifestEntry.dependencies.end(),
            "자기 참조가 간선으로 새면 안 된다");

        char summary[380]{};
        std::snprintf(summary, sizeof(summary),
            "  실자산 단정 %zu/%zu · 간선 %zu(model %zu·prefab %zu·texture %zu)"
            " · legacy이름 %zu · producer없음 %zu\n",
            check.passed, check.passed + check.failed,
            product.manifestEntry.dependencies.size(), product.modelEdges,
            product.prefabEdges, product.textureEdges,
            product.legacyTextureNameReferences,
            product.unproducedGuidReferences);
        outLog += summary;
        return check.failed == 0u;
    }
}
