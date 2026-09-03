#include "ExperimentParity/ExperimentMaterialCookSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/MaterialCookProducer.h"
#include "AuthoringCookedDocument.h"
#include "AuthoringNodeEquality.h"
#include "AuthoringParsedDocument.h"
#include "Experiment/Cooked/ModelGenerationExportProducer.h" // MBC11
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelSidecarV2.h"
#include <set>
#include <variant>
#include "Experiment/Cooked/TextureCookProducer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <process.h>
#include <set>
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
            name << "cemc-matcook-" << std::hex
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

        inline constexpr const char* kNilGuid =
            "00000000-0000-0000-0000-000000000000";

        struct MaterialSpec final
        {
            std::string folder{ "Mat" };
            std::string stem{ "probe" };
            std::string extension{ ".asset" };
            std::string metaGuid{ "11111111-1111-4111-8111-111111111111" };
            std::string shaderGuid{ "22222222-2222-4222-8222-222222222222" };
            std::string textureGuidA{ "33333333-3333-4333-8333-333333333333" };
            std::string textureGuidB{ "44444444-4444-4444-8444-444444444444" };
            std::string documentOverride{};
            bool writeSidecar{ true };
        };

        [[nodiscard]] std::string MaterialYaml(const MaterialSpec& spec)
        {
            // 같은 texture 를 두 슬롯이 가리키게 해서(dup) dependency 접기까지
            // 함께 본다. nil 은 "텍스처 없음"이라 간선이 되면 안 된다.
            return
                "m_name: ProbeMaterial\n"
                "m_shaderMetaGuid: " + spec.shaderGuid + "\n"
                "m_propertyValues:\n"
                "  - m_name: baseColorMap\n"
                "    m_textureGuid: " + spec.textureGuidA + "\n"
                "  - m_name: normalMap\n"
                "    m_textureGuid: " + spec.textureGuidB + "\n"
                "  - m_name: ormMap\n"
                "    m_textureGuid: " + spec.textureGuidA + "\n"
                "  - m_name: emissiveMap\n"
                "    m_textureGuid: " + std::string(kNilGuid) + "\n";
        }

        struct Fixture final
        {
            std::filesystem::path assetRoot{};
            std::filesystem::path source{};
        };

        [[nodiscard]] Fixture MakeFixture(const std::filesystem::path& root,
            const MaterialSpec& spec)
        {
            Fixture fixture;
            fixture.assetRoot = root;
            fixture.source = root / spec.folder / (spec.stem + spec.extension);
            (void)WriteTextFile(fixture.source, spec.documentOverride.empty()
                ? MaterialYaml(spec) : spec.documentOverride);
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
            const ck::MaterialCookProductResult result =
                ck::BuildMaterialCookProduct(
                    { fixture.source, fixture.assetRoot });
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

    bool RunExperimentMaterialCookSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matcook] 합성 검사\n";

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
            MaterialSpec spec;
            const Fixture fixture = MakeFixture(root, spec);
            const ck::MaterialCookProductResult result =
                ck::BuildMaterialCookProduct({ fixture.source, root });
            check.Check(result.Succeeded(), "정상 material 은 통과해야 한다");
            if (result.Succeeded())
            {
                const ck::MaterialCookProduct& product = *result.product;

                std::string original;
                check.Check(ReadFileBytes(fixture.source, original),
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

                const std::string expectedPath = "Derived/Materials/"
                    + spec.metaGuid.substr(0u, 2u) + "/" + spec.metaGuid
                    + ".asset";
                check.Check(product.artifactPath == expectedPath,
                    "artifactPath 가 GUID 주소여야 한다");
                check.Check(product.name == "ProbeMaterial", "파싱된 name");

                experiment::AssetId shaderId{}, textureA{}, textureB{};
                check.Check(experiment::TryParseCanonicalAssetId(
                    spec.shaderGuid, shaderId), "fixture shader GUID 파싱");
                check.Check(experiment::TryParseCanonicalAssetId(
                    spec.textureGuidA, textureA), "fixture textureA 파싱");
                check.Check(experiment::TryParseCanonicalAssetId(
                    spec.textureGuidB, textureB), "fixture textureB 파싱");
                check.Check(product.shaderMetaAssetId == shaderId,
                    "shaderMetaAssetId");

                const auto& dependencies = product.manifestEntry.dependencies;
                // shader 1 + 서로 다른 texture 2. 같은 texture 를 가리키는 두
                // 슬롯은 접혀야 하고 nil 은 간선이 되면 안 된다.
                check.Check(dependencies.size() == 3u,
                    "dependency 는 shader 1 + 고유 texture 2 여야 한다");
                check.Check(!dependencies.empty()
                    && dependencies.front() == shaderId,
                    "첫 dependency 는 shader 여야 한다");
                check.Check(std::ranges::find(dependencies, textureA)
                    != dependencies.end(), "textureA 간선");
                check.Check(std::ranges::find(dependencies, textureB)
                    != dependencies.end(), "textureB 간선");
                experiment::AssetId nil{};
                check.Check(std::ranges::find(dependencies, nil)
                    == dependencies.end(), "nil GUID 는 간선이 되면 안 된다");
                check.Check(product.texturePropertyCount == 3u,
                    "nil 을 뺀 texture property 수");
                check.Check(product.distinctTextureCount == 2u,
                    "서로 다른 texture 수");

                check.Check(product.manifestEntry.kind
                    == ck::CookedAssetKind::Material, "manifest kind");
                check.Check(product.manifestEntry.formatVersion
                    == ck::kMaterialArtifactVersion, "manifest formatVersion");

                ck::Sha256Digest expected{};
                std::string hashError;
                const bool hashed = ck::ComputeSha256(product.artifactBytes,
                    expected, hashError);
                check.Check(hashed
                    && product.manifestEntry.contentSha256 == expected,
                    "manifest contentSha256 가 내용 해시여야 한다");
            }
        }

        // ── 2. fail-closed ─────────────────────────────────────────────
        {
            MaterialSpec spec;
            spec.folder = "BadExt";
            spec.extension = ".yaml";
            spec.metaGuid = "55555555-5555-4555-8555-555555555555";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.extension", "확장자가 .asset 이 아님");
        }
        {
            MaterialSpec spec;
            spec.folder = "NoSidecar";
            spec.writeSidecar = false;
            ExpectRejected(check, MakeFixture(root, spec),
                "material.meta", ".meta 누락");
        }
        {
            MaterialSpec spec;
            spec.folder = "Brace";
            spec.metaGuid = "{66666666-6666-4666-8666-666666666666}";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.meta", "brace 표기 GUID");
        }
        {
            // ★ 스키마가 바뀌면 여기서 시끄럽게 깨져야 한다.
            MaterialSpec spec;
            spec.folder = "NoShader";
            spec.metaGuid = "77777777-7777-4777-8777-777777777777";
            spec.documentOverride = "m_name: NoShader\nm_propertyValues: []\n";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.shaderMetaGuid", "m_shaderMetaGuid 누락");
        }
        {
            MaterialSpec spec;
            spec.folder = "BadShader";
            spec.metaGuid = "88888888-8888-4888-8888-888888888888";
            spec.shaderGuid = "not-a-guid";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.shaderMetaGuid", "비정규 m_shaderMetaGuid");
        }
        {
            MaterialSpec spec;
            spec.folder = "BadTexture";
            spec.metaGuid = "99999999-9999-4999-8999-999999999999";
            spec.textureGuidB = "not-a-guid";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.textureGuid", "비정규 m_textureGuid");
        }
        {
            // ★ brace 표기는 YAML 이 flow mapping 으로 읽는다. 이걸 “스칼라가
            //   아니니 건너뛰자”로 두면 잘못 적힌 GUID 가 “텍스처 없음”과
            //   같은 모습이 된다 — 폐포가 원리적으로 못 잡는 형태다.
            MaterialSpec spec;
            spec.folder = "BraceTexture";
            spec.metaGuid = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
            spec.textureGuidB = "{44444444-4444-4444-8444-444444444444}";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.textureGuid", "brace 표기 m_textureGuid(비스칼라)");
        }
        {
            MaterialSpec spec;
            spec.folder = "BadYaml";
            spec.metaGuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
            spec.documentOverride = "m_name: [unclosed\n";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.yaml", "깨진 YAML");
        }
        {
            MaterialSpec spec;
            spec.folder = "Empty";
            spec.metaGuid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
            spec.documentOverride = " ";
            ExpectRejected(check, MakeFixture(root, spec),
                "material.yaml", "매핑이 아닌 문서");
        }
        {
            MaterialSpec spec;
            spec.folder = "BadRoot";
            spec.metaGuid = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
            Fixture fixture = MakeFixture(root, spec);
            fixture.assetRoot = root / "no-such-directory";
            ExpectRejected(check, fixture,
                "request.assetRoot", "디렉터리가 아닌 asset root");
        }

        // ── 3. 경로 헬퍼 ───────────────────────────────────────────────
        {
            experiment::AssetId id{};
            check.Check(experiment::TryParseCanonicalAssetId(
                "12345678-1234-4234-8234-123456789abc", id),
                "경로 헬퍼 fixture GUID 파싱");
            check.Check(ck::MakeDerivedMaterialArtifactPath(id)
                == "Derived/Materials/12/"
                   "12345678-1234-4234-8234-123456789abc.asset",
                "경로 헬퍼 정상 표기");
            check.Check(ck::MakeDerivedMaterialArtifactPath(
                experiment::AssetId{}).empty(), "경로 헬퍼 — nil GUID 거부");
        }

        // ── 4. 매직 바이트 판별기 ──────────────────────────────────────
        // 임베디드 texture 는 `mimeType` 이 비어 있어 이것만이 근거다.
        {
            const auto sniff = [](std::initializer_list<int> values)
            {
                std::vector<std::byte> bytes;
                for (const int value : values)
                    bytes.push_back(static_cast<std::byte>(value));
                return std::string(ck::SniffTextureExtension(bytes));
            };
            check.Check(sniff({ 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A })
                == ".png", "sniff PNG");
            check.Check(sniff({ 0xFF, 0xD8, 0xFF, 0xE0 }) == ".jpg", "sniff JPEG");
            check.Check(sniff({ 0x44, 0x44, 0x53, 0x20 }) == ".dds", "sniff DDS");
            check.Check(sniff({ 0x23, 0x3F, 0x52, 0x41 }) == ".hdr", "sniff Radiance");
            check.Check(sniff({ 0x00, 0x01, 0x02, 0x03 }).empty(),
                "sniff — 모르는 컨테이너는 빈 문자열");
            check.Check(sniff({ 0x89, 0x50 }).empty(),
                "sniff — 매직보다 짧으면 판별하지 않는다");
            // PNG 서명 8바이트 중 뒤가 다르면 PNG 가 아니다. 앞 네 바이트만
            // 보면 여기서 잘못 통과한다.
            check.Check(sniff({ 0x89, 0x50, 0x4E, 0x47, 0x00, 0x00, 0x00, 0x00 })
                .empty(), "sniff — PNG 서명 뒷부분까지 본다");
            check.Check(sniff({ 0xFF, 0xD8, 0x00 }).empty(),
                "sniff — JPEG 은 세 번째 바이트까지 본다");
        }

        std::filesystem::remove_all(root, error);

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed,
            check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentMaterialCookReal(const std::string& assetRootPath,
        const std::string& materialPath, std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matcook] 실자산 material: " + materialPath + "\n";

        const std::filesystem::path source(materialPath);
        const ck::MaterialCookProductResult result =
            ck::BuildMaterialCookProduct(
                { source, std::filesystem::path(assetRootPath) });
        check.Check(result.Succeeded(), "실자산 cook 이 통과해야 한다");
        if (!result.Succeeded())
        {
            for (const ck::MaterialCookProductIssue& issue : result.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }

        const ck::MaterialCookProduct& product = *result.product;
		check.Check(Authoring::IsCookedDocument(product.artifactBytes),
			"artifact 가 CEDO binary document여야 한다");
		std::string sourceParseError;
		std::string cookedParseError;
		const Authoring::ParsedDocument sourceDocument =
			Authoring::ParsedDocument::ParseFile(source.string(), sourceParseError);
		const Authoring::ParsedDocument cookedDocument =
			Authoring::ParsedDocument::ParseCooked(
				product.artifactBytes, cookedParseError);
		check.Check(static_cast<bool>(sourceDocument),
			"authoring material 문서를 다시 로드할 수 있어야 한다");
		check.Check(static_cast<bool>(cookedDocument),
			"cooked material payload를 로드할 수 있어야 한다");
		if (sourceDocument && cookedDocument)
		{
			check.Check(Authoring::NodesEqual(
				sourceDocument.Root(), cookedDocument.Root()),
				"authoring/cooked material-payload 구조가 같아야 한다");
		}
        check.Check(experiment::IsAssetIdV4(product.shaderMetaAssetId),
            "shaderMetaAssetId 가 canonical UUIDv4 여야 한다");
        check.Check(!product.manifestEntry.dependencies.empty(),
            "dependency 가 최소 하나(shader)는 있어야 한다");

        char summary[320]{};
        std::snprintf(summary, sizeof(summary),
			"  material parity 단정 %zu/%zu · %s · 의존 %zu · %s\n",
            check.passed, check.passed + check.failed, product.name.c_str(),
            product.manifestEntry.dependencies.size(),
            product.artifactPath.c_str());
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentModelDependencyReal(const std::string& assetRootPath,
        const std::string& modelPath, std::string& outLog)
    {
        // MBC11 — 모델 재질의 의존은 더 이상 cook이 그리는 manifest 간선이 아니라
        // 게시된 generation의 closure다. 실자산 모델의 generation을 내보내 재질마다
        // shader 신원이 있고, 재질의 texture property가 전부 같은 generation의
        // embedded texture로 해소되는지를 잰다.
        Checker check{ outLog };
        outLog += "[experiment.matcook] 실자산 model: " + modelPath + "\n";

        const std::filesystem::path source(modelPath);
        const std::filesystem::path assetRoot(assetRootPath);
        const std::filesystem::path projectRoot = assetRoot.parent_path();
        ck::ModelGenerationExportRequest request;
        request.sourcePath = source;
        request.assetRoot = assetRoot;
        request.generationRoot = projectRoot / "Library" / "ModelAssetGenerations";
        request.identityHeaderPath = projectRoot / "ProjectSetting" / "AssetIdentity.asset";
        const ck::ModelGenerationExportResult exported =
            ck::BuildModelGenerationExportProduct(request);
        check.Check(exported.Succeeded(), "실자산 model generation 내보내기가 통과해야 한다");
        if (!exported.Succeeded())
        {
            for (const ck::ModelGenerationExportIssue& issue : exported.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }
        const ck::ModelGenerationExportProduct& product = *exported.product;

        std::filesystem::path sidecarPath = source;
        sidecarPath += ".meta";
        assets::ModelAssetGenerationLoadRequest load;
        load.identityHeaderPath = request.identityHeaderPath;
        load.generationRoot = request.generationRoot;
        load.canonicalSidecarPath = sidecarPath;
        const assets::ModelAssetGenerationLoadResult loaded =
            assets::LoadModelAssetGeneration(load);
        check.Check(loaded.Succeeded(), "generation을 런타임 리더로 읽는다");
        if (!loaded.Succeeded()) return false;
        const assets::ModelAssetGeneration& generation = *loaded.generation;

        std::set<Uuid::Uuid16> embeddedIds;
        for (const assets::ModelTextureAsset& texture : generation.Textures())
            embeddedIds.insert(texture.textureId);
        std::set<Uuid::Uuid16> referencedTextures;
        std::size_t materials = 0, shaderEdges = 0, textureEdges = 0;
        bool unresolvedTexture = false;
        for (const assets::ModelMaterialAsset& material : generation.Materials())
        {
            ++materials;
            if (!material.shaderAssetId.IsNil()) ++shaderEdges;
            for (const assets::ModelMaterialProperty& property : material.properties)
            {
                const auto* handle = std::get_if<assets::ModelTextureHandle>(&property.value);
                if (nullptr == handle || handle->textureId.IsNil()) continue;
                ++textureEdges;
                referencedTextures.insert(handle->textureId);
                if (!embeddedIds.contains(handle->textureId)) unresolvedTexture = true;
            }
        }
        check.Check(materials == product.materialCount,
            "내보낸 재질 수가 generation과 같아야 한다");
        check.Check(embeddedIds.size() == product.embeddedTextureCount,
            "내보낸 embedded texture 수가 generation과 같아야 한다");
        check.Check(shaderEdges == materials, "재질마다 shader 신원이 정확히 하나여야 한다");
        check.Check(!unresolvedTexture,
            "재질의 texture 의존이 generation closure 안에서 해소돼야 한다");
        // 반대 방향 — 뽑아 둔 embedded texture가 어떤 재질에서든 참조되는가.
        bool everyEmbeddedReferenced = true;
        for (const Uuid::Uuid16& embedded : embeddedIds)
            if (!referencedTextures.contains(embedded)) everyEmbeddedReferenced = false;
        check.Check(everyEmbeddedReferenced,
            "generation의 embedded texture는 모두 어떤 재질의 의존이어야 한다");
        check.Check(!product.files.empty() && product.artifactBytes > 0u,
            "내보낸 generation 파일이 0개가 아니어야 한다");

        char summary[400]{};
        std::snprintf(summary, sizeof(summary),
            "  model 단정 %zu/%zu · 재질 %zu · 임베디드 %zu · shader 간선 %zu"
            " · texture 간선 %zu\n",
            check.passed, check.passed + check.failed, materials,
            embeddedIds.size(), shaderEdges, textureEdges);
        outLog += summary;
        return check.failed == 0u;
    }
}
