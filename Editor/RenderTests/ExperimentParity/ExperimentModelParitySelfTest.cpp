#include "ExperimentParity/ExperimentModelParitySelfTest.h"
#include "ExperimentParity/ExperimentLegacyBridge.h"

#include "Model.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "Material.h"

#include <cstddef>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        using bridge::BoneRemap;
        using bridge::ConvertVertex;
        using bridge::Eq;

        constexpr std::size_t MaxDiffLines = 30;

        struct DiffLog final
        {
            std::string& log;
            std::size_t count{};

            void Add(std::string text)
            {
                ++count;
                if (count <= MaxDiffLines)
                    log += "  [diff] " + std::move(text) + "\n";
                else if (count == MaxDiffLines + 1)
                    log += "  [diff] ... 이후 생략\n";
            }
        };

        void CompareNodes(::Model& legacy, const ex::Model& model, DiffLog& diff)
        {
            const auto& legacyNodes = legacy.GetNodes();
            const auto nodes = model.Nodes();
            if (legacyNodes.size() != nodes.size())
            {
                diff.Add("node 수 " + std::to_string(legacyNodes.size())
                    + " vs " + std::to_string(nodes.size()));
                return;
            }
            for (std::size_t i = 0; i < nodes.size(); ++i)
            {
                const ::ModelNode& a = *legacyNodes[i];
                const ex::ModelNode& b = nodes[i];
                const std::string where = "nodes[" + std::to_string(i) + "]";
                if (a.m_name != b.name)
                    diff.Add(where + ".name '" + a.m_name + "' vs '" + b.name + "'");
                const bool expectRoot = (i == 0);
                if (expectRoot != !b.parent.IsValid()
                    || (!expectRoot && b.parent.Value() != a.m_parentIndex))
                {
                    diff.Add(where + ".parent 불일치");
                }
                if (!Eq(a.m_transform, b.localTransform))
                    diff.Add(where + ".localTransform 불일치");
                bool meshListEqual = a.m_meshes.size() == b.meshes.size();
                for (std::size_t m = 0; meshListEqual && m < b.meshes.size(); ++m)
                    meshListEqual = a.m_meshes[m] == b.meshes[m].Value();
                if (!meshListEqual)
                    diff.Add(where + ".meshes 목록 불일치");
            }
        }

        void CompareMeshes(::Model& legacy, const ex::Model& model,
            const BoneRemap& remap, DiffLog& diff)
        {
            const auto meshes = model.Meshes();
            if (legacy.GetMeshCount() != meshes.size())
            {
                diff.Add("mesh 수 " + std::to_string(legacy.GetMeshCount())
                    + " vs " + std::to_string(meshes.size()));
                return;
            }
            for (std::size_t i = 0; i < meshes.size(); ++i)
            {
                ::Mesh& a = *legacy.GetMesh(static_cast<int>(i));
                const ex::Mesh& b = meshes[i];
                const std::string where = "meshes[" + std::to_string(i) + "]";
                if (a.GetName() != b.name)
                    diff.Add(where + ".name 불일치");
                if (!b.material.IsValid() || a.GetMaterialIndex() != b.material.Value())
                    diff.Add(where + ".material 불일치");
                if (a.GetVertices().size() != b.vertices.size())
                {
                    diff.Add(where + " 정점 수 불일치");
                    continue;
                }
                if (a.GetIndices() != b.indices)
                    diff.Add(where + " index 배열 불일치");
                for (std::size_t v = 0; v < b.vertices.size(); ++v)
                {
                    if (!Eq(ConvertVertex(a.GetVertices()[v], remap), b.vertices[v]))
                    {
                        diff.Add(where + " 정점 값 첫 불일치 at "
                            + std::to_string(v));
                        break;
                    }
                }
            }
        }

        void CompareMaterials(::Model& legacy, const ex::Model& model, DiffLog& diff)
        {
            const auto materials = model.Materials();
            if (legacy.GetMaterialCount() != materials.size())
            {
                diff.Add("material 수 " + std::to_string(legacy.GetMaterialCount())
                    + " vs " + std::to_string(materials.size()));
                return;
            }
            for (std::size_t i = 0; i < materials.size(); ++i)
            {
                const ::Material& a = *legacy.GetMaterial(static_cast<int>(i));
                const ex::Material& b = materials[i];
                const std::string where = "materials[" + std::to_string(i) + "]";
                if (a.m_name != b.name)
                    diff.Add(where + ".name 불일치");
                if (!(a.m_fileGuid.m_guid == b.assetId.value)
                    && b.assetId.IsValid())
                {
                    diff.Add(where + ".assetId 불일치");
                }
                if (!(a.m_shaderMetaGuid.m_guid == b.shaderAssetId.value)
                    && b.shaderAssetId.IsValid())
                {
                    diff.Add(where + ".shaderAssetId 불일치");
                }

                const bool legacyTransparent =
                    a.m_renderingMode == MaterialRenderingMode::Transparent;
                if (legacyTransparent
                    != (b.blendMode == ex::MaterialBlendMode::Transparent))
                {
                    diff.Add(where + ".blendMode 불일치");
                }

                bool selectionsEqual =
                    a.m_keywordSelections.size() == b.keywordSelections.size();
                for (std::size_t k = 0;
                    selectionsEqual && k < b.keywordSelections.size(); ++k)
                {
                    selectionsEqual =
                        a.m_keywordSelections[k] == b.keywordSelections[k];
                }
                if (!selectionsEqual)
                    diff.Add(where + ".keywordSelections 불일치");

                if (a.m_propertyValues.size() != b.properties.size())
                {
                    diff.Add(where + " property 수 "
                        + std::to_string(a.m_propertyValues.size())
                        + " vs " + std::to_string(b.properties.size()));
                    continue;
                }
                for (std::size_t p = 0; p < b.properties.size(); ++p)
                {
                    const ex::MaterialProperty expected =
                        bridge::ConvertMaterialProperty(a.m_propertyValues[p]);
                    const ex::MaterialProperty& actual = b.properties[p];
                    if (expected.name != actual.name)
                    {
                        diff.Add(where + ".properties[" + std::to_string(p)
                            + "] 이름 '" + expected.name + "' vs '"
                            + actual.name + "'");
                        continue;
                    }
                    if (!Eq(expected.value, actual.value))
                    {
                        diff.Add(where + ".properties." + expected.name
                            + " 값 불일치");
                    }
                }
            }
        }

        void CompareSkeleton(::Model& legacy, const ex::Model& model,
            const BoneRemap& remap, DiffLog& diff)
        {
            ::Skeleton* a = legacy.m_Skeleton;
            const ex::Skeleton* b = model.TryGetSkeleton();
            if ((a != nullptr) != (b != nullptr))
            {
                diff.Add("skeleton 존재 여부 불일치");
                return;
            }
            if (!a) return;

            if (a->m_bones.size() != b->bones.size())
            {
                diff.Add("bone 수 " + std::to_string(a->m_bones.size())
                    + " vs " + std::to_string(b->bones.size()));
                return;
            }
            for (std::size_t oldIndex = 0; oldIndex < b->bones.size(); ++oldIndex)
            {
                const ::Bone& la = *a->m_bones[oldIndex];
                const std::size_t newIndex = remap.Empty()
                    ? oldIndex : remap.oldToNew[oldIndex];
                const ex::Bone& lb = b->bones[newIndex];
                const std::string where = "bones[old " + std::to_string(oldIndex) + "]";
                if (la.m_name != lb.name)
                    diff.Add(where + ".name 불일치");
                if (!Eq(la.m_offset, lb.inverseBindMatrix))
                    diff.Add(where + ".inverseBindMatrix 불일치");

                const bool isRoot = (&la == a->m_rootBone)
                    || la.m_parentIndex < 0
                    || la.m_parentIndex == static_cast<int>(oldIndex);
                const bool parentMatches = isRoot
                    ? !lb.parent.IsValid()
                    : (lb.parent.IsValid() && !remap.Empty()
                        && lb.parent.Value() == remap.oldToNew[
                            static_cast<std::size_t>(la.m_parentIndex)]);
                if (!parentMatches)
                    diff.Add(where + ".parent 사상 불일치");
            }

            if (a->m_animations.size() != b->clips.size())
            {
                diff.Add("clip 수 " + std::to_string(a->m_animations.size())
                    + " vs " + std::to_string(b->clips.size()));
                return;
            }
            for (std::size_t i = 0; i < b->clips.size(); ++i)
            {
                const ::Animation& la = a->m_animations[i];
                const ex::AnimationClip& lb = b->clips[i];
                const std::string where = "clips[" + std::to_string(i) + "]";
                if (la.m_name != lb.name)
                    diff.Add(where + ".name 불일치");
                if (static_cast<double>(la.m_duration) != lb.durationTicks
                    || la.m_ticksPerSecond != lb.ticksPerSecond
                    || la.m_isLoop != lb.looping)
                {
                    diff.Add(where + " duration/tps/loop 불일치");
                }

                // bone 에 매핑된 채널만 게시본에 남는다 — 그 수를 잰다.
                std::size_t mappable = 0;
                for (const auto& [nodeName, nodeAnim] : la.m_nodeAnimations)
                {
                    if (a->FindBone(nodeName)) ++mappable;
                }
                if (mappable != lb.channels.size())
                {
                    diff.Add(where + " 매핑 가능 채널 수 " + std::to_string(mappable)
                        + " vs 게시 " + std::to_string(lb.channels.size()));
                }
            }
        }

        std::size_t CountLegacyMeshVertices(::Model& legacy)
        {
            std::size_t total = 0;
            for (std::size_t i = 0; i < legacy.GetMeshCount(); ++i)
                total += legacy.GetMesh(static_cast<int>(i))->GetVertices().size();
            return total;
        }
    }

    bool RunExperimentModelParitySelfTest(
        const std::string& modelPath, std::string& outLog)
    {
        outLog += "[experiment.model] 대상: " + modelPath + "\n";

        const bridge::LoadedPair pair = bridge::LoadAndBridge(modelPath);
        if (pair.legacy)
        {
            ::Model& legacy = *pair.legacy;
            outLog += "  legacy 로드: nodes " + std::to_string(legacy.GetNodes().size())
                + ", meshes " + std::to_string(legacy.GetMeshCount())
                + ", materials " + std::to_string(legacy.GetMaterialCount())
                + ", vertices " + std::to_string(CountLegacyMeshVertices(legacy))
                + (legacy.m_Skeleton
                    ? ", bones " + std::to_string(legacy.m_Skeleton->m_bones.size())
                      + ", clips " + std::to_string(legacy.m_Skeleton->m_animations.size())
                    : std::string(", skeleton 없음"))
                + "\n";
        }

        if (!bridge::AppendOutcome(pair, outLog))
        {
            outLog += pair.report.failures.empty()
                ? "  결과: 실패 (Experiment 검증이 게시를 차단)\n"
                : "  결과: 실패 (브리지 불가 구조)\n";
            return false;
        }

        DiffLog diff{ outLog };
        CompareNodes(*pair.legacy, *pair.result.model, diff);
        CompareMeshes(*pair.legacy, *pair.result.model, pair.remap, diff);
        CompareMaterials(*pair.legacy, *pair.result.model, diff);
        CompareSkeleton(*pair.legacy, *pair.result.model, pair.remap, diff);

        std::size_t propertyTotal = 0;
        for (const ex::Material& material : pair.result.model->Materials())
            propertyTotal += material.properties.size();
        outLog += "  material property 브리지: " + std::to_string(propertyTotal)
            + "개 (material " + std::to_string(pair.result.model->Materials().size())
            + "개)\n";

        if (pair.report.totalNodeAnimChannels > 0)
        {
            outLog += "  [설계 갭 실측] node 애니메이션 채널 "
                + std::to_string(pair.report.totalNodeAnimChannels) + "개 중 "
                + std::to_string(pair.report.droppedNodeAnimChannels)
                + "개가 bone 미매핑으로 탈락\n";
        }

        outLog += "  구조 불일치: " + std::to_string(diff.count) + "건\n";
        const bool passed = diff.count == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패") + "\n";
        return passed;
    }
}
