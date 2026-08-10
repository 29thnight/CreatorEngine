#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGizmoIconPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
// DeviceState.h include가 여기 있었다 (E, 2026-08-09).
// 이 파일에서 DirectX11:: 심볼을 쓰는 코드가 0이다.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 기즈모 아이콘 패스 자가 검증 (PHASE 3-6, Gizmo 계열 3차 슬라이스).
//
// ── 넷을 따로 단정한다 ──
//
//   ① 픽셀   — 아이콘 쿼드가 제자리에 그려지고(알파 상한 0.5), 밖은 비는가
//   ② 빌보드 — 카메라를 옆으로 옮겨도 쿼드가 카메라를 향해 돌아서는가
//   ③ 배칭   — 같은 텍스처 연속은 묶이고, 갈리면 배치가 늘어나는가
//   ④ 시야   — 아이콘이 뒤에 있으면 화면이 비는가
//
// ★ ②가 이 패스의 본질이다. GS를 VS로 옮기며 확장 수식이 틀리면 정면
//   카메라에서는 멀쩡해 보이고 옆에서만 사라진다(엣지온) — 정면 표본만
//   보면 그 실패가 통과한다.
namespace
{
    constexpr uint32_t kIconWidth = 256;
    constexpr uint32_t kIconHeight = 256;

    struct IconCapture
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
            const uint32_t x1 = (centerX + radius < kIconWidth) ? centerX + radius : kIconWidth - 1;
            const uint32_t y1 = (centerY + radius < kIconHeight) ? centerY + radius : kIconHeight - 1;
            for (uint32_t y = y0; y <= y1; ++y)
                for (uint32_t x = x0; x <= x1; ++x)
                    best = (std::max)(best, At(x, y, channel));
            return best;
        }

        uint32_t CountLit(float threshold) const
        {
            uint32_t lit = 0;
            for (uint32_t y = 0; y < kIconHeight; ++y)
                for (uint32_t x = 0; x < kIconWidth; ++x)
                    if (At(x, y, 0) > threshold) ++lit;
            return lit;
        }
    };

    bool IconProjectToPixel(const Mathf::xMatrix& view, const Mathf::xMatrix& projection,
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

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * static_cast<float>(kIconWidth));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) * static_cast<float>(kIconHeight));
        if (outX >= kIconWidth) outX = kIconWidth - 1;
        if (outY >= kIconHeight) outY = kIconHeight - 1;
        return true;
    }

    FrameCameraSnapshot IconCamera(float eyeX, float eyeY, float eyeZ,
        float atX, float atY, float atZ)
    {
        FrameCameraSnapshot snapshot{};
        snapshot.view = XMMatrixLookAtLH(
            XMVectorSet(eyeX, eyeY, eyeZ, 1.f),
            XMVectorSet(atX, atY, atZ, 1.f),
            XMVectorSet(0.f, 1.f, 0.f, 0.f));
        snapshot.projection = XMMatrixPerspectiveFovLH(
            DirectX::XM_PIDIV4, 1.f, 0.1f, 100.f);
        snapshot.eyePosition = XMVectorSet(eyeX, eyeY, eyeZ, 1.f);
        return snapshot;
    }
}

bool EnhancedSceneRenderer::RunGizmoIconTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 기즈모 아이콘 패스 검증 (PHASE 3-6, Gizmo 계열) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kIconWidth, kIconHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_gizmoicon.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.textureCache = &textureCache;
    frameContext.width = kIconWidth;
    frameContext.height = kIconHeight;

    EnhancedGizmoIconPass gizmo;
    if (!gizmo.Initialize(frameContext, error))
    {
        outLog += "[1/4] 기즈모 아이콘 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 셰이더 컴파일·PSO 생성 통과\n";

    RHIReadback readback{};
    if (!resources.CreateReadback(kIconWidth, kIconHeight,
        EnhancedGizmoIconPass::kOutputFormat, 1, readback, error))
    {
        outLog += "리드백 생성 실패: " + error + "\n";
        gizmo.Shutdown();
        resources.Shutdown();
        return false;
    }

    bool passed = true;
    EnhancedRenderGraph::Stats lastStats{};

    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot, IconCapture& outCapture)
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
            outLog += "아이콘 출력이 선언되지 않았다\n";
            return false;
        }

        graph.AddPass("GizmoIcon.Readback",
            { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(resources.GetDevice(), error))
        {
            outLog += "Compile 실패: " + error + "\n";
            return false;
        }
        if (!graph.Execute(resources.GetCommandList(), error))
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

    // 아이콘 하나 (0,0,0), 크기 2 — 쿼드는 y∈[0,2] · 폭 2로 선다.
    // 텍스처를 안 주므로 1x1 흰색이고, 알파 상한 0.5로 R이 0.5가 된다.
    std::vector<EnhancedGizmoIconPass::Icon> icons;
    {
        EnhancedGizmoIconPass::Icon icon{};
        icon.position = { 0.f, 0.f, 0.f };
        icon.size = 2.f;
        icons.push_back(icon);
    }
    gizmo.SetIcons(&icons);

    // ── ① 정면 카메라 — 픽셀 ──
    const FrameCameraSnapshot front = IconCamera(0.f, 1.f, -10.f, 0.f, 1.f, 0.f);

    if (passed)
    {
        IconCapture capture{};
        if (!renderOnce(front, capture))
        {
            passed = false;
        }
        else
        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "[2/4] 그래프 — 선언 %u · 실행 %u · 컬링 %u · transient %u · "
                "아이콘 %u · 배치 %u\n",
                lastStats.passesDeclared, lastStats.passesExecuted, lastStats.passesCulled,
                lastStats.transientCreated,
                gizmo.GetLastIconCount(), gizmo.GetLastBatchCount());
            outLog += line;

            if (2 != lastStats.passesExecuted || 0 != lastStats.passesCulled ||
                1 != lastStats.transientCreated)
            {
                outLog += "그래프 통계가 다르다 — 패스 2·컬링 0·transient 1이어야 한다\n";
                passed = false;
            }

            // 쿼드 중심 (0,1,0)은 흰색 x 알파 0.5 → R 0.5. 밖 (3,1,0)은 빈다.
            uint32_t quadX = 0, quadY = 0, outsideX = 0, outsideY = 0;
            if (!IconProjectToPixel(front.view, front.projection, 0.f, 1.f, 0.f, quadX, quadY) ||
                !IconProjectToPixel(front.view, front.projection, 3.f, 1.f, 0.f, outsideX, outsideY))
            {
                outLog += "표본 투영 실패 — 카메라 행렬이 화면을 벗어난다\n";
                passed = false;
            }
            else
            {
                const float quadR = capture.MaxInWindow(quadX, quadY, 2, 0);
                const float outsideR = capture.At(outsideX, outsideY, 0);
                const uint32_t lit = capture.CountLit(0.1f);

                char pixelLine[192]{};
                std::snprintf(pixelLine, sizeof(pixelLine),
                    "[2/4] 쿼드 중심 R %.3f(px %u,%u) · 밖 %.3f(px %u,%u) · 점등 %u\n",
                    quadR, quadX, quadY, outsideR, outsideX, outsideY, lit);
                outLog += pixelLine;

                // 알파 상한 0.5가 지켜지면 R은 0.5 근처다. 1.0에 가까우면
                // min(a, 0.5) quirk가 빠진 것이다.
                if (quadR < 0.35f || quadR > 0.65f)
                {
                    outLog += "쿼드 밝기가 0.5 근처가 아니다 — 알파 상한 quirk가 빠졌거나 안 그려졌다\n";
                    passed = false;
                }
                if (outsideR > 0.05f)
                {
                    outLog += "쿼드 밖이 칠해졌다 — 확장 크기가 틀렸다\n";
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

    // ── ② 측면 카메라 — 빌보드 회전 ──
    //
    // 카메라를 +X 옆으로 옮긴다. 쿼드가 고정 평면이라면 엣지온이 되어
    // 사라진다 — 여전히 보여야 확장 수식이 카메라를 따라 도는 것이다.
    if (passed)
    {
        const FrameCameraSnapshot side = IconCamera(10.f, 1.f, 0.f, 0.f, 1.f, 0.f);

        IconCapture capture{};
        if (!renderOnce(side, capture))
        {
            passed = false;
        }
        else
        {
            uint32_t quadX = 0, quadY = 0;
            if (!IconProjectToPixel(side.view, side.projection, 0.f, 1.f, 0.f, quadX, quadY))
            {
                outLog += "[3/4] 측면 표본 투영 실패\n";
                passed = false;
            }
            else
            {
                const float quadR = capture.MaxInWindow(quadX, quadY, 2, 0);

                char line[128]{};
                std::snprintf(line, sizeof(line),
                    "[3/4] 측면 카메라 — 쿼드 중심 R %.3f(px %u,%u)\n", quadR, quadX, quadY);
                outLog += line;

                if (quadR < 0.35f)
                {
                    outLog += "옆에서 보면 사라진다 — 빌보드가 카메라를 안 향한다(엣지온)\n";
                    passed = false;
                }
            }
        }
    }

    // ── ③ 배칭 — 같은 텍스처는 묶이고 갈리면 늘어난다 ──
    //
    // 실제 텍스처 자원 없이 포인터의 같고 다름만 확인한다(UI 검증과 같은
    // 방식) — 배칭이 보는 것이 포인터뿐이라 이것으로 충분하다. 렌더는
    // 하지 않고 PrepareFrame까지만 돌린다.
    if (passed)
    {
        Texture* const fakeA = reinterpret_cast<Texture*>(0x1);
        Texture* const fakeB = reinterpret_cast<Texture*>(0x2);

        std::vector<EnhancedGizmoIconPass::Icon> mixed;
        for (int i = 0; i < 4; ++i)
        {
            EnhancedGizmoIconPass::Icon icon{};
            icon.position = { static_cast<float>(i), 0.f, 0.f };
            icon.size = 1.f;
            mixed.push_back(icon);
        }

        std::string dummy;

        // 같은 텍스처 넷 → 배치 1
        mixed[0].texture = fakeA; mixed[1].texture = fakeA;
        mixed[2].texture = fakeA; mixed[3].texture = fakeA;
        gizmo.SetIcons(&mixed);
        gizmo.PrepareFrame(frameContext, dummy);
        const uint32_t sameBatches = gizmo.GetLastBatchCount();

        // 번갈아 → 배치 4
        mixed[1].texture = fakeB; mixed[3].texture = fakeB;
        gizmo.PrepareFrame(frameContext, dummy);
        const uint32_t mixedBatches = gizmo.GetLastBatchCount();

        char line[128]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 배칭 — 같은 텍스처 넷: 배치 %u · 번갈아: 배치 %u\n",
            sameBatches, mixedBatches);
        outLog += line;

        if (1 != sameBatches)
        {
            outLog += "같은 텍스처인데 배치가 하나가 아니다 — 배칭이 안 묶는다\n";
            passed = false;
        }
        if (4 != mixedBatches)
        {
            outLog += "텍스처가 갈리는데 배치가 안 갈린다 — 배치 경계 판단이 죽었다\n";
            passed = false;
        }

        gizmo.SetIcons(&icons);
    }

    // ── ④ 시야 밖 — 카메라가 등을 돌리면 화면이 빈다 ──
    if (passed)
    {
        const FrameCameraSnapshot away = IconCamera(0.f, 1.f, -10.f, 0.f, 1.f, -20.f);

        IconCapture capture{};
        if (!renderOnce(away, capture))
        {
            passed = false;
        }
        else
        {
            const uint32_t lit = capture.CountLit(0.1f);

            char line[96]{};
            std::snprintf(line, sizeof(line), "[4/4] 등진 카메라 — 점등 %u\n", lit);
            outLog += line;

            if (0 != lit)
            {
                outLog += "등 뒤의 아이콘이 보인다 — 카메라 상수가 셰이더에 안 닿는다\n";
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
    textureCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "기즈모 아이콘 패스 검증 통과\n" : "기즈모 아이콘 패스 검증 실패\n";
    return passed;
}

#endif
