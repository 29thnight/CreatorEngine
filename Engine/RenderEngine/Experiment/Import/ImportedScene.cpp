#include "ImportedScene.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace experiment::importer
{
    void ImportNoteSink::Add(ImportNoteSeverity severity, ImportNoteCode code,
        std::string context, std::string message)
    {
        // 노트 수는 수십 규모라 선형 탐색으로 충분하다.
        for (ImportNote& note : notes_)
        {
            if (note.code == code && note.context == context)
            {
                ++note.count;
                if (severity > note.severity) note.severity = severity;
                return;
            }
        }
        notes_.push_back(ImportNote{
            severity, code, std::move(context), std::move(message), 1 });
    }

    void ImportNoteSink::Info(
        ImportNoteCode code, std::string context, std::string message)
    {
        Add(ImportNoteSeverity::Info, code, std::move(context), std::move(message));
    }

    void ImportNoteSink::Warn(
        ImportNoteCode code, std::string context, std::string message)
    {
        Add(ImportNoteSeverity::Warning, code, std::move(context), std::move(message));
    }

    void ImportNoteSink::Error(
        ImportNoteCode code, std::string context, std::string message)
    {
        Add(ImportNoteSeverity::Error, code, std::move(context), std::move(message));
    }

    bool ImportNoteSink::HasErrors() const noexcept
    {
        return std::ranges::any_of(notes_, [](const ImportNote& note)
        {
            return note.severity == ImportNoteSeverity::Error;
        });
    }

    namespace
    {
        using NoteAccumulator = ImportNoteSink;

        [[nodiscard]] bool IsFinite(float value) noexcept { return std::isfinite(value); }
        [[nodiscard]] bool IsFinite(double value) noexcept { return std::isfinite(value); }

        [[nodiscard]] bool IsFinite(const Float2& v) noexcept
        {
            return IsFinite(v.x) && IsFinite(v.y);
        }

        [[nodiscard]] bool IsFinite(const Float3& v) noexcept
        {
            return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z);
        }

        [[nodiscard]] bool IsFinite(const Float4& v) noexcept
        {
            return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z) && IsFinite(v.w);
        }

        [[nodiscard]] bool IsUsableQuaternion(const Float4& q) noexcept
        {
            if (!IsFinite(q)) return false;
            return q.x != 0.0f || q.y != 0.0f || q.z != 0.0f || q.w != 0.0f;
        }

        [[nodiscard]] bool IsFinite(const Matrix4& m) noexcept
        {
            return std::ranges::all_of(m.rowMajor,
                [](float element) { return IsFinite(element); });
        }

        // 스트림 하나를 검사한다. 비어 있으면 "그 속성 없음"이라 합법이고,
        // 비어 있지 않으면 정점 수와 길이가 같아야 한다.
        template <typename T>
        void CheckStream(const std::vector<T>& stream, std::size_t vertexCount,
            const std::string& context, std::string_view label,
            NoteAccumulator& notes)
        {
            if (stream.empty() || stream.size() == vertexCount) return;
            notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                std::string(label) + " 스트림 길이가 정점 수와 다르다.");
        }

        template <typename Key>
        [[nodiscard]] bool HasSortedFiniteTimes(
            const std::vector<Key>& keys, double duration) noexcept
        {
            double previous = 0.0;
            bool first = true;
            for (const Key& key : keys)
            {
                if (!IsFinite(key.time) || key.time < 0.0 || key.time > duration)
                    return false;
                if (!first && key.time <= previous) return false;
                previous = key.time;
                first = false;
            }
            return true;
        }

        // 메시가 어느 skin 아래에서 쓰이는지 노드에서 역으로 찾는다.
        // JointIndex 는 그 skin 의 joints 배열 순번이므로 이것 없이는 범위를
        // 검사할 수 없다. 서로 다른 skin 을 가진 노드 둘이 같은 메시를 가리키면
        // joint 순번의 의미가 갈리므로 그 자체가 결함이다.
        struct MeshSkinBinding final
        {
            SkinIndex skin{};
            bool ambiguous{};
        };

        [[nodiscard]] std::vector<MeshSkinBinding> ResolveMeshSkins(
            const ImportedScene& scene)
        {
            std::vector<MeshSkinBinding> bindings(scene.meshes.size());
            for (const SceneNode& node : scene.nodes)
            {
                // 범위 밖 skin 은 ValidateNodes 가 따로 보고한다. 여기서 걸러야
                // 아래 joints 조회가 OOB 로 새지 않는다.
                if (!IsInRange(node.skin, scene.skins.size())) continue;
                for (ImportMeshIndex mesh : node.meshes)
                {
                    if (!IsInRange(mesh, scene.meshes.size())) continue;
                    MeshSkinBinding& binding = bindings[mesh.Value()];
                    if (!binding.skin.IsValid())
                    {
                        binding.skin = node.skin;
                    }
                    else if (binding.skin != node.skin)
                    {
                        binding.ambiguous = true;
                    }
                }
            }
            return bindings;
        }

        void ValidateNodes(const ImportedScene& scene, NoteAccumulator& notes)
        {
            std::size_t rootCount = 0;
            for (std::size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex)
            {
                const SceneNode& node = scene.nodes[nodeIndex];
                const std::string context = "nodes[" + std::to_string(nodeIndex) + "]";

                if (!node.parent.IsValid())
                {
                    ++rootCount;
                }
                else if (node.parent.Value() >= nodeIndex)
                {
                    notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                        "parent 는 반드시 현재 node 보다 앞서야 한다"
                        "(임포터가 parent-before-child 로 정렬할 의무).");
                }

                if (!IsFinite(node.local.translation) || !IsFinite(node.local.scale)
                    || !IsUsableQuaternion(node.local.rotation))
                {
                    notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                        "local TRS 에 유한하지 않은 값 또는 zero quaternion 이 있다.");
                }

                for (ImportMeshIndex mesh : node.meshes)
                {
                    if (!IsInRange(mesh, scene.meshes.size()))
                    {
                        notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                            "범위를 벗어난 mesh index 를 참조한다.");
                    }
                }
                if (node.skin.IsValid() && !IsInRange(node.skin, scene.skins.size()))
                {
                    notes.Error(ImportNoteCode::InvalidSceneStructure, context,
                        "범위를 벗어난 skin index 를 참조한다.");
                }
            }

            if (scene.nodes.empty())
            {
                notes.Error(ImportNoteCode::InvalidSceneStructure, "nodes",
                    "노드가 하나도 없다.");
            }
            else if (rootCount == 0)
            {
                notes.Error(ImportNoteCode::InvalidSceneStructure, "nodes",
                    "root 가 없다 — parent 사슬이 순환한다.");
            }
            else if (rootCount > 1)
            {
                // glTF scene 은 루트 여럿이 합법이다. ModelDraft 는 정확히 하나를
                // 요구하므로 변환 경계가 합성 루트를 만든다 — 결함이 아니라 기록.
                notes.Info(ImportNoteCode::InvalidSceneStructure, "nodes",
                    "root 가 " + std::to_string(rootCount)
                    + "개 — 변환 경계에서 합성 루트가 필요하다.");
            }
        }

        void ValidateMeshes(const ImportedScene& scene,
            const std::vector<MeshSkinBinding>& meshSkins, NoteAccumulator& notes)
        {
            for (std::size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex)
            {
                const ImportedMesh& mesh = scene.meshes[meshIndex];
                const std::string context = "meshes[" + std::to_string(meshIndex) + "]";
                const VertexStreams& streams = mesh.streams;
                const std::size_t vertexCount = streams.VertexCount();

                if (vertexCount == 0 || mesh.indices.empty())
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "정점 또는 인덱스가 비어 있다.");
                    continue;
                }

                CheckStream(streams.normals, vertexCount, context, "normal", notes);
                CheckStream(streams.uv0, vertexCount, context, "uv0", notes);
                CheckStream(streams.uv1, vertexCount, context, "uv1", notes);
                CheckStream(streams.tangents, vertexCount, context, "tangent", notes);
                CheckStream(streams.colors, vertexCount, context, "color", notes);

                if (streams.normals.empty())
                {
                    notes.Add(ImportNoteSeverity::Warning,
                        ImportNoteCode::MissingVertexAttribute, context,
                        "법선이 없다 — 생성 패스가 필요하다.");
                }
                if (streams.tangents.empty() && !streams.uv0.empty())
                {
                    notes.Add(ImportNoteSeverity::Warning,
                        ImportNoteCode::MissingVertexAttribute, context,
                        "탄젠트가 없다 — mikktspace 생성 패스가 필요하다.");
                }

                for (std::size_t v = 0; v < vertexCount; ++v)
                {
                    if (!IsFinite(streams.positions[v]))
                    {
                        notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                            "position 에 유한하지 않은 값이 있다.");
                        break;
                    }
                }

                if (mesh.indices.size() % 3 != 0)
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "index 수가 3의 배수가 아니다(삼각형 목록이어야 한다).");
                }
                for (std::uint32_t index : mesh.indices)
                {
                    if (index >= vertexCount)
                    {
                        notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                            "정점 범위를 벗어난 index 가 있다.");
                        break;
                    }
                }

                if (mesh.material.IsValid()
                    && !IsInRange(mesh.material, scene.materials.size()))
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "범위를 벗어난 material index 를 참조한다.");
                }

                if (!streams.HasSkin())
                {
                    if (!streams.influenceOffsets.empty()
                        || !streams.influences.empty())
                    {
                        notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                            "influence offset 과 influence 중 한쪽만 채워져 있다.");
                    }
                    continue;
                }

                if (streams.influenceOffsets.size() != vertexCount + 1)
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "influenceOffsets 길이는 정점 수 + 1 이어야 한다.");
                    continue;
                }
                if (streams.influenceOffsets.front() != 0
                    || streams.influenceOffsets.back() != streams.influences.size())
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "influenceOffsets 의 처음/끝이 influence 배열과 맞지 않는다.");
                    continue;
                }
                bool monotonic = true;
                for (std::size_t v = 0; v + 1 < streams.influenceOffsets.size(); ++v)
                {
                    if (streams.influenceOffsets[v] > streams.influenceOffsets[v + 1])
                    {
                        monotonic = false;
                        break;
                    }
                }
                if (!monotonic)
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "influenceOffsets 가 단조 증가하지 않는다.");
                    continue;
                }

                const MeshSkinBinding& binding = meshSkins[meshIndex];
                if (binding.ambiguous)
                {
                    notes.Error(ImportNoteCode::InvalidSkin, context,
                        "서로 다른 skin 을 가진 노드들이 같은 메시를 참조한다 "
                        "— joint 순번의 의미가 갈린다.");
                    continue;
                }
                if (!binding.skin.IsValid())
                {
                    notes.Error(ImportNoteCode::InvalidSkin, context,
                        "skin influence 가 있지만 이 메시를 드는 노드에 skin 이 없다.");
                    continue;
                }

                const std::size_t jointCount =
                    scene.skins[binding.skin.Value()].joints.size();
                for (const JointInfluence& influence : streams.influences)
                {
                    if (!IsFinite(influence.weight) || influence.weight < 0.0f)
                    {
                        notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                            "bone weight 는 유한한 0 이상의 값이어야 한다.");
                        break;
                    }
                    if (!IsInRange(influence.joint, jointCount))
                    {
                        notes.Error(ImportNoteCode::InvalidSkin, context,
                            "joint 순번이 skin 의 joints 범위를 벗어났다.");
                        break;
                    }
                }

                // 4개 상한은 런타임 모델의 제약이다. 여기서는 결함이 아니라
                // 변환 경계가 클램프·재정규화할 대상이라는 기록이다.
                for (std::size_t v = 0; v < vertexCount; ++v)
                {
                    const std::uint32_t begin = streams.influenceOffsets[v];
                    const std::uint32_t end = streams.influenceOffsets[v + 1];
                    std::uint32_t positive = 0;
                    for (std::uint32_t i = begin; i < end; ++i)
                    {
                        if (streams.influences[i].weight > 0.0f) ++positive;
                    }
                    if (positive > MaxBoneInfluences)
                    {
                        notes.Add(ImportNoteSeverity::Warning,
                            ImportNoteCode::InfluenceBudgetExceeded, context,
                            "양수 weight influence 가 런타임 상한("
                            + std::to_string(MaxBoneInfluences)
                            + ")을 넘는 정점이 있다 — 변환 경계에서 클램프·재정규화.");
                        break;
                    }
                }
            }
        }

        void ValidateMaterials(const ImportedScene& scene, NoteAccumulator& notes)
        {
            const auto checkSlot = [&](const TextureSlot& slot,
                const std::string& context, std::string_view label)
            {
                if (!slot.IsValid()) return;
                if (!IsInRange(slot.texture, scene.textures.size()))
                {
                    notes.Error(ImportNoteCode::MaterialSemanticUnmapped, context,
                        std::string(label) + " 슬롯이 범위를 벗어난 texture 를 참조한다.");
                }
                if (!IsFinite(slot.offset) || !IsFinite(slot.tiling))
                {
                    notes.Error(ImportNoteCode::MaterialSemanticUnmapped, context,
                        std::string(label) + " 슬롯의 offset/tiling 이 유한하지 않다.");
                }
            };

            for (std::size_t i = 0; i < scene.materials.size(); ++i)
            {
                const ImportedMaterial& material = scene.materials[i];
                const std::string context = "materials[" + std::to_string(i) + "]";

                if (material.name.empty())
                {
                    notes.Add(ImportNoteSeverity::Warning,
                        ImportNoteCode::MaterialSemanticUnmapped, context,
                        "material 이름이 비어 있다.");
                }
                if (!IsFinite(material.baseColorFactor)
                    || !IsFinite(material.emissiveFactor)
                    || !IsFinite(material.metallicFactor)
                    || !IsFinite(material.roughnessFactor)
                    || !IsFinite(material.normalScale)
                    || !IsFinite(material.occlusionStrength)
                    || !IsFinite(material.emissiveStrength)
                    || !IsFinite(material.alphaCutoff))
                {
                    notes.Error(ImportNoteCode::MaterialSemanticUnmapped, context,
                        "PBR factor 에 유한하지 않은 값이 있다.");
                }

                checkSlot(material.baseColor, context, "baseColor");
                checkSlot(material.metallicRoughness, context, "metallicRoughness");
                checkSlot(material.normal, context, "normal");
                checkSlot(material.occlusion, context, "occlusion");
                checkSlot(material.emissive, context, "emissive");
            }
        }

        void ValidateSkins(const ImportedScene& scene, NoteAccumulator& notes)
        {
            for (std::size_t i = 0; i < scene.skins.size(); ++i)
            {
                const ImportedSkin& skin = scene.skins[i];
                const std::string context = "skins[" + std::to_string(i) + "]";

                if (skin.joints.empty())
                {
                    notes.Error(ImportNoteCode::InvalidSkin, context,
                        "joint 가 하나도 없다.");
                    continue;
                }
                if (skin.joints.size() != skin.inverseBind.size())
                {
                    notes.Error(ImportNoteCode::InvalidSkin, context,
                        "inverseBind 개수가 joint 수와 다르다.");
                }
                if (skin.skeletonRoot.IsValid()
                    && !IsInRange(skin.skeletonRoot, scene.nodes.size()))
                {
                    notes.Error(ImportNoteCode::InvalidSkin, context,
                        "skeletonRoot 가 범위를 벗어났다.");
                }
                for (SceneNodeIndex joint : skin.joints)
                {
                    if (!IsInRange(joint, scene.nodes.size()))
                    {
                        notes.Error(ImportNoteCode::InvalidSkin, context,
                            "joint 가 범위를 벗어난 node 를 참조한다.");
                        break;
                    }
                }
                for (const Matrix4& inverseBind : skin.inverseBind)
                {
                    if (!IsFinite(inverseBind))
                    {
                        notes.Error(ImportNoteCode::InvalidSkin, context,
                            "inverse bind matrix 에 유한하지 않은 값이 있다.");
                        break;
                    }
                }
            }
        }

        void ValidateClips(const ImportedScene& scene, NoteAccumulator& notes)
        {
            // joint 로 쓰이는 node 집합. 여기에 없는 node 를 타깃하는 채널이
            // 변환 경계에서 베이크 또는 탈락 대상이다(실측 10/620).
            //
            // ★ 스킨이 하나도 없으면 joint 라는 개념 자체가 없으므로 이 비교는
            //   무의미하다. 그때는 변환 경계가 채널 타깃에서 skeleton 을
            //   유도하므로 전 채널이 제자리를 찾는다 — 경고를 내면 전부 거짓이
            //   된다(실측: 애니메이션 전용 FBX 에서 84채널 전부 오경보).
            const bool hasJoints = !scene.skins.empty();
            std::vector<std::uint8_t> isJoint(scene.nodes.size(), 0);
            for (const ImportedSkin& skin : scene.skins)
            {
                for (SceneNodeIndex joint : skin.joints)
                {
                    if (IsInRange(joint, scene.nodes.size()))
                        isJoint[joint.Value()] = 1;
                }
            }

            for (std::size_t clipIndex = 0; clipIndex < scene.clips.size(); ++clipIndex)
            {
                const ImportedClip& clip = scene.clips[clipIndex];
                const std::string context = "clips[" + std::to_string(clipIndex) + "]";

                const bool timingOk = IsFinite(clip.durationSeconds)
                    && clip.durationSeconds >= 0.0;
                if (clip.name.empty() || !timingOk)
                {
                    notes.Error(ImportNoteCode::InvalidAnimation, context,
                        "clip 이름 또는 durationSeconds 가 유효하지 않다.");
                }

                std::vector<std::uint8_t> targeted(scene.nodes.size(), 0);
                for (const ImportedChannel& channel : clip.channels)
                {
                    if (!IsInRange(channel.target, scene.nodes.size()))
                    {
                        notes.Error(ImportNoteCode::InvalidAnimation, context,
                            "channel 이 범위를 벗어난 node 를 타깃한다.");
                        continue;
                    }
                    const std::size_t target = channel.target.Value();
                    if (targeted[target])
                    {
                        notes.Error(ImportNoteCode::InvalidAnimation, context,
                            "같은 node 를 타깃하는 channel 이 중복됐다.");
                    }
                    targeted[target] = 1;

                    if (hasJoints && !isJoint[target])
                    {
                        notes.Add(ImportNoteSeverity::Warning,
                            ImportNoteCode::NonJointChannelTarget, context,
                            "joint 가 아닌 node 를 타깃하는 channel 이 있다 "
                            "— 변환 경계에서 베이크 또는 탈락 대상.");
                    }

                    if (channel.translationInterpolation == KeyInterpolation::CubicSpline
                        || channel.rotationInterpolation == KeyInterpolation::CubicSpline
                        || channel.scaleInterpolation == KeyInterpolation::CubicSpline)
                    {
                        notes.Add(ImportNoteSeverity::Warning,
                            ImportNoteCode::UnsupportedInterpolation, context,
                            "CubicSpline 보간이 있다 — 변환 경계에서 Linear 리샘플.");
                    }

                    if (timingOk
                        && (!HasSortedFiniteTimes(channel.translations, clip.durationSeconds)
                            || !HasSortedFiniteTimes(channel.rotations, clip.durationSeconds)
                            || !HasSortedFiniteTimes(channel.scales, clip.durationSeconds)))
                    {
                        notes.Error(ImportNoteCode::InvalidAnimation, context,
                            "key time 이 유한하지 않거나 오름차순/범위를 위반한다.");
                    }

                    const bool valuesFinite =
                        std::ranges::all_of(channel.translations,
                            [](const TranslationKey& k) { return IsFinite(k.value); })
                        && std::ranges::all_of(channel.rotations,
                            [](const RotationKey& k) { return IsUsableQuaternion(k.quaternion); })
                        && std::ranges::all_of(channel.scales,
                            [](const ScaleKey& k) { return IsFinite(k.value); });
                    if (!valuesFinite)
                    {
                        notes.Error(ImportNoteCode::InvalidAnimation, context,
                            "key 값이 유한하지 않거나 zero quaternion 이 있다.");
                    }

                    // 비균등 scale 키는 IR 이 보존한다. 런타임이 uniform 만
                    // 쓴다면(legacy calculAni 가 x 성분만 읽는다) 변환 경계가
                    // 버리는 것이므로 여기서 미리 기록해 둔다.
                    //
                    // ★ 정확한 등호로 재면 안 된다. 행렬 분해를 거친 실자산은
                    //   축마다 1e-5 수준의 잡음이 남아(실측 1.1e-5) 사실상 모든
                    //   채널이 "비균등"으로 걸린다 — 진짜 손실만 보고하도록
                    //   런타임이 무시해도 눈에 안 보이는 크기는 통과시킨다.
                    constexpr float ScaleAnisotropyEpsilon = 1e-4f;
                    for (const ScaleKey& key : channel.scales)
                    {
                        const float deviation = (std::max)(
                            std::abs(key.value.x - key.value.y),
                            std::abs(key.value.y - key.value.z));
                        if (deviation > ScaleAnisotropyEpsilon)
                        {
                            notes.Info(ImportNoteCode::NonUniformScaleDropped, context,
                                "비균등 scale 키가 있다 — 런타임이 uniform scale 만 "
                                "쓴다면 변환 경계에서 손실된다.");
                            break;
                        }
                    }
                }
            }
        }
    }

    std::span<const JointInfluence> VertexStreams::InfluencesOf(
        std::size_t vertexIndex) const noexcept
    {
        if (!HasSkin() || vertexIndex + 1 >= influenceOffsets.size()) return {};
        const std::uint32_t begin = influenceOffsets[vertexIndex];
        const std::uint32_t end = influenceOffsets[vertexIndex + 1];
        if (begin > end || end > influences.size()) return {};
        return std::span<const JointInfluence>(
            influences.data() + begin, static_cast<std::size_t>(end - begin));
    }

    std::size_t TotalVertexCount(const ImportedScene& scene) noexcept
    {
        std::size_t total = 0;
        for (const ImportedMesh& mesh : scene.meshes)
            total += mesh.streams.VertexCount();
        return total;
    }

    std::size_t TotalTriangleCount(const ImportedScene& scene) noexcept
    {
        std::size_t total = 0;
        for (const ImportedMesh& mesh : scene.meshes)
            total += mesh.indices.size() / 3;
        return total;
    }

    std::vector<ImportNote> ValidateImportedScene(const ImportedScene& scene)
    {
        NoteAccumulator notes;

        if (scene.metadata.sourcePath.empty())
        {
            notes.Error(ImportNoteCode::InvalidSceneStructure, "metadata.sourcePath",
                "소스 경로가 비어 있다.");
        }
        if (scene.metadata.importerName.empty())
        {
            notes.Add(ImportNoteSeverity::Warning,
                ImportNoteCode::InvalidSceneStructure, "metadata.importerName",
                "임포터 이름이 비어 있다 — 재현·진단이 어려워진다.");
        }

        ValidateNodes(scene, notes);
        const std::vector<MeshSkinBinding> meshSkins = ResolveMeshSkins(scene);
        ValidateMeshes(scene, meshSkins, notes);
        ValidateMaterials(scene, notes);
        ValidateSkins(scene, notes);
        ValidateClips(scene, notes);

        return notes.Release();
    }

    std::string_view ToString(ImportNoteCode code) noexcept
    {
        switch (code)
        {
        case ImportNoteCode::UnsupportedFeature:       return "UnsupportedFeature";
        case ImportNoteCode::MissingVertexAttribute:   return "MissingVertexAttribute";
        case ImportNoteCode::EmbeddedTextureExtracted: return "EmbeddedTextureExtracted";
        case ImportNoteCode::ShearedNodeTransform:     return "ShearedNodeTransform";
        case ImportNoteCode::OriginalAxisConverted:    return "OriginalAxisConverted";
        case ImportNoteCode::NonJointChannelTarget:    return "NonJointChannelTarget";
        case ImportNoteCode::InfluenceBudgetExceeded:  return "InfluenceBudgetExceeded";
        case ImportNoteCode::UnsupportedInterpolation: return "UnsupportedInterpolation";
        case ImportNoteCode::KeyTimeCollapsed:         return "KeyTimeCollapsed";
        case ImportNoteCode::NonUniformScaleDropped:   return "NonUniformScaleDropped";
        case ImportNoteCode::MaterialSemanticUnmapped: return "MaterialSemanticUnmapped";
        case ImportNoteCode::InvalidSceneStructure:    return "InvalidSceneStructure";
        case ImportNoteCode::InvalidVertexStreams:     return "InvalidVertexStreams";
        case ImportNoteCode::InvalidSkin:              return "InvalidSkin";
        case ImportNoteCode::InvalidAnimation:         return "InvalidAnimation";
        }
        return "Unknown";
    }
}
