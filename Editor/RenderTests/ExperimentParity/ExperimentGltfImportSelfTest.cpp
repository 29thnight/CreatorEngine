#include "ExperimentParity/ExperimentGltfImportSelfTest.h"
#include "ExperimentParity/ExperimentLegacyBridge.h"

#include "Model.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "Experiment/Import/GltfImporter.h"
#include "Experiment/Import/ImportedScene.h"
#include "Experiment/Import/SceneToModelDraft.h"
#include "Experiment/ModelLoader.h"
#include "Uuid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        namespace im = experiment::importer;

        constexpr std::size_t MaxGltfDiffLines = 24;
        constexpr float BoundsEpsilon = 1e-3f;

        struct GltfDiffLog final
        {
            std::string& log;
            std::size_t count{};

            void Add(std::string text)
            {
                ++count;
                if (count <= MaxGltfDiffLines)
                    log += "  [diff] " + std::move(text) + "\n";
                else if (count == MaxGltfDiffLines + 1)
                    log += "  [diff] ... 이후 생략\n";
            }
        };

        void AppendNotes(const std::vector<im::ImportNote>& notes,
            std::string_view tag, std::string& outLog, std::size_t& outErrors)
        {
            for (const im::ImportNote& note : notes)
            {
                if (note.severity == im::ImportNoteSeverity::Error) ++outErrors;
                outLog += "  [" + std::string(tag) + "] "
                    + (note.severity == im::ImportNoteSeverity::Error ? "ERROR " :
                       note.severity == im::ImportNoteSeverity::Warning ? "WARN  " : "INFO  ")
                    + std::string(im::ToString(note.code)) + " @ " + note.context
                    + " ×" + std::to_string(note.count) + " — " + note.message + "\n";
            }
        }

        struct Aabb final
        {
            float minimum[3]{};
            float maximum[3]{};
            bool valid{};

            void Add(const ex::Bounds& bounds)
            {
                const float lo[3] = {
                    bounds.minimum.x, bounds.minimum.y, bounds.minimum.z };
                const float hi[3] = {
                    bounds.maximum.x, bounds.maximum.y, bounds.maximum.z };
                if (!valid)
                {
                    for (int i = 0; i < 3; ++i) { minimum[i] = lo[i]; maximum[i] = hi[i]; }
                    valid = true;
                    return;
                }
                for (int i = 0; i < 3; ++i)
                {
                    minimum[i] = (std::min)(minimum[i], lo[i]);
                    maximum[i] = (std::max)(maximum[i], hi[i]);
                }
            }
        };

        [[nodiscard]] Aabb ModelBounds(const ex::Model& model)
        {
            Aabb aabb;
            for (const ex::Mesh& mesh : model.Meshes()) aabb.Add(mesh.bounds);
            return aabb;
        }

        [[nodiscard]] std::size_t TriangleCount(const ex::Model& model)
        {
            std::size_t total = 0;
            for (const ex::Mesh& mesh : model.Meshes()) total += mesh.indices.size() / 3;
            return total;
        }

        [[nodiscard]] std::size_t VertexCount(const ex::Model& model)
        {
            std::size_t total = 0;
            for (const ex::Mesh& mesh : model.Meshes()) total += mesh.vertices.size();
            return total;
        }

        // ★ 방향을 구분해야 한다. fastgltf 경로는 **의도적으로** legacy 보다
        //   많이 보존한다(스켈레톤을 joint 의 조상 폐포로 잡아 계층 노드를
        //   더 담는다 — 그 덕에 채널 10개가 살아난다). 그것을 실패로 세면
        //   "고침이 성공할수록 게이트가 빨개지는" 자가당착이 된다.
        //   회귀(legacy 에 있는데 fastgltf 에 없음)만 실패로 세고, 추가분은
        //   정보로 남긴다.
        void CompareNameSets(std::vector<std::string> legacy,
            std::vector<std::string> gltf, std::string_view what,
            GltfDiffLog& diff, std::string& outLog)
        {
            std::ranges::sort(legacy);
            std::ranges::sort(gltf);
            std::vector<std::string> onlyLegacy, onlyGltf;
            std::ranges::set_difference(legacy, gltf, std::back_inserter(onlyLegacy));
            std::ranges::set_difference(gltf, legacy, std::back_inserter(onlyGltf));

            for (const std::string& name : onlyLegacy)
            {
                diff.Add(std::string(what) + " '" + name
                    + "' 이 fastgltf 경로에서 누락됐다");
            }
            for (const std::string& name : onlyGltf)
            {
                outLog += "  [gain] " + std::string(what) + " '" + name
                    + "' 은 fastgltf 경로에만 있다(보존 개선)\n";
            }
        }
    }

    bool RunExperimentGltfImportSelfTest(
        const std::string& modelPath, std::string& outLog)
    {
        outLog += "[experiment.gltf] 대상: " + modelPath + "\n";

        im::GltfImporter importer;
        const std::filesystem::path sourcePath(modelPath);
        if (!importer.CanImport(sourcePath))
        {
            outLog += "  결과: 실패 (glTF/GLB 확장자가 아니다)\n";
            return false;
        }

        // 1. 임포트
        im::ImportRequest request;
        request.sourcePath = sourcePath;
        const im::ImportResult imported = importer.Import(request);
        std::size_t errors = 0;
        AppendNotes(imported.notes, "import", outLog, errors);
        if (!imported.Succeeded())
        {
            outLog += "  결과: 실패 (임포터가 ImportedScene 을 만들지 못함)\n";
            return false;
        }
        const im::ImportedScene& scene = *imported.scene;

        char scaleLine[220];
        std::snprintf(scaleLine, sizeof(scaleLine),
            "  fastgltf 임포트: nodes %zu, meshes %zu, materials %zu, textures %zu,"
            " skins %zu, clips %zu | vertices %zu, triangles %zu\n",
            scene.nodes.size(), scene.meshes.size(), scene.materials.size(),
            scene.textures.size(), scene.skins.size(), scene.clips.size(),
            im::TotalVertexCount(scene), im::TotalTriangleCount(scene));
        outLog += scaleLine;

        // 2. IR 자체 검증
        AppendNotes(im::ValidateImportedScene(scene), "ir", outLog, errors);

        // 3. 변환 경계
        im::ConversionOptions options;
        options.modelAssetId.value =
            Uuid::FromName(FileGuid::ns_filesystem(), sourcePath.string());
        options.modelName = sourcePath.stem().string();
        options.ticksPerSecond = 30.0;
        const im::ConversionResult converted =
            im::ConvertToModelDraft(scene, options);
        AppendNotes(converted.notes, "convert", outLog, errors);
        if (!converted.Succeeded())
        {
            outLog += "  결과: 실패 (변환 경계가 draft 를 만들지 못함)\n";
            return false;
        }

        // 4. 게시
        ex::ModelLoader loader(std::make_unique<bridge::LegacyBridgeDecoder>(
            ex::ModelDraft(*converted.draft)));
        ex::ModelLoadRequest loadRequest;
        loadRequest.sourcePath = sourcePath;
        loadRequest.sourcePreference = ex::ModelSourcePreference::SourceOnly;
        const ex::ModelLoadResult published = loader.Load(loadRequest);
        for (const ex::ModelLoadIssue& issue : published.issues)
        {
            outLog += std::string("  [validate] ")
                + (issue.severity == ex::ModelLoadIssueSeverity::Error
                    ? "ERROR " : "WARN  ")
                + std::string(bridge::ToString(issue.code)) + " @ " + issue.context
                + " — " + issue.message + "\n";
        }
        if (!published.Succeeded())
        {
            outLog += "  결과: 실패 (fastgltf 산출물이 게시 검증을 통과하지 못함)\n";
            return false;
        }

        // 5. legacy(Assimp) 기준선
        const bridge::LoadedPair legacyPair = bridge::LoadAndBridge(modelPath);
        if (!legacyPair.result.Succeeded())
        {
            outLog += "  [note] legacy 기준선을 만들지 못해 비교를 건너뛴다\n";
            outLog += "  구조 검증 오류: " + std::to_string(errors) + "건\n";
            const bool selfOnly = errors == 0;
            outLog += std::string("  결과: ")
                + (selfOnly ? "통과(비교 없음)" : "실패") + "\n";
            return selfOnly;
        }
        const ex::Model& legacy = *legacyPair.result.model;
        const ex::Model& fromGltf = *published.model;

        // 6. 파서 무관 신호로 비교
        GltfDiffLog diff{ outLog };

        const std::size_t legacyTriangles = TriangleCount(legacy);
        const std::size_t gltfTriangles = TriangleCount(fromGltf);
        char sizeLine[240];
        std::snprintf(sizeLine, sizeof(sizeLine),
            "  규모 비교(Assimp → fastgltf): 정점 %zu → %zu, 삼각형 %zu → %zu,"
            " 메시 %zu → %zu\n",
            VertexCount(legacy), VertexCount(fromGltf),
            legacyTriangles, gltfTriangles,
            legacy.Meshes().size(), fromGltf.Meshes().size());
        outLog += sizeLine;
        if (legacyTriangles != gltfTriangles)
        {
            // JoinIdenticalVertices 는 정점만 합치고 삼각형은 보존한다.
            // 삼각형 수가 다르면 기하 자체가 달라진 것이다.
            diff.Add("삼각형 수 불일치 " + std::to_string(legacyTriangles)
                + " vs " + std::to_string(gltfTriangles));
        }

        const Aabb legacyBounds = ModelBounds(legacy);
        const Aabb gltfBounds = ModelBounds(fromGltf);
        if (legacyBounds.valid && gltfBounds.valid)
        {
            float maxDelta = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                maxDelta = (std::max)(maxDelta,
                    std::abs(legacyBounds.minimum[i] - gltfBounds.minimum[i]));
                maxDelta = (std::max)(maxDelta,
                    std::abs(legacyBounds.maximum[i] - gltfBounds.maximum[i]));
            }
            char boundsLine[200];
            std::snprintf(boundsLine, sizeof(boundsLine),
                "  AABB 최대 편차: %.6f → %s (좌표 규약 일치 검증)\n", maxDelta,
                maxDelta <= BoundsEpsilon ? "일치" : "불일치");
            outLog += boundsLine;
            if (maxDelta > BoundsEpsilon)
            {
                diff.Add("메시 로컬 AABB 가 어긋난다 — 좌표 변환이 legacy 와 다르다");
            }
        }

        {
            std::vector<std::string> a, b;
            for (const ex::ModelNode& node : legacy.Nodes()) a.push_back(node.name);
            for (const ex::ModelNode& node : fromGltf.Nodes()) b.push_back(node.name);
            CompareNameSets(std::move(a), std::move(b), "node", diff, outLog);
        }
        {
            // 재질은 이름 집합만으로 판단하면 안 된다. legacy 에만 있는 재질이
            // **어떤 메시도 참조하지 않는다면** 그것은 파서가 덧붙인 유령이지
            // fastgltf 경로의 누락이 아니다. 참조 여부로 갈라 판정한다.
            std::vector<std::uint8_t> referenced(legacy.Materials().size(), 0);
            for (const ex::Mesh& mesh : legacy.Meshes())
            {
                if (ex::IsInRange(mesh.material, referenced.size()))
                    referenced[mesh.material.Value()] = 1;
            }

            std::vector<std::string> gltfNames;
            for (const ex::Material& m : fromGltf.Materials())
                gltfNames.push_back(m.name);

            const auto join = [](const std::vector<std::string>& names)
            {
                std::string out;
                for (const std::string& name : names)
                {
                    if (!out.empty()) out += ", ";
                    out += name.empty() ? "<이름 없음>" : name;
                }
                return out;
            };

            std::vector<std::string> legacyNames;
            std::size_t phantomCount = 0;
            for (std::size_t i = 0; i < legacy.Materials().size(); ++i)
            {
                const std::string& name = legacy.Materials()[i].name;
                legacyNames.push_back(name);
                const bool present = std::ranges::find(gltfNames, name) != gltfNames.end();
                if (present) continue;
                if (!referenced[i])
                {
                    ++phantomCount;
                    outLog += "  [artifact] material '" + name
                        + "' 은 Assimp 에만 있고 어떤 메시도 참조하지 않는다 "
                          "— 파서가 덧붙인 유령이라 누락으로 세지 않는다\n";
                }
                else
                {
                    diff.Add("material '" + name
                        + "' 이 fastgltf 경로에서 누락됐다(메시가 참조 중)");
                }
            }
            if (legacyNames != gltfNames)
            {
                outLog += "  [재질 목록] Assimp: [" + join(legacyNames) + "]\n";
                outLog += "             fastgltf: [" + join(gltfNames) + "]\n";
            }
            for (const std::string& name : gltfNames)
            {
                if (std::ranges::find(legacyNames, name) == legacyNames.end())
                {
                    outLog += "  [gain] material '" + name
                        + "' 은 fastgltf 경로에만 있다(보존 개선)\n";
                }
            }
            (void)phantomCount;
        }

        const ex::Skeleton* legacySkeleton = legacy.TryGetSkeleton();
        const ex::Skeleton* gltfSkeleton = fromGltf.TryGetSkeleton();
        if ((legacySkeleton != nullptr) != (gltfSkeleton != nullptr))
        {
            diff.Add("skeleton 존재 여부가 다르다");
        }
        else if (legacySkeleton && gltfSkeleton)
        {
            std::vector<std::string> a, b;
            for (const ex::Bone& bone : legacySkeleton->bones) a.push_back(bone.name);
            for (const ex::Bone& bone : gltfSkeleton->bones) b.push_back(bone.name);
            CompareNameSets(std::move(a), std::move(b), "bone", diff, outLog);

            std::vector<std::string> ca, cb;
            for (const ex::AnimationClip& clip : legacySkeleton->clips)
                ca.push_back(clip.name);
            for (const ex::AnimationClip& clip : gltfSkeleton->clips)
                cb.push_back(clip.name);
            CompareNameSets(std::move(ca), std::move(cb), "clip", diff, outLog);

            std::size_t legacyChannels = 0, gltfChannels = 0;
            for (const ex::AnimationClip& clip : legacySkeleton->clips)
                legacyChannels += clip.channels.size();
            for (const ex::AnimationClip& clip : gltfSkeleton->clips)
                gltfChannels += clip.channels.size();
            char animLine[200];
            std::snprintf(animLine, sizeof(animLine),
                "  스켈레톤 비교: bone %zu → %zu, clip %zu → %zu, 채널 %zu → %zu\n",
                legacySkeleton->bones.size(), gltfSkeleton->bones.size(),
                legacySkeleton->clips.size(), gltfSkeleton->clips.size(),
                legacyChannels, gltfChannels);
            outLog += animLine;
        }

        outLog += "  구조 불일치: " + std::to_string(diff.count) + "건, 검증 오류 "
            + std::to_string(errors) + "건\n";
        const bool passed = diff.count == 0 && errors == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패") + "\n";
        return passed;
    }
}
