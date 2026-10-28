#include "../../../Engine/Containers/ContainerAPI.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#include "../../../Engine/RenderEngine/RenderAllocator.h"
#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"

#include "RHI/ModelVertexInputLayout.h"
#include "Experiment/Import/MeshletBuilder.h"
#include "Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "MaterialGraphGeometryContract.h"
#include "RHI/RHIModelMeshletUpload.h"
#include "RHI/DX12/Tests/Geometry/GraphSceneFixture.h"
#include "Mesh.h" // legacy ::Vertex — 전환표 단정용

#include <stddef.h>
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

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

        [[nodiscard]] bool ElementIs(const RHIInputElement& element,
            const char* semantic, std::uint32_t semanticIndex, RHIFormat format,
            std::uint32_t offset)
        {
            return std::string(element.semantic) == semantic
                && element.semanticIndex == semanticIndex
                && element.format == format
                && element.alignedByteOffset == offset
                && element.inputSlot == 0
                && element.instanceDataStepRate == 0;
        }
    }

    bool RunExperimentVertexLayoutSelfTest(std::string& outLog)
    {
        Checker check{ outLog };
        outLog += "[assets.modelrender] MBC6 model mask→RHI/PSO 계약\n";
        std::string error;

        // ── 1. core(정적 48B) ─────────────────────────────────────────────
        {
            ce::dynamic_array<RHIInputElement> elements;
            check.Check(ModelVertexInput::BuildInputElements(
                assets::kCoreVertexAttributes, elements, error),
                "core 마스크 유도 (" + error + ")");
            check.Check(4u == elements.size(), "core 원소 수 4");
            if (4u == elements.size())
            {
                check.Check(ElementIs(elements[0], "POSITION", 0,
                    RHIFormat::RGB32Float, 0), "POSITION RGB32 @0");
                check.Check(ElementIs(elements[1], "NORMAL", 0,
                    RHIFormat::RGB32Float, 12), "NORMAL RGB32 @12");
                check.Check(ElementIs(elements[2], "TEXCOORD", 0,
                    RHIFormat::RG32Float, 24), "TEXCOORD0 RG32 @24");
                check.Check(ElementIs(elements[3], "TANGENT", 0,
                    RHIFormat::RGBA32Float, 32),
                    "TANGENT RGBA32(w=handedness) @32");
            }
            check.Check(48u == assets::StrideOf(
                assets::kCoreVertexAttributes), "core stride 48");
        }

        // ── 2. V2(스킨 68B) ───────────────────────────────────────────────
        {
            ce::dynamic_array<RHIInputElement> elements;
            check.Check(ModelVertexInput::BuildInputElements(
                assets::kV2VertexAttributes, elements, error),
                "스킨 마스크 유도 (" + error + ")");
            check.Check(6u == elements.size(), "스킨 원소 수 6");
            if (6u == elements.size())
            {
                check.Check(ElementIs(elements[4], "BLENDINDICES", 0,
                    RHIFormat::RGBA8Uint, 48), "BLENDINDICES RGBA8Uint @48");
                check.Check(ElementIs(elements[5], "BLENDWEIGHT", 0,
                    RHIFormat::RGBA32Float, 52), "BLENDWEIGHT RGBA32 @52");
            }
            check.Check(68u == assets::StrideOf(
                assets::kV2VertexAttributes), "스킨 stride 68");
        }

        // ── 3. uv1(라이트맵) — 뒤 속성 오프셋이 표에서 밀린다 ─────────────
        {
            const assets::VertexAttributeMask mask =
                assets::kCoreVertexAttributes
                | assets::Bit(assets::VertexAttribute::Uv1);
            ce::dynamic_array<RHIInputElement> elements;
            check.Check(ModelVertexInput::BuildInputElements(mask,
                elements, error), "uv1 마스크 유도 (" + error + ")");
            check.Check(5u == elements.size(), "uv1 원소 수 5");
            if (5u == elements.size())
            {
                check.Check(ElementIs(elements[3], "TEXCOORD", 1,
                    RHIFormat::RG32Float, 32), "TEXCOORD1 RG32 @32");
                check.Check(ElementIs(elements[4], "TANGENT", 0,
                    RHIFormat::RGBA32Float, 40), "uv1이 TANGENT를 @40으로 민다");
            }
        }

        // ── 4. fail-closed ────────────────────────────────────────────────
        {
            ce::dynamic_array<RHIInputElement> elements;
            check.Check(!ModelVertexInput::BuildInputElements(
                assets::Bit(assets::VertexAttribute::Position),
                elements, error) && !error.empty(),
                "core 결손 마스크는 거부");
            check.Check(!ModelVertexInput::BuildInputElements(
                assets::kCoreVertexAttributes
                | assets::Bit(assets::VertexAttribute::BoneIndices),
                elements, error) && !error.empty(),
                "skin 반쪽 마스크는 거부");
            check.Check(!ModelVertexInput::BuildInputElements(
                assets::kCoreVertexAttributes | (1u << 30), elements,
                error) && !error.empty(),
                "표 밖 비트는 거부");
        }

        // ── 5. 전환표 — legacy ::Vertex(96B) 대비 D4가 지킬 계약 ──────────
        //
        // 유도 레이아웃은 legacy와 오프셋 호환이 아니다. 이 다름이 명세다:
        // 레이아웃만 먼저 바꾸면 96B 버퍼와 어긋난다(D2 실측 — 전환은 D4에서
        // 정점 버퍼와 동시).
        {
            check.Check(offsetof(::Vertex, tangent) == 40u
                && offsetof(::Vertex, bitangent) == 52u,
                "legacy: TANGENT RGB32 @40 + BINORMAL @52 (유도: RGBA32 @32,"
                " BINORMAL 없음 — 셰이더가 cross(N,T)*w 재구성)");
            check.Check(offsetof(::Vertex, boneIndices) == 64u
                && sizeof(::Vertex{}.boneIndices) == 16u,
                "legacy: BLENDINDICES RGBA32Float @64 (유도: RGBA8Uint @48 —"
                " 셰이더가 uint로 읽는다)");
            check.Check(sizeof(::Vertex) == 96u
                && assets::StrideOf(assets::kV2VertexAttributes) == 68u,
                "stride 96 → 68 — 버퍼와 레이아웃은 동시에만 바뀐다");
        }

        // core/color/skin 및 UV1 조합이 layout과 shader permutation 양쪽에서 서로 다른
        // key를 가져야 한다. skin bool 하나로 축약하면 이 단정이 붉어진다.
        {
            constexpr ce::fixed_array<std::uint32_t, 8> expectedStride{{ 48u, 64u, 68u, 84u, 56u, 72u, 76u, 92u }};
            constexpr ce::fixed_array<std::size_t, 8> expectedElements{{ 4u, 5u, 6u, 7u, 5u, 6u, 7u, 8u }};
            static_assert(expectedStride.size() == assets::kModelVertexMasks.size());
            static_assert(expectedElements.size() == assets::kModelVertexMasks.size());
            ce::dynamic_array<RHIShaderPermutationKey> keys;
            for (std::size_t i = 0; i < assets::kModelVertexMasks.size(); ++i)
            {
                const auto mask = assets::kModelVertexMasks[i];
                ce::dynamic_array<RHIInputElement> elements;
                check.Check(ModelVertexInput::BuildInputElements(mask, elements, error),
                    "model mask layout " + std::to_string(mask));
                check.Check(elements.size() == expectedElements[i],
                    "model mask element count " + std::to_string(mask));
                check.Check(assets::StrideOf(mask) == expectedStride[i],
                    "model mask stride " + std::to_string(mask));
                RHIShaderPermutation permutation;
                check.Check(ModelVertexInput::ApplyShaderPermutation(mask,
                    permutation, error), "model mask shader axes "
                        + std::to_string(mask));
                keys.push_back(permutation.Key());
            }
            std::sort(keys.begin(), keys.end(), [](const auto& left, const auto& right)
                { return left.hi != right.hi ? left.hi < right.hi : left.lo < right.lo; });
            check.Check(std::adjacent_find(keys.begin(), keys.end()) == keys.end(),
                "core/color/skin/color+skin 및 UV1 PSO key 8종 분리");
            check.Check(assets::StrideOf(assets::kCoreColorSkinVertexAttributes) == 84u
                && assets::OffsetOf(assets::kCoreColorSkinVertexAttributes,
                    assets::VertexAttribute::BoneIndices) == 64u
                && assets::OffsetOf(assets::kCoreColorSkinVertexAttributes,
                    assets::VertexAttribute::BoneWeights) == 68u,
                "SU full mask stride 84/bone offsets 64,68");
        }

        // Decode the actual packed stream as the mesh shader will. Check an
        // ordered triangle stream: never reorder, lose, duplicate or rotate
        // a triangle across sealed chunk-local remaps.
        {
            ce::dynamic_array<ce::fixed_array<float, 12>> vertices(16 * 12);
            for (unsigned y = 0; y < 12; ++y)
            {
                for (unsigned x = 0; x < 16; ++x)
                {
                    // Current core layout: position, normal, UV0, tangent.xyzw.
                    vertices[y * 16 + x] = {float(x), float(y), 0, 0, 0, 1, 0, 0, 1, 0, 0, 1};
                }
            }
            ce::dynamic_array<uint32_t> indices;
            for (unsigned y = 0; y < 11; ++y)
            {
                for (unsigned x = 0; x < 15; ++x)
                {
                    const unsigned a = y * 16 + x;
                    indices.insert(indices.end(), {a, a + 1, a + 16, a + 1, a + 17, a + 16});
                }
            }
            RHIModelMeshView geometry;
            geometry.vertexData = vertices.data();
            geometry.vertexBytes = vertices.size() * sizeof(vertices[0]);
            geometry.vertexStride = sizeof(vertices[0]);
            geometry.vertexAttributeMask = assets::kCoreVertexAttributes;
            geometry.indexData = indices.data();
            geometry.indexCount = static_cast<uint32_t>(indices.size());
            experiment::Mesh authored;
            check.Check(authored.vertices.AssignPacked(geometry.vertexAttributeMask, vertices.size(),
                {reinterpret_cast<const std::byte*>(vertices.data()), geometry.vertexBytes}), "METEOR authored vertex contract");
            authored.indices = indices;
            authored.bounds = math::aabb::from_min_max({0, 0, 0}, {15, 11, 0});
            experiment::MeshletPayload cooked;
            const bool built = experiment::importer::BuildMeshlets(authored, cooked, error);
            check.Check(built, "METEOR cooked topology: " + error);
            if (built)
            {
                bool exact = cooked.descriptors.size() > 1;
                for (size_t m = 0; m < cooked.descriptors.size(); ++m)
                {
                    const auto& d = cooked.descriptors[m];
                    exact = exact && d.clusterId == m;
                    for (uint32_t triangle = 0; triangle < d.triangleCount; ++triangle)
                    {
                        const auto primitive = cooked.primitiveRemap[d.primitiveOffset + triangle];
                        exact = exact && primitive == d.primitiveOffset + triangle;
                        for (uint32_t corner = 0; corner < 3; ++corner)
                            exact = exact && cooked.vertexRemap[d.vertexOffset + cooked.triangleIndices[d.triangleOffset + triangle * 3 + corner]] == indices[primitive * 3 + corner];
                    }
                }
                check.Check(exact, "METEOR cooked clusters retain source primitive and corner identity");
                RHIModelMeshletUpload upload;
                check.Check(PackMeteorMeshletGpuPayload(cooked, vertices.size(), geometry.vertexStride,
                    geometry.vertexAttributeMask, 3, geometry.indexCount / 3, static_cast<uint32_t>(vertices.size()), {}, upload, error), "LX canonical GPU packing");
                RHIMeshletBufferHeader header{};
                if (upload.bytes.size() >= sizeof(header)) std::memcpy(&header, upload.bytes.data(), sizeof(header));
                check.Check(header.schemaVersion == 5 && header.hierarchyOffset == 0 && header.sourceLodIndex == 3 && header.meshletCount == cooked.descriptors.size()
                    && header.descriptorStride == sizeof(experiment::MeshletDescriptor) && header.sourcePrimitiveCount == indices.size() / 3,
                    "METEOR GPU header has explicit cluster/LOD/primitive contract");
                experiment::cooked::ModelGeometryArtifact artifact;
                artifact.mesh = authored;
                artifact.mesh.meshlets = cooked;
                ce::dynamic_array<std::byte> encoded;
                const bool encodedOk = experiment::cooked::WriteModelGeometryArtifact(artifact, encoded, error);
                check.Check(encodedOk, "METEOR cooked geometry encoding: " + error);
                experiment::cooked::ModelGeometryArtifact decoded;
                const bool decodedOk = experiment::cooked::ExpandModelGeometryPages(encoded, decoded, error);
                check.Check(decodedOk, "METEOR cooked geometry round trip: " + error);
                auto old = encoded;
                if (old.size() > 7) { const uint32_t version = 2; std::memcpy(old.data() + 4, &version, sizeof(version)); }
                check.Check(!experiment::cooked::ExpandModelGeometryPages(old, decoded, error), "METEOR rejects prior cooked geometry version");
                artifact.mesh.meshlets = {};
                check.Check(!experiment::cooked::WriteModelGeometryArtifact(artifact, encoded, error), "METEOR cooker rejects indexed-only geometry");
                check.Check(error == "METEOR geometry requires cooked meshlets; force reimport/recook.",
                    "METEOR rejection retains the diagnostic after its string storage changes");
                material_graph::SceneGeometryIdentity identity{0x1122334455667788ull, 11, 12, 13, 14, 3, 330};
                check.Check(identity.geometryKey == 0x1122334455667788ull && identity.materialSlot == 14 &&
                    identity.instanceId == 13 && identity.lodIndex == 3 && sizeof(identity) == 64, "METEOR geometry/instance/material/LOD scalar ABI");
                GraphSceneFixture fixture;
                ce::dynamic_array<::Vertex> fixtureVertices(vertices.size());
                for (size_t i = 0; i < vertices.size(); ++i)
                {
                    fixtureVertices[i].position = {vertices[i][0], vertices[i][1], vertices[i][2]};
                    fixtureVertices[i].normal = {0, 0, 1};
                    fixtureVertices[i].tangent = {1, 0, 0};
                }
                const auto meshIndex = fixture.AddMesh(fixtureVertices, indices);
                const auto draw = fixture.Draw(meshIndex, math::matrix4x4::identity());
                material_graph::MeshSurfacePlanBudget chunkBudget;
                chunkBudget.maxChunkPoints = 64;
                own::shared_owner<const material_graph::MeshSurfacePlan> plan;
                check.Check(material_graph::MeshSurfacePlan::BuildForScene(draw, {{0, 0, -5}, 1, 1, 1}, chunkBudget,
                    plan, error), "METEOR cooked cluster-aligned chunk partition");
                if (plan)
                {
                    uint32_t nextCluster{}, nextPrimitive{};
                    bool retained = plan->Chunks().size() > 1;
                    for (const auto& chunk : plan->Chunks())
                    {
                        const auto& input = *chunk.input;
                        const auto& topology = input.Meshlets();
                        retained = retained && input.SourceVertexCount() == vertices.size();
                        for (const auto& d : topology.descriptors)
                        {
                            retained = retained && d.clusterId == nextCluster++;
                            for (uint32_t tri = 0; tri < d.triangleCount; ++tri)
                            {
                                const auto primitive = topology.primitiveRemap[d.primitiveOffset + tri];
                                retained = retained && primitive == nextPrimitive++;
                                for (uint32_t corner = 0; corner < 3; ++corner)
                                {
                                    const auto local = topology.vertexRemap[d.vertexOffset + topology.triangleIndices[d.triangleOffset + tri * 3 + corner]];
                                    retained = retained && input.SourceVertexIds()[local] == indices[primitive * 3 + corner];
                                }
                            }
                        }
                    }
                    check.Check(retained && nextPrimitive == indices.size() / 3, "METEOR chunking preserves cooked cluster/primitive/source-vertex identity");
                }
                auto bad = cooked;
                ++bad.descriptors[0].clusterId;
                check.Check(!experiment::importer::ValidateMeshlets(authored, bad, error), "METEOR rejects changed cluster identity");
                bad = cooked; bad.primitiveRemap[0] = bad.primitiveRemap[1];
                check.Check(!experiment::importer::ValidateMeshlets(authored, bad, error), "METEOR rejects duplicate primitive identity");
                bad = cooked; bad.settings.profileVersion = 1;
                check.Check(!PackMeteorMeshletGpuPayload(bad, vertices.size(), geometry.vertexStride,
                    geometry.vertexAttributeMask, 0, geometry.indexCount / 3, static_cast<uint32_t>(vertices.size()), {}, upload, error), "METEOR rejects old GPU payload without conversion");
                check.Check(!experiment::importer::ValidateMeshlets(authored, {}, error), "METEOR rejects missing cooked topology");
            }
        }

        char summary[120]{};
        std::snprintf(summary, sizeof(summary), "  합성 단정 %zu/%zu\n",
            check.passed, check.passed + check.failed);
        outLog += summary;
        return check.failed == 0u;
    }

    bool RunModelRenderWiringSelfTest(std::string& outLog)
    {
        return RunExperimentVertexLayoutSelfTest(outLog);
    }
}
