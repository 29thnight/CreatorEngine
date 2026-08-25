#include "ExperimentParity/ExperimentLegacyBridge.h"

#include "Model.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "AnimatorData.h"
#include "Material.h"
#include "Uuid.h"

#include <cstring>
#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace RenderTest::bridge
{
    namespace ex = experiment;

    math::matrix4x4 ToMatrix4(const DirectX::XMFLOAT4X4& source)
    {
        math::matrix4x4 out;
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                out.m[row][column] = source.m[row][column];
        return out;
    }

    math::matrix4x4 ToMatrix4(DirectX::FXMMATRIX source)
    {
        DirectX::XMFLOAT4X4 stored;
        DirectX::XMStoreFloat4x4(&stored, source);
        return ToMatrix4(stored);
    }

    DirectX::XMMATRIX ToXMMatrix(const math::matrix4x4& source)
    {
        DirectX::XMFLOAT4X4 stored;
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t column = 0; column < 4; ++column)
                stored.m[row][column] = source.m[row][column];
        return DirectX::XMLoadFloat4x4(&stored);
    }

    namespace
    {
        math::vector3 ToFloat3(const Mathf::Vector3& v) { return { v.x, v.y, v.z }; }

        math::vector3 ToFloat3(DirectX::FXMVECTOR v)
        {
            DirectX::XMFLOAT3 stored;
            DirectX::XMStoreFloat3(&stored, v);
            return { stored.x, stored.y, stored.z };
        }

        math::vector4 ToFloat4(DirectX::FXMVECTOR v)
        {
            DirectX::XMFLOAT4 stored;
            DirectX::XMStoreFloat4(&stored, v);
            return { stored.x, stored.y, stored.z, stored.w };
        }

        void BridgeSkeleton(::Skeleton& legacySkeleton, const BoneRemap& remap,
            ex::ModelDraft& draft, BridgeReport& report)
        {
            ex::Skeleton out;
            out.rootTransform = ToMatrix4(legacySkeleton.m_rootTransform);
            out.globalInverseTransform =
                ToMatrix4(legacySkeleton.m_globalInverseTransform);

            const std::size_t boneCount = legacySkeleton.m_bones.size();
            out.bones.reserve(boneCount);
            for (std::size_t newIndex = 0; newIndex < boneCount; ++newIndex)
            {
                const std::size_t oldIndex = remap.newToOld[newIndex];
                const ::Bone& legacyBone = *legacySkeleton.m_bones[oldIndex];
                const bool isRoot =
                    (&legacyBone == legacySkeleton.m_rootBone)
                    || legacyBone.m_parentIndex < 0
                    || legacyBone.m_parentIndex == static_cast<int>(oldIndex);
                if (&legacyBone == legacySkeleton.m_rootBone)
                {
                    out.rootBone = ex::BoneIndex(
                        static_cast<ex::BoneIndex::value_type>(newIndex));
                }

                ex::Bone bone;
                bone.name = legacyBone.m_name;
                if (!isRoot)
                {
                    bone.parent = ex::BoneIndex(remap.oldToNew[
                        static_cast<std::size_t>(legacyBone.m_parentIndex)]);
                }
                bone.inverseBindMatrix = ToMatrix4(legacyBone.m_offset);
                out.bones.push_back(std::move(bone));
            }
            if (legacySkeleton.m_rootBone && !out.rootBone.IsValid())
            {
                report.failures.push_back("m_rootBone 이 m_bones 목록에 없음");
                return;
            }

            out.clips.reserve(legacySkeleton.m_animations.size());
            for (::Animation& legacyClip : legacySkeleton.m_animations)
            {
                ex::AnimationClip clip;
                clip.name = legacyClip.m_name;
                clip.durationTicks = static_cast<double>(legacyClip.m_duration);
                clip.ticksPerSecond = legacyClip.m_ticksPerSecond;
                clip.looping = legacyClip.m_isLoop;

                for (const auto& [nodeName, nodeAnim] : legacyClip.m_nodeAnimations)
                {
                    ++report.totalNodeAnimChannels;
                    // m_boneMap 은 cooked 로드 경로에서 채워지지 않는 죽은 필드다.
                    // 런타임이 실제로 쓰는 사상(FindBone: m_bones 정확 이름 탐색)을
                    // 그대로 따라야 legacy 배선과 같은 의미의 매핑이 된다.
                    const ::Bone* found = legacySkeleton.FindBone(nodeName);
                    if (!found)
                    {
                        // Experiment 데이터 모델은 bone 대상 채널만 표현한다.
                        // 여기 떨어지는 수가 곧 설계 갭의 실측치다.
                        ++report.droppedNodeAnimChannels;
                        continue;
                    }

                    ex::AnimationChannel channel;
                    channel.bone = ex::BoneIndex(remap.oldToNew[
                        static_cast<std::size_t>(found->m_index)]);
                    channel.translations.reserve(nodeAnim.m_positionKeys.size());
                    for (const auto& key : nodeAnim.m_positionKeys)
                        channel.translations.push_back({ key.m_time, ToFloat3(key.m_position) });
                    channel.rotations.reserve(nodeAnim.m_rotationKeys.size());
                    for (const auto& key : nodeAnim.m_rotationKeys)
                        channel.rotations.push_back({ key.m_time, ToFloat4(key.m_rotation) });
                    channel.scales.reserve(nodeAnim.m_scaleKeys.size());
                    for (const auto& key : nodeAnim.m_scaleKeys)
                        channel.scales.push_back({ key.m_time, ToFloat3(key.m_scale) });
                    clip.channels.push_back(std::move(channel));
                }
                out.clips.push_back(std::move(clip));
            }

            draft.skeleton = std::move(out);
        }
    }

    BoneRemap ComputeBoneRemap(
        const ::Skeleton& legacySkeleton, BridgeReport& report)
    {
        BoneRemap remap;
        const std::size_t boneCount = legacySkeleton.m_bones.size();

        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            const ::Bone* bone = legacySkeleton.m_bones[boneIndex];
            if (!bone)
            {
                report.failures.push_back(
                    "skeleton.m_bones[" + std::to_string(boneIndex) + "] 가 null");
                return remap;
            }
            if (bone->m_index != static_cast<int>(boneIndex))
            {
                report.failures.push_back(
                    "bone m_index(" + std::to_string(bone->m_index)
                    + ")가 배열 위치(" + std::to_string(boneIndex) + ")와 다름");
                return remap;
            }
        }

        // 루트 취급: m_rootBone 이거나, parent 가 음수/자기 자신인 bone.
        const auto parentOf = [&](std::size_t index) -> int
        {
            const ::Bone* bone = legacySkeleton.m_bones[index];
            if (bone == legacySkeleton.m_rootBone) return -1;
            const int parent = bone->m_parentIndex;
            if (parent < 0 || parent == static_cast<int>(index)) return -1;
            return parent;
        };

        std::vector<std::uint32_t> depth(boneCount, 0);
        for (std::size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
        {
            std::size_t steps = 0;
            int cursor = static_cast<int>(boneIndex);
            while (true)
            {
                const int parent = parentOf(static_cast<std::size_t>(cursor));
                if (parent < 0) break;
                if (parent >= static_cast<int>(boneCount))
                {
                    report.failures.push_back(
                        "bones[" + std::to_string(boneIndex)
                        + "] parent 사슬이 범위를 벗어남");
                    return remap;
                }
                if (++steps > boneCount)
                {
                    report.failures.push_back(
                        "bones[" + std::to_string(boneIndex) + "] parent 사슬 순환");
                    return remap;
                }
                cursor = parent;
            }
            depth[boneIndex] = static_cast<std::uint32_t>(steps);
        }

        remap.newToOld.resize(boneCount);
        for (std::size_t i = 0; i < boneCount; ++i)
            remap.newToOld[i] = static_cast<std::uint32_t>(i);
        std::stable_sort(remap.newToOld.begin(), remap.newToOld.end(),
            [&](std::uint32_t a, std::uint32_t b) { return depth[a] < depth[b]; });

        remap.oldToNew.resize(boneCount);
        for (std::size_t newIndex = 0; newIndex < boneCount; ++newIndex)
        {
            remap.oldToNew[remap.newToOld[newIndex]] =
                static_cast<std::uint32_t>(newIndex);
            if (remap.newToOld[newIndex] != newIndex) remap.reordered = true;
        }
        if (remap.reordered)
        {
            report.notes.push_back(
                "legacy bone 배열이 위상 정렬돼 있지 않아 재배열함");
        }
        return remap;
    }

    ex::Vertex ConvertVertex(const ::Vertex& source, const BoneRemap& remap)
    {
        ex::Vertex out;
        out.position  = { source.position.x,  source.position.y,  source.position.z };
        out.normal    = { source.normal.x,    source.normal.y,    source.normal.z };
        out.uv0       = { source.uv0.x,       source.uv0.y };
        out.uv1       = { source.uv1.x,       source.uv1.y };
        out.tangent   = { source.tangent.x,   source.tangent.y,   source.tangent.z };
        out.bitangent = { source.bitangent.x, source.bitangent.y, source.bitangent.z };

        const float weights[4] = {
            source.boneWeights.x, source.boneWeights.y,
            source.boneWeights.z, source.boneWeights.w };
        const float indices[4] = {
            source.boneIndices.x, source.boneIndices.y,
            source.boneIndices.z, source.boneIndices.w };
        for (std::size_t slot = 0; slot < 4; ++slot)
        {
            if (weights[slot] == 0.0f) continue;
            ex::BoneInfluence influence;
            influence.weight = weights[slot];
            // 음수·비유한 index 는 invalid 로 두어 Experiment 검증이 잡게 한다.
            if (indices[slot] >= 0.0f
                && indices[slot] < static_cast<float>(ex::BoneIndex::InvalidValue))
            {
                const auto oldIndex =
                    static_cast<ex::BoneIndex::value_type>(indices[slot]);
                if (remap.Empty())
                {
                    influence.bone = ex::BoneIndex(oldIndex);
                }
                else if (oldIndex < remap.oldToNew.size())
                {
                    influence.bone = ex::BoneIndex(remap.oldToNew[oldIndex]);
                }
            }
            out.skin[slot] = influence;
        }
        return out;
    }

    ex::ModelDraft BuildDraftFromLegacy(
        ::Model& legacy, const BoneRemap& remap, BridgeReport& report)
    {
        ex::ModelDraft draft;

        draft.metadata.assetId.value = legacy.guid.m_guid;
        if (!draft.metadata.assetId.IsValid())
        {
            // .meta 없는 자산. 정체성 자체가 배선 차이는 아니므로 경로에서
            // 결정론적으로 합성하고 기록만 남긴다.
            draft.metadata.assetId.value = Uuid::FromName(
                FileGuid::ns_filesystem(), legacy.path.string());
            report.notes.push_back("legacy guid 가 nil — 경로 기반 v5 UUID 합성");
        }
        draft.metadata.name = legacy.name;
        if (draft.metadata.name.empty())
        {
            draft.metadata.name = legacy.path.stem().string();
            report.notes.push_back("legacy 이름이 비어 있어 파일명으로 대체");
        }
        draft.metadata.sourcePath = legacy.path;
        draft.metadata.payloadKind = ex::ModelPayloadKind::SourceImport;

        const auto& legacyNodes = legacy.GetNodes();
        draft.nodes.reserve(legacyNodes.size());
        for (std::size_t nodeIndex = 0; nodeIndex < legacyNodes.size(); ++nodeIndex)
        {
            const ::ModelNode* legacyNode = legacyNodes[nodeIndex];
            if (!legacyNode)
            {
                report.failures.push_back(
                    "m_nodes[" + std::to_string(nodeIndex) + "] 가 null");
                return draft;
            }
            if (legacyNode->m_index != static_cast<uint32>(nodeIndex))
            {
                report.failures.push_back(
                    "node m_index(" + std::to_string(legacyNode->m_index)
                    + ")가 배열 위치(" + std::to_string(nodeIndex) + ")와 다름");
                return draft;
            }

            ex::ModelNode node;
            node.name = legacyNode->m_name;
            // legacy 는 루트가 m_parentIndex:0(자기 참조)으로 저장된다.
            // 배열 첫 노드만 루트로 간주한다 — 위반은 Experiment 검증이 잡는다.
            if (nodeIndex != 0)
            {
                node.parent = ex::NodeIndex(legacyNode->m_parentIndex);
            }
            node.localTransform = ToMatrix4(legacyNode->m_transform);
            node.meshes.reserve(legacyNode->m_meshes.size());
            for (uint32 meshIndex : legacyNode->m_meshes)
                node.meshes.push_back(ex::MeshIndex(meshIndex));
            draft.nodes.push_back(std::move(node));
        }

        const std::size_t meshCount = legacy.GetMeshCount();
        draft.meshes.reserve(meshCount);
        for (std::size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex)
        {
            ::Mesh* legacyMesh = legacy.GetMesh(static_cast<int>(meshIndex));
            if (!legacyMesh)
            {
                report.failures.push_back(
                    "meshes[" + std::to_string(meshIndex) + "] 가 null");
                return draft;
            }

            ex::Mesh mesh;
            mesh.name = legacyMesh->GetName();
            mesh.material = ex::MaterialIndex(legacyMesh->GetMaterialIndex());
            const std::vector<::Vertex>& vertices = legacyMesh->GetVertices();
            mesh.vertices.reserve(vertices.size());
            for (const ::Vertex& vertex : vertices)
                mesh.vertices.push_back(ConvertVertex(vertex, remap));
            mesh.indices = legacyMesh->GetIndices();

            // legacy BoundingBox 도 center/extents 라 이제 그대로 옮긴다.
            // 예전에는 여기서 min/max 로 폈다가 검증에서 다시 비교했다.
            const DirectX::BoundingBox box = legacyMesh->GetBoundingBox();
            mesh.bounds = math::aabb{
                math::vector3{ box.Center.x, box.Center.y, box.Center.z },
                math::vector3{ box.Extents.x, box.Extents.y, box.Extents.z } };
            draft.meshes.push_back(std::move(mesh));
        }

        const std::size_t materialCount = legacy.GetMaterialCount();
        draft.materials.reserve(materialCount);
        for (std::size_t materialIndex = 0;
            materialIndex < materialCount; ++materialIndex)
        {
            ::Material* legacyMaterial =
                legacy.GetMaterial(static_cast<int>(materialIndex));
            if (!legacyMaterial)
            {
                report.failures.push_back(
                    "materials[" + std::to_string(materialIndex) + "] 가 null");
                return draft;
            }
            ex::Material material;
            material.name = legacyMaterial->m_name;
            material.assetId.value = legacyMaterial->m_fileGuid.m_guid;
            material.shaderAssetId.value = legacyMaterial->m_shaderMetaGuid.m_guid;
            material.blendMode =
                legacyMaterial->m_renderingMode == MaterialRenderingMode::Transparent
                ? ex::MaterialBlendMode::Transparent
                : ex::MaterialBlendMode::Opaque;
            material.properties.reserve(legacyMaterial->m_propertyValues.size());
            for (const ::MaterialPropertyValue& property :
                legacyMaterial->m_propertyValues)
            {
                material.properties.push_back(ConvertMaterialProperty(property));
            }
            material.keywordSelections.assign(
                legacyMaterial->m_keywordSelections.begin(),
                legacyMaterial->m_keywordSelections.end());
            // 이름 기반 keywords는 ShaderMeta 해석이 필요해 브리지가 채우지
            // 않는다(선택 인덱스가 디스크 정본). m_materialInfo/m_flowInfo 등
            // M6 은퇴 예정 호환 표면도 브리지하지 않는다.
            draft.materials.push_back(std::move(material));
        }

        if (legacy.m_Skeleton)
        {
            BridgeSkeleton(*legacy.m_Skeleton, remap, draft, report);
            if (!report.failures.empty()) return draft;
        }

        if (legacy.m_animator)
        {
            ex::AnimatorData animator;
            animator.motionAssetId.value = legacy.m_animator->m_Motion.m_guid;
            draft.animator = animator;
        }

        return draft;
    }

    ex::MaterialProperty ConvertMaterialProperty(
        const ::MaterialPropertyValue& source)
    {
        ex::MaterialProperty out;
        out.name = source.m_name;

        if (!source.m_textureGuid.m_guid.IsNil())
        {
            ex::TextureReference texture;
            texture.assetId.value = source.m_textureGuid.m_guid;
            texture.logicalName = source.m_name;
            // color space 는 ShaderMeta 가 알고 있는 정보다 — 브리지는 기본값을
            // 둔다. 실디코더/어댑터는 meta 로 확정해야 한다.
            out.value = std::move(texture);
            return out;
        }

        switch (source.m_numericValue.size())
        {
        case 1:
            out.value = source.m_numericValue[0];
            return out;
        case 2:
            out.value = math::vector2{
                source.m_numericValue[0], source.m_numericValue[1] };
            return out;
        case 3:
            out.value = math::vector3{
                source.m_numericValue[0], source.m_numericValue[1],
                source.m_numericValue[2] };
            return out;
        case 4:
            out.value = math::vector4{
                source.m_numericValue[0], source.m_numericValue[1],
                source.m_numericValue[2], source.m_numericValue[3] };
            return out;
        case 0:
            break;
        default:
            // variant 가 float[N>4] 를 표현하지 못한다. 조용한 절단 대신
            // marker 를 남겨 diff/검증 로그에서 보이게 한다.
            out.value = "unrepresented:float["
                + std::to_string(source.m_numericValue.size()) + "]";
            return out;
        }

        // numeric 도 texture 도 없으면 int/bool 인데, meta 없이는 구분이
        // 안 된다. 결정론 규칙: 비0 int → int32, true → bool, 그 외 int32(0).
        if (source.m_integerValue != 0)
        {
            out.value = source.m_integerValue;
        }
        else if (source.m_boolValue)
        {
            out.value = true;
        }
        else
        {
            out.value = std::int32_t{ 0 };
        }
        return out;
    }

    std::string_view ToString(ex::ModelLoadIssueCode code)
    {
        using Code = ex::ModelLoadIssueCode;
        switch (code)
        {
        case Code::MissingDecoder:            return "MissingDecoder";
        case Code::EmptyRequestPath:          return "EmptyRequestPath";
        case Code::InvalidImportOptions:      return "InvalidImportOptions";
        case Code::DecoderFailure:            return "DecoderFailure";
        case Code::MissingDraft:              return "MissingDraft";
        case Code::InvalidAssetIdentity:      return "InvalidAssetIdentity";
        case Code::EmptyModelName:            return "EmptyModelName";
        case Code::MissingNodes:              return "MissingNodes";
        case Code::InvalidNodeHierarchy:      return "InvalidNodeHierarchy";
        case Code::InvalidNodeMesh:           return "InvalidNodeMesh";
        case Code::InvalidMesh:               return "InvalidMesh";
        case Code::InvalidMeshMaterial:       return "InvalidMeshMaterial";
        case Code::InvalidMeshIndex:          return "InvalidMeshIndex";
        case Code::InvalidBounds:             return "InvalidBounds";
        case Code::InvalidMaterial:           return "InvalidMaterial";
        case Code::DuplicateMaterialProperty: return "DuplicateMaterialProperty";
        case Code::InvalidTextureReference:   return "InvalidTextureReference";
        case Code::InvalidSkeleton:           return "InvalidSkeleton";
        case Code::InvalidBoneHierarchy:      return "InvalidBoneHierarchy";
        case Code::InvalidAnimation:          return "InvalidAnimation";
        case Code::InvalidAnimationChannel:   return "InvalidAnimationChannel";
        case Code::InvalidAnimationKey:       return "InvalidAnimationKey";
        case Code::MissingSkeletonForSkinning:return "MissingSkeletonForSkinning";
        case Code::InvalidBoneInfluence:      return "InvalidBoneInfluence";
        case Code::InvalidAnimator:           return "InvalidAnimator";
        case Code::InvalidVertexAttribute:    return "InvalidVertexAttribute";
        case Code::ImportNote:                return "ImportNote";
        }
        return "Unknown";
    }

    bool Eq(const math::vector2& a, const math::vector2& b)
    {
        return a.x == b.x && a.y == b.y;
    }

    bool Eq(const math::vector3& a, const math::vector3& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }

    bool Eq(const math::vector4& a, const math::vector4& b)
    {
        return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
    }

    bool Eq(const math::matrix4x4& a, const math::matrix4x4& b)
    {
        // math::matrix4x4 는 operator== 가 없다(부동소수 동등 비교를 기본으로
        // 주지 않는 설계). 여기서 원하는 것은 **비트 동일성**이므로 명시한다.
        return 0 == std::memcmp(&a, &b, sizeof(math::matrix4x4));
    }

    bool Eq(const ex::Vertex& a, const ex::Vertex& b)
    {
        if (!Eq(a.position, b.position) || !Eq(a.normal, b.normal)
            || !Eq(a.uv0, b.uv0) || !Eq(a.uv1, b.uv1)
            || !Eq(a.tangent, b.tangent) || !Eq(a.bitangent, b.bitangent))
        {
            return false;
        }
        for (std::size_t slot = 0; slot < ex::MaxBoneInfluences; ++slot)
        {
            if (a.skin[slot].bone.Value() != b.skin[slot].bone.Value()
                || a.skin[slot].weight != b.skin[slot].weight)
            {
                return false;
            }
        }
        return true;
    }

    bool Eq(const ex::TextureReference& a, const ex::TextureReference& b)
    {
        return a.assetId == b.assetId && a.logicalName == b.logicalName
            && a.fallbackPath == b.fallbackPath && a.colorSpace == b.colorSpace;
    }

    bool Eq(const ex::MaterialPropertyValue& a, const ex::MaterialPropertyValue& b)
    {
        if (a.index() != b.index()) return false;
        return std::visit([&](const auto& valueA) -> bool
        {
            using Alternative = std::remove_cvref_t<decltype(valueA)>;
            const auto& valueB = std::get<Alternative>(b);
            if constexpr (std::is_same_v<Alternative, math::vector2>
                || std::is_same_v<Alternative, math::vector3>
                || std::is_same_v<Alternative, math::vector4>
                || std::is_same_v<Alternative, ex::TextureReference>)
            {
                return Eq(valueA, valueB);
            }
            else
            {
                return valueA == valueB;
            }
        }, a);
    }

    LoadedPair LoadAndBridge(const std::string& modelPath)
    {
        LoadedPair pair;
        pair.legacy = ::Model::LoadModelShared(modelPath);
        if (!pair.legacy)
        {
            pair.report.failures.push_back(
                "legacy Model::LoadModelShared 가 null 반환 — 로드 실패");
            return pair;
        }

        if (pair.legacy->m_Skeleton)
        {
            pair.remap = ComputeBoneRemap(*pair.legacy->m_Skeleton, pair.report);
        }
        if (!pair.report.failures.empty()) return pair;

        ex::ModelDraft draft =
            BuildDraftFromLegacy(*pair.legacy, pair.remap, pair.report);
        if (!pair.report.failures.empty()) return pair;

        ex::ModelLoader loader(
            std::make_unique<LegacyBridgeDecoder>(std::move(draft)));
        ex::ModelLoadRequest request;
        request.sourcePath = modelPath;
        request.sourcePreference = ex::ModelSourcePreference::SourceOnly;
        pair.result = loader.Load(request);
        return pair;
    }

    bool AppendOutcome(const LoadedPair& pair, std::string& outLog)
    {
        for (const std::string& note : pair.report.notes)
            outLog += "  [note] " + note + "\n";
        if (!pair.report.failures.empty())
        {
            for (const std::string& failure : pair.report.failures)
                outLog += "  [bridge] " + failure + "\n";
            return false;
        }
        for (const ex::ModelLoadIssue& issue : pair.result.issues)
        {
            outLog += std::string("  [validate] ")
                + (issue.severity == ex::ModelLoadIssueSeverity::Error
                    ? "ERROR " : "WARN  ")
                + std::string(ToString(issue.code)) + " @ " + issue.context
                + " — " + issue.message + "\n";
        }
        return pair.result.Succeeded();
    }
}
