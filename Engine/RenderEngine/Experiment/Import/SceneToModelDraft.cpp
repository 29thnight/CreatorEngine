#include "SceneToModelDraft.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace experiment::importer
{
    namespace
    {
        constexpr std::uint32_t InvalidMapping =
            (std::numeric_limits<std::uint32_t>::max)();

        template <typename T>
        [[nodiscard]] T ValueAt(const std::vector<T>& stream, std::size_t index) noexcept
        {
            return index < stream.size() ? stream[index] : T{};
        }

        // ── 노드 ────────────────────────────────────────────────────────
        struct NodeLayout final
        {
            // 원본 node index → ModelDraft node index. 합성 루트가 생기면 +1.
            std::vector<std::uint32_t> sceneToDraft{};
            bool synthesizedRoot{};
            std::size_t rootCount{};
        };

        [[nodiscard]] NodeLayout PlanNodes(const ImportedScene& scene)
        {
            NodeLayout layout;
            for (const SceneNode& node : scene.nodes)
            {
                if (!node.parent.IsValid()) ++layout.rootCount;
            }
            layout.synthesizedRoot = layout.rootCount != 1;

            const std::uint32_t offset = layout.synthesizedRoot ? 1u : 0u;
            layout.sceneToDraft.resize(scene.nodes.size());
            for (std::size_t i = 0; i < scene.nodes.size(); ++i)
            {
                layout.sceneToDraft[i] = static_cast<std::uint32_t>(i) + offset;
            }
            return layout;
        }

        void BuildNodes(const ImportedScene& scene, const NodeLayout& layout,
            const ConversionOptions& options, ModelDraft& draft)
        {
            draft.nodes.reserve(scene.nodes.size() + (layout.synthesizedRoot ? 1 : 0));

            if (layout.synthesizedRoot)
            {
                ModelNode root;
                root.name = options.synthesizedRootName;
                draft.nodes.push_back(std::move(root));
            }

            for (std::size_t i = 0; i < scene.nodes.size(); ++i)
            {
                const SceneNode& source = scene.nodes[i];
                ModelNode node;
                node.name = source.name;
                if (source.parent.IsValid())
                {
                    node.parent = NodeIndex(
                        layout.sceneToDraft[source.parent.Value()]);
                }
                else if (layout.synthesizedRoot)
                {
                    node.parent = NodeIndex(0);
                }
                node.localTransform = ComposeTrs(source.local);
                node.meshes.reserve(source.meshes.size());
                for (ImportMeshIndex mesh : source.meshes)
                {
                    node.meshes.push_back(MeshIndex(mesh.Value()));
                }
                draft.nodes.push_back(std::move(node));
            }
        }

        // ── 스켈레톤 ────────────────────────────────────────────────────
        struct SkeletonPlan final
        {
            bool present{};
            SkinIndex sourceSkin{};
            // 원본 node index → bone index (없으면 InvalidMapping)
            std::vector<std::uint32_t> nodeToBone{};
            // skin.joints 순번 → bone index (스킨이 없으면 비어 있다)
            std::vector<std::uint32_t> jointToBone{};
            std::vector<std::uint32_t> boneToNode{};

            // 스킨이 없어 애니메이션 채널에서 유도한 스켈레톤이다.
            // ★ 바인드 포즈가 **존재하지 않는다** — inverse bind 는 항등이고,
            //   이 스켈레톤으로 스키닝을 하면 안 된다. 본 이름으로 리타겟할
            //   클립을 담는 그릇이다.
            bool fromAnimationOnly{};
        };

        // bone 집합은 skin 의 joint 뿐 아니라 **그 조상 전부**를 포함한다.
        // 중간 노드가 빠지면 global = local * parent 누적이 끊겨 포즈가 틀어지고,
        // 그 노드를 타깃하는 채널도 갈 곳을 잃는다.
        [[nodiscard]] SkeletonPlan PlanSkeleton(
            const ImportedScene& scene, ImportNoteSink& notes)
        {
            SkeletonPlan plan;

            // 씨앗을 고른다. 스킨이 있으면 그 joint 가 정본이고, 없으면
            // **애니메이션 채널이 타깃하는 노드**가 씨앗이다.
            //
            // ★ 후자는 실측으로 드러난 구멍이다. 스킨 있는 자산만 보고 짠
            //   코드라 애니메이션 전용 FBX(메시 0·스킨 0·클립 2)가 통째로
            //   버려졌다. 조상 폐포는 두 경우에 똑같이 필요하므로 알고리즘은
            //   그대로 두고 씨앗만 갈라 준다.
            std::vector<SceneNodeIndex> seeds;
            if (!scene.skins.empty())
            {
                if (scene.skins.size() > 1)
                {
                    notes.Warn(ImportNoteCode::InvalidSkin, "skins",
                        "skin 이 " + std::to_string(scene.skins.size())
                        + "개다 — ModelDraft 는 skeleton 하나만 담으므로 첫 skin 만 쓴다.");
                }
                plan.sourceSkin = SkinIndex(0);
                seeds = scene.skins[0].joints;
            }
            else if (!scene.clips.empty())
            {
                plan.fromAnimationOnly = true;
                for (const ImportedClip& clip : scene.clips)
                {
                    for (const ImportedChannel& channel : clip.channels)
                    {
                        if (IsInRange(channel.target, scene.nodes.size()))
                            seeds.push_back(channel.target);
                    }
                }
                if (!seeds.empty())
                {
                    notes.Info(ImportNoteCode::InvalidSkin, "clips",
                        "스킨이 없어 애니메이션 채널이 타깃하는 노드에서"
                        " skeleton 을 유도했다. **바인드 포즈가 없으므로**"
                        " inverse bind 는 항등이며 이 skeleton 으로 스키닝을"
                        " 해서는 안 된다 — 이름 기반 리타겟용이다.");
                }
            }
            if (seeds.empty()) return plan;

            std::vector<std::uint8_t> inSkeleton(scene.nodes.size(), 0);
            for (SceneNodeIndex joint : seeds)
            {
                if (!IsInRange(joint, scene.nodes.size())) continue;
                std::uint32_t cursor = joint.Value();
                while (true)
                {
                    if (inSkeleton[cursor]) break;
                    inSkeleton[cursor] = 1;
                    const SceneNodeIndex sceneParent = scene.nodes[cursor].parent;
                    if (!IsInRange(sceneParent, scene.nodes.size())) break;
                    cursor = sceneParent.Value();
                }
            }

            // 깊이 기준 안정 정렬로 parent < index 계약을 만든다.
            std::vector<std::uint32_t> members;
            for (std::uint32_t i = 0; i < inSkeleton.size(); ++i)
            {
                if (inSkeleton[i]) members.push_back(i);
            }
            // 문맥은 씨앗이 어디서 왔는지를 따라간다. 스킨이 없는데
            // "skins[0]" 이라고 적으면 로그가 거짓말을 한다.
            const char* const seedContext =
                plan.fromAnimationOnly ? "clips" : "skins[0]";

            if (members.empty())
            {
                notes.Error(ImportNoteCode::InvalidSkin, seedContext,
                    "씨앗이 유효한 node 를 하나도 가리키지 않는다.");
                return plan;
            }

            std::vector<std::uint32_t> depth(scene.nodes.size(), 0);
            for (std::uint32_t member : members)
            {
                std::uint32_t steps = 0;
                std::uint32_t cursor = member;
                // 순환은 검증이 잡지만, 변환기가 멈추지 않도록 상한을 둔다.
                while (steps <= scene.nodes.size())
                {
                    const SceneNodeIndex parent = scene.nodes[cursor].parent;
                    if (!IsInRange(parent, scene.nodes.size())
                        || !inSkeleton[parent.Value()])
                    {
                        break;
                    }
                    ++steps;
                    cursor = parent.Value();
                }
                depth[member] = steps;
            }
            std::ranges::stable_sort(members,
                [&](std::uint32_t a, std::uint32_t b) { return depth[a] < depth[b]; });

            std::size_t rootCount = 0;
            for (std::uint32_t member : members)
            {
                const SceneNodeIndex parent = scene.nodes[member].parent;
                if (!IsInRange(parent, scene.nodes.size())
                    || !inSkeleton[parent.Value()])
                {
                    ++rootCount;
                }
            }
            if (rootCount != 1)
            {
                // ModelDraft 는 단일 루트 skeleton 만 담는다. 가짜 부모를 끼우면
                // 변환 합성이 달라지므로 조용히 붙이지 않고 실패로 보고한다.
                notes.Error(ImportNoteCode::InvalidSkin, seedContext,
                    "skeleton 루트가 " + std::to_string(rootCount)
                    + "개다 — 단일 루트로 표현할 수 없다.");
                return plan;
            }

            plan.nodeToBone.assign(scene.nodes.size(), InvalidMapping);
            plan.boneToNode.reserve(members.size());
            for (std::uint32_t member : members)
            {
                plan.nodeToBone[member] =
                    static_cast<std::uint32_t>(plan.boneToNode.size());
                plan.boneToNode.push_back(member);
            }

            // joint 매핑은 스킨이 있을 때만 의미가 있다. 애니메이션 유도
            // 스켈레톤에는 joint 라는 개념 자체가 없으므로 비워 둔다 —
            // 정점 weight 도 없으니 참조하는 쪽이 없다.
            if (plan.sourceSkin.IsValid())
            {
                const std::vector<SceneNodeIndex>& joints =
                    scene.skins[plan.sourceSkin.Value()].joints;
                plan.jointToBone.assign(joints.size(), InvalidMapping);
                for (std::size_t j = 0; j < joints.size(); ++j)
                {
                    if (IsInRange(joints[j], scene.nodes.size()))
                    {
                        plan.jointToBone[j] = plan.nodeToBone[joints[j].Value()];
                    }
                }
            }

            plan.present = true;
            return plan;
        }

        void BuildSkeleton(const ImportedScene& scene, const SkeletonPlan& plan,
            ImportNoteSink& notes, Skeleton& out)
        {
            const ImportedSkin* skin = plan.sourceSkin.IsValid()
                ? &scene.skins[plan.sourceSkin.Value()] : nullptr;

            // joint 순번 → inverse bind. skin joint 가 아닌 계층 bone 은 항등.
            // 스킨이 없으면(애니메이션 유도) 전부 항등이다 — 바인드 포즈가
            // 존재하지 않으므로 그것이 지어내지 않은 정확한 값이다.
            std::vector<const math::matrix4x4*> inverseBindOf(plan.boneToNode.size(), nullptr);
            if (skin)
            {
                for (std::size_t j = 0; j < skin->joints.size(); ++j)
                {
                    const std::uint32_t bone = plan.jointToBone[j];
                    if (bone == InvalidMapping) continue;
                    if (j < skin->inverseBind.size())
                    {
                        inverseBindOf[bone] = &skin->inverseBind[j];
                    }
                }
            }

            out.bones.reserve(plan.boneToNode.size());
            for (std::size_t boneIndex = 0; boneIndex < plan.boneToNode.size(); ++boneIndex)
            {
                const std::uint32_t nodeIndex = plan.boneToNode[boneIndex];
                const SceneNode& node = scene.nodes[nodeIndex];

                Bone bone;
                bone.name = node.name;
                if (bone.name.empty())
                {
                    bone.name = "bone_" + std::to_string(boneIndex);
                    notes.Warn(ImportNoteCode::InvalidSkin,
                        plan.fromAnimationOnly ? "clips" : "skins[0]",
                        "이름 없는 bone 에 합성 이름을 부여했다.");
                }
                const SceneNodeIndex parent = node.parent;
                if (IsInRange(parent, scene.nodes.size())
                    && plan.nodeToBone[parent.Value()] != InvalidMapping)
                {
                    bone.parent = BoneIndex(plan.nodeToBone[parent.Value()]);
                }
                else
                {
                    out.rootBone = BoneIndex(static_cast<std::uint32_t>(boneIndex));
                }
                if (const math::matrix4x4* inverseBind = inverseBindOf[boneIndex])
                {
                    bone.inverseBindMatrix = *inverseBind;
                }
                out.bones.push_back(std::move(bone));
            }

            if (skin && IsInRange(skin->skeletonRoot, scene.nodes.size()))
            {
                const std::uint32_t declared = plan.nodeToBone[skin->skeletonRoot.Value()];
                if (declared != InvalidMapping && out.rootBone.IsValid()
                    && declared != out.rootBone.Value())
                {
                    notes.Info(ImportNoteCode::InvalidSkin, "skins[0].skeletonRoot",
                        "선언된 skeletonRoot 가 계층에서 유도한 루트와 다르다 "
                        "— 계층 유도값을 쓴다.");
                }
            }
        }

        // ── 메시 ────────────────────────────────────────────────────────
        // 이름이 ImportedScene.cpp 의 동류 헬퍼와 겹치면 안 된다 — 유니티 빌드가
        // 두 TU 를 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다.
        [[nodiscard]] std::vector<SkinIndex> MapMeshToSkin(const ImportedScene& scene)
        {
            std::vector<SkinIndex> bindings(scene.meshes.size());
            for (const SceneNode& node : scene.nodes)
            {
                if (!IsInRange(node.skin, scene.skins.size())) continue;
                for (ImportMeshIndex mesh : node.meshes)
                {
                    if (!IsInRange(mesh, scene.meshes.size())) continue;
                    if (!bindings[mesh.Value()].IsValid())
                        bindings[mesh.Value()] = node.skin;
                }
            }
            return bindings;
        }

        // 상위 4개만 남기고 재정규화한다. 버려진 weight 는 계수된다.
        void FillSkin(const VertexStreams& streams, std::size_t vertexIndex,
            const SkeletonPlan& plan, Vertex& vertex,
            const std::string& context, ImportNoteSink& notes)
        {
            const std::span<const JointInfluence> influences =
                streams.InfluencesOf(vertexIndex);
            if (influences.empty()) return;

            std::array<BoneInfluence, MaxBoneInfluences> best{};
            std::size_t kept = 0;
            bool droppedAny = false;
            bool unmappedJoint = false;

            for (const JointInfluence& influence : influences)
            {
                if (!(influence.weight > 0.0f)) continue;
                if (!IsInRange(influence.joint, plan.jointToBone.size())
                    || plan.jointToBone[influence.joint.Value()] == InvalidMapping)
                {
                    unmappedJoint = true;
                    continue;
                }
                BoneInfluence candidate;
                candidate.bone = BoneIndex(plan.jointToBone[influence.joint.Value()]);
                candidate.weight = influence.weight;

                if (kept < MaxBoneInfluences)
                {
                    best[kept++] = candidate;
                    continue;
                }
                // 가장 작은 것을 찾아 교체. 넘치는 순간부터 손실이 시작된다.
                std::size_t weakest = 0;
                for (std::size_t i = 1; i < MaxBoneInfluences; ++i)
                {
                    if (best[i].weight < best[weakest].weight) weakest = i;
                }
                droppedAny = true;
                if (candidate.weight > best[weakest].weight) best[weakest] = candidate;
            }

            if (unmappedJoint)
            {
                notes.Warn(ImportNoteCode::InvalidSkin, context,
                    "bone 으로 매핑되지 않는 joint 를 참조하는 weight 가 있다 — 버림.");
            }
            if (droppedAny)
            {
                notes.Warn(ImportNoteCode::InfluenceBudgetExceeded, context,
                    "influence 가 " + std::to_string(MaxBoneInfluences)
                    + "개를 넘어 상위 항목만 남기고 재정규화했다.");
            }

            float total = 0.0f;
            for (std::size_t i = 0; i < kept; ++i) total += best[i].weight;
            if (total > 0.0f)
            {
                for (std::size_t i = 0; i < kept; ++i) best[i].weight /= total;
            }
            for (std::size_t i = 0; i < kept; ++i)
            {
                const std::uint32_t bone = best[i].bone.Value();
                if (bone > MaxPackedBoneIndex)
                {
                    notes.Error(ImportNoteCode::InvalidSkin, context,
                        "bone index " + std::to_string(bone)
                        + "는 uint8 BLENDINDICES 범위(0..254)를 넘는다.");
                    continue;
                }
                vertex.boneIndices[i] = static_cast<PackedBoneIndex>(bone);
                vertex.boneWeights[i] = best[i].weight;
            }
        }

        void BuildMeshes(const ImportedScene& scene, const SkeletonPlan& plan,
            ImportNoteSink& notes, ModelDraft& draft)
        {
            const std::vector<SkinIndex> meshSkins = MapMeshToSkin(scene);
            draft.meshes.reserve(scene.meshes.size());

            for (std::size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex)
            {
                const ImportedMesh& source = scene.meshes[meshIndex];
                const VertexStreams& streams = source.streams;
                const std::string context = "meshes[" + std::to_string(meshIndex) + "]";

                Mesh mesh;
                mesh.name = source.name;
                if (source.material.IsValid())
                {
                    mesh.material = MaterialIndex(source.material.Value());
                }
                mesh.indices = source.indices;

                const bool skinnable = plan.present && streams.HasSkin()
                    && meshSkins[meshIndex] == plan.sourceSkin;
                if (streams.HasSkin() && !skinnable)
                {
                    notes.Warn(ImportNoteCode::InvalidSkin, context,
                        "이 메시의 skin 이 채택된 skeleton 과 달라 weight 를 버렸다.");
                }

                const std::size_t vertexCount = streams.VertexCount();
                VertexAttributeMask attributes = kCoreVertexAttributes;
                if (!streams.uv1.empty()) attributes |= Bit(VertexAttribute::Uv1);
                if (!streams.colors.empty()) attributes |= Bit(VertexAttribute::Color);
                if (skinnable) attributes |= kSkinVertexAttributes;
                if (!mesh.vertices.SetLayout(attributes))
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "지원하지 않는 runtime vertex attribute 조합이다.");
                    continue;
                }
                mesh.vertices.reserve(vertexCount);
                math::vector3 minimum{}, maximum{};
                for (std::size_t v = 0; v < vertexCount; ++v)
                {
                    Vertex vertex{};
                    vertex.position = streams.positions[v];
                    vertex.normal = ValueAt(streams.normals, v);
                    vertex.uv0 = ValueAt(streams.uv0, v);
                    // IR이 이미 tangent.xyz + handedness.w를 정본으로 가진다.
                    // bitangent는 shader에서 cross(normal, tangent.xyz) * w로 재현한다.
                    vertex.tangent = ValueAt(streams.tangents, v);

                    if (skinnable) FillSkin(streams, v, plan, vertex, context, notes);

                    const math::vector2* uv1 = streams.uv1.empty()
                        ? nullptr : &streams.uv1[v];
                    const math::vector4* color = streams.colors.empty()
                        ? nullptr : &streams.colors[v];
                    if (!mesh.vertices.Append(vertex, uv1, color))
                    {
                        notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                            "vertex attribute mask와 실제 stream이 어긋났다.");
                        break;
                    }

                    if (v == 0)
                    {
                        minimum = vertex.position;
                        maximum = vertex.position;
                    }
                    else
                    {
                        minimum.x = (std::min)(minimum.x, vertex.position.x);
                        minimum.y = (std::min)(minimum.y, vertex.position.y);
                        minimum.z = (std::min)(minimum.z, vertex.position.z);
                        maximum.x = (std::max)(maximum.x, vertex.position.x);
                        maximum.y = (std::max)(maximum.y, vertex.position.y);
                        maximum.z = (std::max)(maximum.z, vertex.position.z);
                    }
                }
                // math::aabb 는 center/extents 다. min/max 로 만들 때는 반드시
                // from_min_max 를 쓴다 — 필드에 그냥 넣으면 조용히 오독한다.
                mesh.bounds = math::aabb::from_min_max(minimum, maximum);
                draft.meshes.push_back(std::move(mesh));
            }
        }

        // ── 머테리얼 ────────────────────────────────────────────────────
        void AddTextureProperty(const ImportedScene& scene, const TextureSlot& slot,
            const std::string& propertyName, TextureColorSpace colorSpace,
            const ConversionOptions& options, const std::string& context,
            ImportNoteSink& notes, Material& material)
        {
            if (!IsInRange(slot.texture, scene.textures.size())) return;
            const ImportedTexture& texture = scene.textures[slot.texture.Value()];

            TextureReference reference;
            reference.logicalName = texture.name;
            reference.colorSpace = colorSpace;
            reference.coordinates = {slot.uvSet, {slot.offset.x, slot.offset.y},
                {slot.tiling.x, slot.tiling.y}, slot.rotation};
            if (options.resolveTextureAsset)
            {
                reference.assetId = options.resolveTextureAsset(texture);
            }
            if (!reference.assetId.IsValid())
            {
                reference.fallbackPath = texture.sourcePath;
            }
            if (!reference.assetId.IsValid() && reference.fallbackPath.empty())
            {
                // 임베디드 텍스처인데 추출/등록 정책이 없다. 조용히 빈 참조를
                // 만들면 ModelDraft 검증이 게시를 막으므로 property 자체를
                // 넣지 않고 계수한다.
                notes.Warn(ImportNoteCode::EmbeddedTextureExtracted, context,
                    "임베디드 텍스처를 자산으로 풀 방법이 없어 '" + propertyName
                    + "' property 를 생략했다(resolveTextureAsset 필요).");
                return;
            }

            MaterialProperty property;
            property.name = propertyName;
            property.value = std::move(reference);
            material.properties.push_back(std::move(property));
        }

        void BuildMaterials(const ImportedScene& scene,
            const ConversionOptions& options, ImportNoteSink& notes, ModelDraft& draft)
        {
            const MaterialPropertyNames& names = options.propertyNames;
            draft.materials.reserve(scene.materials.size());

            for (std::size_t i = 0; i < scene.materials.size(); ++i)
            {
                const ImportedMaterial& source = scene.materials[i];
                const std::string context = "materials[" + std::to_string(i) + "]";

                Material material;
                material.name = source.name.empty()
                    ? "material_" + std::to_string(i) : source.name;
                if (options.resolveMaterialAsset)
                {
                    material.assetId = options.resolveMaterialAsset(source, i);
                }
                material.shaderAssetId = options.resolveShaderAsset
                    ? options.resolveShaderAsset(source, i)
                    : options.shaderAssetId;
                material.blendMode = source.alphaMode == AlphaMode::Blend
                    ? MaterialBlendMode::Transparent : source.alphaMode == AlphaMode::Mask
                    ? MaterialBlendMode::Masked : MaterialBlendMode::Opaque;

                const auto addNumeric = [&](const std::string& name, auto value)
                {
                    if (name.empty()) return;
                    MaterialProperty property;
                    property.name = name;
                    property.value = value;
                    material.properties.push_back(std::move(property));
                };

                addNumeric(names.baseColorFactor, source.baseColorFactor);
                addNumeric(names.metallicFactor, source.metallicFactor);
                addNumeric(names.roughnessFactor, source.roughnessFactor);
                addNumeric(names.emissiveFactor, source.emissiveFactor);
                addNumeric(names.emissiveStrength, source.emissiveStrength);
                addNumeric(names.normalScale, source.normalScale);
                addNumeric(names.occlusionStrength, source.occlusionStrength);
                if (source.alphaMode == AlphaMode::Mask)
                {
                    addNumeric(names.alphaCutoff, source.alphaCutoff);
                }

                // color space 는 semantic 이 정한다 — baseColor·emissive 만 sRGB.
                AddTextureProperty(scene, source.baseColor, names.baseColorMap,
                    TextureColorSpace::Srgb, options, context, notes, material);
                AddTextureProperty(scene, source.metallicRoughness,
                    names.metallicRoughnessMap, TextureColorSpace::Linear,
                    options, context, notes, material);
                AddTextureProperty(scene, source.normal, names.normalMap,
                    TextureColorSpace::Linear, options, context, notes, material);
                AddTextureProperty(scene, source.occlusion, names.occlusionMap,
                    TextureColorSpace::Linear, options, context, notes, material);
                AddTextureProperty(scene, source.emissive, names.emissiveMap,
                    TextureColorSpace::Srgb, options, context, notes, material);

                if (source.doubleSided)
                {
                    addNumeric(std::string(standard_material::property::DoubleSided), true);
                }

                draft.materials.push_back(std::move(material));
            }
        }

        // ── 애니메이션 ──────────────────────────────────────────────────
        template <typename Key>
        [[nodiscard]] std::vector<Key> ScaleKeyTimes(const std::vector<Key>& keys,
            double ticksPerSecond, double durationTicks, InterpolationMode mode,
            const std::string& context, ImportNoteSink& notes)
        {
            std::vector<Key> out;
            out.reserve(keys.size());
            for (const Key& key : keys)
            {
                Key scaled = key;
                scaled.time = (std::min)(key.time * ticksPerSecond, durationTicks);
                // ModelDraft 는 시간 순증가를 요구한다. 초→tick 환산이나 duration
                // 클램프로 같은 tick 에 뭉친 키는 하나만 남을 수 있다.
                if (!out.empty() && scaled.time <= out.back().time)
                {
                    // Step 은 "그 시각부터의 값"이므로 뭉친 구간에서는 마지막
                    // 키가 이겨야 한다. Linear 는 두 값 사이를 지나므로 기존
                    // 규칙(먼저 온 키 유지)을 바꾸지 않는다.
                    if (mode == InterpolationMode::Step) out.back() = std::move(scaled);
                    notes.Warn(ImportNoteCode::KeyTimeCollapsed, context,
                        "초→tick 환산이 키를 같은 tick 에 뭉쳐 하나만 남겼다 "
                        "— ticksPerSecond 가 원본 키 밀도보다 낮다.");
                    continue;
                }
                out.push_back(std::move(scaled));
            }
            return out;
        }

        // source 보간을 런타임이 표현 가능한 것으로 좁힌다. Step 은 그대로
        // 보존되고, 런타임 타입에 자리가 없는 CubicSpline 만 강등·계수된다.
        [[nodiscard]] InterpolationMode ToRuntimeInterpolation(
            KeyInterpolation source, std::string_view track,
            const std::string& context, ImportNoteSink& notes)
        {
            switch (source)
            {
            case KeyInterpolation::Step:
                return InterpolationMode::Step;
            case KeyInterpolation::CubicSpline:
                notes.Warn(ImportNoteCode::UnsupportedInterpolation, context,
                    std::string(track) + " 트랙이 CubicSpline 인데 런타임 샘플러가"
                    " 표현하지 못해 Linear 로 강등했다(리샘플 미구현).");
                return InterpolationMode::Linear;
            default:
                return InterpolationMode::Linear;
            }
        }

        void BuildAnimations(const ImportedScene& scene, const SkeletonPlan& plan,
            const ConversionOptions& options, ImportNoteSink& notes, Skeleton& out)
        {
            const double tps = options.ticksPerSecond;
            out.clips.reserve(scene.clips.size());

            for (std::size_t clipIndex = 0; clipIndex < scene.clips.size(); ++clipIndex)
            {
                const ImportedClip& source = scene.clips[clipIndex];
                const std::string context = "clips[" + std::to_string(clipIndex) + "]";

                AnimationClip clip;
                clip.name = source.name.empty()
                    ? "clip_" + std::to_string(clipIndex) : source.name;
                clip.durationTicks = source.durationSeconds * tps;
                clip.ticksPerSecond = tps;
                clip.looping = true;

                for (const ImportedChannel& channel : source.channels)
                {
                    if (!IsInRange(channel.target, plan.nodeToBone.size())
                        || plan.nodeToBone[channel.target.Value()] == InvalidMapping)
                    {
                        // ★ 실측된 손실 지점. skeleton 밖 node 를 타깃하는 채널은
                        //   ModelDraft 가 표현하지 못한다.
                        notes.Warn(ImportNoteCode::NonJointChannelTarget, context,
                            "skeleton 밖 node 를 타깃하는 channel 을 버렸다.");
                        continue;
                    }

                    AnimationChannel converted;
                    converted.bone =
                        BoneIndex(plan.nodeToBone[channel.target.Value()]);
                    converted.translationInterpolation = ToRuntimeInterpolation(
                        channel.translationInterpolation, "translation", context, notes);
                    converted.rotationInterpolation = ToRuntimeInterpolation(
                        channel.rotationInterpolation, "rotation", context, notes);
                    converted.scaleInterpolation = ToRuntimeInterpolation(
                        channel.scaleInterpolation, "scale", context, notes);
                    converted.translations = ScaleKeyTimes(
                        channel.translations, tps, clip.durationTicks,
                        converted.translationInterpolation, context, notes);
                    converted.rotations = ScaleKeyTimes(
                        channel.rotations, tps, clip.durationTicks,
                        converted.rotationInterpolation, context, notes);
                    converted.scales = ScaleKeyTimes(
                        channel.scales, tps, clip.durationTicks,
                        converted.scaleInterpolation, context, notes);
                    clip.channels.push_back(std::move(converted));
                }

                out.clips.push_back(std::move(clip));
            }
        }
    }

    math::matrix4x4 ComposeTrs(const TrsTransform& transform) noexcept
    {
        // 쿼터니언 정규화. zero quaternion 은 검증이 잡지만 여기서도 항등으로
        // 떨어뜨려 NaN 이 하류로 새지 않게 한다.
        float x = transform.rotation.x;
        float y = transform.rotation.y;
        float z = transform.rotation.z;
        float w = transform.rotation.w;
        const float lengthSquared = x * x + y * y + z * z + w * w;
        if (lengthSquared > 0.0f)
        {
            const float inverse = 1.0f / std::sqrt(lengthSquared);
            x *= inverse; y *= inverse; z *= inverse; w *= inverse;
        }
        else
        {
            x = 0.0f; y = 0.0f; z = 0.0f; w = 1.0f;
        }

        // 행 벡터 규약 회전 행렬(legacy DirectXMath 결과와 동일).
        const float r00 = 1.0f - 2.0f * (y * y + z * z);
        const float r01 = 2.0f * (x * y + z * w);
        const float r02 = 2.0f * (x * z - y * w);
        const float r10 = 2.0f * (x * y - z * w);
        const float r11 = 1.0f - 2.0f * (x * x + z * z);
        const float r12 = 2.0f * (y * z + x * w);
        const float r20 = 2.0f * (x * z + y * w);
        const float r21 = 2.0f * (y * z - x * w);
        const float r22 = 1.0f - 2.0f * (x * x + y * y);

        // M = S * R * T : S 는 R 의 각 행을 스케일하고, T 는 마지막 행에 들어간다.
        const math::vector3& s = transform.scale;
        const math::vector3& t = transform.translation;
        return math::matrix4x4{
            s.x * r00, s.x * r01, s.x * r02, 0.0f,
            s.y * r10, s.y * r11, s.y * r12, 0.0f,
            s.z * r20, s.z * r21, s.z * r22, 0.0f,
            t.x,       t.y,       t.z,       1.0f };
    }

    ConversionResult ConvertToModelDraft(
        const ImportedScene& scene, const ConversionOptions& options)
    {
        ConversionResult result;
        ImportNoteSink notes;

        if (scene.nodes.empty())
        {
            notes.Error(ImportNoteCode::InvalidSceneStructure, "nodes",
                "노드가 없는 씬은 변환할 수 없다.");
            result.notes = notes.Release();
            return result;
        }

        const NodeLayout layout = PlanNodes(scene);
        if (layout.synthesizedRoot && !options.synthesizeRootNode)
        {
            notes.Error(ImportNoteCode::InvalidSceneStructure, "nodes",
                "root 가 " + std::to_string(layout.rootCount)
                + "개인데 합성 루트가 비활성이라 단일 루트를 만들 수 없다.");
            result.notes = notes.Release();
            return result;
        }
        if (layout.synthesizedRoot)
        {
            notes.Info(ImportNoteCode::InvalidSceneStructure, "nodes",
                "root " + std::to_string(layout.rootCount)
                + "개를 합성 루트 하나로 접었다.");
        }

        ModelDraft draft;
        draft.metadata.assetId = options.modelAssetId;
        draft.metadata.name = options.modelName.empty()
            ? scene.metadata.sourcePath.stem().string() : options.modelName;
        draft.metadata.sourcePath = scene.metadata.sourcePath;
        draft.metadata.payloadKind = ModelPayloadKind::SourceImport;

        BuildNodes(scene, layout, options, draft);

        const SkeletonPlan plan = PlanSkeleton(scene, notes);
        if (!scene.skins.empty() && !plan.present)
        {
            // skeleton 을 만들지 못했는데 스킨드 메시가 있으면 게시가 막힌다.
            // 원인은 위 note 에 이미 남았다.
            result.notes = notes.Release();
            return result;
        }

        BuildMeshes(scene, plan, notes, draft);
        BuildMaterials(scene, options, notes, draft);

        if (plan.present)
        {
            Skeleton skeleton;
            BuildSkeleton(scene, plan, notes, skeleton);
            BuildAnimations(scene, plan, options, notes, skeleton);
            draft.skeleton = std::move(skeleton);
        }
        else if (!scene.clips.empty())
        {
            notes.Warn(ImportNoteCode::InvalidAnimation, "clips",
                "skeleton 이 없어 clip "
                + std::to_string(scene.clips.size()) + "개를 전부 버렸다.");
        }

        if (notes.HasErrors())
        {
            result.notes = notes.Release();
            return result;
        }

        result.draft = std::move(draft);
        result.notes = notes.Release();
        return result;
    }
}
