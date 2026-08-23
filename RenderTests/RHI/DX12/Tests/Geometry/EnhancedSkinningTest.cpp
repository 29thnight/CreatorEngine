#include "Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Mesh.h"
// DeviceState.h include가 여기 있었다 (E, 2026-08-09).
// 이 파일에서 DirectX11:: 심볼을 쓰는 코드가 0이다.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

bool EnhancedSceneRenderer::RunSkinningTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

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

    EnhancedGBufferPass gbuffer;
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
        const Mathf::xVector eye = XMVectorSet(0.f, 0.f, -10.f, 1.f);
        const Mathf::xVector at = XMVectorSet(0.f, 0.f, 0.f, 1.f);
        const Mathf::xVector up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
        camera.view = XMMatrixLookAtLH(eye, at, up);
        camera.projection = XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, 1.f, 0.1f, 100.f);
        camera.inverseView = XMMatrixInverse(nullptr, camera.view);
        camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
        camera.eyePosition = eye;
        camera.forward = XMVector3Normalize(XMVectorSubtract(at, eye));
        camera.right = XMVector3Normalize(XMVector3Cross(up, camera.forward));
        camera.up = XMVector3Cross(camera.forward, camera.right);
        camera.fov = DirectX::XM_PIDIV4;
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
    std::vector<Mathf::xMatrix> palette(2, XMMatrixIdentity());

    const auto renderOnce = [&](const std::vector<EnhancedDrawItem>& draws,
        SkinCapture& outCapture) -> bool
    {
        frameContext.draws = &draws;

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
        EnhancedRenderGraph graph(resources);
        gbuffer.Declare(graph, frameContext);

        const RGHandle bitmask = gbuffer.GetOutputs().bitmask;
        if (!bitmask.IsValid())
        {
            outLog += "bitmask 출력이 없다\n";
            return false;
        }

        graph.AddPass("Skinning.Readback",
            { { bitmask, RHIResourceState::CopySource } },
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
        resources.WaitForGpu();

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
    draws[0].worldMatrix = XMMatrixIdentity();
    draws[0].bonePalette = palette.data();
    draws[0].boneCount = 2;
    draws[0].animatorKey = 0x5EED;
    draws[1].mesh = &staticMesh;
    draws[1].worldMatrix = XMMatrixIdentity();
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
        palette[1] = XMMatrixTranslation(1.f, 0.f, 0.f);

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
        palette[1] = XMMatrixTranslation(-3.f, 0.f, 0.f);

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
    // 그림자 맵을 직접 읽는 대신 캐스터 통계로 본다 — 스킨드 PSO로 그린
    // 인스턴스가 실제로 있어야 하고, 팔레트도 올라가 있어야 한다.
    if (passed)
    {
        RHIReadback shadowReadback{};
        {
            std::string readbackError;
            if (!resources.CreateReadback(EnhancedShadowPass::kShadowMapSize,
                EnhancedShadowPass::kShadowMapSize, FromDXGI(DXGI_FORMAT_R32_FLOAT), 1,
                shadowReadback, readbackError))
            {
                outLog += "[5/5] 그림자 리드백 생성 실패: " + readbackError + "\n";
                passed = false;
            }
        }

        EnhancedShadowPass shadow;
        if (!passed)
        {
            // 리드백 생성이 실패했으면 아래를 돌릴 이유가 없다.
        }
        else if (!shadow.Initialize(frameContext, error))
        {
            outLog += "[5/5] 그림자 초기화 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            // 방향광이 있어야 캐스케이드가 선다.
            std::vector<EnhancedLight> lights(1);
            lights[0].position = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);   // w=0 방향광
            lights[0].direction = Mathf::Vector4(
                Mathf::Vector3(XMVector3Normalize(XMVectorSet(0.3f, -1.f, 0.4f, 0.f))));
            lights[0].color = Mathf::Color4(1.f, 1.f, 1.f, 1.f);
            frameContext.lights = &lights;
            frameContext.draws = &draws;

            if (!resources.BeginFrame(error) ||
                !shadow.PrepareFrame(frameContext, error))
            {
                outLog += "[5/5] 그림자 준비 실패: " + error + "\n";
                passed = false;
            }
            else
            {
                // ★ 그래프는 제출 이후까지 살아 있어야 한다.
                EnhancedRenderGraph graph(resources);
                shadow.Declare(graph, frameContext);

                // ★ 소비자를 붙여야 한다.
                //
                // 그림자 패스는 hasSideEffect=false로 선언된다 — 실전에서는
                // Deferred가 읽으므로 역방향 도달로 살아남지만, 여기에는 그
                // 소비자가 없어 그래프가 통째로 걷어낸다. 실제로 첫 실행이
                // '드로우 0 · 컬링 0'으로 그 사실을 알려 줬다(컬링 0이 결정적
                // 단서다 — 판정까지 갔으면 컬린 수라도 셌을 것이다).
                //
                // 리드백을 붙이면 소비자가 생기는 동시에 '그림자 맵에 실제로
                // 깊이가 남았는가'도 볼 수 있다.
                const RGHandle shadowMap = shadow.GetShadowMap();
                graph.AddPass("Skinning.ShadowReadback",
                    { { shadowMap, RHIResourceState::CopySource } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {
                        // 첫 캐스케이드만(서브리소스 0).
                        executeContext.encoder->CopyToReadback(
                            shadowReadback, executeContext.ResolveHandle(shadowMap), 0, 0);
                    }, true);

                if (!graph.Compile(error) ||
                    !graph.Execute(error))
                {
                    outLog += "[5/5] 그림자 실행 실패: " + error + "\n";
                    passed = false;
                }

                if (!resources.EndFrame(error))
                {
                    outLog += "[5/5] EndFrame 실패: " + error + "\n";
                    passed = false;
                }
                resources.WaitForGpu();

                // 그림자 맵에 실제로 깊이가 남았는지 — 클리어 값(1.0)이 아닌
                // 텍셀 수. 통계만 보면 '드로우는 냈는데 아무것도 안 그려진'
                // 경우를 통과시킨다.
                uint32_t shadowTexels = 0;
                {
                    RHIReadbackImage shadowCaptured{};
                    std::string readbackError;
                    if (resources.MapReadback(shadowReadback, shadowCaptured, readbackError))
                    {
                        for (uint32_t y = 0; y < EnhancedShadowPass::kShadowMapSize; ++y)
                            for (uint32_t x = 0; x < EnhancedShadowPass::kShadowMapSize; ++x)
                            {
                                if (shadowCaptured.At(x, y, 0) < 0.999f) ++shadowTexels;
                            }
                    }
                }

                char line[256]{};
                std::snprintf(line, sizeof(line),
                    "[5/5] 그림자 캐스터 — 드로우 %u · 스킨드 %u · 팔레트 %u · 컬링 %u"
                    " · 맵 텍셀 %u\n",
                    shadow.GetLastDrawCount(), shadow.GetLastSkinnedDrawCount(),
                    shadow.GetLastBonePaletteCount(), shadow.GetLastCulledCount(),
                    shadowTexels);
                outLog += line;

                if (0 == shadowTexels)
                {
                    outLog += "그림자 맵이 비었다 — 드로우는 났는데 깊이가 안 남았다\n";
                    passed = false;
                }

                if (0 == shadow.GetLastSkinnedDrawCount())
                {
                    outLog += "그림자에 스킨드 캐스터가 없다 — 그림자만 바인드 포즈로 남는다\n";
                    passed = false;
                }
                if (1 != shadow.GetLastBonePaletteCount())
                {
                    outLog += "그림자 팔레트 수가 1이 아니다 — 수집이 GBuffer와 다르다\n";
                    passed = false;
                }
            }

            shadow.Shutdown();
        }
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
