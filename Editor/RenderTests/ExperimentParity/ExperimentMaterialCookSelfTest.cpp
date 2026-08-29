#include "ExperimentParity/ExperimentMaterialCookSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/MaterialCookProducer.h"
#include "Experiment/Cooked/ModelCookProducer.h"
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

        [[nodiscard]] std::string BytesToString(
            const std::vector<std::byte>& bytes)
        {
            std::string text;
            text.resize(bytes.size());
            for (std::size_t index = 0u; index < bytes.size(); ++index)
                text[index] = static_cast<char>(bytes[index]);
            return text;
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
                check.Check(BytesToString(product.artifactBytes) == original,
                    "artifact 가 원본과 비트 단위로 같아야 한다");

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
        std::string original;
        check.Check(ReadFileBytes(source, original), "원본을 읽을 수 있어야 한다");
        check.Check(BytesToString(product.artifactBytes) == original,
            "artifact 가 원본과 비트 단위로 같아야 한다");
        check.Check(experiment::IsAssetIdV4(product.shaderMetaAssetId),
            "shaderMetaAssetId 가 canonical UUIDv4 여야 한다");
        check.Check(!product.manifestEntry.dependencies.empty(),
            "dependency 가 최소 하나(shader)는 있어야 한다");

        char summary[320]{};
        std::snprintf(summary, sizeof(summary),
            "  material 단정 %zu/%zu · %s · 의존 %zu · %s\n",
            check.passed, check.passed + check.failed, product.name.c_str(),
            product.manifestEntry.dependencies.size(),
            product.artifactPath.c_str());
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentModelDependencyReal(const std::string& assetRootPath,
        const std::string& modelPath, std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.matcook] 실자산 model: " + modelPath + "\n";

        const ck::ModelCookProductResult result = ck::BuildModelCookProduct(
            { std::filesystem::path(modelPath),
              std::filesystem::path(assetRootPath) });
        check.Check(result.Succeeded(), "실자산 model cook 이 통과해야 한다");
        if (!result.Succeeded())
        {
            for (const ck::ModelCookProductIssue& issue : result.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }

        const ck::ModelCookProduct& product = *result.product;

        // 이 product 가 스스로 만든 노드 집합.
        std::set<experiment::AssetId> produced;
        std::set<std::string> paths;
        std::size_t materialEntries = 0u;
        std::size_t textureEntries = 0u;
        bool duplicatePath = false;
        for (const ck::CookedAssetManifestEntry& entry : product.manifestEntries)
        {
            produced.insert(entry.assetId);
            if (entry.kind == ck::CookedAssetKind::Material) ++materialEntries;
            if (entry.kind == ck::CookedAssetKind::Texture)
            {
                ++textureEntries;
                // 재질 entry 는 model artifact 경로를 공유하므로 texture 만 본다.
                if (!paths.insert(entry.artifactPath).second) duplicatePath = true;
            }
        }
        check.Check(!duplicatePath, "임베디드 texture artifact 경로가 서로 달라야 한다");
        check.Check(materialEntries == product.materialCount,
            "material entry 수가 materialCount 와 같아야 한다");
        check.Check(textureEntries == product.embeddedTextures.size(),
            "texture entry 수가 임베디드 artifact 수와 같아야 한다");
        check.Check(product.embeddedTextures.size()
            == product.embeddedTextureCount,
            "임베디드 artifact 수가 embeddedTextureCount 와 같아야 한다");

        // ★ 핵심: 재질 의존이 전부 해소되는가.
        //
        //   ShaderMeta 는 이 product 밖(별도 producer)이므로 "GUID 가 유효한가"
        //   까지만 본다. texture 는 이 product 가 직접 만들어야 하므로
        //   **여기서 해소되지 않으면 실패**다.
        std::size_t shaderEdges = 0u;
        std::size_t textureEdges = 0u;
        bool unresolvedTexture = false;
        bool invalidEdge = false;
        for (const ck::CookedAssetManifestEntry& entry : product.manifestEntries)
        {
            if (entry.kind != ck::CookedAssetKind::Material) continue;
            check.Check(!entry.dependencies.empty(),
                "재질은 최소 shader 의존 하나를 가져야 한다");
            for (std::size_t index = 0u; index < entry.dependencies.size(); ++index)
            {
                const experiment::AssetId& dependency = entry.dependencies[index];
                if (!experiment::IsAssetIdV4(dependency)) { invalidEdge = true; continue; }
                if (0u == index) { ++shaderEdges; continue; }
                ++textureEdges;
                if (!produced.contains(dependency)) unresolvedTexture = true;
            }
        }
        check.Check(!invalidEdge, "모든 의존 GUID 가 canonical UUIDv4 여야 한다");
        check.Check(!unresolvedTexture,
            "재질의 texture 의존이 이 product 안에서 해소돼야 한다");
        check.Check(shaderEdges == materialEntries,
            "재질마다 shader 간선이 정확히 하나여야 한다");

        // ★ 반대 방향도 본다. 위의 단정들은 "그린 간선이 해소되는가"만 보므로
        //   **간선을 하나도 안 그려도 전부 통과한다** — 변이로 확인했다.
        //   b2c-3 의 본체가 바로 그 간선이므로, 뽑아 놓은 임베디드 texture 가
        //   실제로 어떤 재질에서 참조되는지를 함께 단정한다.
        std::set<experiment::AssetId> referencedTextures;
        for (const ck::CookedAssetManifestEntry& entry : product.manifestEntries)
        {
            if (entry.kind != ck::CookedAssetKind::Material) continue;
            for (std::size_t index = 1u; index < entry.dependencies.size(); ++index)
                referencedTextures.insert(entry.dependencies[index]);
        }
        bool everyEmbeddedReferenced = true;
        for (const ck::EmbeddedTextureArtifact& embedded : product.embeddedTextures)
        {
            if (!referencedTextures.contains(embedded.textureAssetId))
                everyEmbeddedReferenced = false;
        }
        check.Check(everyEmbeddedReferenced,
            "뽑아낸 임베디드 texture 는 모두 어떤 재질의 의존이어야 한다");
        check.Check(referencedTextures.size() == product.embeddedTextures.size(),
            "참조된 texture 수와 뽑아낸 임베디드 수가 같아야 한다");
        check.Check(textureEdges >= product.embeddedTextures.size(),
            "texture 간선 수가 임베디드 수 이상이어야 한다");

        // 임베디드 artifact 자체.
        bool emptyBytes = false;
        bool badExtension = false;
        for (const ck::EmbeddedTextureArtifact& embedded : product.embeddedTextures)
        {
            if (embedded.artifactBytes.empty()) emptyBytes = true;
            if (!ck::IsSupportedTextureExtension(embedded.extension))
                badExtension = true;
            // 판별 결과가 실제 바이트와 맞는가. 확장자를 상수로 박아 두면
            // 여기서 걸린다.
            if (ck::SniffTextureExtension(embedded.artifactBytes)
                != embedded.extension)
            {
                badExtension = true;
            }
        }
        check.Check(!emptyBytes, "임베디드 artifact 바이트가 비면 안 된다");
        check.Check(!badExtension,
            "임베디드 확장자가 allowlist 이고 바이트와 일치해야 한다");

        char summary[400]{};
        std::snprintf(summary, sizeof(summary),
            "  model 단정 %zu/%zu · 재질 %zu · 임베디드 %zu · shader 간선 %zu"
            " · texture 간선 %zu\n",
            check.passed, check.passed + check.failed, materialEntries,
            product.embeddedTextures.size(), shaderEdges, textureEdges);
        outLog += summary;
        return check.failed == 0u;
    }
}
