#ifndef DYNAMICCPP_EXPORTS
#include "../../../../Render/Passes/Editor/EnhancedGizmoLinePass.h"
#include "../../DX12DeviceResources.h"
#include "../../DX12PSOManager.h"
#include "../../DX12RootSignatureCache.h"
#include "../../../../Render/Graph/EnhancedRenderGraph.h"
#include "../../../../Render/Scene/EnhancedSceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 기즈모 라인 패스 자가 검증 (PHASE 3-6, Gizmo 계열 2차 슬라이스).
//
// ── 넷을 따로 단정한다 ──
//
//   ① 도형 정점 수 — DX11 수식과 같은 수가 나오는가(구성의 동일성)
//   ② 픽셀        — 축 선·원이 제자리에 그려지고 빈 곳은 비는가
//   ③ 드로우 병합 — 도형 여럿이 드로우 하나로 나가는가
//   ④ 카메라      — 카메라를 옮기면 화면이 비는가
//
// ★ ③이 이 재작성의 존재 이유다. DX11은 도형마다 Map + 드로우가 나갔고
//   (캡슐 하나 = 드로우 12회), 병합해도 그림은 똑같이 나온다 — 수치로만
//   드러난다. GBuffer의 '드로우 704 배치 704' 교훈과 같은 부류다.
namespace
{
    constexpr uint32_t kGizmoWidth = 256;
    constexpr uint32_t kGizmoHeight = 256;

    struct GizmoCapture
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
            const uint32_t x1 = (centerX + radius < kGizmoWidth) ? centerX + radius : kGizmoWidth - 1;
            const uint32_t y1 = (centerY + radius < kGizmoHeight) ? centerY + radius : kGizmoHeight - 1;
            for (uint32_t y = y0; y <= y1; ++y)
                for (uint32_t x = x0; x <= x1; ++x)
                    best = (std::max)(best, At(x, y, channel));
            return best;
        }

        // 어느 채널이든 켜진 픽셀 수. 선 색이 전부 알파 1이라 켜졌으면 뚜렷하다.
        uint32_t CountLit(float threshold) const
        {
            uint32_t lit = 0;
            for (uint32_t y = 0; y < kGizmoHeight; ++y)
                for (uint32_t x = 0; x < kGizmoWidth; ++x)
                    if (At(x, y, 0) > threshold || At(x, y, 1) > threshold ||
                        At(x, y, 2) > threshold) ++lit;
            return lit;
        }
    };

    bool GizmoProjectToPixel(const Mathf::xMatrix& view, const Mathf::xMatrix& projection,
        float worldX, float worldY, float worldZ, uint32_t& outX, uint32_t& outY)
    {
        const Mathf::xMatrix vp = XMMatrixMultiply(view, projection);
        const Mathf::xVector clip = XMVector4Transform(
            XMVectorSet(worldX, worldY, worldZ, 1.f), vp);
        const float w = XMVectorGetW(clip);
        if (w <= 1e-6f) return false;

        const float ndcX = XMVectorGetX(clip) / w;
        const float ndcY = XMVectorGetY(clip) / w;
        if (ndcX < -1.f || ndcX > 1.f || ndcY < -1.f || ndcY > 1.f) return false;

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * static_cast<float>(kGizmoWidth));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) * static_cast<float>(kGizmoHeight));
        if (outX >= kGizmoWidth) outX = kGizmoWidth - 1;
        if (outY >= kGizmoHeight) outY = kGizmoHeight - 1;
        return true;
    }
}

bool EnhancedSceneRenderer::RunGizmoLineTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 기즈모 라인 패스 검증 (PHASE 3-6, Gizmo 계열) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kGizmoWidth, kGizmoHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_gizmoline.cache", error) ||
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
    frameContext.width = kGizmoWidth;
    frameContext.height = kGizmoHeight;

    EnhancedGizmoLinePass gizmo;
    if (!gizmo.Initialize(frameContext, error))
    {
        outLog += "[1/4] 기즈모 라인 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 셰이더 컴파일·PSO 생성 통과\n";

    bool passed = true;

    // ── ① 도형 정점 수 — DX11 수식과 같은가 ──
    //
    // 원 64세그(128정점) · 구 3원(384) · 박스 24 · 캡슐 16수직선(32)+구2(768)
    // +링2(256)=1056 · 콘 32세그x4(128) · 프러스텀 24. 수가 다르면 구성이
    // 원본과 갈라진 것이고, 그림 비교 전에 여기서 걸린다.
    //
    // GetLastVertexCount는 PrepareFrame에서 채워지므로, 도형마다
    // ResetLines → Add → PrepareFrame으로 갈라 센다.
    {
        std::string dummy;
        const auto countOf = [&](auto&& add) -> uint32_t
        {
            gizmo.ResetLines();
            add();
            gizmo.PrepareFrame(frameContext, dummy);
            return gizmo.GetLastVertexCount();
        };

        struct Expect { const char* name; uint32_t expected; uint32_t actual; };
        const Expect expects[] = {
            { "원", 128, countOf([&] {
                gizmo.AddWireCircle({ 0, 0, 0 }, 1.f, { 0, 1, 0 }, { 1, 1, 1, 1 }); }) },
            { "구", 384, countOf([&] {
                gizmo.AddWireSphere({ 0, 0, 0 }, 1.f, { 1, 1, 1, 1 }); }) },
            { "박스", 24, countOf([&] {
                gizmo.AddWireBox(Mathf::Matrix::Identity, { 1, 1, 1 }, { 1, 1, 1, 1 }); }) },
            { "캡슐", 1056, countOf([&] {
                gizmo.AddWireCapsule(Mathf::Matrix::Identity, 0.5f, 2.f, { 1, 1, 1, 1 }); }) },
            { "콘", 128, countOf([&] {
                gizmo.AddWireCone({ 0, 0, 0 }, { 0, -1, 0 }, 2.f, 45.f, { 1, 1, 1, 1 }); }) },
            { "프러스텀", 24, countOf([&] {
                const DirectX::BoundingFrustum frustum(
                    XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 10.f));
                gizmo.AddBoundingFrustum(frustum, { 1, 1, 1, 1 }); }) },
        };

        for (const auto& expect : expects)
        {
            if (expect.expected != expect.actual)
            {
                char line[160]{};
                std::snprintf(line, sizeof(line),
                    "%s 정점 수가 다르다 — 기대 %u · 실제 %u\n",
                    expect.name, expect.expected, expect.actual);
                outLog += line;
                passed = false;
            }
        }

        if (passed) outLog += "[2/4] 도형 정점 수 6종 — DX11 수식과 일치\n";
    }

    RHIReadback readback{};
    if (!resources.CreateReadback(kGizmoWidth, kGizmoHeight,
        EnhancedGizmoLinePass::kOutputFormat, 1, readback, error))
    {
        outLog += "리드백 생성 실패: " + error + "\n";
        gizmo.Shutdown();
        resources.Shutdown();
        return false;
    }

    EnhancedRenderGraph::Stats lastStats{};

    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot, GizmoCapture& outCapture)
        -> bool
    {
        frameContext.camera = &snapshot;

        if (!resources.BeginFrame(error))
        {
            outLog += "BeginFrame 실패: " + error + "\n";
            return false;
        }

        if (!gizmo.PrepareFrame(frameContext, error))
        {
            outLog += "PrepareFrame 실패: " + error + "\n";
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);
        gizmo.Declare(graph, frameContext);

        const RGHandle output = gizmo.GetOutput();
        if (!output.IsValid())
        {
            outLog += "기즈모 출력이 선언되지 않았다\n";
            return false;
        }

        graph.AddPass("GizmoLine.Readback",
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

    // ── ②③ 알려진 도형 — 축 십자 + 원, 그리고 드로우 병합 ──
    //
    // 눈 (0,30,0)에서 원점을 내려다본다. X축 선(빨강)·Z축 선(초록)·반지름 4
    // 원(파랑)을 함께 넣는다. 도형이 셋이어도 드로우는 하나여야 한다.
    FrameCameraSnapshot topDown{};
    topDown.view = XMMatrixLookAtLH(
        XMVectorSet(0.f, 30.f, 0.f, 1.f),
        XMVectorSet(0.f, 0.f, 0.f, 1.f),
        XMVectorSet(0.f, 0.f, 1.f, 0.f));
    topDown.projection = XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 500.f);
    topDown.eyePosition = XMVectorSet(0.f, 30.f, 0.f, 1.f);

    if (passed)
    {
        gizmo.ResetLines();
        gizmo.AddLine({ -5.f, 0.f, 0.f }, { 5.f, 0.f, 0.f }, { 1.f, 0.f, 0.f, 1.f });
        gizmo.AddLine({ 0.f, 0.f, -5.f }, { 0.f, 0.f, 5.f }, { 0.f, 1.f, 0.f, 1.f });
        gizmo.AddWireCircle({ 0.f, 0.f, 0.f }, 4.f, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f, 1.f });

        GizmoCapture capture{};
        if (!renderOnce(topDown, capture))
        {
            passed = false;
        }
        else
        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "[3/4] 그래프 — 선언 %u · 실행 %u · 컬링 %u · transient %u · "
                "정점 %u · 드로우 %u\n",
                lastStats.passesDeclared, lastStats.passesExecuted, lastStats.passesCulled,
                lastStats.transientCreated,
                gizmo.GetLastVertexCount(), gizmo.GetLastDrawCount());
            outLog += line;

            if (2 != lastStats.passesExecuted || 0 != lastStats.passesCulled ||
                1 != lastStats.transientCreated)
            {
                outLog += "그래프 통계가 다르다 — 패스 2·컬링 0·transient 1이어야 한다\n";
                passed = false;
            }

            // ③ 도형 셋(선 2 + 원 1 = 4 + 128 = 132정점)이 드로우 하나로.
            if (132 != gizmo.GetLastVertexCount() || 1 != gizmo.GetLastDrawCount())
            {
                outLog += "도형 셋이 드로우 하나로 안 나갔다 — 병합이 이 재작성의 목적이다\n";
                passed = false;
            }

            // ② 픽셀. X축 중간(2.5,0,0)은 빨강, 원 위 45도(2.83,0,2.83)는
            //   파랑, 축과 원 사이(2,0,2)는 비어야 한다.
            uint32_t redX = 0, redY = 0, blueX = 0, blueY = 0, emptyX = 0, emptyY = 0;
            const float diag = 4.f * 0.70710678f;
            if (!GizmoProjectToPixel(topDown.view, topDown.projection, 2.5f, 0.f, 0.f, redX, redY) ||
                !GizmoProjectToPixel(topDown.view, topDown.projection, diag, 0.f, diag, blueX, blueY) ||
                !GizmoProjectToPixel(topDown.view, topDown.projection, 2.f, 0.f, 2.f, emptyX, emptyY))
            {
                outLog += "표본 투영 실패 — 카메라 행렬이 화면을 벗어난다\n";
                passed = false;
            }
            else
            {
                const float redR = capture.MaxInWindow(redX, redY, 2, 0);
                const float blueB = capture.MaxInWindow(blueX, blueY, 2, 2);
                const float emptyMax = (std::max)({
                    capture.At(emptyX, emptyY, 0),
                    capture.At(emptyX, emptyY, 1),
                    capture.At(emptyX, emptyY, 2) });
                const uint32_t lit = capture.CountLit(0.1f);

                char pixelLine[224]{};
                std::snprintf(pixelLine, sizeof(pixelLine),
                    "[3/4] X축 R %.3f(px %u,%u) · 원 B %.3f(px %u,%u) · "
                    "빈 곳 %.3f(px %u,%u) · 점등 %u\n",
                    redR, redX, redY, blueB, blueX, blueY, emptyMax, emptyX, emptyY, lit);
                outLog += pixelLine;

                if (redR < 0.9f)
                {
                    outLog += "X축 선이 없다 — 정점 업로드나 투영이 틀렸다\n";
                    passed = false;
                }
                if (blueB < 0.9f)
                {
                    outLog += "원이 제자리에 없다 — 원 생성 수식이 원본과 다르다\n";
                    passed = false;
                }
                if (emptyMax > 0.05f)
                {
                    outLog += "축과 원 사이가 칠해졌다 — 선이 아닌 것이 그려지고 있다\n";
                    passed = false;
                }
                if (0 == lit)
                {
                    outLog += "아무것도 안 그려졌다\n";
                    passed = false;
                }
            }
        }
    }

    // ── ④ 카메라 반응 — 같은 도형, 먼 카메라 ──
    //
    // 도형은 그대로 두고 카메라만 (200,30,200) 위로 옮긴다. 도형이 시야
    // 밖이므로 화면이 비어야 한다 — 상수가 안 닿으면 ③과 같은 그림이 나온다.
    if (passed)
    {
        FrameCameraSnapshot farAway{};
        farAway.view = XMMatrixLookAtLH(
            XMVectorSet(200.f, 30.f, 200.f, 1.f),
            XMVectorSet(200.f, 0.f, 200.f, 1.f),
            XMVectorSet(0.f, 0.f, 1.f, 0.f));
        farAway.projection = XMMatrixPerspectiveFovLH(
            DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 500.f);
        farAway.eyePosition = XMVectorSet(200.f, 30.f, 200.f, 1.f);

        GizmoCapture farCapture{};
        if (!renderOnce(farAway, farCapture))
        {
            passed = false;
        }
        else
        {
            const uint32_t lit = farCapture.CountLit(0.1f);

            char line[128]{};
            std::snprintf(line, sizeof(line), "[4/4] 먼 카메라 — 점등 %u\n", lit);
            outLog += line;

            if (0 != lit)
            {
                outLog += "시야 밖 도형이 보인다 — 카메라 상수가 셰이더에 안 닿는다\n";
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

    gizmo.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "기즈모 라인 패스 검증 통과\n" : "기즈모 라인 패스 검증 실패\n";
    return passed;
}

#endif
