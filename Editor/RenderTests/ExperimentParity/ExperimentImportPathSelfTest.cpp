#include "ExperimentParity/ExperimentImportPathSelfTest.h"
#include "ExperimentParity/ExperimentLegacyBridge.h"

#include "Model.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "Material.h"
#include "Experiment/Import/ImportedScene.h"
#include "Experiment/Import/SceneToModelDraft.h"
#include "Experiment/ModelLoader.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        namespace im = experiment::importer;
        using namespace DirectX;

        // 이름이 같은 폴더의 다른 자가 검사와 겹치면 안 된다 — 유니티 빌드가
        // 두 TU 를 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다.
        constexpr std::size_t MaxImportDiffLines = 24;
        constexpr float WeightEpsilon = 1e-4f;

        struct ImportDiffLog final
        {
            std::string& log;
            std::size_t count{};

            void Add(std::string text)
            {
                ++count;
                if (count <= MaxImportDiffLines)
                    log += "  [diff] " + std::move(text) + "\n";
                else if (count == MaxImportDiffLines + 1)
                    log += "  [diff] ... 이후 생략\n";
            }
        };

        // ── legacy → ImportedScene ──────────────────────────────────────
        struct SceneBuildReport final
        {
            std::vector<std::string> failures{};
            std::size_t unmatchedBones{};
            std::size_t unmatchedChannels{};
            std::size_t shearedNodes{};
            float maxTrsRoundTripError{};
        };

        [[nodiscard]] im::Float3 ToFloat3(FXMVECTOR v)
        {
            XMFLOAT3 stored;
            XMStoreFloat3(&stored, v);
            return { stored.x, stored.y, stored.z };
        }

        [[nodiscard]] im::Float4 ToFloat4(FXMVECTOR v)
        {
            XMFLOAT4 stored;
            XMStoreFloat4(&stored, v);
            return { stored.x, stored.y, stored.z, stored.w };
        }

        [[nodiscard]] float Matrix4MaxAbsDiff(const ex::Matrix4& a, const ex::Matrix4& b)
        {
            float maxDiff = 0.0f;
            for (std::size_t i = 0; i < a.rowMajor.size(); ++i)
                maxDiff = (std::max)(maxDiff,
                    std::abs(a.rowMajor[i] - b.rowMajor[i]));
            return maxDiff;
        }

        // legacy 는 노드 트리와 뼈 트리를 따로 들지만, source 포맷에서는 하나의
        // 노드 트리다. ImportedScene 은 그 원형을 복원한다: 노드가 계층 정본이고
        // skin 은 이름으로 매칭된 노드를 참조할 뿐이다.
        [[nodiscard]] im::ImportedScene BuildImportedSceneFromLegacy(
            ::Model& legacy, SceneBuildReport& report)
        {
            im::ImportedScene scene;
            scene.metadata.sourcePath = legacy.path;
            scene.metadata.importerName = "LegacyBridgeImporter";
            scene.metadata.importerVersion = "1";
            scene.metadata.originalUpAxis = "engine";

            const auto& legacyNodes = legacy.GetNodes();
            std::unordered_map<std::string, std::uint32_t> nodeByName;
            scene.nodes.reserve(legacyNodes.size());

            for (std::size_t i = 0; i < legacyNodes.size(); ++i)
            {
                const ::ModelNode* source = legacyNodes[i];
                if (!source)
                {
                    report.failures.push_back(
                        "m_nodes[" + std::to_string(i) + "] 가 null");
                    return scene;
                }

                im::SceneNode node;
                node.name = source->m_name;
                if (i != 0) node.parent = im::SceneNodeIndex(source->m_parentIndex);

                // 행렬 → TRS 분해. legacy 정본은 행렬이므로 여기서 IR 형태로
                // 되돌린다. 분해 불가(shear)는 계수하고, 왕복 오차도 잰다.
                const XMMATRIX matrix = XMLoadFloat4x4(&source->m_transform);
                XMVECTOR scale, rotation, translation;
                if (XMMatrixDecompose(&scale, &rotation, &translation, matrix))
                {
                    node.local.translation = ToFloat3(translation);
                    node.local.rotation = ToFloat4(rotation);
                    node.local.scale = ToFloat3(scale);
                    report.maxTrsRoundTripError = (std::max)(
                        report.maxTrsRoundTripError,
                        Matrix4MaxAbsDiff(bridge::ToMatrix4(source->m_transform),
                            im::ComposeTrs(node.local)));
                }
                else
                {
                    ++report.shearedNodes;
                }

                node.meshes.reserve(source->m_meshes.size());
                for (uint32 meshIndex : source->m_meshes)
                    node.meshes.push_back(im::ImportMeshIndex(meshIndex));

                nodeByName.emplace(node.name, static_cast<std::uint32_t>(i));
                scene.nodes.push_back(std::move(node));
            }

            // skin: joint 순번 = legacy bone index 로 맞춘다. 그래야 정점의
            // boneIndices 를 그대로 JointIndex 로 쓸 수 있다.
            ::Skeleton* legacySkeleton = legacy.m_Skeleton;
            const bool hasSkeleton =
                legacySkeleton && !legacySkeleton->m_bones.empty();
            if (hasSkeleton)
            {
                im::ImportedSkin skin;
                skin.name = "skin0";
                skin.joints.resize(legacySkeleton->m_bones.size());
                skin.inverseBind.resize(legacySkeleton->m_bones.size());
                for (std::size_t b = 0; b < legacySkeleton->m_bones.size(); ++b)
                {
                    const ::Bone* bone = legacySkeleton->m_bones[b];
                    if (!bone)
                    {
                        report.failures.push_back(
                            "bones[" + std::to_string(b) + "] 가 null");
                        return scene;
                    }
                    const auto found = nodeByName.find(bone->m_name);
                    if (found == nodeByName.end())
                    {
                        // 노드 트리에 대응 노드가 없는 뼈. legacy 가 두 트리를
                        // 따로 든 탓에 생길 수 있는 어긋남이다.
                        ++report.unmatchedBones;
                        continue;
                    }
                    skin.joints[b] = im::SceneNodeIndex(found->second);
                    skin.inverseBind[b] = bridge::ToMatrix4(bone->m_offset);
                    if (bone == legacySkeleton->m_rootBone)
                        skin.skeletonRoot = im::SceneNodeIndex(found->second);
                }
                scene.skins.push_back(std::move(skin));
            }

            // 메시: AoS → SoA. 정점의 boneIndices 는 legacy bone index 이고
            // 그것이 곧 JointIndex 다(위에서 joints 순번을 맞춰 두었다).
            scene.meshes.reserve(legacy.GetMeshCount());
            for (std::size_t m = 0; m < legacy.GetMeshCount(); ++m)
            {
                ::Mesh* source = legacy.GetMesh(static_cast<int>(m));
                if (!source)
                {
                    report.failures.push_back(
                        "meshes[" + std::to_string(m) + "] 가 null");
                    return scene;
                }

                im::ImportedMesh mesh;
                mesh.name = source->GetName();
                mesh.material = im::ImportMaterialIndex(source->GetMaterialIndex());
                mesh.indices = source->GetIndices();

                const std::vector<::Vertex>& vertices = source->GetVertices();
                im::VertexStreams& streams = mesh.streams;
                streams.positions.reserve(vertices.size());
                streams.normals.reserve(vertices.size());
                streams.uv0.reserve(vertices.size());
                streams.uv1.reserve(vertices.size());
                streams.tangents.reserve(vertices.size());
                streams.influenceOffsets.reserve(vertices.size() + 1);
                streams.influenceOffsets.push_back(0);

                for (const ::Vertex& vertex : vertices)
                {
                    streams.positions.push_back(
                        { vertex.position.x, vertex.position.y, vertex.position.z });
                    streams.normals.push_back(
                        { vertex.normal.x, vertex.normal.y, vertex.normal.z });
                    streams.uv0.push_back({ vertex.uv0.x, vertex.uv0.y });
                    streams.uv1.push_back({ vertex.uv1.x, vertex.uv1.y });
                    // legacy 는 bitangent 를 따로 들지만 IR 정본은 handedness 다.
                    // 부호는 cross(normal, tangent) 와의 일치로 되돌린다.
                    const im::Float3 normal{
                        vertex.normal.x, vertex.normal.y, vertex.normal.z };
                    const im::Float3 tangent{
                        vertex.tangent.x, vertex.tangent.y, vertex.tangent.z };
                    const XMVECTOR expected = XMVector3Cross(
                        XMVectorSet(normal.x, normal.y, normal.z, 0.0f),
                        XMVectorSet(tangent.x, tangent.y, tangent.z, 0.0f));
                    const XMVECTOR actual = XMVectorSet(
                        vertex.bitangent.x, vertex.bitangent.y, vertex.bitangent.z, 0.0f);
                    const float alignment =
                        XMVectorGetX(XMVector3Dot(expected, actual));
                    streams.tangents.push_back(
                        { tangent.x, tangent.y, tangent.z,
                          alignment < 0.0f ? -1.0f : 1.0f });

                    if (hasSkeleton)
                    {
                        const float weights[4] = {
                            vertex.boneWeights.x, vertex.boneWeights.y,
                            vertex.boneWeights.z, vertex.boneWeights.w };
                        const float indices[4] = {
                            vertex.boneIndices.x, vertex.boneIndices.y,
                            vertex.boneIndices.z, vertex.boneIndices.w };
                        for (std::size_t slot = 0; slot < 4; ++slot)
                        {
                            if (!(weights[slot] > 0.0f)) continue;
                            if (indices[slot] < 0.0f) continue;
                            im::JointInfluence influence;
                            influence.joint = im::JointIndex(
                                static_cast<std::uint32_t>(indices[slot]));
                            influence.weight = weights[slot];
                            streams.influences.push_back(influence);
                        }
                    }
                    streams.influenceOffsets.push_back(
                        static_cast<std::uint32_t>(streams.influences.size()));
                }
                if (!hasSkeleton || streams.influences.empty())
                {
                    streams.influenceOffsets.clear();
                    streams.influences.clear();
                }
                scene.meshes.push_back(std::move(mesh));
            }

            // 메시를 드는 노드에 skin 을 붙인다(IR 규약: skin 은 노드가 든다).
            if (hasSkeleton)
            {
                for (im::SceneNode& node : scene.nodes)
                {
                    for (im::ImportMeshIndex mesh : node.meshes)
                    {
                        if (!IsInRange(mesh, scene.meshes.size())) continue;
                        if (scene.meshes[mesh.Value()].streams.HasSkin())
                        {
                            node.skin = im::SkinIndex(0);
                            break;
                        }
                    }
                }
            }

            // 머테리얼: legacy 는 PBR semantic 이 아니라 property block 을 든다.
            // semantic 복원은 원리적으로 불가하므로 이름만 옮기고, 비교에서도
            // 머테리얼은 제외한다(경로가 서로 다른 표현을 만든다).
            scene.materials.reserve(legacy.GetMaterialCount());
            for (std::size_t i = 0; i < legacy.GetMaterialCount(); ++i)
            {
                const ::Material* source = legacy.GetMaterial(static_cast<int>(i));
                im::ImportedMaterial material;
                material.name = source ? source->m_name : std::string{};
                scene.materials.push_back(std::move(material));
            }

            // 애니메이션: 채널은 **노드**를 타깃한다. legacy 가 이름으로 들고
            // 있으므로 그대로 노드 인덱스로 푼다.
            if (legacySkeleton)
            {
                scene.clips.reserve(legacySkeleton->m_animations.size());
                for (const ::Animation& legacyClip : legacySkeleton->m_animations)
                {
                    im::ImportedClip clip;
                    clip.name = legacyClip.m_name;
                    const double tps = legacyClip.m_ticksPerSecond > 0.0
                        ? legacyClip.m_ticksPerSecond : 1.0;
                    clip.durationSeconds =
                        static_cast<double>(legacyClip.m_duration) / tps;
                    scene.metadata.originalTicksPerSecond = tps;

                    for (const auto& [nodeName, nodeAnim] : legacyClip.m_nodeAnimations)
                    {
                        const auto found = nodeByName.find(nodeName);
                        if (found == nodeByName.end())
                        {
                            ++report.unmatchedChannels;
                            continue;
                        }
                        im::ImportedChannel channel;
                        channel.target = im::SceneNodeIndex(found->second);
                        for (const auto& key : nodeAnim.m_positionKeys)
                        {
                            channel.translations.push_back(
                                { key.m_time / tps, ToFloat3(key.m_position) });
                        }
                        for (const auto& key : nodeAnim.m_rotationKeys)
                        {
                            channel.rotations.push_back(
                                { key.m_time / tps, ToFloat4(key.m_rotation) });
                        }
                        for (const auto& key : nodeAnim.m_scaleKeys)
                        {
                            channel.scales.push_back(
                                { key.m_time / tps,
                                  im::Float3{ key.m_scale.x, key.m_scale.y, key.m_scale.z } });
                        }
                        clip.channels.push_back(std::move(channel));
                    }
                    scene.clips.push_back(std::move(clip));
                }
            }

            return scene;
        }

        // ── 비교 (이름 기준) ────────────────────────────────────────────
        struct NamedWeight final
        {
            std::string bone{};
            float weight{};
        };

        [[nodiscard]] std::vector<NamedWeight> NamedSkin(
            const ex::Vertex& vertex, const ex::Skeleton* skeleton)
        {
            std::vector<NamedWeight> out;
            for (const ex::BoneInfluence& influence : vertex.skin)
            {
                if (!(influence.weight > 0.0f)) continue;
                if (!skeleton || !ex::IsInRange(influence.bone, skeleton->bones.size()))
                    continue;
                out.push_back({ skeleton->bones[influence.bone.Value()].name,
                    influence.weight });
            }
            std::ranges::sort(out, [](const NamedWeight& a, const NamedWeight& b)
            {
                return a.bone < b.bone;
            });
            return out;
        }

        void CompareMeshesAcrossPaths(const ex::Model& direct,
            const ex::Model& viaImport, ImportDiffLog& diff)
        {
            const auto a = direct.Meshes();
            const auto b = viaImport.Meshes();
            if (a.size() != b.size())
            {
                diff.Add("mesh 수 " + std::to_string(a.size())
                    + " vs " + std::to_string(b.size()));
                return;
            }
            const ex::Skeleton* skeletonA = direct.TryGetSkeleton();
            const ex::Skeleton* skeletonB = viaImport.TryGetSkeleton();

            for (std::size_t m = 0; m < a.size(); ++m)
            {
                const std::string where = "meshes[" + std::to_string(m) + "]";
                if (a[m].name != b[m].name) diff.Add(where + ".name 불일치");
                if (a[m].indices != b[m].indices) diff.Add(where + ".indices 불일치");
                if (a[m].vertices.size() != b[m].vertices.size())
                {
                    diff.Add(where + " 정점 수 불일치");
                    continue;
                }

                bool reportedPosition = false;
                bool reportedSkin = false;
                for (std::size_t v = 0; v < a[m].vertices.size(); ++v)
                {
                    const ex::Vertex& va = a[m].vertices[v];
                    const ex::Vertex& vb = b[m].vertices[v];
                    if (!reportedPosition
                        && (!bridge::Eq(va.position, vb.position)
                            || !bridge::Eq(va.normal, vb.normal)
                            || !bridge::Eq(va.uv0, vb.uv0)))
                    {
                        reportedPosition = true;
                        diff.Add(where + " 정점 속성 첫 불일치 at "
                            + std::to_string(v));
                    }

                    if (reportedSkin) continue;
                    const std::vector<NamedWeight> skinA = NamedSkin(va, skeletonA);
                    const std::vector<NamedWeight> skinB = NamedSkin(vb, skeletonB);
                    bool same = skinA.size() == skinB.size();
                    for (std::size_t i = 0; same && i < skinA.size(); ++i)
                    {
                        same = skinA[i].bone == skinB[i].bone
                            && std::abs(skinA[i].weight - skinB[i].weight) <= WeightEpsilon;
                    }
                    if (!same)
                    {
                        reportedSkin = true;
                        diff.Add(where + " 스킨(이름 기준) 첫 불일치 at "
                            + std::to_string(v));
                    }
                }
            }
        }
    }

    bool RunExperimentImportPathSelfTest(
        const std::string& modelPath, std::string& outLog)
    {
        outLog += "[experiment.import] 대상: " + modelPath + "\n";

        // 1. 직행 경로(기존 하네스) — 비교 기준
        const bridge::LoadedPair directPair = bridge::LoadAndBridge(modelPath);
        if (!bridge::AppendOutcome(directPair, outLog))
        {
            outLog += "  결과: 실패 (직행 경로가 기준을 만들지 못함)\n";
            return false;
        }

        // 2. legacy → ImportedScene
        SceneBuildReport buildReport;
        const im::ImportedScene scene =
            BuildImportedSceneFromLegacy(*directPair.legacy, buildReport);
        for (const std::string& failure : buildReport.failures)
            outLog += "  [bridge] " + failure + "\n";
        if (!buildReport.failures.empty())
        {
            outLog += "  결과: 실패 (ImportedScene 구성 불가)\n";
            return false;
        }

        // NonUniformScaleDropped 는 정확한 등호로 판정하므로 부동소수 잡음도
        // 걸린다. "런타임이 x 성분만 읽어 생기는 실제 손실"인지 가르려면 편차
        // 크기를 봐야 한다 — 잡음이면 1e-6 수준, 진짜면 그보다 훨씬 크다.
        float maxScaleAnisotropy = 0.0f;
        std::size_t anisotropicKeys = 0;
        for (const im::ImportedClip& clip : scene.clips)
        {
            for (const im::ImportedChannel& channel : clip.channels)
            {
                for (const ex::ScaleKey& key : channel.scales)
                {
                    const float deviation = (std::max)(
                        std::abs(key.value.x - key.value.y),
                        std::abs(key.value.y - key.value.z));
                    if (deviation > 0.0f) ++anisotropicKeys;
                    maxScaleAnisotropy = (std::max)(maxScaleAnisotropy, deviation);
                }
            }
        }
        char scaleLine[200];
        std::snprintf(scaleLine, sizeof(scaleLine),
            "  scale 비균등 편차: 최대 %.8f (비균등 키 %zu개) → %s\n",
            maxScaleAnisotropy, anisotropicKeys,
            maxScaleAnisotropy > 1e-4f ? "실제 손실 — 런타임이 x 성분만 읽는다"
                                       : "부동소수 잡음 수준");
        outLog += scaleLine;

        char buildLine[220];
        std::snprintf(buildLine, sizeof(buildLine),
            "  IR 구성: nodes %zu, meshes %zu, skins %zu, clips %zu"
            " | TRS 왕복 최대오차 %.7f, shear %zu, 이름 미매칭 bone %zu·channel %zu\n",
            scene.nodes.size(), scene.meshes.size(), scene.skins.size(),
            scene.clips.size(), buildReport.maxTrsRoundTripError,
            buildReport.shearedNodes, buildReport.unmatchedBones,
            buildReport.unmatchedChannels);
        outLog += buildLine;

        // 3. IR 자체 검증
        std::size_t irErrors = 0;
        for (const im::ImportNote& note : im::ValidateImportedScene(scene))
        {
            if (note.severity == im::ImportNoteSeverity::Error) ++irErrors;
            outLog += std::string("  [ir] ")
                + (note.severity == im::ImportNoteSeverity::Error ? "ERROR " :
                   note.severity == im::ImportNoteSeverity::Warning ? "WARN  " : "INFO  ")
                + std::string(im::ToString(note.code)) + " @ " + note.context
                + " ×" + std::to_string(note.count) + " — " + note.message + "\n";
        }

        // 4. 변환 경계 — 손실이 여기서 계수된다
        im::ConversionOptions options;
        options.modelAssetId = directPair.result.model->Metadata().assetId;
        options.modelName = directPair.result.model->Metadata().name;
        options.ticksPerSecond = scene.metadata.originalTicksPerSecond > 0.0
            ? scene.metadata.originalTicksPerSecond : 30.0;
        const im::ConversionResult converted = im::ConvertToModelDraft(scene, options);
        for (const im::ImportNote& note : converted.notes)
        {
            outLog += std::string("  [convert] ")
                + (note.severity == im::ImportNoteSeverity::Error ? "ERROR " :
                   note.severity == im::ImportNoteSeverity::Warning ? "WARN  " : "INFO  ")
                + std::string(im::ToString(note.code)) + " @ " + note.context
                + " ×" + std::to_string(note.count) + " — " + note.message + "\n";
        }
        if (!converted.Succeeded())
        {
            outLog += "  결과: 실패 (변환 경계가 draft 를 만들지 못함)\n";
            return false;
        }

        // 5. 게시
        ex::ModelLoader loader(std::make_unique<bridge::LegacyBridgeDecoder>(
            ex::ModelDraft(*converted.draft)));
        ex::ModelLoadRequest request;
        request.sourcePath = modelPath;
        request.sourcePreference = ex::ModelSourcePreference::SourceOnly;
        const ex::ModelLoadResult viaImport = loader.Load(request);
        for (const ex::ModelLoadIssue& issue : viaImport.issues)
        {
            outLog += std::string("  [validate] ")
                + (issue.severity == ex::ModelLoadIssueSeverity::Error
                    ? "ERROR " : "WARN  ")
                + std::string(bridge::ToString(issue.code)) + " @ " + issue.context
                + " — " + issue.message + "\n";
        }
        if (!viaImport.Succeeded())
        {
            outLog += "  결과: 실패 (변환 산출물이 게시 검증을 통과하지 못함)\n";
            return false;
        }

        // 6. 두 경로 비교
        const ex::Skeleton* directSkeleton = directPair.result.model->TryGetSkeleton();
        const ex::Skeleton* importSkeleton = viaImport.model->TryGetSkeleton();

        std::size_t directChannels = 0;
        std::size_t importChannels = 0;
        if (directSkeleton)
            for (const ex::AnimationClip& clip : directSkeleton->clips)
                directChannels += clip.channels.size();
        if (importSkeleton)
            for (const ex::AnimationClip& clip : importSkeleton->clips)
                importChannels += clip.channels.size();

        char summary[300];
        std::snprintf(summary, sizeof(summary),
            "  경로 비교: bone %zu → %zu, 채널 %zu → %zu (원본 node-anim 채널 %zu)\n",
            directSkeleton ? directSkeleton->bones.size() : 0,
            importSkeleton ? importSkeleton->bones.size() : 0,
            directChannels, importChannels,
            directPair.report.totalNodeAnimChannels);
        outLog += summary;

        if (directPair.report.totalNodeAnimChannels > 0)
        {
            const std::size_t directLost =
                directPair.report.totalNodeAnimChannels - directChannels;
            const std::size_t importLost =
                directPair.report.totalNodeAnimChannels - importChannels;
            outLog += "  [설계 갭] 채널 손실 직행 " + std::to_string(directLost)
                + "개 → 임포트 경로 " + std::to_string(importLost) + "개"
                + (importLost < directLost ? " (조상 폐포가 회수했다)" : "") + "\n";
        }

        ImportDiffLog diff{ outLog };
        CompareMeshesAcrossPaths(*directPair.result.model, *viaImport.model, diff);

        if (directSkeleton && importSkeleton)
        {
            std::vector<std::string> directNames, importNames;
            for (const ex::Bone& bone : directSkeleton->bones)
                directNames.push_back(bone.name);
            for (const ex::Bone& bone : importSkeleton->bones)
                importNames.push_back(bone.name);
            std::ranges::sort(directNames);
            std::ranges::sort(importNames);
            std::vector<std::string> onlyDirect;
            std::ranges::set_difference(directNames, importNames,
                std::back_inserter(onlyDirect));
            for (const std::string& name : onlyDirect)
                diff.Add("직행 경로에만 있는 bone '" + name + "'");
        }

        outLog += "  머테리얼: legacy 는 PBR semantic 을 들지 않아 두 경로가 서로 "
            "다른 표현을 만든다 — 비교 제외\n";
        outLog += "  구조 불일치: " + std::to_string(diff.count) + "건, IR 오류 "
            + std::to_string(irErrors) + "건\n";

        const bool passed = diff.count == 0 && irErrors == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패") + "\n";
        return passed;
    }
}
