#include "ExperimentParity/ExperimentCatalogSelfTest.h"

#include "Experiment/AssetIdentity.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
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

        [[nodiscard]] bool ReadBytes(const std::filesystem::path& path,
            std::vector<std::byte>& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff bytes = stream.tellg();
            if (bytes < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(bytes));
            if (!out.empty())
            {
                stream.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(out.size()));
            }
            return stream.good() || stream.eof();
        }

        [[nodiscard]] experiment::AssetId Guid(const char* text)
        {
            experiment::AssetId id{};
            (void)experiment::TryParseCanonicalAssetId(text, id);
            return id;
        }

        [[nodiscard]] ck::CookedAssetManifestEntry MakeEntry(
            const experiment::AssetId& id, ck::CookedAssetKind kind,
            std::string path, std::vector<experiment::AssetId> dependencies = {})
        {
            ck::CookedAssetManifestEntry entry;
            entry.assetId = id;
            entry.kind = kind;
            entry.formatVersion = 1u;
            entry.byteSize = 8u;
            entry.contentSha256.fill(0x11u);
            entry.artifactPath = std::move(path);
            entry.dependencies = std::move(dependencies);
            return entry;
        }
    }

    bool RunExperimentCatalogSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.catalog] 합성 검사\n";

        const experiment::AssetId scene =
            Guid("11111111-1111-4111-8111-111111111111");
        const experiment::AssetId model =
            Guid("22222222-2222-4222-8222-222222222222");
        const experiment::AssetId material =
            Guid("33333333-3333-4333-8333-333333333333");
        const experiment::AssetId texture =
            Guid("44444444-4444-4444-8444-444444444444");
        const experiment::AssetId absent =
            Guid("99999999-9999-4999-8999-999999999999");

        // scene -> model -> material -> texture 사슬.
        ck::CookedAssetManifest manifest;
        manifest.entries.push_back(MakeEntry(scene, ck::CookedAssetKind::Scene,
            "Derived/Scenes/11/11111111-1111-4111-8111-111111111111.creator",
            { model }));
        manifest.entries.push_back(MakeEntry(model, ck::CookedAssetKind::Model,
            "Derived/Models/22/22222222-2222-4222-8222-222222222222.cemc",
            { material }));
        manifest.entries.push_back(MakeEntry(material, ck::CookedAssetKind::Material,
            "Derived/Models/22/22222222-2222-4222-8222-222222222222.cemc",
            { texture }));
        manifest.entries.push_back(MakeEntry(texture, ck::CookedAssetKind::Texture,
            "Derived/Textures/44/44444444-4444-4444-8444-444444444444.png"));
        manifest.sourceAssets.push_back({ model, "Models/Probe.glb" });
        manifest.sourceAssets.push_back({ scene, "Scenes/Probe.creator" });
        manifest.sourceAssets.push_back({ texture, "Textures/Probe.png" });

        const ck::AssetManifestWriteResult written =
            ck::WriteAssetManifest(manifest);
        check.Check(written.Succeeded(), "fixture manifest 기록");
        if (!written.Succeeded()) return false;

        // ── 1. 정상 로드 ───────────────────────────────────────────────
        std::vector<ck::AssetManifestIssue> issues;
        const ck::CookedAssetCatalog catalog =
            ck::CookedAssetCatalog::Load(written.bytes, "C:/probe/Assets", issues);
        check.Check(issues.empty(), "정상 CEMF 는 issue 가 없어야 한다");
        check.Check(!catalog.IsEmpty(), "catalog 가 비면 안 된다");
        check.Check(catalog.Size() == 4u, "entry 수");
        check.Check(catalog.SourceAssetCount() == 3u, "source identity 수");
        check.Check(catalog.Find(scene) != nullptr, "scene 조회");
        check.Check(catalog.Find(absent) == nullptr, "없는 GUID 는 null");
        check.Check(catalog.CountOfKind(ck::CookedAssetKind::Texture) == 1u,
            "kind 별 계수");
        check.Check(catalog.CountOfKind(ck::CookedAssetKind::Prefab) == 0u,
            "없는 kind 는 0");
        check.Check(catalog.ResolveSourcePath(scene) ==
            std::filesystem::path("C:/probe/Assets/Scenes/Probe.creator"),
            "source GUID가 package Assets 경로로 해석된다");

        // artifact 경로가 derivedRoot 아래로 붙는가.
        const std::filesystem::path resolved = catalog.ResolveArtifactPath(texture);
        check.Check(resolved.generic_string()
            == "C:/probe/Assets/Derived/Textures/44/"
               "44444444-4444-4444-8444-444444444444.png",
            "artifact 실경로 해석");
        check.Check(catalog.ResolveArtifactPath(absent).empty(),
            "없는 GUID 의 경로는 비어야 한다");

        // ── 2. 폐포 — 위상 순서 ────────────────────────────────────────
        {
            std::vector<experiment::AssetId> ordered;
            std::string failure;
            check.Check(catalog.CollectClosure(scene, ordered, failure),
                "scene 폐포 수집");
            check.Check(ordered.size() == 4u, "폐포에 4개가 들어야 한다");

            // ★ 위상 순서: 의존이 자기보다 먼저. 로더가 이 순서대로 열면
            //   참조가 항상 준비돼 있다.
            std::map<experiment::AssetId, std::size_t> position;
            for (std::size_t index = 0u; index < ordered.size(); ++index)
                position[ordered[index]] = index;
            check.Check(position.size() == ordered.size(), "폐포에 중복이 없다");
            check.Check(position.contains(texture) && position.contains(scene)
                && position[texture] < position[scene], "texture 가 scene 보다 먼저");
            check.Check(position.contains(material)
                && position[material] < position[model], "material 이 model 보다 먼저");
            check.Check(position.contains(model)
                && position[model] < position[scene], "model 이 scene 보다 먼저");
            check.Check(!ordered.empty() && ordered.back() == scene,
                "root 가 마지막이어야 한다");
        }
        {
            // 잎에서 시작하면 자기 하나.
            std::vector<experiment::AssetId> ordered;
            std::string failure;
            check.Check(catalog.CollectClosure(texture, ordered, failure),
                "잎 폐포 수집");
            check.Check(ordered.size() == 1u && ordered.front() == texture,
                "잎 폐포는 자기 하나");
        }
        {
            std::vector<experiment::AssetId> ordered;
            std::string failure;
            check.Check(!catalog.CollectClosure(absent, ordered, failure),
                "없는 root 는 실패");
            check.Check(ordered.empty(), "실패 시 결과가 비어야 한다");
            check.Check(!failure.empty(), "실패 사유가 있어야 한다");
            // ★ **사유까지 본다.** root 검사를 지워도 아래 dependency 검사가
            //   같은 입력을 거부해서 "실패했다"만으로는 초록이었다(변이로
            //   확인). root 를 물었는데 "dependency" 라고 답하면 호출자가
            //   엉뚱한 곳을 본다.
            check.Check(failure.find("root GUID") != std::string::npos,
                "없는 root 의 사유는 root 를 가리켜야 한다");
        }

        // ── 3. 순환이 있어도 멈춘다 ────────────────────────────────────
        {
            // A -> B -> A. 중첩 프리팹이 서로를 품는 저작 오류가 데이터로
            // 존재할 수 있고, 그때도 로드는 되어야 한다.
            const experiment::AssetId a = Guid("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
            const experiment::AssetId b = Guid("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
            ck::CookedAssetManifest cyclic;
            cyclic.entries.push_back(MakeEntry(a, ck::CookedAssetKind::Prefab,
                "Derived/Prefabs/aa/aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa.prefab",
                { b }));
            cyclic.entries.push_back(MakeEntry(b, ck::CookedAssetKind::Prefab,
                "Derived/Prefabs/bb/bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb.prefab",
                { a }));
            const ck::AssetManifestWriteResult cyclicBytes =
                ck::WriteAssetManifest(cyclic);
            check.Check(cyclicBytes.Succeeded(), "순환 fixture 기록");
            if (cyclicBytes.Succeeded())
            {
                std::vector<ck::AssetManifestIssue> cyclicIssues;
                const ck::CookedAssetCatalog cyclicCatalog =
                    ck::CookedAssetCatalog::Load(cyclicBytes.bytes, "C:/probe",
                        cyclicIssues);
                std::vector<experiment::AssetId> ordered;
                std::string failure;
                check.Check(cyclicCatalog.CollectClosure(a, ordered, failure),
                    "순환 폐포가 멈춰야 한다");
                check.Check(ordered.size() == 2u, "순환 폐포는 각 노드 한 번");
            }
        }

        // ── 4. fail-closed ─────────────────────────────────────────────
        {
            std::vector<ck::AssetManifestIssue> bad;
            const std::vector<std::byte> garbage(32, std::byte{ 0x7Fu });
            const ck::CookedAssetCatalog broken =
                ck::CookedAssetCatalog::Load(garbage, "C:/probe", bad);
            check.Check(broken.IsEmpty(), "깨진 CEMF 는 빈 catalog");
            check.Check(!bad.empty(), "깨진 CEMF 는 issue 를 남긴다");
            // ★ 부분적으로 채워진 catalog 를 내놓으면 "없는 자산"과 "아직 안
            //   읽은 자산"을 구별할 수 없다.
            check.Check(broken.Find(scene) == nullptr, "깨진 catalog 는 조회 0");
        }
        {
            std::vector<ck::AssetManifestIssue> bad;
            const ck::CookedAssetCatalog noRoot =
                ck::CookedAssetCatalog::Load(written.bytes, {}, bad);
            check.Check(noRoot.IsEmpty(), "derived root 가 비면 catalog 도 빈다");
            check.Check(!bad.empty(), "derived root 누락은 issue 를 남긴다");
        }

        char summary[160]{};
        std::snprintf(summary, sizeof(summary),
            "  합성 단정 %zu/%zu\n", check.passed, check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunExperimentCatalogReal(const std::string& derivedRootPath,
        std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.catalog] 전수 해석: " + derivedRootPath + "\n";

        const std::filesystem::path root(derivedRootPath);
        const std::filesystem::path manifestPath =
            root / "Derived" / "asset-manifest.cemf";

        std::vector<std::byte> bytes;
        check.Check(ReadBytes(manifestPath, bytes), "CEMF 를 읽을 수 있어야 한다");
        if (bytes.empty())
        {
            outLog += "    CEMF 가 없다: " + manifestPath.string() + "\n";
            return false;
        }

        std::vector<ck::AssetManifestIssue> issues;
        const ck::CookedAssetCatalog catalog =
            ck::CookedAssetCatalog::Load(bytes, root, issues);
        check.Check(issues.empty(), "CEMF 판독에 issue 가 없어야 한다");
        check.Check(!catalog.IsEmpty(), "catalog 가 비면 안 된다");
        if (catalog.IsEmpty())
        {
            for (const ck::AssetManifestIssue& issue : issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }

        // catalog 전 entry 를 다시 훑기 위해 GUID 목록을 만든다. Find 로만
        // 접근하므로 kind 별 계수로 총수를 재구성한다.
        std::vector<experiment::AssetId> all;
        for (const ck::CookedAssetKind kind : {
                ck::CookedAssetKind::Model, ck::CookedAssetKind::Material,
                ck::CookedAssetKind::Texture, ck::CookedAssetKind::ShaderMeta,
                ck::CookedAssetKind::Scene, ck::CookedAssetKind::Prefab })
        {
            (void)kind;
        }

        // manifest 를 다시 읽어 GUID 목록을 얻는다(catalog 는 조회 전용이다).
        ck::CookedAssetManifest manifest;
        std::vector<ck::AssetManifestIssue> reread;
        check.Check(ck::ReadAssetManifest(bytes, manifest, reread),
            "GUID 열거를 위한 재판독");
        check.Check(!manifest.sourceAssets.empty(),
            "실 CEMF에 source identity table이 있어야 한다");

        std::size_t resolvedArtifacts = 0u;
        std::size_t verifiedBytes = 0u;
        std::size_t dependencyEdges = 0u;
        std::size_t closureTotal = 0u;
        std::size_t largestClosure = 0u;
        bool missingArtifact = false;
        bool staleArtifact = false;
        bool unresolvedDependency = false;
        bool closureFailed = false;
        bool orderViolation = false;
        std::string firstFailure;

        // artifact 는 여러 entry 가 공유할 수 있다(재질이 model CEMC 를 쓴다).
        // 같은 파일을 반복해서 해시하지 않는다.
        std::map<std::string, std::pair<std::uint64_t, ck::Sha256Digest>> fileCache;

        for (const ck::CookedAssetManifestEntry& entry : manifest.entries)
        {
            // 1. 조회
            const ck::CookedAssetManifestEntry* found = catalog.Find(entry.assetId);
            if (!found) { unresolvedDependency = true; continue; }

            // 2. artifact 실재 + 크기·해시
            const std::filesystem::path file =
                catalog.ResolveArtifactPath(entry.assetId);
            if (file.empty()) { missingArtifact = true; continue; }
            ++resolvedArtifacts;

            auto cached = fileCache.find(entry.artifactPath);
            if (cached == fileCache.end())
            {
                std::vector<std::byte> artifact;
                if (!ReadBytes(file, artifact))
                {
                    missingArtifact = true;
                    if (firstFailure.empty()) firstFailure = entry.artifactPath;
                    continue;
                }
                ck::Sha256Digest digest{};
                std::string hashError;
                if (!ck::ComputeSha256(artifact, digest, hashError))
                {
                    staleArtifact = true;
                    continue;
                }
                cached = fileCache.emplace(entry.artifactPath,
                    std::pair{ static_cast<std::uint64_t>(artifact.size()),
                        digest }).first;
            }
            std::vector<ck::AssetManifestIssue> verifyIssues;
            if (!ck::VerifyArtifact(*found, cached->second.first,
                cached->second.second, verifyIssues))
            {
                staleArtifact = true;
                if (firstFailure.empty()) firstFailure = entry.artifactPath;
                continue;
            }
            ++verifiedBytes;

            // 3. dependency 해소
            for (const experiment::AssetId& dependency : entry.dependencies)
            {
                ++dependencyEdges;
                if (!catalog.Find(dependency))
                {
                    unresolvedDependency = true;
                    if (firstFailure.empty())
                        firstFailure = "dep " + entry.artifactPath;
                }
            }

            // 4. 폐포와 위상 순서
            std::vector<experiment::AssetId> ordered;
            std::string failure;
            if (!catalog.CollectClosure(entry.assetId, ordered, failure))
            {
                closureFailed = true;
                if (firstFailure.empty()) firstFailure = failure;
                continue;
            }
            closureTotal += ordered.size();
            largestClosure = (std::max)(largestClosure, ordered.size());

            // ★ 의존이 자기보다 먼저 나와야 한다. 이걸 안 보면 "폐포에 다
            //   들었다"만 맞고 순서는 아무래도 좋은 상태가 된다.
            std::map<experiment::AssetId, std::size_t> position;
            for (std::size_t index = 0u; index < ordered.size(); ++index)
                position[ordered[index]] = index;
            for (const experiment::AssetId& node : ordered)
            {
                const ck::CookedAssetManifestEntry* nodeEntry = catalog.Find(node);
                if (!nodeEntry) continue;
                for (const experiment::AssetId& dependency : nodeEntry->dependencies)
                {
                    const auto self = position.find(node);
                    const auto other = position.find(dependency);
                    if (other == position.end() || self == position.end()) continue;
                    if (other->second > self->second) orderViolation = true;
                }
            }
            if (ordered.empty() || ordered.back() != entry.assetId)
                orderViolation = true;
        }

        check.Check(!missingArtifact, "모든 artifact 가 실재해야 한다");
        check.Check(!staleArtifact, "모든 artifact 의 크기·해시가 manifest 와 같아야 한다");
        check.Check(!unresolvedDependency, "모든 dependency 가 catalog 에서 해소돼야 한다");
        check.Check(!closureFailed, "모든 entry 의 폐포가 수집돼야 한다");
        check.Check(!orderViolation, "폐포가 위상 순서여야 한다(의존이 먼저)");
        check.Check(resolvedArtifacts == manifest.entries.size(),
            "모든 entry 의 artifact 경로가 해석돼야 한다");
        check.Check(verifiedBytes == manifest.entries.size(),
            "모든 entry 가 내용 검증을 통과해야 한다");

        if (!firstFailure.empty()) outLog += "    첫 실패: " + firstFailure + "\n";

        char summary[420]{};
        std::snprintf(summary, sizeof(summary),
            "  전수 단정 %zu/%zu · entry %zu · 파일 %zu · 간선 %zu · 폐포합 %zu"
            " · 최대폐포 %zu\n"
            "    kind: model %zu · material %zu · texture %zu · shadermeta %zu"
            " · scene %zu · prefab %zu\n",
            check.passed, check.passed + check.failed, manifest.entries.size(),
            fileCache.size(), dependencyEdges, closureTotal, largestClosure,
            catalog.CountOfKind(ck::CookedAssetKind::Model),
            catalog.CountOfKind(ck::CookedAssetKind::Material),
            catalog.CountOfKind(ck::CookedAssetKind::Texture),
            catalog.CountOfKind(ck::CookedAssetKind::ShaderMeta),
            catalog.CountOfKind(ck::CookedAssetKind::Scene),
            catalog.CountOfKind(ck::CookedAssetKind::Prefab));
        outLog += summary;
        return check.failed == 0u;
    }
}
