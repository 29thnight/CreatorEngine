#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../../../Tools/build/ModuleAdapters/Creator.Core.DynamicBitset.h"
#else
import Creator.Core.DynamicBitset;
#endif
#include "RHI/DX12/DX12Format.h"
#include "Render/Passes/Geometry/SceneSurfaceTargets.h"
#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include "GraphSceneFixture.h"
#include "RHI/RHIModelMeshletUpload.h"

#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "Mesh.h"
// DeviceState.h include가 여기 있었다 (E, 2026-08-09).
// 이 파일에서 DirectX11:: 심볼을 쓰는 코드가 0이다.

#include <stdint.h>
#include "../../../../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include <mathematics/transform.hpp>

namespace material_graph
{
#include "MeshletStreamingTopology.inl"
}

// The oracle reads the actual GPU pose/packet bytes, then independently decodes
// positions on the CPU. The dedicated cluster probe checks the GPU decoder.
namespace
{
    bool ShadowReadbackPoints(const material_graph::MeshSurfaceBatch& geometry,
        std::span<const std::byte> pose, std::span<const std::byte> topology,
        std::vector<material_graph::SurfacePoint>& points, std::string& error)
    {
        using namespace assets::quantized;
        try
        {
            points.resize(geometry.Count());
            if (!geometry.UsesDirectClusters())
            {
                Require(pose.size() == points.size() * sizeof(points[0]), "Shadow compatibility point capacity");
                std::memcpy(points.data(), pose.data(), pose.size());
                return true;
            }
            const auto boneCount = Word(pose, 0);
            Require(boneCount == geometry.Input()->Bones().size() && 32ull + boneCount * 48ull <= pose.size(),
                "Shadow GPU pose header");
            const auto map = Word(topology, 80);
            std::map<uint32_t, std::vector<std::byte>> decoded;
            for (uint32_t vertex = 0; vertex < geometry.Count(); ++vertex)
            {
                const auto packet = Word(topology, map + vertex * 8ull);
                const auto local = Word(topology, map + vertex * 8ull + 4);
                Require(packet && packet < topology.size(), "Shadow resident vertex map");
                auto found = decoded.find(packet);
                if (found == decoded.end())
                {
                    const auto bytes = Word(topology, packet + 20ull);
                    Require(bytes <= topology.size() - packet, "Shadow packet range");
                    std::vector<std::byte> attributes;
                    Require(assets::cluster::Decode(topology.subspan(packet, bytes), attributes, error), "Shadow packet decode");
                    found = decoded.emplace(packet, std::move(attributes)).first;
                }
                const auto mask = Word(topology, packet + 8ull);
                const auto stride = assets::StrideOf(mask);
                const auto& attributes = found->second;
                Require(uint64_t(local + 1) * stride <= attributes.size(), "Shadow local vertex range");
                float position[4]{0, 0, 0, 1};
                std::memcpy(position, attributes.data() + uint64_t(local) * stride, 12);
                if (boneCount)
                {
                    const auto base = uint64_t(local) * stride;
                    const auto indices = Word(attributes, base + assets::OffsetOf(mask, assets::VertexAttribute::BoneIndices));
                    float weights[4];
                    std::memcpy(weights, attributes.data() + base + assets::OffsetOf(mask, assets::VertexAttribute::BoneWeights), 16);
                    const auto sum = weights[0] + weights[1] + weights[2] + weights[3];
                    if (sum > 0)
                    {
                        float result[3]{};
                        for (unsigned influence = 0; influence < 4; ++influence)
                        {
                            if (weights[influence] <= 0)
                            {
                                continue;
                            }
                            const auto bone = (indices >> (influence * 8)) & 255u;
                            Require(bone < boneCount, "Shadow bone range");
                            PackedBoneMatrix packed;
                            std::memcpy(&packed, pose.data() + 32 + bone * sizeof(packed), sizeof(packed));
                            for (unsigned axis = 0; axis < 3; ++axis)
                            {
                                for (unsigned row = 0; row < 4; ++row)
                                {
                                    result[axis] += (weights[influence] / sum) * position[row] * packed.rows[axis][row];
                                }
                            }
                        }
                        std::memcpy(position, result, 12);
                    }
                }
                std::memcpy(&points[vertex].position, position, 12);
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }
}

// GBuffer 스키닝 검증 (PHASE 3-6).
//
// ── 넷을 따로 단정한다 ──
//
//   ① 항등 팔레트 = 바인드 포즈 — 스키닝 경로를 타되 아무것도 안 움직인다.
//      이것이 서야 ②의 이동이 '팔레트 때문'이라고 말할 수 있다.
//   ② 본 이동이 그 본에 묶인 정점만 옮긴다 — 위 절반만 밀린다.
//   ③ 가중 혼합이 선형이다 — 0.5/0.5인 정점은 딱 절반만 간다.
//   ④ 비스킨드가 섞여도 안 움직인다 — boneOffset·boneWeight 분기가 정확한가.
//
// ★ ④가 이 슬라이스에서 가장 위험한 지점이다. 스키닝을 붙이면서 비스킨드
//   메시를 망가뜨리는 것이 흔한 실패인데(팔레트 인덱스가 0으로 읽혀 첫 본이
//   전체에 곱해진다), 스킨드만 보는 검증은 그것을 통과시킨다.
//
// 합성 메시를 쓴다 — 실제 캐릭터로는 '얼마나 움직여야 맞는지'를 모른다.
// 정점 위치를 손으로 정하면 기대값이 계산으로 나온다.
namespace
{
    constexpr uint32_t kSkinWidth = 256;
    constexpr uint32_t kSkinHeight = 256;

    // 커버리지는 bitmask 타깃(R32_UINT)에서 읽는다 — 그려진 곳만 0이 아니다.
    //
    // 디코드와 행 간격은 RHIReadbackImage가 안다(R2c-b2). 남는 것은 이 검사만
    // 아는 판독 규칙이다 — '그려졌는가'는 0이 아닌 것이고, 그 위에 얹은 셋은
    // 어디를 어떻게 재는가다.
    struct SkinCapture
    {
        RHIReadbackImage image;

        bool At(uint32_t x, uint32_t y) const { return 0.f != image.At(x, y, 0); }

        uint32_t CountCovered() const
        {
            uint32_t covered = 0;
            for (uint32_t y = 0; y < kSkinHeight; ++y)
                for (uint32_t x = 0; x < kSkinWidth; ++x)
                    if (At(x, y)) ++covered;
            return covered;
        }

        // 지정한 행에서 그려진 구간의 오른쪽 끝. 본 이동이 x축이므로
        // 이 값이 '얼마나 밀렸는가'를 바로 말해 준다.
        //
        // xLimit이 필요한 이유: 같은 행에 비스킨드 사각형이 있으면 그쪽
        // 오른쪽 끝을 잡아 스킨드의 이동을 못 본다. 스킨드 띠가 절대
        // 넘지 않는 x를 상한으로 준다.
        uint32_t RightEdgeAtRow(uint32_t y, uint32_t xLimit) const
        {
            for (uint32_t x = (std::min)(xLimit, kSkinWidth); x-- > 0; )
            {
                if (At(x, y)) return x;
            }
            return UINT32_MAX;
        }

        // 스킨드 띠가 그려진 세로 범위. 표본 행을 실측으로 잡기 위한 것이다.
        //
        // ★ 표본 행을 상수로 두면 안 된다. 처음에 화면 중앙 ±40으로 뒀다가
        //   '아래 층이 +5 움직였다'로 실패했는데, 실제로는 그 행이 아래 층
        //   정점이 아니라 아래-가운데 사이의 보간 구간이었다(계산해 보니
        //   딱 그만큼 나오는 자리). 스키닝은 맞았고 표본이 틀렸다.
        bool VerticalRange(uint32_t xLimit, uint32_t& outTop, uint32_t& outBottom) const
        {
            outTop = UINT32_MAX;
            outBottom = 0;
            for (uint32_t y = 0; y < kSkinHeight; ++y)
            {
                if (UINT32_MAX == RightEdgeAtRow(y, xLimit)) continue;
                if (UINT32_MAX == outTop) outTop = y;
                outBottom = y;
            }
            return UINT32_MAX != outTop;
        }
    };

    /// 스킨드 띠와 비스킨드 사각형을 가르는 화면 x. 띠는 이동 후에도 이
    /// 왼쪽에 있고, 사각형은 오른쪽에 있다 — 둘이 같은 행을 공유하므로
    /// 이 경계가 없으면 서로의 측정을 오염시킨다.
    constexpr uint32_t kSkinnedRegionLimit = kSkinWidth * 3 / 4;

    // 세로로 선 띠. 아래 절반은 본 0, 위 절반은 본 1, 가운데 행은 0.5/0.5.
    //
    // 정점을 세 층으로 둔다(y = -1, 0, +1). 가운데 층의 가중치를 반씩
    // 나누면 본 1이 옮겨갈 때 딱 절반만 따라간다 — ③의 재료다.
    void BuildSkinnedStrip(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices)
    {
        const float kHalfWidth = 1.f;
        const float kRows[3] = { -1.f, 0.f, 1.f };

        for (int row = 0; row < 3; ++row)
        {
            for (int side = 0; side < 2; ++side)
            {
                Vertex vertex{};
                vertex.position = { side ? kHalfWidth : -kHalfWidth, kRows[row], 0.f };
                vertex.normal = { 0.f, 0.f, -1.f };

                if (0 == row)        // 아래 — 본 0만
                {
                    vertex.boneIndices = { 0.f, 0.f, 0.f, 0.f };
                    vertex.boneWeights = { 1.f, 0.f, 0.f, 0.f };
                }
                else if (1 == row)   // 가운데 — 반반
                {
                    vertex.boneIndices = { 0.f, 1.f, 0.f, 0.f };
                    vertex.boneWeights = { 0.5f, 0.5f, 0.f, 0.f };
                }
                else                 // 위 — 본 1만
                {
                    vertex.boneIndices = { 1.f, 0.f, 0.f, 0.f };
                    vertex.boneWeights = { 1.f, 0.f, 0.f, 0.f };
                }

                outVertices.push_back(vertex);
            }
        }

        // 두 사각형(아래 띠 · 위 띠).
        for (uint32_t band = 0; band < 2; ++band)
        {
            const uint32_t base = band * 2;
            outIndices.push_back(base + 0); outIndices.push_back(base + 1); outIndices.push_back(base + 3);
            outIndices.push_back(base + 0); outIndices.push_back(base + 3); outIndices.push_back(base + 2);
        }
    }

    // 스키닝 가중치가 전혀 없는 사각형 — ④의 대조군.
    void BuildStaticQuad(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices,
        float centerX)
    {
        const uint32_t base = static_cast<uint32_t>(outVertices.size());
        const float corners[4][2] = { { -0.5f, -1.f }, { 0.5f, -1.f },
            { 0.5f, 1.f }, { -0.5f, 1.f } };

        for (const auto& corner : corners)
        {
            Vertex vertex{};
            vertex.position = { centerX + corner[0], corner[1], 0.f };
            vertex.normal = { 0.f, 0.f, -1.f };
            // boneWeights는 전부 0 — 셰이더의 스키닝 분기가 꺼져야 한다.
            outVertices.push_back(vertex);
        }

        outIndices.push_back(base + 0); outIndices.push_back(base + 1); outIndices.push_back(base + 2);
        outIndices.push_back(base + 0); outIndices.push_back(base + 2); outIndices.push_back(base + 3);
    }
}

bool DX12Test::RunSkinningTest(std::string& outLog)
{

    outLog += "── 스키닝 검증 (GBuffer · 그림자, PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kSkinWidth, kSkinHeight, error))
    {
        outLog += "[1/5] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(&resources, L"dx12_skinning.cache", error) ||
        !rootSignatures.Initialize(&resources, error) ||
        !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/5] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.textureCache = &textureCache;
    frameContext.width = kSkinWidth;
    frameContext.height = kSkinHeight;

    GraphSceneFixture gbuffer;
    if (!gbuffer.Initialize(frameContext, error))
    {
        outLog += "[1/5] GBuffer 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/5] 셰이더 컴파일·PSO 생성 통과(BLENDINDICES·BLENDWEIGHT 포함)\n";

    // ── 합성 메시 ──
    std::vector<Vertex> skinnedVertices;
    std::vector<uint32_t> skinnedIndices;
    BuildSkinnedStrip(skinnedVertices, skinnedIndices);
    Mesh skinnedMesh("dx12_skin_strip", skinnedVertices, skinnedIndices);
    skinnedMesh.RecalculateBounds();

    std::vector<Vertex> staticVertices;
    std::vector<uint32_t> staticIndices;
    BuildStaticQuad(staticVertices, staticIndices, 3.f);   // 오른쪽에 따로 세운다
    Mesh staticMesh("dx12_skin_static", staticVertices, staticIndices);
    staticMesh.RecalculateBounds();

    // 정면에서 보는 카메라. 화면 x가 곧 월드 x라 이동량을 픽셀로 읽는다.
    //
    // 거리 10은 시야에 맞춘 값이다: 화면 절반이 월드 4.14라 스킨드 띠
    // (월드 -1~2, 이동 포함)와 비스킨드 사각형(월드 2.5~3.5)이 겹치지 않고
    // 둘 다 화면 안에 들어온다.
    FrameCameraSnapshot camera{};
    {
        const math::vector3 eye{0.f, 0.f, -10.f};
        const math::vector3 at{};
        const math::vector3 up = math::vector3::unit_y();
        camera.view = math::look_at_lh(eye, at, up);
        camera.projection = math::perspective_fov_lh(
            math::pi * 0.25f, 1.f, 0.1f, 100.f);
        camera.inverseView = math::inverse(camera.view);
        camera.inverseProjection = math::inverse(camera.projection);
        camera.eyePosition = eye;
        camera.forward = math::normalize(at - eye);
        camera.right = math::normalize(math::cross(up, camera.forward));
        camera.up = math::cross(camera.forward, camera.right);
        camera.fov = 45.f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100.f;
        camera.isOrthographic = false;
    }
    frameContext.camera = &camera;

    RHIReadback readback{};
    {
        std::string readbackError;
        if (!resources.CreateReadback(kSkinWidth, kSkinHeight,
            FromDXGI(DXGI_FORMAT_R32_UINT), 1, readback, readbackError))
        {
            outLog += "[1/5] 리드백 생성 실패: " + readbackError + "\n";
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;

    // 본 팔레트 둘. [0]은 항상 항등(아래 절반이 기준으로 남는다).
    std::vector<math::matrix4x4> palette(
        2, math::matrix4x4::identity());

    const auto renderOnce = [&](const std::vector<EnhancedDrawItem>& draws,
        SkinCapture& outCapture) -> bool
    {
        frameContext.draws = &draws;
        ++frameContext.frameId;

        if (!resources.BeginFrame(error))
        {
            outLog += "BeginFrame 실패: " + error + "\n";
            return false;
        }
        if (!gbuffer.PrepareFrame(frameContext, error))
        {
            outLog += "PrepareFrame 실패: " + error + "\n";
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned);
        gbuffer.Declare(graph, frameContext);

        const RGHandle bitmask = gbuffer.GetOutputs().bitmask;
        if (!bitmask.IsValid())
        {
            outLog += "bitmask 출력이 없다\n";
            return false;
        }

        graph.AddPass("Skinning.Readback",
            { { bitmask, RHIResourceState::CopySource, RGAccessMode::Read } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(bitmask));
            }, true);

        if (!graph.Compile(error))
        {
            outLog += "Compile 실패: " + error + "\n";
            return false;
        }
        if (!graph.Execute(error))
        {
            outLog += "Execute 실패: " + error + "\n";
            return false;
        }
        if (!resources.EndFrame(error))
        {
            outLog += "EndFrame 실패: " + error + "\n";
            return false;
        }
        if (!gbuffer.Publish(resources, frameContext.frameId, error) ||
            !GraphSceneFixture::CompleteSubmission(resources, error)) return false;

        if (!resources.MapReadback(readback, outCapture.image, error))
        {
            outLog += "리드백 Map 실패: " + error + "\n";
            return false;
        }
        return true;
    };

    // 스킨드 띠 + 비스킨드 사각형을 함께 그린다 — ④가 매 프레임 확인된다.
    std::vector<EnhancedDrawItem> draws(2);
    draws[0].mesh = &skinnedMesh;
    draws[0].worldMatrix = math::matrix4x4::identity();
    draws[0].bonePalette = palette.data();
    draws[0].boneCount = 2;
    draws[0].animatorKey = 0x5EED;
    draws[1].mesh = &staticMesh;
    draws[1].worldMatrix = math::matrix4x4::identity();
    // 비스킨드 — 팔레트를 주지 않는다.

    // ── [2/5] 항등 팔레트 = 바인드 포즈 ──
    SkinCapture identityCapture{};
    if (passed)
    {
        if (!renderOnce(draws, identityCapture))
        {
            passed = false;
        }
        else
        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "[2/5] 항등 팔레트 — 커버 %u · 드로우 %u · 배치 %u · 스킨드 %u · 팔레트 %u\n",
                identityCapture.CountCovered(), gbuffer.GetLastDrawCount(),
                gbuffer.GetLastBatchCount(), gbuffer.GetLastSkinnedCount(),
                gbuffer.GetLastBonePaletteCount());
            outLog += line;

            if (0 == identityCapture.CountCovered())
            {
                outLog += "아무것도 안 그려졌다 — 입력 레이아웃이나 팔레트 배선이 깨졌다\n";
                passed = false;
            }
            if (1 != gbuffer.GetLastSkinnedCount() || 1 != gbuffer.GetLastBonePaletteCount())
            {
                outLog += "스킨드 수·팔레트 수가 1이 아니다 — 수집이 틀렸다\n";
                passed = false;
            }
        }
    }

    // ── [3/4] 본 1 이동 — 위 절반만, 가운데는 절반만 ──
    if (passed)
    {
        // 본 1을 +x로 1.0 옮긴다. 위 층(가중 1)은 그대로 1.0, 가운데 층
        // (가중 0.5)은 0.5만 간다. 아래 층은 본 0이라 움직이지 않는다.
        palette[1] = math::translation_matrix(math::vector3{ 1.f, 0.f, 0.f });

        SkinCapture movedCapture{};
        if (!renderOnce(draws, movedCapture))
        {
            passed = false;
        }
        else
        {
            // 표본 행을 실측으로 잡는다. 위·아래는 정점 층에 최대한 붙이고
            // (가장자리 한 줄은 래스터 경계라 피한다), 가운데는 정확히 중앙.
            uint32_t top = 0, bottom = 0;
            if (!identityCapture.VerticalRange(kSkinnedRegionLimit, top, bottom))
            {
                outLog += "스킨드 띠가 안 보인다 — 측정할 것이 없다\n";
                passed = false;
                top = bottom = kSkinHeight / 2;
            }

            const uint32_t kTopRow = top + 2;               // 위 층 정점 바로 아래
            const uint32_t kMiddleRow = (top + bottom) / 2; // 가운데 층
            const uint32_t kBottomRow = bottom - 2;         // 아래 층 정점 바로 위

            const uint32_t topBefore = identityCapture.RightEdgeAtRow(kTopRow, kSkinnedRegionLimit);
            const uint32_t topAfter = movedCapture.RightEdgeAtRow(kTopRow, kSkinnedRegionLimit);
            const uint32_t middleBefore = identityCapture.RightEdgeAtRow(kMiddleRow, kSkinnedRegionLimit);
            const uint32_t middleAfter = movedCapture.RightEdgeAtRow(kMiddleRow, kSkinnedRegionLimit);
            const uint32_t bottomBefore = identityCapture.RightEdgeAtRow(kBottomRow, kSkinnedRegionLimit);
            const uint32_t bottomAfter = movedCapture.RightEdgeAtRow(kBottomRow, kSkinnedRegionLimit);

            const int topDelta = static_cast<int>(topAfter) - static_cast<int>(topBefore);
            const int middleDelta = static_cast<int>(middleAfter) - static_cast<int>(middleBefore);
            const int bottomDelta = static_cast<int>(bottomAfter) - static_cast<int>(bottomBefore);

            char line[288]{};
            std::snprintf(line, sizeof(line),
                "[3/5] 본 1 +x 이동 — 오른쪽 끝 변화: 위 %+d · 가운데 %+d · 아래 %+d (px)"
                " · 표본 행 %u/%u/%u(띠 %u~%u)\n",
                topDelta, middleDelta, bottomDelta,
                kTopRow, kMiddleRow, kBottomRow, top, bottom);
            outLog += line;

            // ② 위 층은 실제로 밀린다.
            if (topDelta < 10)
            {
                outLog += "위 층이 안 움직인다 — 스키닝이 정점에 닿지 않는다\n";
                passed = false;
            }
            // 아래 층은 본 0(항등)이라 그대로여야 한다. 여기가 움직이면
            // 팔레트 인덱스가 무시되고 한 본이 전체에 곱해진 것이다.
            if (std::abs(bottomDelta) > 2)
            {
                outLog += "아래 층이 움직였다 — 본 인덱스가 무시된다\n";
                passed = false;
            }
            // ③ 가중 혼합 — 가운데는 위의 절반쯤(±25%)이어야 한다.
            const double expectedMiddle = topDelta * 0.5;
            if (middleDelta < expectedMiddle * 0.75 || middleDelta > expectedMiddle * 1.25)
            {
                outLog += "가운데 층이 절반이 아니다 — 가중 합이 선형이 아니다\n";
                passed = false;
            }
        }
    }

    // ── [4/4] 비스킨드는 그대로 ──
    //
    // 같은 두 프레임에서 오른쪽 사각형의 커버리지가 한 픽셀도 달라지면 안 된다.
    if (passed)
    {
        // ★ 반대 방향으로 크게 흔든다. +x로 키우면 스킨드 띠가 경계를 넘어
        //   비스킨드 구역으로 들어와, '비스킨드가 늘었다'로 오검출된다
        //   (실제로 +2에서 1922 → 2208이 나왔고 원인이 그것이었다).
        //   -x면 띠는 화면 왼쪽으로 달아나므로 오른쪽 구역은 순수하다.
        palette[1] = math::translation_matrix(math::vector3{ -3.f, 0.f, 0.f });

        SkinCapture shakenCapture{};
        if (!renderOnce(draws, shakenCapture))
        {
            passed = false;
        }
        else
        {
            // 비스킨드 사각형은 월드 x 2.5~3.5에 있다 — 스킨드 띠가 절대
            // 넘지 않는 경계 오른쪽만 센다.
            const auto countRightRegion = [](const SkinCapture& capture)
            {
                uint32_t count = 0;
                for (uint32_t y = 0; y < kSkinHeight; ++y)
                    for (uint32_t x = kSkinnedRegionLimit; x < kSkinWidth; ++x)
                        if (capture.At(x, y)) ++count;
                return count;
            };

            const uint32_t staticBefore = countRightRegion(identityCapture);
            const uint32_t staticAfter = countRightRegion(shakenCapture);

            char line[192]{};
            std::snprintf(line, sizeof(line),
                "[4/5] 비스킨드 사각형(화면 오른쪽 1/4) — 항등 %u · 본 흔든 뒤 %u\n",
                staticBefore, staticAfter);
            outLog += line;

            if (0 == staticBefore)
            {
                outLog += "비스킨드 사각형이 안 보인다 — 대조군이 성립하지 않는다\n";
                passed = false;
            }
            if (staticBefore != staticAfter)
            {
                outLog += "비스킨드가 본을 따라 움직였다 — 스키닝 분기가 새고 있다\n";
                passed = false;
            }
        }
    }

    // ── [5/5] 그림자 패스도 같은 팔레트를 따라가는가 ──
    //
    // GBuffer만 스키닝하면 캐릭터는 움직이는데 그림자는 바인드 포즈로 남는다.
    // 그 증상은 '그림자가 좀 이상하다'로만 보여서 오래 방치되기 쉽다.
    //
    // 캐스터는 제품과 같은 재질 그래프 경로로 그린다. 같은 장면을 바인드 포즈와
    // 본을 흔든 포즈로 두 번 그려 맵이 실제로 달라지는지 본다 — 캐스터 수를 세는
    // 것보다 강하다. 그림자가 팔레트를 무시하면 두 맵이 같아진다.
    if (passed)
    {
        RHIReadback shadowReadback{};
        EnhancedShadowPass shadow;
        GraphSceneFixture casters;
        char suiteFlag[8]{};
        size_t suiteBytes{};
        const bool suite = getenv_s(&suiteBytes, suiteFlag, sizeof(suiteFlag), "CREATOR_METEOR_SHADOW_CASE_SUITE") == 0 &&
            std::strcmp(suiteFlag, "1") == 0;
        if (!resources.CreateReadback(EnhancedShadowPass::kShadowMapSize,
                EnhancedShadowPass::kShadowMapSize, FromDXGI(DXGI_FORMAT_R32_FLOAT), 3,
                shadowReadback, error) ||
            !shadow.Initialize(frameContext, error) ||
            !casters.Initialize(frameContext, error, suite ? .5f : 1.f))
        {
            outLog += "[5/5] 그림자 준비 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            // 방향광이 있어야 캐스케이드가 선다.
            std::vector<EnhancedLight> lights(1);
            lights[0].position = math::vector4(0.f, 0.f, 0.f, 0.f);   // w=0 방향광
            const math::vector3 lightDirection = math::normalize(
                math::vector3{ 0.3f, -1.f, 0.4f });
            lights[0].direction = math::vector4{
                lightDirection.x, lightDirection.y, lightDirection.z, 0.f };
            lights[0].color = math::color(1.f, 1.f, 1.f, 1.f);
            frameContext.lights = &lights;
            frameContext.draws = &draws;
            // 캐스터 드로우는 같은 팔레트 포인터를 들고 간다. 봉인이 프레임마다
            // 그 시점의 포즈를 복사한다.
            const std::vector<std::string> cases = suite
                ? std::vector<std::string>{"base", "alpha-pass", "alpha-equal", "alpha-reject",
                    "face-front", "face-back", "face-double", "transform", "boundary-near", "boundary-far", "far", "multi"}
                : std::vector<std::string>{"base"};
            for (const auto& caseName : cases)
            {
            std::vector<EnhancedDrawItem> shadowCasters{
                casters.Caster(draws[0]), casters.Caster(draws[1]) };
            frameContext.shadowDraws = &shadowCasters;
            auto& staticCaster = shadowCasters[1];
            if (caseName.starts_with("alpha-"))
            {
                staticCaster.coverage.flags |= EnhancedMaterialCoverage::Masked;
                staticCaster.coverage.cutoff = caseName == "alpha-pass" ? .4f : caseName == "alpha-equal" ? .5f : .6f;
            }
            if (caseName.starts_with("face-"))
            {
                if (caseName != "face-front")
                {
                    auto reversed = staticIndices;
                    for (size_t triangle = 0; triangle < reversed.size(); triangle += 3)
                        std::swap(reversed[triangle + 1], reversed[triangle + 2]);
                    staticCaster = casters.Draw(casters.AddMesh(staticVertices, reversed), draws[1].worldMatrix);
                }
                if (caseName != "face-double") staticCaster.coverage.flags = EnhancedMaterialCoverage::Enabled;
            }
            if (caseName == "transform")
            {
                auto& world = staticCaster.worldMatrix;
                world.m[0][0] = -1.7f; world.m[1][1] = .6f; world.m[2][2] = 1.2f;
                world.m[0][1] = .3f;
                world.m[3][0] = 8.f; world.m[3][1] = 3.f; world.m[3][2] = 2.f;
            }
            if (caseName.starts_with("boundary-"))
            {
                if (!shadow.PrepareGraphFrame(frameContext, error)) { passed = false; break; }
                const float split = shadow.GetShadowData().splitDepths.x;
                const float offset = caseName == "boundary-near" ? -.01f : .01f;
                auto& world = staticCaster.worldMatrix;
                // Tilt the quad across the camera's first split, with its center
                // on either side. Camera here looks along +Z from (0,0,-10).
                world.m[0][2] = .8f;
                world.m[3][2] = split + offset - 10.f - 2.4f;
                outLog += "SHADOW_BOUNDARY case=" + caseName + " split=" + std::to_string(split) +
                    " centerDepth=" + std::to_string(split + offset) + " minDepth=" +
                    std::to_string(split + offset - .4f) + " maxDepth=" + std::to_string(split + offset + .4f) + "\n";
            }
            if (caseName == "far")
            {
                auto& world = staticCaster.worldMatrix;
                world.m[3][0] = -lightDirection.x * 1000.f;
                world.m[3][1] = -lightDirection.y * 1000.f;
                world.m[3][2] = -lightDirection.z * 1000.f;
                const float depth = 10.f + world.m[3][2];
                if (depth >= camera.nearPlane && depth <= camera.farPlane) passed = false;
                outLog += "SHADOW_FAR viewDepth=" + std::to_string(depth) + " near=" +
                    std::to_string(camera.nearPlane) + " far=" + std::to_string(camera.farPlane) + "\n";
            }
            if (caseName == "multi")
            {
                std::vector<Vertex> grid;
                std::vector<uint32_t> indices;
                for (unsigned y = 0; y <= 16; ++y)
                    for (unsigned x = 0; x <= 16; ++x)
                    {
                        Vertex vertex{};
                        vertex.position = {2.5f + x / 16.f, -1.f + y / 8.f, 0.f};
                        vertex.normal = {0.f, 0.f, -1.f};
                        grid.push_back(vertex);
                    }
                for (unsigned y = 0; y < 16; ++y)
                    for (unsigned x = 0; x < 16; ++x)
                    {
                        const auto a = y * 17 + x;
                        indices.insert(indices.end(), {a, a + 1, a + 18, a, a + 18, a + 17});
                    }
                staticCaster = casters.Draw(casters.AddMesh(grid, indices), draws[1].worldMatrix);
                // Row bands form distinct meshlets; stretch across cascade clips
                // so compaction must retain a proper subset, not just all/none.
                staticCaster.worldMatrix.m[1][1] = 32.f;
                outLog += "SHADOW_MULTI vertices=289 triangles=512 worldScaleY=32\n";
            }
            if (suite) outLog += "SHADOW_CASE case=" + caseName + " alpha=0.5 flags=" +
                std::to_string(staticCaster.coverage.flags) + " cutoff=" + std::to_string(staticCaster.coverage.cutoff) + "\n";

            const auto renderShadow = [&](RHIReadbackImage& outMap,
                const char* label, std::array<math::matrix4x4, 3>& matrices) -> bool
            {
                ++frameContext.frameId;
                if (!resources.BeginFrame(error) || !casters.Prepare(frameContext, shadow, error))
                {
                    outLog += "[5/5] 그림자 준비 실패: " + error + "\n";
                    return false;
                }
                // ★ 그래프는 제출 이후까지 살아 있어야 한다.
                EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned);
                const RGHandle shadowMap = casters.Declare(graph, shadow, frameContext);
                const auto outputs = casters.ShadowMeshletOutputs();
                struct Readbacks {
                    struct Row {
                        std::array<RHIReadback, 4> buffers;
                        std::vector<std::pair<uint32_t, RHIReadback>> packets;
                        bool sparse{};
                        RHIReadback& operator[](size_t index) { return buffers[index]; }
                        const RHIReadback& operator[](size_t index) const { return buffers[index]; }
                    };
                    IRenderDeviceServices& device;
                    std::vector<Row> rows;
                    ~Readbacks() { for (auto& row : rows) {
                        for (auto& item : row.buffers) device.ReleaseReadback(item);
                        for (auto& packet : row.packets) device.ReleaseReadback(packet.second);
                    } }
                } lists{resources};
                if (outputs.empty()) { outLog += "Missing shadow compute outputs.\n"; return false; }
                for (const auto& output : outputs) {
                    const auto& geometry = output.frame->MeshletGeometry(output.bin);
                    auto& row = lists.rows.emplace_back();
                    auto metadataBytes = geometry->Meshlets().size;
                    if (geometry->UsesDirectClusters() && geometry->ClusterResidency()->residencyAlignment == RHISparseTileBytes)
                    {
                        row.sparse = true;
                        const auto& input = *geometry->Input();
                        RHIModelMeshletUpload metadata;
                        material_graph::SparseClusterPayload sparse;
                        if (!PackMeteorMeshletGpuPayload(input.Meshlets(), input.Count(), input.Geometry().vertexStride,
                            input.Geometry().vertexAttributeMask, input.Geometry().sourceLodIndex, input.SourcePrimitiveCount(),
                            input.SourceVertexCount(), input.SourceVertexIds(), metadata, error, &input.Hierarchy())) return false;
                        material_graph::AppendClusterTopology(metadata, input, *geometry->ClusterResidency(), &sparse);
                        metadataBytes = metadata.bytes.size();
                        for (const auto& packet : sparse.packets)
                        {
                            auto& read = row.packets.emplace_back();
                            read.first = packet.offset;
                            if (!resources.CreateBufferReadback(packet.bytes->size(), read.second, error)) return false;
                        }
                    }
                    if (!resources.CreateBufferReadback(20, row[0], error) ||
                        !resources.CreateBufferReadback((geometry->MeshletCount() + 1) * 4, row[1], error) ||
                        !resources.CreateBufferReadback(geometry->BufferBytes(), row[2], error) ||
                        !resources.CreateBufferReadback(metadataBytes, row[3], error)) return false;
                    std::vector<EnhancedRenderGraph::RGPassUsage> uses;
                    output.frame->AddReadUsages(graph, uses);
                    for (auto& use : uses) use.state = RHIResourceState::CopySource;
                    uses.push_back({geometry->GraphOutput(graph), RHIResourceState::CopySource,
                        graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read});
                    uses.push_back({geometry->GraphMeshlets(graph), RHIResourceState::CopySource,
                        graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read});
                    graph.AddPass("Skinning.ShadowListReadback", uses, [output, row](const auto& execution) {
                        execution.encoder->CopyBufferToReadback(row[0], output.frame->Arguments(), output.frame->ArgsOffset(output.bin));
                        execution.encoder->CopyBufferToReadback(row[1], output.frame->VisibleIds(output.frame->MeshletOutputOffset(output.bin),
                            output.frame->MeshletGeometry(output.bin)->MeshletCount() + 1).buffer,
                            std::uint64_t(output.frame->MeshletOutputOffset(output.bin)) * sizeof(std::uint32_t));
                        execution.encoder->CopyBufferToReadback(row[2], output.frame->MeshletGeometry(output.bin)->Buffer());
                        const auto data = output.frame->MeshletGeometry(output.bin)->Meshlets();
                        execution.encoder->CopyBufferToReadback(row[3], data.buffer, data.offset);
                        for (const auto& packet : row.packets)
                            execution.encoder->CopyBufferToReadback(packet.second, data.buffer, data.offset + packet.first);
                    }, true);
                }
                // 리드백이 소비자다 — 없으면 그래프가 그림자 패스를 걷어낸다.
                graph.AddPass("Skinning.ShadowReadback",
                    { { shadowMap, RHIResourceState::CopySource, RGAccessMode::Read } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {
                        for (unsigned cascade = 0; cascade < 3; ++cascade)
                            executeContext.encoder->CopyToReadback(
                                shadowReadback, executeContext.ResolveHandle(shadowMap), cascade, cascade);
                    }, true);
                if (!graph.Compile(error) || !graph.Execute(error))
                {
                    outLog += "[5/5] 그림자 실행 실패: " + error + "\n";
                    return false;
                }
                if (!resources.EndFrame(error) || !casters.Publish(resources, frameContext.frameId, error) ||
                    !GraphSceneFixture::CompleteSubmission(resources, error))
                {
                    outLog += "[5/5] 그림자 제출 실패: " + error + "\n";
                    return false;
                }
                resources.WaitForGpu();
                if (!resources.MapReadback(shadowReadback, outMap, error))
                {
                    outLog += "[5/5] 그림자 리드백 실패: " + error + "\n";
                    return false;
                }
                for (size_t outputIndex = 0; outputIndex < outputs.size(); ++outputIndex) {
                    const auto& output = outputs[outputIndex];
                    const auto& geometry = output.frame->MeshletGeometry(output.bin);
                    std::array<RHIReadbackImage, 4> images;
                    for (unsigned i = 0; i < 4; ++i)
                        if (!resources.MapReadback(lists.rows[outputIndex][i], images[i], error)) return false;
                    if (lists.rows[outputIndex].sparse)
                    {
                        // Reconstruct only ranges actually copied from mapped GPU
                        // tiles. Never copy unmapped virtual gaps from the device.
                        images[3].data.resize(geometry->Meshlets().size);
                        for (const auto& packet : lists.rows[outputIndex].packets)
                        {
                            RHIReadbackImage bytes;
                            if (!resources.MapReadback(packet.second, bytes, error) ||
                                packet.first > images[3].data.size() || bytes.data.size() > images[3].data.size() - packet.first) return false;
                            std::memcpy(images[3].data.data() + packet.first, bytes.data.data(), bytes.data.size());
                        }
                    }
                    const auto args = images[0].Elements<RHIDrawIndexedIndirectArguments>();
                    const auto ids = images[1].Elements<std::uint32_t>();
                    std::vector<material_graph::SurfacePoint> points;
                    if (!ShadowReadbackPoints(*geometry, std::as_bytes(std::span(images[2].data)),
                        std::as_bytes(std::span(images[3].data)), points, error))
                    {
                        outLog += "Shadow point decode failed: " + error + "\n";
                        return false;
                    }
                    const auto count = geometry->MeshletCount();
                    if (images[1].ElementCount<std::uint32_t>() != count + 1) return false;
                    const auto survivorCount = ids[0];
                    if (images[0].ElementCount<RHIDrawIndexedIndirectArguments>() != 1 || survivorCount > count ||
                        args[0].indexCount != (std::min)(survivorCount, 65535u) ||
                        args[0].instanceCount != (std::max)(1u, (survivorCount + 65534u) / 65535u) || args[0].firstIndex != 1 ||
                        images[2].data.size() != geometry->BufferBytes()) { outLog += "Invalid shadow list capacity/arguments.\n"; return false; }
                    const auto& topology = geometry->Input()->Meshlets();
                    if (topology.descriptors.size() != count) return false;
                    const auto& sealed = *geometry->Input();
                    RHIModelMeshletUpload packed;
                    if (!PackMeteorMeshletGpuPayload(topology, sealed.Count(), sealed.Geometry().vertexStride,
                        sealed.Geometry().vertexAttributeMask, sealed.Geometry().sourceLodIndex,
                        sealed.SourcePrimitiveCount(), sealed.SourceVertexCount(), sealed.SourceVertexIds(), packed, error, &sealed.Hierarchy())) return false;
                    if (geometry->UsesDirectClusters())
                    {
                        material_graph::SparseClusterPayload sparse;
                        const bool tiled = geometry->ClusterResidency()->residencyAlignment == RHISparseTileBytes;
                        material_graph::AppendClusterTopology(packed, sealed, *geometry->ClusterResidency(), tiled ? &sparse : nullptr);
                        if (tiled)
                        {
                            packed.bytes.resize(sparse.virtualBytes);
                            for (const auto& packet : sparse.packets)
                                std::memcpy(packed.bytes.data() + packet.offset, packet.bytes->data(), packet.bytes->size());
                        }
                    }
                    if (images[3].data.size() != packed.bytes.size() ||
                        std::memcmp(images[3].data.data(), packed.bytes.data(), packed.bytes.size()) != 0)
                    { outLog += "METEOR GPU cooked meshlet payload identity mismatch.\n"; return false; }
                    RHIMeshletBufferHeader actualHeader;
                    std::memcpy(&actualHeader, images[3].data.data(), sizeof(actualHeader));
                    if (actualHeader.schemaVersion != 5 || actualHeader.profileVersion != experiment::kMeshletProfileVersion)
                    { outLog += "METEOR GPU meshlet payload schema/profile mismatch.\n"; return false; }
                    outLog += "MESHLET_GPU_PAYLOAD case=" + caseName + " label=" + label + " cascade=" +
                        std::to_string(output.cascade) + " bytes=" + std::to_string(packed.bytes.size()) +
                        " schema=" + std::to_string(actualHeader.schemaVersion) + " profile=" + std::to_string(actualHeader.profileVersion) + " identity=exact\n";
                    ce::dynamic_bitset survivors(count);
                    for (unsigned i = 0; i < survivorCount; ++i) {
                        if (ids[i + 1] >= count || survivors.test(ids[i + 1]))
                        {
                            outLog += "Invalid/duplicate survivor ID.\n";
                            return false;
                        }
                        survivors.set(ids[i + 1], true);
                    }
                    // The readback contains posed object-space points. Match the
                    // renderer's object -> world -> light-clip transform.
                    const auto matrix = geometry->ObjectToWorld() * shadow.GetShadowData().lightViewProjection[output.cascade];
                    unsigned required{}, missing{}, extra{};
                    for (unsigned meshlet = 0; meshlet < count; ++meshlet) {
                        const auto& descriptor = topology.descriptors[meshlet];
                        unsigned outside = 63;
                        for (unsigned vertex = 0; vertex < descriptor.vertexCount; ++vertex) {
                            const auto& p = points[topology.vertexRemap[descriptor.vertexOffset + vertex]].position;
                            const float pos[4]{p[0], p[1], p[2], 1}; float clip[4]{};
                            for (unsigned c = 0; c < 4; ++c) for (unsigned r = 0; r < 4; ++r) clip[c] += pos[r] * matrix.m[r][c];
                            const float epsilon = std::max({1.f, std::abs(clip[0]), std::abs(clip[1]), std::abs(clip[2]), std::abs(clip[3])}) * 1e-5f;
                            unsigned mask{};
                            if (std::ranges::all_of(clip, [](float v) { return std::isfinite(v); }))
                                mask = (clip[0] < -clip[3] - epsilon ? 1u : 0u) | (clip[0] > clip[3] + epsilon ? 2u : 0u) |
                                    (clip[1] < -clip[3] - epsilon ? 4u : 0u) | (clip[1] > clip[3] + epsilon ? 8u : 0u) |
                                    (clip[2] < -epsilon ? 16u : 0u) | (clip[2] > clip[3] + epsilon ? 32u : 0u);
                            outside &= mask;
                        }
                        const bool expected = outside == 0;
                        required += expected; missing += expected && !survivors.test(meshlet); extra += !expected && survivors.test(meshlet);
                    }
                    outLog += "SHADOW_LIST " + std::string(suite ? "case=" + caseName + " " : "") + "label=" + label +
                        " cascade=" + std::to_string(output.cascade) + " draw=" + std::to_string(outputIndex / 3) +
                        " candidates=" + std::to_string(count) + " required=" + std::to_string(required) +
                        " survivors=" + std::to_string(survivorCount) + " missing=" + std::to_string(missing) +
                        " extra=" + std::to_string(extra) + "\n";
                    if (missing) { outLog += "CPU-required meshlet missing from GPU list.\n"; return false; }
                    if (suite && caseName == "multi" && output.cascade == 0 && count > 1 &&
                        (required == 0 || required == count)) {
                        outLog += "Multi-meshlet fixture did not exercise partial compaction.\n"; return false;
                    }
                }
                const auto routes = casters.RecordedShadowMeshletDrawCounts();
                const auto shadowCache = casters.ShadowCacheStats();
                for (unsigned cascade = 0; cascade < 3; ++cascade)
                {
                    matrices[cascade] = shadow.GetShadowData().lightViewProjection[cascade];
                    outLog += std::string("SHADOW_ROUTE ") + (suite ? "case=" + caseName + " " : "") + "label=" + label + " cascade=" +
                        std::to_string(cascade) + " meshDispatches=" + std::to_string(routes[cascade]) +
                        " staticCacheHit=" + std::to_string(shadowCache.hit) + "\n";
                    if (routes[cascade] == 0 && !(shadowCache.hit && shadowCache.staticCasters != 0 && shadowCache.dynamicCasters == 0))
                    {
                        outLog += "Requested shadow route was not recorded.\n";
                        return false;
                    }
                }
                char directory[32768]{};
                size_t directoryBytes{};
                if (getenv_s(&directoryBytes, directory, sizeof(directory), "CREATOR_METEOR_SHADOW_CAPTURE_DIR") == 0 &&
                    directoryBytes > 1)
                {
                    std::filesystem::create_directories(directory);
                    const auto capturePath = suite ? std::filesystem::path(directory) / caseName : std::filesystem::path(directory);
                    std::filesystem::create_directories(capturePath);
                    for (unsigned cascade = 0; cascade < 3; ++cascade)
                    {
                        const auto stem = std::string(label) + "-C" + std::to_string(cascade);
                        std::ofstream pixels(capturePath / (stem + ".f32"), std::ios::binary);
                        for (unsigned y = 0; y < EnhancedShadowPass::kShadowMapSize; ++y)
                            for (unsigned x = 0; x < EnhancedShadowPass::kShadowMapSize; ++x)
                            {
                                const float value = outMap.At(x, y, 0, cascade);
                                pixels.write(reinterpret_cast<const char*>(&value), sizeof(value));
                            }
                        pixels.close();
                        std::ofstream matrix(capturePath / (stem + ".matrix"), std::ios::binary);
                        matrix.write(reinterpret_cast<const char*>(&matrices[cascade]), sizeof(matrices[cascade]));
                        matrix.close();
                        if (!pixels || !matrix) { outLog += "Shadow evidence write failed.\n"; return false; }
                    }
                }
                return true;
            };

            RHIReadbackImage bindPose{}, posed{};
            std::array<math::matrix4x4, 3> bindMatrices{}, posedMatrices{};
            const math::matrix4x4 posedBone = palette[1];
            palette[1] = math::matrix4x4::identity();
            bool rendered = renderShadow(bindPose, "meshlet-bind", bindMatrices);
            palette[1] = posedBone;
            rendered = rendered && renderShadow(posed, "meshlet-posed", posedMatrices);

            if (!rendered)
            {
                passed = false;
            }
            else
            {

                // 클리어 값(1.0)이 아닌 텍셀과 두 포즈 사이에 달라진 텍셀.
                uint32_t shadowTexels = 0;
                uint32_t movedTexels = 0;
                for (uint32_t y = 0; y < EnhancedShadowPass::kShadowMapSize; ++y)
                    for (uint32_t x = 0; x < EnhancedShadowPass::kShadowMapSize; ++x)
                    {
                        if (posed.At(x, y, 0) < 0.999f) ++shadowTexels;
                        if (std::fabs(posed.At(x, y, 0) - bindPose.At(x, y, 0)) > 1e-6f) ++movedTexels;
                    }

                char line[256]{};
                std::snprintf(line, sizeof(line),
                    "[5/5] 그래프 그림자 캐스터 — 명령 %u · 맵 텍셀 %u · 포즈로 바뀐 텍셀 %u\n",
                    casters.ShadowDrawCount(), shadowTexels, movedTexels);
                outLog += line;

                if (0 == shadowTexels)
                {
                    outLog += "그림자 맵이 비었다 — 드로우는 났는데 깊이가 안 남았다\n";
                    passed = false;
                }
                if (0 == movedTexels)
                {
                    outLog += "본을 흔들어도 그림자 맵이 그대로다 — 그림자만 바인드 포즈로 남는다\n";
                    passed = false;
                }
            }
            }
            frameContext.shadowDraws = nullptr;
        }
        casters.Shutdown();
        shadow.Shutdown();
        resources.ReleaseReadback(shadowReadback);
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    gbuffer.Shutdown();
    textureCache.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "스키닝 검증 통과(GBuffer·그림자)\n" : "스키닝 검증 실패\n";
    return passed;
}
