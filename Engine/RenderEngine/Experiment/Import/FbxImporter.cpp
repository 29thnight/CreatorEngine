#include "FbxImporter.h"
#include "NormalGeneration.h"
#include "TangentGeneration.h"

#include "ufbx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace experiment::importer
{
    namespace
    {
        // 이름이 다른 TU 의 동류 헬퍼와 겹치면 안 된다 — 유니티 빌드가 두 TU 를
        // 합치면 같은 익명 네임스페이스로 병합돼 재정의가 된다. Fbx 접두사 유지.
        [[nodiscard]] std::string FbxText(const ufbx_string& text)
        {
            return std::string(text.data ? text.data : "", text.length);
        }

        [[nodiscard]] math::vector3 FbxVec3(const ufbx_vec3& v)
        {
            return { static_cast<float>(v.x), static_cast<float>(v.y),
                static_cast<float>(v.z) };
        }

        [[nodiscard]] math::quaternion FbxQuat(const ufbx_quat& q)
        {
            return { static_cast<float>(q.x), static_cast<float>(q.y),
                static_cast<float>(q.z), static_cast<float>(q.w) };
        }

        // ufbx_matrix 는 3x4(회전·스케일 3열 + 평행이동)다. 행 우선 4x4 로 펼친다.
        // 엔진 규약이 행 벡터라 각 축이 한 '행'이 된다.
        [[nodiscard]] math::matrix4x4 FbxMatrix(const ufbx_matrix& m)
        {
            // 16-float 생성자는 row-major 순서다 — 예전 rowMajor 배열과 같다.
            return math::matrix4x4{
                static_cast<float>(m.m00), static_cast<float>(m.m10),
                static_cast<float>(m.m20), 0.0f,
                static_cast<float>(m.m01), static_cast<float>(m.m11),
                static_cast<float>(m.m21), 0.0f,
                static_cast<float>(m.m02), static_cast<float>(m.m12),
                static_cast<float>(m.m22), 0.0f,
                static_cast<float>(m.m03), static_cast<float>(m.m13),
                static_cast<float>(m.m23), 1.0f };
        }

        [[nodiscard]] TrsTransform FbxTransform(const ufbx_transform& t)
        {
            TrsTransform out;
            out.translation = FbxVec3(t.translation);
            out.rotation = FbxQuat(t.rotation);
            out.scale = FbxVec3(t.scale);
            return out;
        }

        // legacy 가 aiProcess_FlipUVs 를 함께 받으므로 v 를 뒤집는다.
        [[nodiscard]] math::vector2 FbxUv(const ufbx_vec2& uv)
        {
            return { static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y) };
        }

        // ── 노드 ────────────────────────────────────────────────────────
        // ufbx 가 부모 우선 순서를 보장한다고 적혀 있지만, IR 계약(parent <
        // index)은 우리가 책임진다. 실자산 bone 배열이 정렬을 어긴 전례가 있어
        // 믿지 않고 루트에서 BFS 로 직접 만든다.
        struct FbxNodeOrder final
        {
            std::vector<const ufbx_node*> ordered{};
            // ufbx typed_id → IR node index
            std::vector<std::uint32_t> nodeToIr{};
        };

        [[nodiscard]] FbxNodeOrder OrderFbxNodes(const ufbx_scene& scene)
        {
            FbxNodeOrder order;
            order.nodeToIr.assign(scene.nodes.count,
                (std::numeric_limits<std::uint32_t>::max)());
            order.ordered.reserve(scene.nodes.count);

            std::vector<const ufbx_node*> queue;
            for (std::size_t i = 0; i < scene.nodes.count; ++i)
            {
                const ufbx_node* node = scene.nodes.data[i];
                if (node && node->parent == nullptr) queue.push_back(node);
            }

            for (std::size_t head = 0; head < queue.size(); ++head)
            {
                const ufbx_node* node = queue[head];
                order.nodeToIr[node->typed_id] =
                    static_cast<std::uint32_t>(order.ordered.size());
                order.ordered.push_back(node);
                for (std::size_t c = 0; c < node->children.count; ++c)
                {
                    if (const ufbx_node* child = node->children.data[c])
                        queue.push_back(child);
                }
            }
            return order;
        }

        // ── 메시 ────────────────────────────────────────────────────────
        // FBX 속성은 코너 단위로 인덱싱된다(같은 위치라도 코너마다 다른 법선·UV).
        // 코너의 속성 인덱스 조합이 같으면 같은 정점이다 — 그 조합을 키로 용접한다.
        struct FbxCornerKey final
        {
            std::uint32_t position{};
            std::uint32_t normal{};
            std::uint32_t uv{};
            std::uint32_t uv1{};
            std::uint32_t color{};

            [[nodiscard]] bool operator==(const FbxCornerKey& o) const noexcept
            {
                return position == o.position && normal == o.normal
                    && uv == o.uv && uv1 == o.uv1 && color == o.color;
            }
        };

        struct FbxCornerKeyHash final
        {
            [[nodiscard]] std::size_t operator()(
                const FbxCornerKey& key) const noexcept
            {
                std::size_t hash = key.position;
                for (const std::uint32_t part :
                    { key.normal, key.uv, key.uv1, key.color })
                {
                    hash ^= static_cast<std::size_t>(part) + 0x9e3779b97f4a7c15ULL
                        + (hash << 6) + (hash >> 2);
                }
                return hash;
            }
        };

        [[nodiscard]] std::uint32_t AttributeIndex(
            const ufbx_uint32_list& indices, std::size_t corner) noexcept
        {
            return corner < indices.count ? indices.data[corner] : UFBX_NO_INDEX;
        }

        struct SkinBinding final
        {
            const ufbx_skin_deformer* deformer{};
            SkinIndex irSkin{};
        };

        void BuildMeshPart(const ufbx_mesh& mesh, const ufbx_mesh_part& part,
            const SkinBinding& skin, const std::string& context,
            ImportNoteSink& notes, ImportedMesh& out)
        {
            VertexStreams& streams = out.streams;
            const bool hasNormal = mesh.vertex_normal.exists;
            const bool hasUv = mesh.vertex_uv.exists;
            const bool hasColor = mesh.vertex_color.exists;
            const bool hasSkin = skin.deformer != nullptr;

            std::unordered_map<FbxCornerKey, std::uint32_t, FbxCornerKeyHash> lookup;
            lookup.reserve(part.num_triangles * 3);
            // 코너 → ufbx 정점 인덱스. 스킨 가중치가 정점 단위라 따로 들고 간다.
            std::vector<std::uint32_t> vertexOfNew;

            if (hasSkin) streams.influenceOffsets.push_back(0);

            // n-gon 이 있을 수 있어 ufbx 의 삼각화를 쓴다. 대각선 선택 규약을
            // 우리가 다시 정하면 노멀맵 이음매가 달라진다.
            std::vector<std::uint32_t> triangleCorners(
                (std::max<std::size_t>)(mesh.max_face_triangles, 1) * 3);

            for (std::size_t f = 0; f < part.face_indices.count; ++f)
            {
                const ufbx_face face = mesh.faces.data[part.face_indices.data[f]];
                if (face.num_indices < 3) continue;   // 점·선 면은 삼각형이 아니다

                const std::uint32_t triangles = ufbx_triangulate_face(
                    triangleCorners.data(), triangleCorners.size(), &mesh, face);

                for (std::uint32_t t = 0; t < triangles * 3; ++t)
                {
                    const std::size_t corner = triangleCorners[t];

                    FbxCornerKey key;
                    key.position = AttributeIndex(
                        mesh.vertex_position.indices, corner);
                    key.normal = hasNormal
                        ? AttributeIndex(mesh.vertex_normal.indices, corner)
                        : UFBX_NO_INDEX;
                    key.uv = hasUv
                        ? AttributeIndex(mesh.vertex_uv.indices, corner)
                        : UFBX_NO_INDEX;
                    key.uv1 = UFBX_NO_INDEX;
                    key.color = hasColor
                        ? AttributeIndex(mesh.vertex_color.indices, corner)
                        : UFBX_NO_INDEX;

                    const auto found = lookup.find(key);
                    if (found != lookup.end())
                    {
                        out.indices.push_back(found->second);
                        continue;
                    }

                    const auto fresh =
                        static_cast<std::uint32_t>(streams.positions.size());

                    streams.positions.push_back(FbxVec3(
                        mesh.vertex_position.values.data[key.position]));
                    if (hasNormal && key.normal != UFBX_NO_INDEX)
                    {
                        streams.normals.push_back(FbxVec3(
                            mesh.vertex_normal.values.data[key.normal]));
                    }
                    if (hasUv && key.uv != UFBX_NO_INDEX)
                    {
                        streams.uv0.push_back(FbxUv(
                            mesh.vertex_uv.values.data[key.uv]));
                    }
                    if (hasColor && key.color != UFBX_NO_INDEX)
                    {
                        const ufbx_vec4& c = mesh.vertex_color.values.data[key.color];
                        streams.colors.push_back({ static_cast<float>(c.x),
                            static_cast<float>(c.y), static_cast<float>(c.z),
                            static_cast<float>(c.w) });
                    }

                    if (hasSkin)
                    {
                        const std::uint32_t vertex = key.position;
                        if (vertex < skin.deformer->vertices.count)
                        {
                            const ufbx_skin_vertex& skinVertex =
                                skin.deformer->vertices.data[vertex];
                            for (std::uint32_t w = 0; w < skinVertex.num_weights; ++w)
                            {
                                const ufbx_skin_weight& weight =
                                    skin.deformer->weights.data[
                                        skinVertex.weight_begin + w];
                                JointInfluence influence;
                                influence.joint = JointIndex(weight.cluster_index);
                                influence.weight = static_cast<float>(weight.weight);
                                streams.influences.push_back(influence);
                            }
                        }
                        streams.influenceOffsets.push_back(
                            static_cast<std::uint32_t>(streams.influences.size()));
                    }

                    vertexOfNew.push_back(key.position);
                    lookup.emplace(key, fresh);
                    out.indices.push_back(fresh);
                }
            }

            if (hasSkin && streams.influences.empty())
            {
                // 스킨은 붙어 있는데 가중치가 하나도 없다. 빈 offsets 를 남기면
                // HasSkin() 이 참인데 내용이 없어 하류가 헷갈린다.
                streams.influenceOffsets.clear();
            }
            if (!hasNormal)
            {
                notes.Warn(ImportNoteCode::MissingVertexAttribute, context,
                    "법선이 없다 — ufbx generate_missing_normals 가 꺼졌거나"
                    " 메시가 비었다.");
            }
            if (!hasUv)
            {
                notes.Info(ImportNoteCode::MissingVertexAttribute, context,
                    "UV0 가 없다 — 탄젠트를 생성할 수 없다.");
            }
        }

        // ── 머테리얼 ────────────────────────────────────────────────────
        [[nodiscard]] float MapReal(const ufbx_material_map& map, float fallback)
        {
            return map.has_value ? static_cast<float>(map.value_real) : fallback;
        }

        void BuildFbxMaterial(const ufbx_material& source, ImportedMaterial& out)
        {
            out.name = FbxText(source.name);

            const ufbx_material_pbr_maps& pbr = source.pbr;
            if (pbr.base_color.has_value)
            {
                const ufbx_vec4& c = pbr.base_color.value_vec4;
                out.baseColorFactor = { static_cast<float>(c.x),
                    static_cast<float>(c.y), static_cast<float>(c.z),
                    static_cast<float>(c.w) };
            }
            out.metallicFactor = MapReal(pbr.metalness, 0.0f);
            out.roughnessFactor = MapReal(pbr.roughness, 1.0f);
            if (pbr.emission_color.has_value)
            {
                out.emissiveFactor = FbxVec3(pbr.emission_color.value_vec3);
            }
            out.emissiveStrength = MapReal(pbr.emission_factor, 1.0f);
            out.normalScale = MapReal(pbr.normal_map, 1.0f);

            const float opacity = MapReal(pbr.opacity, 1.0f);
            if (opacity < 1.0f)
            {
                out.alphaMode = AlphaMode::Blend;
                out.baseColorFactor.w = opacity;
            }
        }
    }

    bool FbxImporter::CanImport(const std::filesystem::path& sourcePath) const
    {
        std::string extension = sourcePath.extension().string();
        std::ranges::transform(extension, extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".fbx";
    }

    ImportResult FbxImporter::Import(const ImportRequest& request)
    {
        ImportResult result;
        ImportNoteSink notes;

        ufbx_load_opts options{};
        // legacy 가 aiProcess_ConvertToLeftHanded 를 주므로 같은 좌표계로 받는다.
        // 손으로 z 를 뒤집던 glTF 경로와 달리 축 변환을 라이브러리에 맡긴다.
        options.target_axes = ufbx_axes_left_handed_y_up;
        options.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
        // ★ 단위는 건드리지 않는다. FBX 는 보통 cm 인데 legacy 가 환산을 하지
        //   않으므로 여기서 m 로 맞추면 기준선과 100배 어긋난다.
        options.target_unit_meters = 0.0f;
        // ★ 손잡이 변환에 쓸 미러 축. legacy(Assimp MakeLeftHanded)는 **z 를**
        //   뒤집는데 ufbx 기본값은 다른 축이라, 이것을 맞추지 않으면 z 부호만
        //   어긋난다 — 실측으로 잡았다: x·y 는 정확히 일치하고 z 범위만
        //   [-1.501, 0] 대 [0, 1.501] 로 뒤집혀 있었다.
        options.handedness_conversion_axis = UFBX_MIRROR_AXIS_Z;
        // FBX 는 노드와 별개로 기하 변환(GeometricTranslation 등)을 들 수 있다.
        // 기본값(PRESERVE)은 그것을 ufbx_node.geometry_transform 에 남기는데 우리
        // IR 에는 자리가 없어 조용히 사라진다. 정점에 접어 넣어 legacy 와 같은
        // 메시 로컬 공간으로 맞춘다. (이 자산에는 기하 변환이 없어 위 z 문제와는
        // 무관했다 — 그래도 표현 못 하는 것을 흘려보내지 않으려고 남긴다.)
        options.geometry_transform_handling =
            UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
        // ★ ufbx 의 법선 생성을 쓰지 않는다. 그것을 켜면 glTF 는 우리 패스를,
        //   FBX 는 ufbx 를 타서 **같은 모델이 포맷에 따라 다른 법선을 갖는다.**
        //   탄젠트에 적용한 것과 같은 규칙이다 — 후처리는 한 곳에서만.
        options.generate_missing_normals = false;
        options.load_external_files = true;
        options.ignore_missing_external_files = true;

        ufbx_error error{};
        ufbx_scene* loaded = ufbx_load_file(
            request.sourcePath.string().c_str(), &options, &error);
        if (!loaded)
        {
            char description[512];
            ufbx_format_error(description, sizeof(description), &error);
            notes.Error(ImportNoteCode::InvalidSceneStructure, "file",
                "FBX 로드 실패: " + std::string(description));
            result.notes = notes.Release();
            return result;
        }

        ImportedScene scene;
        scene.metadata.sourcePath = request.sourcePath;
        scene.metadata.importerName = "FbxImporter(ufbx)";
        scene.metadata.importerVersion = std::to_string(UFBX_HEADER_VERSION);
        scene.metadata.generator = FbxText(loaded->metadata.creator);
        scene.metadata.originalUnitMeters =
            static_cast<double>(loaded->settings.unit_meters);
        scene.metadata.originalTicksPerSecond =
            static_cast<double>(loaded->settings.frames_per_second);
        scene.metadata.originalUpAxis = "FBX settings axes";
        notes.Info(ImportNoteCode::OriginalAxisConverted, "metadata",
            "ufbx target_axes 로 왼손 Y-up 으로 변환했다(UV v 반전은 별도)."
            " 단위 환산은 legacy 재현을 위해 하지 않는다.");

        // ── 노드 ────────────────────────────────────────────────────────
        const FbxNodeOrder order = OrderFbxNodes(*loaded);
        scene.nodes.reserve(order.ordered.size());
        for (const ufbx_node* node : order.ordered)
        {
            SceneNode irNode;
            irNode.name = FbxText(node->name);
            if (node->parent)
            {
                irNode.parent = SceneNodeIndex(
                    order.nodeToIr[node->parent->typed_id]);
            }
            irNode.local = FbxTransform(node->local_transform);
            scene.nodes.push_back(std::move(irNode));
        }

        // ── 스킨 ────────────────────────────────────────────────────────
        // ufbx 는 skin deformer 를 메시에 붙인다. IR 은 skin 을 따로 들고
        // 노드가 참조하므로 먼저 수집해 deformer → skin index 표를 만든다.
        std::unordered_map<const ufbx_skin_deformer*, std::uint32_t> skinOfDeformer;
        for (std::size_t m = 0; m < loaded->meshes.count; ++m)
        {
            const ufbx_mesh* mesh = loaded->meshes.data[m];
            if (!mesh || mesh->skin_deformers.count == 0) continue;
            const ufbx_skin_deformer* deformer = mesh->skin_deformers.data[0];
            if (!deformer || skinOfDeformer.contains(deformer)) continue;
            if (mesh->skin_deformers.count > 1)
            {
                notes.Warn(ImportNoteCode::InvalidSkin, "meshes",
                    "skin deformer 가 둘 이상이라 첫 번째만 쓴다.");
            }

            ImportedSkin skin;
            skin.name = FbxText(deformer->name);
            skin.joints.reserve(deformer->clusters.count);
            skin.inverseBind.reserve(deformer->clusters.count);
            for (std::size_t c = 0; c < deformer->clusters.count; ++c)
            {
                const ufbx_skin_cluster* cluster = deformer->clusters.data[c];
                SceneNodeIndex joint;
                if (cluster && cluster->bone_node)
                {
                    joint = SceneNodeIndex(
                        order.nodeToIr[cluster->bone_node->typed_id]);
                }
                else
                {
                    notes.Warn(ImportNoteCode::InvalidSkin, "skins",
                        "bone 노드가 없는 cluster 가 있다 — joint 를 비워 둔다.");
                }
                skin.joints.push_back(joint);
                // geometry_to_bone 이 곧 inverse bind 다(ufbx 권장 경로).
                skin.inverseBind.push_back(cluster
                    ? FbxMatrix(cluster->geometry_to_bone) : math::matrix4x4{});
            }
            if (!skin.joints.empty()) skin.skeletonRoot = skin.joints.front();

            skinOfDeformer.emplace(deformer,
                static_cast<std::uint32_t>(scene.skins.size()));
            scene.skins.push_back(std::move(skin));
        }

        // ── 머테리얼 ────────────────────────────────────────────────────
        std::unordered_map<const ufbx_material*, std::uint32_t> materialIndex;
        scene.materials.reserve(loaded->materials.count);
        for (std::size_t i = 0; i < loaded->materials.count; ++i)
        {
            const ufbx_material* material = loaded->materials.data[i];
            if (!material) continue;
            ImportedMaterial imported;
            BuildFbxMaterial(*material, imported);
            materialIndex.emplace(material,
                static_cast<std::uint32_t>(scene.materials.size()));
            scene.materials.push_back(std::move(imported));
        }

        // ── 메시 ────────────────────────────────────────────────────────
        // 노드 순서로 돌아야 IR 의 node→mesh 참조가 순서에 맞는다.
        for (std::size_t irNode = 0; irNode < order.ordered.size(); ++irNode)
        {
            const ufbx_node* node = order.ordered[irNode];
            const ufbx_mesh* mesh = node->mesh;
            if (!mesh) continue;

            SkinBinding binding;
            if (mesh->skin_deformers.count > 0)
            {
                const ufbx_skin_deformer* deformer = mesh->skin_deformers.data[0];
                const auto found = skinOfDeformer.find(deformer);
                if (found != skinOfDeformer.end())
                {
                    binding.deformer = deformer;
                    binding.irSkin = SkinIndex(found->second);
                    scene.nodes[irNode].skin = binding.irSkin;
                }
            }

            // 재질별로 갈라 glTF primitive 와 같은 알갱이로 맞춘다.
            for (std::size_t p = 0; p < mesh->material_parts.count; ++p)
            {
                const ufbx_mesh_part& part = mesh->material_parts.data[p];
                if (part.num_triangles == 0) continue;

                const std::string context =
                    "meshes[" + std::to_string(scene.meshes.size()) + "]";
                ImportedMesh imported;
                imported.name = FbxText(node->name);
                if (mesh->material_parts.count > 1)
                {
                    imported.name += "_" + std::to_string(p);
                }
                if (p < mesh->materials.count)
                {
                    const auto found = materialIndex.find(mesh->materials.data[p]);
                    if (found != materialIndex.end())
                        imported.material = ImportMaterialIndex(found->second);
                }

                BuildMeshPart(*mesh, part, binding, context, notes, imported);
                if (imported.streams.positions.empty()) continue;

                scene.nodes[irNode].meshes.push_back(
                    ImportMeshIndex(static_cast<std::uint32_t>(scene.meshes.size())));
                scene.meshes.push_back(std::move(imported));
            }
        }

        // ── 애니메이션 ──────────────────────────────────────────────────
        // 커브를 직접 읽지 않고 ufbx 의 베이크를 쓴다. FBX 의 회전 순서·
        // 프리/포스트 회전·상속 모드까지 ufbx 가 이미 접어 주므로, 그것을
        // 손으로 재현하는 것보다 훨씬 안전하다. 결과는 초 단위 TRS 키다.
        for (std::size_t s = 0; s < loaded->anim_stacks.count; ++s)
        {
            const ufbx_anim_stack* stack = loaded->anim_stacks.data[s];
            if (!stack) continue;

            ufbx_bake_opts bakeOptions{};
            ufbx_error bakeError{};
            ufbx_baked_anim* baked =
                ufbx_bake_anim(loaded, stack->anim, &bakeOptions, &bakeError);
            if (!baked)
            {
                char description[512];
                ufbx_format_error(description, sizeof(description), &bakeError);
                notes.Warn(ImportNoteCode::InvalidAnimation,
                    "anim_stacks[" + std::to_string(s) + "]",
                    "베이크 실패로 clip 을 버렸다: " + std::string(description));
                continue;
            }

            ImportedClip clip;
            clip.name = FbxText(stack->name);
            clip.durationSeconds = baked->playback_duration;

            std::size_t steppedKeys = 0;
            for (std::size_t n = 0; n < baked->nodes.count; ++n)
            {
                const ufbx_baked_node& bakedNode = baked->nodes.data[n];
                if (bakedNode.typed_id >= order.nodeToIr.size()) continue;
                const std::uint32_t target = order.nodeToIr[bakedNode.typed_id];
                if (target == (std::numeric_limits<std::uint32_t>::max)()) continue;

                ImportedChannel channel;
                channel.target = SceneNodeIndex(target);

                channel.translations.reserve(bakedNode.translation_keys.count);
                for (std::size_t k = 0; k < bakedNode.translation_keys.count; ++k)
                {
                    const ufbx_baked_vec3& key = bakedNode.translation_keys.data[k];
                    if (key.flags & UFBX_BAKED_KEY_STEP_KEY) ++steppedKeys;
                    channel.translations.push_back(
                        { key.time, FbxVec3(key.value) });
                }
                channel.rotations.reserve(bakedNode.rotation_keys.count);
                for (std::size_t k = 0; k < bakedNode.rotation_keys.count; ++k)
                {
                    const ufbx_baked_quat& key = bakedNode.rotation_keys.data[k];
                    if (key.flags & UFBX_BAKED_KEY_STEP_KEY) ++steppedKeys;
                    channel.rotations.push_back(
                        { key.time, FbxQuat(key.value) });
                }
                channel.scales.reserve(bakedNode.scale_keys.count);
                for (std::size_t k = 0; k < bakedNode.scale_keys.count; ++k)
                {
                    const ufbx_baked_vec3& key = bakedNode.scale_keys.data[k];
                    if (key.flags & UFBX_BAKED_KEY_STEP_KEY) ++steppedKeys;
                    channel.scales.push_back({ key.time, FbxVec3(key.value) });
                }

                if (channel.translations.empty() && channel.rotations.empty()
                    && channel.scales.empty())
                {
                    continue;
                }
                clip.channels.push_back(std::move(channel));
            }

            if (steppedKeys > 0)
            {
                // 베이크는 계단을 **촘촘한 Linear 키 쌍**으로 표현한다. 우리
                // InterpolationMode::Step 으로 접으려면 구간별 판정이 필요한데
                // 현 채널 모델은 트랙 단위라 표현하지 못한다 — 계수만 한다.
                notes.Info(ImportNoteCode::UnsupportedInterpolation,
                    "anim_stacks[" + std::to_string(s) + "]",
                    "계단 키 " + std::to_string(steppedKeys)
                    + "개가 Linear 키 쌍으로 베이크됐다(값은 보존, 표현만 다름).");
            }

            ufbx_free_baked_anim(baked);
            scene.clips.push_back(std::move(clip));
        }

        ufbx_free_scene(loaded);

        // ── 후처리 ──────────────────────────────────────────────────────
        // glTF 경로와 **같은 패스**를 부른다. 임포터마다 다른 결과가 나오면
        // 같은 모델이 포맷에 따라 다르게 보인다.
        // ★ 순서가 규약이다 — 법선이 탄젠트의 입력이다.
        GenerateMissingNormals(scene, request.options, notes);
        GenerateMissingTangents(scene, request.options, notes);

        result.notes = notes.Release();
        scene.notes = result.notes;
        result.scene = std::move(scene);
        return result;
    }
}
