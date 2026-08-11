#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGridPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 그리드 패스 자가 검증 (PHASE 3-6, Gizmo 계열 첫 슬라이스).
//
// ── 넷을 따로 단정한다 ──
//
//   ① 라인   — 원점 교차선 자리에 선이 실제로 그려지는가
//   ② 내부   — 셀 가운데는 투명한가(전부 선이 되는 실패를 잡는다)
//   ③ 밀도   — 화면에서 선이 차지하는 비율이 상식 범위인가
//   ④ 카메라 — 측면 카메라에서 지평선 위가 비는가
//
// ★ ④가 이 검증의 핵심이다. 상수가 셰이더에 안 닿으면 두 카메라가 같은
//   그림을 낸다 — dx12.scene에서 세운 것과 같은 원칙으로, '그려지긴 하는데
//   카메라를 무시한다'를 잡는다. 하향 카메라는 화면 전체가 바닥이라
//   지평선이 없고, 측면 카메라만 위쪽이 빈다.
//
// ★ 라인 표본은 한 픽셀이 아니라 5x5 창의 최댓값을 본다. 기본 두께의
//   선은 1픽셀 안팎이라, 투영이 반 픽셀만 어긋나도 정확한 한 점 표본은
//   선을 놓친다 — 그건 패스의 실패가 아니라 표본의 실패다.
namespace
{
    constexpr uint32_t kGridWidth = 256;
    constexpr uint32_t kGridHeight = 256;

    // 리드백을 프레임 사이에 재사용하므로 표본은 복사본에서 뜬다.
    //
    // ★ 배관과 '읽는 법'은 RHIReadbackImage가 갖는다(R2c-b). 여기 남는 것은
    //   이 검사에만 있는 질의뿐이다. 예전에는 kGridRowPitch(행 정렬)와
    //   GridHalfToFloat(반정밀도 디코드)까지 검사마다 한 벌씩 있었고,
    //   그 둘이 열여덟·열일곱 번 복제돼 있었다 — 본문은 바이트 단위로 같았다.
    struct GridCapture
    {
        RHIReadbackImage image;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            return image.At(x, y, channel);
        }

        float MaxInWindow(uint32_t centerX, uint32_t centerY, uint32_t radius,
            uint32_t channel) const
        {
            float best = 0.f;
            const uint32_t x0 = (centerX > radius) ? centerX - radius : 0;
            const uint32_t y0 = (centerY > radius) ? centerY - radius : 0;
            const uint32_t x1 = (centerX + radius < kGridWidth) ? centerX + radius : kGridWidth - 1;
            const uint32_t y1 = (centerY + radius < kGridHeight) ? centerY + radius : kGridHeight - 1;
            for (uint32_t y = y0; y <= y1; ++y)
                for (uint32_t x = x0; x <= x1; ++x)
                    best = (std::max)(best, At(x, y, channel));
            return best;
        }

        // R 채널로 '선이 있는' 픽셀을 센다. 선 색이 (0.45, ...)라 알파 0.5로
        // 블렌드되면 R이 0.2 안팎이고, 빈 곳은 0이다.
        uint32_t CountLit(uint32_t yBegin, uint32_t yEnd, float threshold) const
        {
            uint32_t lit = 0;
            for (uint32_t y = yBegin; y < yEnd; ++y)
                for (uint32_t x = 0; x < kGridWidth; ++x)
                    if (At(x, y, 0) > threshold) ++lit;
            return lit;
        }
    };

    // 월드 점을 화면 픽셀로. 셰이더와 같은 규약(행 벡터 v * VP)이므로
    // 여기서 계산한 자리와 셰이더가 그린 자리가 어긋나면 그것이 곧 발견이다.
    bool GridProjectToPixel(const Mathf::xMatrix& view, const Mathf::xMatrix& projection,
        float worldX, float worldZ, uint32_t& outX, uint32_t& outY)
    {
        const Mathf::xMatrix vp = XMMatrixMultiply(view, projection);
        const Mathf::xVector clip = XMVector4Transform(
            XMVectorSet(worldX, 0.f, worldZ, 1.f), vp);
        const float w = XMVectorGetW(clip);
        if (w <= 1e-6f) return false;

        const float ndcX = XMVectorGetX(clip) / w;
        const float ndcY = XMVectorGetY(clip) / w;
        if (ndcX < -1.f || ndcX > 1.f || ndcY < -1.f || ndcY > 1.f) return false;

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * static_cast<float>(kGridWidth));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) * static_cast<float>(kGridHeight));
        if (outX >= kGridWidth) outX = kGridWidth - 1;
        if (outY >= kGridHeight) outY = kGridHeight - 1;
        return true;
    }
}

bool EnhancedSceneRenderer::RunGridTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 그리드 패스 검증 (PHASE 3-6, Gizmo 계열) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kGridWidth, kGridHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_grid.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kGridWidth;
    frameContext.height = kGridHeight;

    EnhancedGridPass grid;
    if (!grid.Initialize(frameContext, error))
    {
        outLog += "[1/4] 그리드 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 셰이더 컴파일·PSO 생성 통과\n";

    RHIReadback readback{};
    if (!resources.CreateReadback(kGridWidth, kGridHeight,
        EnhancedGridPass::kOutputFormat, 1, readback, error))
    {
        outLog += "[1/4] 리드백 생성 실패: " + error + "\n";
        grid.Shutdown();
        resources.Shutdown();
        return false;
    }

    bool passed = true;
    EnhancedRenderGraph::Stats lastStats{};

    // 한 프레임을 그려 리드백까지 뜬다. 두 카메라를 같은 경로로 돌린다.
    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot, GridCapture& outCapture)
        -> bool
    {
        frameContext.camera = &snapshot;

        if (!resources.BeginFrame(error))
        {
            outLog += "BeginFrame 실패: " + error + "\n";
            return false;
        }

        if (!grid.PrepareFrame(frameContext, error))
        {
            outLog += "PrepareFrame 실패: " + error + "\n";
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);
        grid.Declare(graph, frameContext);

        const RGHandle output = grid.GetOutput();
        if (!output.IsValid())
        {
            outLog += "그리드 출력이 선언되지 않았다\n";
            return false;
        }

        graph.AddPass("Grid.Readback",
            { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(output));
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
        lastStats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "EndFrame 실패: " + error + "\n";
            return false;
        }
        resources.WaitForGpu();

        if (!resources.MapReadback(readback, outCapture.image, error))
        {
            outLog += error + "\n";
            return false;
        }
        return true;
    };

    // ── 하향 카메라 — 라인·내부·밀도 ──
    //
    // 눈 (0,30,0)에서 원점을 내려다본다. 위쪽은 +Z. 눈의 XZ를 정수(0)에
    // 두는 이유: 셰이더가 카메라 위치를 int3로 잘라 쿼드를 재중심하므로
    // 비정수 위치면 표본 계산에 그 잘림까지 넣어야 한다.
    FrameCameraSnapshot topDown{};
    topDown.view = XMMatrixLookAtLH(
        XMVectorSet(0.f, 30.f, 0.f, 1.f),
        XMVectorSet(0.f, 0.f, 0.f, 1.f),
        XMVectorSet(0.f, 0.f, 1.f, 0.f));
    topDown.projection = XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 500.f);
    topDown.eyePosition = XMVectorSet(0.f, 30.f, 0.f, 1.f);

    GridCapture capture{};
    if (passed && !renderOnce(topDown, capture)) passed = false;

    if (passed)
    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 그래프 — 선언 %u · 실행 %u · 컬링 %u · transient %u · 배리어 %u건/%u번\n",
            lastStats.passesDeclared, lastStats.passesExecuted, lastStats.passesCulled,
            lastStats.transientCreated, lastStats.barriersEmitted, lastStats.barrierBatches);
        outLog += line;

        // 그리드 + 리드백. 리드백이 소비자이므로 그리드는 뿌리 표시 없이
        // 역방향 도달로 살아남아야 한다(3-5 컬링 검증과 같은 원리).
        if (2 != lastStats.passesExecuted || 0 != lastStats.passesCulled)
        {
            outLog += "패스 수가 다르다 — 그리드가 컬링됐거나 사슬이 끊겼다\n";
            passed = false;
        }
        if (2 != lastStats.transientCreated)
        {
            outLog += "transient가 둘(색·깊이)이 아니다\n";
            passed = false;
        }
    }

    if (passed)
    {
        // ① 라인 — 원점 (0,0)은 주선 두 개가 교차하는 자리다.
        uint32_t lineX = 0, lineY = 0;
        // ② 내부 — (0.5, 0.5)는 부선 셀의 한가운데다(부선 간격 1.0).
        uint32_t cellX = 0, cellY = 0;
        if (!GridProjectToPixel(topDown.view, topDown.projection, 0.f, 0.f, lineX, lineY) ||
            !GridProjectToPixel(topDown.view, topDown.projection, 0.5f, 0.5f, cellX, cellY))
        {
            outLog += "[3/4] 표본 투영 실패 — 카메라 행렬이 화면을 벗어난다\n";
            passed = false;
        }
        else
        {
            const float lineR = capture.MaxInWindow(lineX, lineY, 2, 0);
            const float lineA = capture.MaxInWindow(lineX, lineY, 2, 3);
            const float cellR = capture.At(cellX, cellY, 0);
            const float cellA = capture.At(cellX, cellY, 3);

            const uint32_t litTotal = capture.CountLit(0, kGridHeight, 0.05f);
            const float litRatio =
                static_cast<float>(litTotal) / static_cast<float>(kGridWidth * kGridHeight);

            char line[256]{};
            std::snprintf(line, sizeof(line),
                "[3/4] 원점 선(R %.3f A %.3f, px %u,%u) · 셀 내부(R %.3f A %.3f, px %u,%u) · "
                "점등 %u(%.1f%%)\n",
                lineR, lineA, lineX, lineY, cellR, cellA, cellX, cellY,
                litTotal, litRatio * 100.f);
            outLog += line;

            // 선 색 (0.45,...) x 알파 0.5 블렌드 → R이 0.2 안팎. 절반을 여유로 둔다.
            if (lineR < 0.1f)
            {
                outLog += "원점 교차선 자리가 비었다 — 그리드가 안 그려지거나 자리가 틀렸다\n";
                passed = false;
            }
            if (cellR > 0.05f || cellA > 0.05f)
            {
                outLog += "셀 가운데가 칠해졌다 — 선 두께 계산이 화면을 채우고 있다\n";
                passed = false;
            }
            // ③ 밀도 — 0이면 안 그려진 것이고, 화면 대부분이면 전부 선이 된 것이다.
            if (litRatio < 0.02f || litRatio > 0.6f)
            {
                outLog += "선 밀도가 상식 범위(2~60%)를 벗어났다\n";
                passed = false;
            }
        }
    }

    // ── 측면 카메라 — 카메라 반응 ──
    //
    // 눈 (0,3,0)에서 +X 지평선 쪽을 본다. 바닥 평면은 화면 아래쪽에만
    // 있으므로 위쪽 1/4은 비어야 한다. 상수가 셰이더에 안 닿으면 하향
    // 카메라와 같은 그림(화면 전체가 바닥)이 나와 이 단정이 잡는다.
    if (passed)
    {
        FrameCameraSnapshot side{};
        side.view = XMMatrixLookAtLH(
            XMVectorSet(0.f, 3.f, 0.f, 1.f),
            XMVectorSet(100.f, 0.f, 0.f, 1.f),
            XMVectorSet(0.f, 1.f, 0.f, 0.f));
        side.projection = XMMatrixPerspectiveFovLH(
            DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 500.f);
        side.eyePosition = XMVectorSet(0.f, 3.f, 0.f, 1.f);

        GridCapture sideCapture{};
        if (!renderOnce(side, sideCapture))
        {
            passed = false;
        }
        else
        {
            const uint32_t topLit = sideCapture.CountLit(0, kGridHeight / 4, 0.05f);
            const uint32_t bottomLit =
                sideCapture.CountLit(kGridHeight / 2, kGridHeight, 0.05f);

            char line[160]{};
            std::snprintf(line, sizeof(line),
                "[4/4] 측면 카메라 — 상단 1/4 점등 %u · 하단 1/2 점등 %u\n",
                topLit, bottomLit);
            outLog += line;

            if (0 != topLit)
            {
                outLog += "지평선 위가 칠해졌다 — 카메라 상수가 셰이더에 안 닿는다\n";
                passed = false;
            }
            if (0 == bottomLit)
            {
                outLog += "발밑에 선이 없다 — 측면 카메라에서 아무것도 안 그려졌다\n";
                passed = false;
            }
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    grid.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "그리드 패스 검증 통과\n" : "그리드 패스 검증 실패\n";
    return passed;
}

#endif
