#include "ExperimentParity/ExperimentModelBridgeSelfTest.h"

#include "ExperimentParity/ExperimentLegacyBridge.h"
#include "DataSystem.h"
#include "Mesh.h"
#include "Model.h"
#include "Skeleton.h"

#include <mathematics/vector3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        struct Checker final
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

        [[nodiscard]] float Lane(const math::vector4& value, std::size_t lane)
        {
            switch (lane)
            {
            case 0: return value.x;
            case 1: return value.y;
            case 2: return value.z;
            default: return value.w;
            }
        }
    }

    bool RunExperimentModelBridgeSelfTest(const std::string& modelPath,
        std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[experiment.modelbridge] 역브리지 왕복 — " + modelPath + "\n";

        bridge::LoadedPair pair = bridge::LoadAndBridge(modelPath);
        if (!pair.legacy || !pair.result.Succeeded())
        {
            outLog += "    [실패] 정브리지 로드/게시 실패\n";
            (void)bridge::AppendOutcome(pair, outLog);
            return false;
        }

        std::shared_ptr<Model> rebuilt;
        std::string error;
        if (!DataSystems->BuildLegacyModelFromExperiment(*pair.result.model,
            rebuilt, error))
        {
            outLog += "    [실패] 역브리지 시공 실패: " + error + "\n";
            return false;
        }

        Model& original = *pair.legacy;
        const bridge::BoneRemap& remap = pair.remap;

        // ── 노드 ──────────────────────────────────────────────────────────
        const auto& originalNodes = original.GetNodes();
        const auto& rebuiltNodes = rebuilt->GetNodes();
        check.Check(originalNodes.size() == rebuiltNodes.size(), "노드 수");
        std::size_t nodeMismatch = 0;
        for (std::size_t i = 0;
            i < originalNodes.size() && i < rebuiltNodes.size(); ++i)
        {
            const ModelNode& a = *originalNodes[i];
            const ModelNode& b = *rebuiltNodes[i];
            if (a.m_name != b.m_name || a.m_parentIndex != b.m_parentIndex
                || a.m_meshes != b.m_meshes
                || a.m_childrenIndex != b.m_childrenIndex
                || !bridge::Eq(a.m_transform, b.m_transform))
            {
                ++nodeMismatch;
            }
        }
        check.Check(0 == nodeMismatch, "노드 필드 왕복(이름/부모/자식/메시/변환)");

        // ── 메시·정점 ─────────────────────────────────────────────────────
        check.Check(original.GetMeshCount() == rebuilt->GetMeshCount(),
            "메시 수");
        std::size_t vertexCompared = 0;
        std::size_t vertexMismatch = 0;
        std::size_t bitangentFlips = 0;
        std::size_t bitangentNonOrthogonal = 0;
        std::size_t boneLaneMismatch = 0;
        for (std::size_t meshIndex = 0; meshIndex < original.GetMeshCount()
            && meshIndex < rebuilt->GetMeshCount(); ++meshIndex)
        {
            Mesh* a = const_cast<Model&>(original).GetMesh(
                static_cast<int>(meshIndex));
            Mesh* b = rebuilt->GetMesh(static_cast<int>(meshIndex));
            if (nullptr == a || nullptr == b)
            {
                ++vertexMismatch;
                continue;
            }
            check.Check(a->GetName() == b->GetName()
                && a->GetMaterialIndex() == b->GetMaterialIndex(),
                "메시 identity — " + a->GetName());
            const std::vector<Vertex>& va = a->GetVertices();
            const std::vector<Vertex>& vb = b->GetVertices();
            check.Check(a->GetIndices() == b->GetIndices(),
                "인덱스 버퍼 왕복 — " + a->GetName());
            if (va.size() != vb.size())
            {
                ++vertexMismatch;
                continue;
            }
            for (std::size_t v = 0; v < va.size(); ++v)
            {
                ++vertexCompared;
                const Vertex& x = va[v];
                const Vertex& y = vb[v];
                if (!bridge::Eq(x.position, y.position)
                    || !bridge::Eq(x.normal, y.normal)
                    || !bridge::Eq(x.uv0, y.uv0)
                    || !bridge::Eq(x.uv1, y.uv1)
                    || !bridge::Eq(x.tangent, y.tangent)
                    || !bridge::Eq(x.boneWeights, y.boneWeights))
                {
                    ++vertexMismatch;
                    continue;
                }
                // bitangent — 계약은 handedness(부호) 보존이다. experiment는
                // w 한 비트만 저장하므로 비직교 원본 성분은 원리적으로 왕복
                // 불가(관측 계수로만 남긴다). 부호 = sign(dot(cross(n,t), b)).
                const math::vector3 orthogonal =
                    math::cross(x.normal, x.tangent);
                const float originalSign =
                    math::dot(orthogonal, x.bitangent);
                const float rebuiltSign =
                    math::dot(orthogonal, y.bitangent);
                if (std::fabs(originalSign) > 1e-8f
                    && std::fabs(rebuiltSign) > 1e-8f
                    && (originalSign < 0.0f) != (rebuiltSign < 0.0f))
                {
                    ++bitangentFlips;
                }
                const float lengthProduct = std::sqrt(
                    math::dot(x.bitangent, x.bitangent)
                    * math::dot(y.bitangent, y.bitangent));
                if (lengthProduct > 1e-8f
                    && math::dot(x.bitangent, y.bitangent) / lengthProduct
                        < 0.999f)
                {
                    ++bitangentNonOrthogonal;
                }
                // 본 lane — 정브리지 위상 정렬 remap을 통과시켜 비교한다.
                for (std::size_t lane = 0; lane < 4; ++lane)
                {
                    const float weight = Lane(x.boneWeights, lane);
                    if (weight <= 0.0f) continue;
                    const std::size_t originalIndex =
                        static_cast<std::size_t>(Lane(x.boneIndices, lane));
                    const std::size_t expected = remap.Empty()
                        ? originalIndex
                        : (originalIndex < remap.oldToNew.size()
                            ? remap.oldToNew[originalIndex] : originalIndex);
                    if (static_cast<std::size_t>(Lane(y.boneIndices, lane))
                        != expected)
                    {
                        ++boneLaneMismatch;
                    }
                }
            }
        }
        check.Check(0 == vertexMismatch,
            "정점 필드 왕복(pos/normal/uv0/uv1/tangent/weight)");
        check.Check(0 == bitangentFlips, "bitangent 방향 보존(handedness)");
        check.Check(0 == boneLaneMismatch, "본 lane 왕복(remap 반영)");

        // ── 스켈레톤·애니메이션 ───────────────────────────────────────────
        check.Check(original.m_hasBones == rebuilt->m_hasBones,
            "스켈레톤 유무");
        if (original.m_hasBones && rebuilt->m_hasBones)
        {
            const Skeleton& sa = *original.m_Skeleton;
            const Skeleton& sb = *rebuilt->m_Skeleton;
            check.Check(sa.m_bones.size() == sb.m_bones.size(), "본 수");
            std::size_t boneMismatch = 0;
            for (std::size_t i = 0; i < sb.m_bones.size(); ++i)
            {
                const Bone& b = *sb.m_bones[i];
                const std::size_t originalIndex = remap.Empty()
                    ? i : (i < remap.newToOld.size()
                        ? remap.newToOld[i] : i);
                if (originalIndex >= sa.m_bones.size())
                {
                    ++boneMismatch;
                    continue;
                }
                const Bone& a = *sa.m_bones[originalIndex];
                if (a.m_name != b.m_name || !bridge::Eq(a.m_offset, b.m_offset))
                {
                    ++boneMismatch;
                }
            }
            check.Check(0 == boneMismatch, "본 이름/offset 왕복(remap 반영)");
            check.Check(nullptr != sb.m_rootBone
                && nullptr != sa.m_rootBone
                && sa.m_rootBone->m_name == sb.m_rootBone->m_name,
                "루트 본 일치");
            check.Check(bridge::Eq(sa.m_rootTransform, sb.m_rootTransform)
                && bridge::Eq(sa.m_globalInverseTransform,
                    sb.m_globalInverseTransform),
                "스켈레톤 변환 왕복");

            check.Check(sa.m_animations.size() == sb.m_animations.size(),
                "클립 수");
            std::size_t clipMismatch = 0;
            std::size_t channelMissing = 0;
            std::size_t keyMismatch = 0;
            for (std::size_t i = 0; i < sa.m_animations.size()
                && i < sb.m_animations.size(); ++i)
            {
                const Animation& a = sa.m_animations[i];
                const Animation& b = sb.m_animations[i];
                if (a.m_name != b.m_name || a.m_isLoop != b.m_isLoop
                    || a.m_ticksPerSecond != b.m_ticksPerSecond
                    || std::fabs(a.m_duration - b.m_duration) > 1e-4f)
                {
                    ++clipMismatch;
                }
                for (const auto& [nodeName, nodeAnim] : b.m_nodeAnimations)
                {
                    const auto found = a.m_nodeAnimations.find(nodeName);
                    if (found == a.m_nodeAnimations.end())
                    {
                        ++channelMissing;
                        continue;
                    }
                    const NodeAnimation& originalAnim = found->second;
                    if (originalAnim.m_positionKeys.size()
                            != nodeAnim.m_positionKeys.size()
                        || originalAnim.m_rotationKeys.size()
                            != nodeAnim.m_rotationKeys.size()
                        || originalAnim.m_scaleKeys.size()
                            != nodeAnim.m_scaleKeys.size())
                    {
                        ++keyMismatch;
                    }
                }
                // 역방향 결손(원본 채널 중 왕복에서 빠진 것)은 정브리지의
                // dropped(본 없는 node-anim) 계수와 일치해야 한다 — 아래 총량
                // 단정이 잰다.
            }
            check.Check(0 == clipMismatch, "클립 identity/loop/tps/duration");
            check.Check(0 == channelMissing, "왕복 채널이 원본에 전부 존재");
            check.Check(0 == keyMismatch, "채널 키 수 왕복");

            std::size_t originalChannels = 0;
            std::size_t rebuiltChannels = 0;
            for (const Animation& animation : sa.m_animations)
            {
                originalChannels += animation.m_nodeAnimations.size();
            }
            for (const Animation& animation : sb.m_animations)
            {
                rebuiltChannels += animation.m_nodeAnimations.size();
            }
            check.Check(originalChannels
                == rebuiltChannels + pair.report.droppedNodeAnimChannels,
                "채널 총량 = 왕복 + 정브리지 dropped(본 없는 node-anim)");
        }

        // ── 재질(느슨 대조 — 값 정본은 matmigrate가 잰다) ─────────────────
        check.Check(original.GetMaterialCount() == rebuilt->GetMaterialCount(),
            "재질 수");

        char summary[260]{};
        std::snprintf(summary, sizeof(summary),
            "  왕복 단정 %zu/%zu · 정점 %zu · 비직교 bitangent %zu(관측)"
            " · dropped 채널 %zu\n",
            check.passed, check.passed + check.failed, vertexCompared,
            bitangentNonOrthogonal, pair.report.droppedNodeAnimChannels);
        outLog += summary;
        return check.failed == 0u;
    }
}
