#include "GltfImporter.h"
#include "NormalGeneration.h"
#include "TangentGeneration.h"
#include "VertexWelding.h"
#include "VertexCacheOptimization.h"

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace experiment::importer
{
    namespace
    {
        // ── 좌표 규약 변환 (legacy aiProcess_ConvertToLeftHanded 와 동일) ──
        [[nodiscard]] math::vector3 ToEngine(const fastgltf::math::fvec3& v) noexcept
        {
            return { v.x(), v.y(), -v.z() };
        }

        [[nodiscard]] math::vector3 ToEngineScale(const fastgltf::math::fvec3& v) noexcept
        {
            // scale 은 부호를 뒤집지 않는다 — 거울 변환의 대칭 성분이라
            // Assimp MakeLeftHanded 도 scale 키를 건드리지 않는다.
            return { v.x(), v.y(), v.z() };
        }

        [[nodiscard]] math::quaternion ToEngine(const fastgltf::math::fquat& q) noexcept
        {
            return { -q.x(), -q.y(), q.z(), q.w() };
        }

        [[nodiscard]] math::vector2 FlipV(const fastgltf::math::fvec2& uv) noexcept
        {
            return { uv.x(), 1.0f - uv.y() };
        }

        [[nodiscard]] std::string ToStdString(std::string_view text)
        {
            return std::string(text);
        }

        // ── 노드 정렬 ───────────────────────────────────────────────────
        // glTF 는 children 만 들고 parent 를 들지 않는다. IR 은 parent 가
        // 정본이고 parent-before-child 정렬을 요구하므로, 루트에서 BFS 로
        // 훑으며 새 인덱스를 부여한다(= 자동으로 정렬을 만족한다).
        struct NodeOrder final
        {
            std::vector<std::size_t> gltfOrder{};                 // 새 인덱스 → glTF
            std::vector<std::uint32_t> gltfToNew{};               // glTF → 새 인덱스
            std::vector<std::uint32_t> parentOf{};                // 새 인덱스 기준
        };

        constexpr std::uint32_t NoParent = (std::numeric_limits<std::uint32_t>::max)();

        [[nodiscard]] NodeOrder OrderNodes(
            const fastgltf::Asset& asset, ImportNoteSink& notes)
        {
            NodeOrder order;
            const std::size_t count = asset.nodes.size();
            order.gltfToNew.assign(count, NoParent);

            std::vector<std::uint8_t> isChild(count, 0);
            for (const fastgltf::Node& node : asset.nodes)
            {
                for (std::size_t child : node.children)
                {
                    if (child < count) isChild[child] = 1;
                }
            }

            // scene 이 지정한 루트를 우선 쓰고, 없으면 부모 없는 노드 전부.
            std::vector<std::size_t> queue;
            if (asset.defaultScene.has_value()
                && *asset.defaultScene < asset.scenes.size())
            {
                for (std::size_t root : asset.scenes[*asset.defaultScene].nodeIndices)
                {
                    if (root < count) queue.push_back(root);
                }
            }
            if (queue.empty())
            {
                for (std::size_t i = 0; i < count; ++i)
                {
                    if (!isChild[i]) queue.push_back(i);
                }
            }

            order.gltfOrder.reserve(count);
            order.parentOf.reserve(count);
            std::vector<std::uint32_t> pendingParent(queue.size(), NoParent);

            for (std::size_t head = 0; head < queue.size(); ++head)
            {
                const std::size_t gltfIndex = queue[head];
                if (order.gltfToNew[gltfIndex] != NoParent)
                {
                    // 같은 노드가 두 부모의 자식으로 등장했다. glTF 는 트리를
                    // 요구하므로 이것은 자산 결함이다 — 첫 등장만 채택한다.
                    notes.Warn(ImportNoteCode::InvalidSceneStructure, "nodes",
                        "한 노드가 여러 부모의 자식으로 등장한다 — 첫 부모만 쓴다.");
                    continue;
                }
                const auto newIndex =
                    static_cast<std::uint32_t>(order.gltfOrder.size());
                order.gltfToNew[gltfIndex] = newIndex;
                order.gltfOrder.push_back(gltfIndex);
                order.parentOf.push_back(pendingParent[head]);

                for (std::size_t child : asset.nodes[gltfIndex].children)
                {
                    if (child >= count) continue;
                    queue.push_back(child);
                    pendingParent.push_back(newIndex);
                }
            }

            if (order.gltfOrder.size() != count)
            {
                notes.Warn(ImportNoteCode::InvalidSceneStructure, "nodes",
                    "scene 에서 도달할 수 없는 노드가 있어 제외했다("
                    + std::to_string(count - order.gltfOrder.size()) + "개).");
            }
            return order;
        }

        // ── 정점 스트림 ─────────────────────────────────────────────────
        [[nodiscard]] const fastgltf::Accessor* FindAccessor(
            const fastgltf::Asset& asset, const fastgltf::Primitive& primitive,
            std::string_view name)
        {
            const auto* attribute = primitive.findAttribute(name);
            if (attribute == primitive.attributes.end()) return nullptr;
            if (attribute->accessorIndex >= asset.accessors.size()) return nullptr;
            return &asset.accessors[attribute->accessorIndex];
        }

        void ReadPositions(const fastgltf::Asset& asset,
            const fastgltf::Accessor& accessor, VertexStreams& streams)
        {
            streams.positions.resize(accessor.count);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset, accessor, [&](fastgltf::math::fvec3 value, std::size_t index)
            {
                streams.positions[index] = ToEngine(value);
            });
        }

        void ReadNormals(const fastgltf::Asset& asset,
            const fastgltf::Accessor& accessor, VertexStreams& streams)
        {
            streams.normals.resize(accessor.count);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                asset, accessor, [&](fastgltf::math::fvec3 value, std::size_t index)
            {
                streams.normals[index] = ToEngine(value);
            });
        }

        void ReadTangents(const fastgltf::Asset& asset,
            const fastgltf::Accessor& accessor, VertexStreams& streams)
        {
            streams.tangents.resize(accessor.count);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                asset, accessor, [&](fastgltf::math::fvec4 value, std::size_t index)
            {
                // xyz 는 z 반전, w(handedness)는 좌표계가 뒤집히므로 함께 반전.
                streams.tangents[index] = {
                    value.x(), value.y(), -value.z(), -value.w() };
            });
        }

        void ReadUv(const fastgltf::Asset& asset,
            const fastgltf::Accessor& accessor, std::vector<math::vector2>& out)
        {
            out.resize(accessor.count);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                asset, accessor, [&](fastgltf::math::fvec2 value, std::size_t index)
            {
                out[index] = FlipV(value);
            });
        }

        void ReadSkin(const fastgltf::Asset& asset,
            const fastgltf::Accessor& joints, const fastgltf::Accessor& weights,
            VertexStreams& streams, const std::string& context,
            ImportNoteSink& notes)
        {
            const std::size_t vertexCount = streams.VertexCount();
            if (joints.count != vertexCount || weights.count != vertexCount)
            {
                notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                    "JOINTS_0/WEIGHTS_0 길이가 POSITION 과 다르다.");
                return;
            }

            // JOINTS_0 은 glTF 상 unsigned byte 또는 unsigned short 다. u16 으로
            // 받으면 둘 다 무손실이고(최대 65535 joint), fastgltf 가 성분 타입
            // 변환을 대신한다. uvec4 는 ElementTraits 가 없어 쓸 수 없다.
            std::vector<fastgltf::math::u16vec4> jointValues(vertexCount);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::u16vec4>(
                asset, joints, [&](fastgltf::math::u16vec4 value, std::size_t index)
            {
                jointValues[index] = value;
            });
            std::vector<fastgltf::math::fvec4> weightValues(vertexCount);
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec4>(
                asset, weights, [&](fastgltf::math::fvec4 value, std::size_t index)
            {
                weightValues[index] = value;
            });

            streams.influenceOffsets.clear();
            streams.influences.clear();
            streams.influenceOffsets.reserve(vertexCount + 1);
            streams.influenceOffsets.push_back(0);
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                for (std::size_t slot = 0; slot < 4; ++slot)
                {
                    const float weight = weightValues[v][slot];
                    if (!(weight > 0.0f)) continue;
                    JointInfluence influence;
                    influence.joint = JointIndex(jointValues[v][slot]);
                    influence.weight = weight;
                    streams.influences.push_back(influence);
                }
                streams.influenceOffsets.push_back(
                    static_cast<std::uint32_t>(streams.influences.size()));
            }
            if (streams.influences.empty())
            {
                streams.influenceOffsets.clear();
            }
        }

        void ReadIndices(const fastgltf::Asset& asset,
            const fastgltf::Primitive& primitive, std::size_t vertexCount,
            ImportedMesh& mesh, const std::string& context, ImportNoteSink& notes)
        {
            if (!primitive.indicesAccessor.has_value())
            {
                // 인덱스 없는 primitive: 순차 인덱스를 만든다.
                mesh.indices.resize(vertexCount);
                for (std::size_t i = 0; i < vertexCount; ++i)
                    mesh.indices[i] = static_cast<std::uint32_t>(i);
            }
            else
            {
                const fastgltf::Accessor& accessor =
                    asset.accessors[*primitive.indicesAccessor];
                mesh.indices.resize(accessor.count);
                fastgltf::iterateAccessorWithIndex<std::uint32_t>(
                    asset, accessor, [&](std::uint32_t value, std::size_t index)
                {
                    mesh.indices[index] = value;
                });
            }

            if (mesh.indices.size() % 3 != 0)
            {
                notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                    "index 수가 3의 배수가 아니다.");
                return;
            }
            // 좌표계를 뒤집었으므로 감김 순서도 뒤집는다(FlipWindingOrder).
            for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            {
                std::swap(mesh.indices[i], mesh.indices[i + 2]);
            }
        }

        // ── 텍스처 ──────────────────────────────────────────────────────
        [[nodiscard]] ImportTextureIndex ResolveTexture(
            const fastgltf::Asset& asset, const fastgltf::TextureInfo& info,
            TextureColorSpace colorSpace, const std::filesystem::path& baseDirectory,
            ImportedScene& scene, std::unordered_map<std::size_t, std::uint32_t>& cache,
            ImportNoteSink& notes)
        {
            if (info.textureIndex >= asset.textures.size()) return {};
            const fastgltf::Texture& texture = asset.textures[info.textureIndex];
            if (!texture.imageIndex.has_value()
                || *texture.imageIndex >= asset.images.size())
            {
                notes.Warn(ImportNoteCode::MaterialSemanticUnmapped, "textures",
                    "이미지가 없는 texture 를 참조한다.");
                return {};
            }
            if (info.transform)
            {
                notes.Warn(ImportNoteCode::UnsupportedFeature, "textures",
                    "KHR_texture_transform 은 아직 옮기지 않는다.");
            }

            if (const auto found = cache.find(*texture.imageIndex);
                found != cache.end())
            {
                return ImportTextureIndex(found->second);
            }

            const fastgltf::Image& image = asset.images[*texture.imageIndex];
            ImportedTexture out;
            out.name = ToStdString(image.name);
            out.colorSpace = colorSpace;

            std::visit(fastgltf::visitor{
                [&](const fastgltf::sources::URI& uri)
                {
                    out.sourcePath = baseDirectory /
                        std::filesystem::path(std::string(uri.uri.path()));
                },
                [&](const fastgltf::sources::Array& array)
                {
                    out.embeddedBytes.assign(
                        array.bytes.begin(), array.bytes.end());
                    notes.Info(ImportNoteCode::EmbeddedTextureExtracted, "images",
                        "임베디드 이미지 바이트를 IR 이 소유한다 — 자산 등록은 "
                        "변환 경계의 resolveTextureAsset 이 결정한다.");
                },
                [&](const fastgltf::sources::BufferView& view)
                {
                    if (view.bufferViewIndex >= asset.bufferViews.size()) return;
                    const fastgltf::BufferView& bufferView =
                        asset.bufferViews[view.bufferViewIndex];
                    if (bufferView.bufferIndex >= asset.buffers.size()) return;
                    const fastgltf::Buffer& buffer =
                        asset.buffers[bufferView.bufferIndex];
                    std::visit(fastgltf::visitor{
                        [&](const fastgltf::sources::Array& array)
                        {
                            const auto begin = array.bytes.begin()
                                + static_cast<std::ptrdiff_t>(bufferView.byteOffset);
                            out.embeddedBytes.assign(begin,
                                begin + static_cast<std::ptrdiff_t>(bufferView.byteLength));
                        },
                        [&](const auto&)
                        {
                            notes.Warn(ImportNoteCode::UnsupportedFeature, "images",
                                "GLB buffer view 이미지를 읽을 수 없는 버퍼 형태다.");
                        } }, buffer.data);
                    notes.Info(ImportNoteCode::EmbeddedTextureExtracted, "images",
                        "GLB 임베디드 이미지 바이트를 IR 이 소유한다.");
                },
                [&](const auto&)
                {
                    notes.Warn(ImportNoteCode::UnsupportedFeature, "images",
                        "지원하지 않는 이미지 소스 형태다.");
                } }, image.data);

            const auto newIndex = static_cast<std::uint32_t>(scene.textures.size());
            scene.textures.push_back(std::move(out));
            cache.emplace(*texture.imageIndex, newIndex);
            return ImportTextureIndex(newIndex);
        }
    }

    bool GltfImporter::CanImport(const std::filesystem::path& sourcePath) const
    {
        std::string extension = sourcePath.extension().string();
        std::ranges::transform(extension, extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".gltf" || extension == ".glb";
    }

    ImportResult GltfImporter::Import(const ImportRequest& request)
    {
        ImportResult result;
        ImportNoteSink notes;

        auto data = fastgltf::GltfDataBuffer::FromPath(request.sourcePath);
        if (data.error() != fastgltf::Error::None)
        {
            notes.Error(ImportNoteCode::InvalidSceneStructure, "file",
                "glTF 파일을 열 수 없다: "
                + std::string(fastgltf::getErrorMessage(data.error())));
            result.notes = notes.Release();
            return result;
        }

        // DecomposeNodeMatrices: 노드 변환을 전부 TRS 로 받는다(IR 정본 형태).
        // LoadExternalBuffers/Images: .bin·이미지 파일을 지금 읽어 둔다.
        // GenerateMeshIndices: 인덱스 없는 primitive 에 인덱스를 만든다.
        constexpr auto options = fastgltf::Options::DecomposeNodeMatrices
            | fastgltf::Options::LoadExternalBuffers
            | fastgltf::Options::GenerateMeshIndices;

        fastgltf::Parser parser;
        const std::filesystem::path baseDirectory =
            request.sourcePath.parent_path();
        auto loaded = parser.loadGltf(data.get(), baseDirectory, options);
        if (loaded.error() != fastgltf::Error::None)
        {
            notes.Error(ImportNoteCode::InvalidSceneStructure, "file",
                "glTF 파싱 실패: "
                + std::string(fastgltf::getErrorMessage(loaded.error())));
            result.notes = notes.Release();
            return result;
        }
        const fastgltf::Asset& asset = loaded.get();

        ImportedScene scene;
        scene.metadata.sourcePath = request.sourcePath;
        scene.metadata.importerName = "GltfImporter(fastgltf)";
        scene.metadata.importerVersion = FASTGLTF_QUOTE(FASTGLTF_VERSION);
        scene.metadata.originalUpAxis = "Y-up right-handed";
        scene.metadata.originalUnitMeters = 1.0;
        if (asset.assetInfo.has_value())
        {
            scene.metadata.generator = ToStdString(asset.assetInfo->generator);
        }
        notes.Info(ImportNoteCode::OriginalAxisConverted, "metadata",
            "오른손 Y-up → 엔진 좌표계로 변환했다(z 반전·UV v 반전·감김 반전).");

        // ── 노드 ────────────────────────────────────────────────────────
        const NodeOrder order = OrderNodes(asset, notes);

        // glTF mesh 하나가 primitive 여럿을 들고, 우리 IR 은 primitive 하나가
        // 메시 하나다(재질별 분할). 노드가 참조할 수 있도록 사상을 만든다.
        std::vector<std::vector<std::uint32_t>> meshPrimitives(asset.meshes.size());

        scene.nodes.reserve(order.gltfOrder.size());
        for (std::size_t newIndex = 0; newIndex < order.gltfOrder.size(); ++newIndex)
        {
            const fastgltf::Node& source = asset.nodes[order.gltfOrder[newIndex]];
            SceneNode node;
            node.name = ToStdString(source.name);
            if (order.parentOf[newIndex] != NoParent)
            {
                node.parent = SceneNodeIndex(order.parentOf[newIndex]);
            }
            if (const auto* trs = std::get_if<fastgltf::TRS>(&source.transform))
            {
                node.local.translation = ToEngine(trs->translation);
                node.local.rotation = ToEngine(trs->rotation);
                node.local.scale = ToEngineScale(trs->scale);
            }
            else
            {
                // DecomposeNodeMatrices 를 켰으므로 도달하지 않아야 한다.
                notes.Warn(ImportNoteCode::ShearedNodeTransform, "nodes",
                    "행렬 변환이 TRS 로 분해되지 않았다 — 항등으로 대체했다.");
            }
            if (source.skinIndex.has_value())
            {
                node.skin = SkinIndex(
                    static_cast<std::uint32_t>(*source.skinIndex));
            }
            if (source.cameraIndex.has_value() || source.lightIndex.has_value())
            {
                notes.Info(ImportNoteCode::UnsupportedFeature, "nodes",
                    "카메라·라이트 노드는 현 스코프 밖이라 옮기지 않는다.");
            }
            scene.nodes.push_back(std::move(node));
        }

        // ── 메시 ────────────────────────────────────────────────────────
        for (std::size_t meshIndex = 0; meshIndex < asset.meshes.size(); ++meshIndex)
        {
            const fastgltf::Mesh& sourceMesh = asset.meshes[meshIndex];
            for (std::size_t p = 0; p < sourceMesh.primitives.size(); ++p)
            {
                const fastgltf::Primitive& primitive = sourceMesh.primitives[p];
                const std::string context = "meshes[" + std::to_string(meshIndex)
                    + "].primitives[" + std::to_string(p) + "]";

                if (primitive.type != fastgltf::PrimitiveType::Triangles)
                {
                    notes.Warn(ImportNoteCode::UnsupportedFeature, context,
                        "삼각형이 아닌 primitive 는 옮기지 않는다.");
                    continue;
                }
                if (primitive.dracoCompression)
                {
                    notes.Warn(ImportNoteCode::UnsupportedFeature, context,
                        "Draco 압축 primitive 는 아직 지원하지 않는다.");
                    continue;
                }
                if (!primitive.targets.empty())
                {
                    notes.Warn(ImportNoteCode::UnsupportedFeature, context,
                        "모프 타깃은 현 스코프 밖이라 버렸다.");
                }

                const fastgltf::Accessor* position =
                    FindAccessor(asset, primitive, "POSITION");
                if (!position || position->count == 0)
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "POSITION 접근자가 없다.");
                    continue;
                }

                ImportedMesh mesh;
                mesh.name = ToStdString(sourceMesh.name);
                if (sourceMesh.primitives.size() > 1)
                {
                    mesh.name += "_" + std::to_string(p);
                }
                if (primitive.materialIndex.has_value())
                {
                    mesh.material = ImportMaterialIndex(
                        static_cast<std::uint32_t>(*primitive.materialIndex));
                }

                ReadPositions(asset, *position, mesh.streams);
                if (const fastgltf::Accessor* normal =
                    FindAccessor(asset, primitive, "NORMAL"))
                {
                    ReadNormals(asset, *normal, mesh.streams);
                }
                else
                {
                    notes.Warn(ImportNoteCode::MissingVertexAttribute, context,
                        "NORMAL 이 없다 — 생성 패스가 필요하다(미구현).");
                }
                if (const fastgltf::Accessor* tangent =
                    FindAccessor(asset, primitive, "TANGENT"))
                {
                    ReadTangents(asset, *tangent, mesh.streams);
                }
                else
                {
                    // 손실이 아니라 후처리 대상이다. 실제 생성은 파싱이 끝난 뒤
                    // GenerateMissingTangents 가 하고(정점이 늘 수 있다),
                    // 그때 생성 여부를 다시 계수한다.
                    notes.Info(ImportNoteCode::MissingVertexAttribute, context,
                        "TANGENT 이 없다 — mikktspace 생성 패스 대상.");
                }
                if (const fastgltf::Accessor* uv0 =
                    FindAccessor(asset, primitive, "TEXCOORD_0"))
                {
                    ReadUv(asset, *uv0, mesh.streams.uv0);
                }
                if (const fastgltf::Accessor* uv1 =
                    FindAccessor(asset, primitive, "TEXCOORD_1"))
                {
                    ReadUv(asset, *uv1, mesh.streams.uv1);
                }

                const fastgltf::Accessor* joints =
                    FindAccessor(asset, primitive, "JOINTS_0");
                const fastgltf::Accessor* weights =
                    FindAccessor(asset, primitive, "WEIGHTS_0");
                if (joints && weights)
                {
                    ReadSkin(asset, *joints, *weights, mesh.streams, context, notes);
                }
                else if (joints || weights)
                {
                    notes.Error(ImportNoteCode::InvalidVertexStreams, context,
                        "JOINTS_0 와 WEIGHTS_0 중 한쪽만 있다.");
                }
                if (FindAccessor(asset, primitive, "JOINTS_1"))
                {
                    notes.Warn(ImportNoteCode::InfluenceBudgetExceeded, context,
                        "JOINTS_1(5개 이상 influence)은 아직 읽지 않는다.");
                }

                ReadIndices(asset, primitive, mesh.streams.VertexCount(),
                    mesh, context, notes);

                meshPrimitives[meshIndex].push_back(
                    static_cast<std::uint32_t>(scene.meshes.size()));
                scene.meshes.push_back(std::move(mesh));
            }
        }

        // 노드가 든 mesh → primitive 목록으로 펼친다.
        for (std::size_t newIndex = 0; newIndex < order.gltfOrder.size(); ++newIndex)
        {
            const fastgltf::Node& source = asset.nodes[order.gltfOrder[newIndex]];
            if (!source.meshIndex.has_value()) continue;
            if (*source.meshIndex >= meshPrimitives.size()) continue;
            for (std::uint32_t primitiveIndex : meshPrimitives[*source.meshIndex])
            {
                scene.nodes[newIndex].meshes.push_back(
                    ImportMeshIndex(primitiveIndex));
            }
        }

        // ── 머테리얼 ────────────────────────────────────────────────────
        std::unordered_map<std::size_t, std::uint32_t> textureCache;
        scene.materials.reserve(asset.materials.size());
        for (const fastgltf::Material& source : asset.materials)
        {
            ImportedMaterial material;
            material.name = ToStdString(source.name);
            material.baseColorFactor = {
                source.pbrData.baseColorFactor.x(),
                source.pbrData.baseColorFactor.y(),
                source.pbrData.baseColorFactor.z(),
                source.pbrData.baseColorFactor.w() };
            material.metallicFactor = source.pbrData.metallicFactor;
            material.roughnessFactor = source.pbrData.roughnessFactor;
            material.emissiveFactor = {
                source.emissiveFactor.x(),
                source.emissiveFactor.y(),
                source.emissiveFactor.z() };
            material.emissiveStrength = source.emissiveStrength;
            material.doubleSided = source.doubleSided;
            material.alphaCutoff = source.alphaCutoff;
            switch (source.alphaMode)
            {
            case fastgltf::AlphaMode::Blend: material.alphaMode = AlphaMode::Blend; break;
            case fastgltf::AlphaMode::Mask:  material.alphaMode = AlphaMode::Mask;  break;
            default:                         material.alphaMode = AlphaMode::Opaque; break;
            }

            const auto slot = [&](const fastgltf::TextureInfo& info,
                TextureColorSpace colorSpace)
            {
                TextureSlot out;
                out.texture = ResolveTexture(asset, info, colorSpace,
                    baseDirectory, scene, textureCache, notes);
                out.uvSet = static_cast<std::uint32_t>(info.texCoordIndex);
                return out;
            };

            if (source.pbrData.baseColorTexture.has_value())
            {
                material.baseColor =
                    slot(*source.pbrData.baseColorTexture, TextureColorSpace::Srgb);
            }
            if (source.pbrData.metallicRoughnessTexture.has_value())
            {
                material.metallicRoughness = slot(
                    *source.pbrData.metallicRoughnessTexture,
                    TextureColorSpace::Linear);
            }
            if (source.normalTexture.has_value())
            {
                material.normal =
                    slot(*source.normalTexture, TextureColorSpace::Linear);
                material.normalScale = source.normalTexture->scale;
            }
            if (source.occlusionTexture.has_value())
            {
                material.occlusion =
                    slot(*source.occlusionTexture, TextureColorSpace::Linear);
                material.occlusionStrength = source.occlusionTexture->strength;
            }
            if (source.emissiveTexture.has_value())
            {
                material.emissive =
                    slot(*source.emissiveTexture, TextureColorSpace::Srgb);
            }
            scene.materials.push_back(std::move(material));
        }

        // ── skin ────────────────────────────────────────────────────────
        scene.skins.reserve(asset.skins.size());
        for (const fastgltf::Skin& source : asset.skins)
        {
            ImportedSkin skin;
            skin.name = ToStdString(source.name);
            skin.joints.reserve(source.joints.size());
            for (std::size_t joint : source.joints)
            {
                if (joint < order.gltfToNew.size()
                    && order.gltfToNew[joint] != NoParent)
                {
                    skin.joints.push_back(SceneNodeIndex(order.gltfToNew[joint]));
                }
                else
                {
                    notes.Error(ImportNoteCode::InvalidSkin, "skins",
                        "joint 가 scene 에서 도달할 수 없는 노드를 가리킨다.");
                    skin.joints.push_back(SceneNodeIndex{});
                }
            }
            if (source.skeleton.has_value() && *source.skeleton < order.gltfToNew.size()
                && order.gltfToNew[*source.skeleton] != NoParent)
            {
                skin.skeletonRoot = SceneNodeIndex(order.gltfToNew[*source.skeleton]);
            }

            skin.inverseBind.resize(skin.joints.size());
            if (source.inverseBindMatrices.has_value()
                && *source.inverseBindMatrices < asset.accessors.size())
            {
                const fastgltf::Accessor& accessor =
                    asset.accessors[*source.inverseBindMatrices];
                fastgltf::iterateAccessorWithIndex<fastgltf::math::fmat4x4>(
                    asset, accessor,
                    [&](fastgltf::math::fmat4x4 value, std::size_t index)
                {
                    if (index >= skin.inverseBind.size()) return;
                    // 좌표계 변환: M' = S * M * S, S = diag(1,1,-1,1).
                    // 성분 기준으로는 3행/3열의 z 교차 항 부호가 뒤집힌다.
                    math::matrix4x4 out;
                    for (std::size_t row = 0; row < 4; ++row)
                    {
                        for (std::size_t column = 0; column < 4; ++column)
                        {
                            const bool flip = (row == 2) != (column == 2);
                            const float element = value[
                                static_cast<std::size_t>(column)][
                                    static_cast<std::size_t>(row)];
                            out.m[row][column] = flip ? -element : element;
                        }
                    }
                    skin.inverseBind[index] = out;
                });
            }
            else
            {
                notes.Warn(ImportNoteCode::InvalidSkin, "skins",
                    "inverseBindMatrices 가 없어 항등으로 둔다.");
            }
            scene.skins.push_back(std::move(skin));
        }

        // ── 애니메이션 ──────────────────────────────────────────────────
        scene.clips.reserve(asset.animations.size());
        for (const fastgltf::Animation& source : asset.animations)
        {
            ImportedClip clip;
            clip.name = ToStdString(source.name);

            // glTF 는 (노드, 경로)마다 채널이 따로다. IR 은 노드마다 T/R/S 를
            // 한 채널에 모으므로 노드 기준으로 합친다.
            std::unordered_map<std::uint32_t, std::size_t> channelOfNode;
            double duration = 0.0;

            for (const fastgltf::AnimationChannel& channel : source.channels)
            {
                if (!channel.nodeIndex.has_value()) continue;
                const std::size_t gltfNode = *channel.nodeIndex;
                if (gltfNode >= order.gltfToNew.size()
                    || order.gltfToNew[gltfNode] == NoParent)
                {
                    notes.Warn(ImportNoteCode::InvalidAnimation, "animations",
                        "도달할 수 없는 노드를 타깃하는 channel 을 버렸다.");
                    continue;
                }
                if (channel.samplerIndex >= source.samplers.size()) continue;
                const fastgltf::AnimationSampler& sampler =
                    source.samplers[channel.samplerIndex];
                if (sampler.inputAccessor >= asset.accessors.size()
                    || sampler.outputAccessor >= asset.accessors.size())
                {
                    continue;
                }

                const std::uint32_t nodeIndex = order.gltfToNew[gltfNode];
                auto found = channelOfNode.find(nodeIndex);
                if (found == channelOfNode.end())
                {
                    ImportedChannel fresh;
                    fresh.target = SceneNodeIndex(nodeIndex);
                    found = channelOfNode.emplace(
                        nodeIndex, clip.channels.size()).first;
                    clip.channels.push_back(std::move(fresh));
                }
                ImportedChannel& target = clip.channels[found->second];

                KeyInterpolation interpolation = KeyInterpolation::Linear;
                switch (sampler.interpolation)
                {
                case fastgltf::AnimationInterpolation::Step:
                    interpolation = KeyInterpolation::Step; break;
                case fastgltf::AnimationInterpolation::CubicSpline:
                    interpolation = KeyInterpolation::CubicSpline; break;
                default: break;
                }

                const fastgltf::Accessor& input = asset.accessors[sampler.inputAccessor];
                const fastgltf::Accessor& output = asset.accessors[sampler.outputAccessor];
                std::vector<double> times(input.count);
                fastgltf::iterateAccessorWithIndex<float>(
                    asset, input, [&](float value, std::size_t index)
                {
                    times[index] = static_cast<double>(value);
                    duration = (std::max)(duration, times[index]);
                });

                // CubicSpline 은 키마다 값 3개(in-tangent·value·out-tangent)다.
                const std::size_t stride =
                    interpolation == KeyInterpolation::CubicSpline ? 3 : 1;
                const std::size_t valueOffset = stride == 3 ? 1 : 0;

                switch (channel.path)
                {
                case fastgltf::AnimationPath::Translation:
                {
                    target.translationInterpolation = interpolation;
                    std::vector<fastgltf::math::fvec3> values(output.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                        asset, output,
                        [&](fastgltf::math::fvec3 value, std::size_t index)
                    {
                        values[index] = value;
                    });
                    target.translations.reserve(times.size());
                    for (std::size_t k = 0; k < times.size(); ++k)
                    {
                        const std::size_t at = k * stride + valueOffset;
                        if (at >= values.size()) break;
                        target.translations.push_back(
                            { times[k], ToEngine(values[at]) });
                    }
                    break;
                }
                case fastgltf::AnimationPath::Rotation:
                {
                    target.rotationInterpolation = interpolation;
                    std::vector<fastgltf::math::fquat> values(output.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fquat>(
                        asset, output,
                        [&](fastgltf::math::fquat value, std::size_t index)
                    {
                        values[index] = value;
                    });
                    target.rotations.reserve(times.size());
                    for (std::size_t k = 0; k < times.size(); ++k)
                    {
                        const std::size_t at = k * stride + valueOffset;
                        if (at >= values.size()) break;
                        target.rotations.push_back(
                            { times[k], ToEngine(values[at]) });
                    }
                    break;
                }
                case fastgltf::AnimationPath::Scale:
                {
                    target.scaleInterpolation = interpolation;
                    std::vector<fastgltf::math::fvec3> values(output.count);
                    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                        asset, output,
                        [&](fastgltf::math::fvec3 value, std::size_t index)
                    {
                        values[index] = value;
                    });
                    target.scales.reserve(times.size());
                    for (std::size_t k = 0; k < times.size(); ++k)
                    {
                        const std::size_t at = k * stride + valueOffset;
                        if (at >= values.size()) break;
                        target.scales.push_back(
                            { times[k], ToEngineScale(values[at]) });
                    }
                    break;
                }
                default:
                    notes.Warn(ImportNoteCode::UnsupportedFeature, "animations",
                        "weights(모프) 채널은 현 스코프 밖이라 버렸다.");
                    break;
                }
            }

            clip.durationSeconds = duration;
            scene.clips.push_back(std::move(clip));
        }

        // ── 후처리 ──────────────────────────────────────────────────────
        // 파싱이 끝난 IR 위에서 돈다. 포맷별 임포터가 각자 만들지 않고 같은
        // 패스를 부르므로, 같은 모델이 glTF/FBX 어느 쪽으로 들어와도 결과가
        // 같다. 정점 수가 늘 수 있어(평면화·이음매 분리) 파싱 도중이 아니다.
        //
        // ★ 순서가 규약이다 — 법선이 탄젠트의 입력이다(mikktspace 전제).
        GenerateMissingNormals(scene, request.options, notes);
        // ★ 순서가 계약이다 — 법선 뒤, 탄젠트 앞.
        //   법선 앞에서 용접하면 평면 법선 패스가 갈라 놓을 정점을 미리
        //   붙이고, 탄젠트 뒤에서 하면 mikktspace 가 갈라 둔 이음매를 도로
        //   붙인다. 사이에 두면 mikktspace 입력도 줄어든다.
        WeldVertices(scene, request.options, notes);
        GenerateMissingTangents(scene, request.options, notes);
        // ★ 맨 끝이다. 앞의 세 패스가 전부 정점을 갈라내거나 합치면서
        //   순서를 다시 쓴다 — 그 앞에서 정렬해 봐야 무너진다.
        OptimizeVertexCache(scene, request.options, notes);

        result.notes = notes.Release();
        // IR 도 자기 이력을 들고 다닌다 — 변환 경계가 임포터 노트를 함께 본다.
        scene.notes = result.notes;
        result.scene = std::move(scene);
        return result;
    }
}
