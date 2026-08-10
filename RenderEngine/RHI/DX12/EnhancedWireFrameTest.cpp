#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedWireFramePass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../../Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 와이어프레임 패스 자가 검증 (PHASE 3-6, Gizmo 계열 4차 슬라이스).
//
// ── 다섯을 따로 단정한다 ──
//
//   ① 선     — 쿼드의 변 자리에 초록 선이 그려지는가
//   ② 비채움 — 삼각형 내부는 비는가(FILL_WIREFRAME이 실제로 걸렸는가)
//   ③ 병합   — 같은 메시 드로우 둘이 배치 하나로 나가고, 둘 다 그려지는가
//   ④ 캐시   — 메시 업로드가 1회뿐인가(두 인스턴스 · 두 프레임에도)
//   ⑤ 스키닝 — 본이 정점을 실제로 옮기는가, 팔레트는 애니메이터당 하나인가
//
// ★ ②가 이 패스의 정체성이다. fillMode가 SOLID로 남으면 선 표본은
//   전부 통과하면서 내부까지 칠해진다 — 내부 표본 없이는 그 실패가
//   '와이어프레임 패스'라는 이름만 남기고 지나간다.
//
// 합성 메시를 쓴다: 엔진 Vertex로 쿼드 하나를 직접 만든다. 씬·에셋에
// 의존하지 않아야 결정적이고, 변·대각선·내부의 자리가 수식으로 나온다.
namespace
{
    constexpr uint32_t kWireWidth = 256;
    constexpr uint32_t kWireHeight = 256;

    struct WireCapture
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
            const uint32_t x1 = (centerX + radius < kWireWidth) ? centerX + radius : kWireWidth - 1;
            const uint32_t y1 = (centerY + radius < kWireHeight) ? centerY + radius : kWireHeight - 1;
            for (uint32_t y = y0; y <= y1; ++y)
                for (uint32_t x = x0; x <= x1; ++x)
                    best = (std::max)(best, At(x, y, channel));
            return best;
        }

        uint32_t CountLit(float threshold) const
        {
            uint32_t lit = 0;
            for (uint32_t y = 0; y < kWireHeight; ++y)
                for (uint32_t x = 0; x < kWireWidth; ++x)
                    if (At(x, y, 1) > threshold) ++lit;
            return lit;
        }
    };

    bool WireProjectToPixel(const Mathf::xMatrix& view, const Mathf::xMatrix& projection,
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

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * static_cast<float>(kWireWidth));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) * static_cast<float>(kWireHeight));
        if (outX >= kWireWidth) outX = kWireWidth - 1;
        if (outY >= kWireHeight) outY = kWireHeight - 1;
        return true;
    }
}

bool EnhancedSceneRenderer::RunWireFrameTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 와이어프레임 패스 검증 (PHASE 3-6, Gizmo 계열) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kWireWidth, kWireHeight, error))
    {
        outLog += "[1/5] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_wireframe.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !meshCache.Initialize(&resources, error))
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
    frameContext.width = kWireWidth;
    frameContext.height = kWireHeight;

    EnhancedWireFramePass wireframe;
    if (!wireframe.Initialize(frameContext, error))
    {
        outLog += "[1/5] 와이어프레임 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/5] 셰이더 컴파일·PSO 생성 통과\n";

    // ── 합성 쿼드 메시 — 변·대각선·내부가 수식으로 나온다 ──
    //
    // v0(-1,-1) v1(1,-1) v2(1,1) v3(-1,1), 인덱스 0,1,2 / 0,2,3.
    // 대각선은 v0-v2(y=x 선)다.
    std::vector<Vertex> vertices(4);
    vertices[0].position = { -1.f, -1.f, 0.f };
    vertices[1].position = { 1.f, -1.f, 0.f };
    vertices[2].position = { 1.f, 1.f, 0.f };
    vertices[3].position = { -1.f, 1.f, 0.f };
    const std::vector<uint32_t> indices{ 0, 1, 2, 0, 2, 3 };

    Mesh quadMesh("dx12_wireframe_quad", vertices, indices);

    // 같은 메시 둘 — 원점과 (3,0,0). 병합이 돌면 배치 하나로 나간다.
    std::vector<EnhancedDrawItem> draws(2);
    draws[0].mesh = &quadMesh;
    draws[0].worldMatrix = XMMatrixIdentity();
    draws[1].mesh = &quadMesh;
    draws[1].worldMatrix = XMMatrixTranslation(3.f, 0.f, 0.f);
    frameContext.draws = &draws;

    RHIReadback readback{};
    if (!resources.CreateReadback(kWireWidth, kWireHeight,
        EnhancedWireFramePass::kOutputFormat, 1, readback, error))
    {
        outLog += "리드백 생성 실패: " + error + "\n";
        wireframe.Shutdown();
        resources.Shutdown();
        return false;
    }

    bool passed = true;
    EnhancedRenderGraph::Stats lastStats{};

    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot, WireCapture& outCapture)
        -> bool
    {
        frameContext.camera = &snapshot;

        if (!resources.BeginFrame(error))
        {
            outLog += "BeginFrame 실패: " + error + "\n";
            return false;
        }

        if (!wireframe.PrepareFrame(frameContext, error))
        {
            outLog += "PrepareFrame 실패: " + error + "\n";
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);
        wireframe.Declare(graph, frameContext);

        const RGHandle output = wireframe.GetOutput();
        if (!output.IsValid())
        {
            outLog += "와이어프레임 출력이 선언되지 않았다\n";
            return false;
        }

        graph.AddPass("WireFrame.Readback",
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

    FrameCameraSnapshot front{};
    front.view = XMMatrixLookAtLH(
        XMVectorSet(0.f, 0.f, -8.f, 1.f),
        XMVectorSet(0.f, 0.f, 0.f, 1.f),
        XMVectorSet(0.f, 1.f, 0.f, 0.f));
    front.projection = XMMatrixPerspectiveFovLH(
        DirectX::XM_PIDIV4, 1.f, 0.1f, 100.f);
    front.eyePosition = XMVectorSet(0.f, 0.f, -8.f, 1.f);

    if (passed)
    {
        WireCapture capture{};
        if (!renderOnce(front, capture))
        {
            passed = false;
        }
        else
        {
            char line[256]{};
            std::snprintf(line, sizeof(line),
                "[2/5] 그래프 — 선언 %u · 실행 %u · 컬링 %u · transient %u · "
                "드로우 %u · 배치 %u · 메시 업로드 %u\n",
                lastStats.passesDeclared, lastStats.passesExecuted, lastStats.passesCulled,
                lastStats.transientCreated,
                wireframe.GetLastDrawItemCount(), wireframe.GetLastBatchCount(),
                meshCache.GetStats().uploads);
            outLog += line;

            if (2 != lastStats.passesExecuted || 0 != lastStats.passesCulled ||
                2 != lastStats.transientCreated)
            {
                outLog += "그래프 통계가 다르다 — 패스 2·컬링 0·transient 2여야 한다\n";
                passed = false;
            }

            // ③ 같은 메시 드로우 둘 → 배치 하나. ④ 업로드도 1회.
            if (2 != wireframe.GetLastDrawItemCount() || 1 != wireframe.GetLastBatchCount())
            {
                outLog += "같은 메시 둘이 배치 하나로 안 묶였다 — 인스턴싱 병합이 죽었다\n";
                passed = false;
            }
            if (1 != meshCache.GetStats().uploads)
            {
                outLog += "메시 업로드가 1회가 아니다 — 캐시가 논다\n";
                passed = false;
            }

            uint32_t edgeX = 0, edgeY = 0, edge2X = 0, edge2Y = 0, insideX = 0, insideY = 0;
            if (!WireProjectToPixel(front.view, front.projection, 0.f, 1.f, 0.f, edgeX, edgeY) ||
                !WireProjectToPixel(front.view, front.projection, 3.f, 1.f, 0.f, edge2X, edge2Y) ||
                !WireProjectToPixel(front.view, front.projection, 0.6f, -0.2f, 0.f, insideX, insideY))
            {
                outLog += "표본 투영 실패 — 카메라 행렬이 화면을 벗어난다\n";
                passed = false;
            }
            else
            {
                const float edgeG = capture.MaxInWindow(edgeX, edgeY, 2, 1);
                const float edge2G = capture.MaxInWindow(edge2X, edge2Y, 2, 1);
                const float insideG = capture.At(insideX, insideY, 1);
                const uint32_t lit = capture.CountLit(0.5f);

                char pixelLine[256]{};
                std::snprintf(pixelLine, sizeof(pixelLine),
                    "[3/5] 변 G %.3f(px %u,%u) · 두 번째 인스턴스 변 G %.3f(px %u,%u) · "
                    "내부 %.3f(px %u,%u) · 점등 %u\n",
                    edgeG, edgeX, edgeY, edge2G, edge2X, edge2Y,
                    insideG, insideX, insideY, lit);
                outLog += pixelLine;

                // ① 변 — 초록 선.
                if (edgeG < 0.9f)
                {
                    outLog += "쿼드 변에 선이 없다 — 메시가 안 그려졌다\n";
                    passed = false;
                }
                // ③ 두 번째 인스턴스도 그려졌는가 — 배치 하나로 묶고 하나만
                //   그리면 배치 수 단정만으로는 못 잡는다.
                if (edge2G < 0.9f)
                {
                    outLog += "두 번째 인스턴스가 안 그려졌다 — 인스턴스 수가 틀렸다\n";
                    passed = false;
                }
                // ② 내부 — FILL_WIREFRAME이 실제로 걸렸는가.
                if (insideG > 0.05f)
                {
                    outLog += "삼각형 내부가 칠해졌다 — fillMode가 SOLID로 남아 있다\n";
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

    // ── ④ 두 번째 프레임 — 업로드가 늘지 않는다 · 먼 카메라 — 화면이 빈다 ──
    if (passed)
    {
        FrameCameraSnapshot away{};
        away.view = XMMatrixLookAtLH(
            XMVectorSet(0.f, 0.f, -8.f, 1.f),
            XMVectorSet(0.f, 0.f, -20.f, 1.f),
            XMVectorSet(0.f, 1.f, 0.f, 0.f));
        away.projection = XMMatrixPerspectiveFovLH(
            DirectX::XM_PIDIV4, 1.f, 0.1f, 100.f);
        away.eyePosition = XMVectorSet(0.f, 0.f, -8.f, 1.f);

        WireCapture capture{};
        if (!renderOnce(away, capture))
        {
            passed = false;
        }
        else
        {
            const uint32_t lit = capture.CountLit(0.5f);
            const uint32_t uploads = meshCache.GetStats().uploads;

            char line[160]{};
            std::snprintf(line, sizeof(line),
                "[4/5] 등진 카메라 — 점등 %u · 메시 업로드 누계 %u(히트 %u)\n",
                lit, uploads, meshCache.GetStats().hits);
            outLog += line;

            if (0 != lit)
            {
                outLog += "등 뒤의 메시가 보인다 — 카메라 상수가 셰이더에 안 닿는다\n";
                passed = false;
            }
            if (1 != uploads)
            {
                outLog += "두 번째 프레임에서 메시가 다시 올라갔다 — 캐시가 논다\n";
                passed = false;
            }
        }
    }

    // ── ⑤ 스키닝 — 본이 정점을 옮기는가 ──
    //
    // ★ 같은 메시·같은 월드 행렬로 두 번 그리고 팔레트 포인터만 바꾼다.
    //   그래서 두 표본이 서로 자리를 맞바꾼다: 팔레트가 있으면 옮겨진
    //   자리가 켜지고 바인드 포즈 자리가 꺼지며, 없으면 정확히 반대다.
    //   '옮겨진 자리가 켜졌다'만 보면 아무 데나 그리는 구현도 통과한다.
    if (passed)
    {
        // 본 하나에 전부 물린 쿼드. 기본 Vertex는 boneWeights가 0이라
        // 셰이더가 스키닝을 건너뛴다 — 가중을 실어야 통로가 열린다.
        std::vector<Vertex> skinnedVertices(4);
        skinnedVertices[0].position = { -1.f, -1.f, 0.f };
        skinnedVertices[1].position = { 1.f, -1.f, 0.f };
        skinnedVertices[2].position = { 1.f, 1.f, 0.f };
        skinnedVertices[3].position = { -1.f, 1.f, 0.f };
        for (Vertex& vertex : skinnedVertices)
        {
            vertex.boneIndices = { 0.f, 0.f, 0.f, 0.f };
            vertex.boneWeights = { 1.f, 0.f, 0.f, 0.f };
        }
        Mesh skinnedMesh("dx12_wireframe_skinned_quad", skinnedVertices, indices);

        // 본 하나가 위로 1.5 옮긴다. 쿼드가 y [-1,1]에서 [0.5,2.5]로 간다.
        const Mathf::xMatrix palette[1] = { XMMatrixTranslation(0.f, 1.5f, 0.f) };

        // 같은 애니메이터를 두 드로우가 공유한다 — 팔레트는 하나여야 한다.
        std::vector<EnhancedDrawItem> skinnedDraws(2);
        for (uint32_t i = 0; i < 2; ++i)
        {
            skinnedDraws[i].mesh = &skinnedMesh;
            skinnedDraws[i].worldMatrix = (0 == i)
                ? XMMatrixIdentity() : XMMatrixTranslation(3.f, 0.f, 0.f);
            skinnedDraws[i].bonePalette = palette;
            skinnedDraws[i].animatorKey = 1;
            skinnedDraws[i].boneCount = 1;
        }
        frameContext.draws = &skinnedDraws;

        uint32_t movedX = 0, movedY = 0, bindX = 0, bindY = 0;
        const bool projected =
            WireProjectToPixel(front.view, front.projection, 0.f, 2.5f, 0.f, movedX, movedY) &&
            WireProjectToPixel(front.view, front.projection, 0.f, -1.f, 0.f, bindX, bindY);

        WireCapture skinnedCapture{};
        WireCapture bindCapture{};
        if (!projected || !renderOnce(front, skinnedCapture))
        {
            outLog += "[5/5] 스킨드 렌더 실패\n";
            passed = false;
        }
        else
        {
            const uint32_t skinnedCount = wireframe.GetLastSkinnedCount();
            const uint32_t paletteCount = wireframe.GetLastBonePaletteCount();
            const uint32_t batches = wireframe.GetLastBatchCount();

            // 대조군 — 팔레트만 뗀다. 나머지는 한 글자도 같다.
            for (EnhancedDrawItem& draw : skinnedDraws)
            {
                draw.bonePalette = nullptr;
                draw.boneCount = 0;
            }
            if (!renderOnce(front, bindCapture))
            {
                outLog += "[5/5] 바인드 포즈 렌더 실패\n";
                passed = false;
            }
            else
            {
                const float skinnedMoved = skinnedCapture.MaxInWindow(movedX, movedY, 2, 1);
                const float skinnedBind = skinnedCapture.MaxInWindow(bindX, bindY, 2, 1);
                const float plainMoved = bindCapture.MaxInWindow(movedX, movedY, 2, 1);
                const float plainBind = bindCapture.MaxInWindow(bindX, bindY, 2, 1);

                char line[320]{};
                std::snprintf(line, sizeof(line),
                    "[5/5] 스키닝 — 팔레트 있음: 옮겨진 자리 %.3f · 바인드 자리 %.3f"
                    " / 팔레트 없음: 옮겨진 자리 %.3f · 바인드 자리 %.3f\n"
                    "      스킨드 드로우 %u · 팔레트 %u · 배치 %u\n",
                    skinnedMoved, skinnedBind, plainMoved, plainBind,
                    skinnedCount, paletteCount, batches);
                outLog += line;

                if (skinnedMoved < 0.9f)
                {
                    outLog += "본이 옮긴 자리에 선이 없다 — 스키닝이 안 먹는다\n";
                    passed = false;
                }
                if (skinnedBind > 0.05f)
                {
                    outLog += "바인드 포즈 자리에 선이 남았다 — 정점이 안 옮겨졌다\n";
                    passed = false;
                }
                if (plainBind < 0.9f)
                {
                    outLog += "팔레트가 없는데 바인드 자리에 선이 없다 — 대조군이 성립하지 않는다\n";
                    passed = false;
                }
                if (plainMoved > 0.05f)
                {
                    outLog += "팔레트가 없는데 옮겨진 자리에 선이 있다 — 대조군이 성립하지 않는다\n";
                    passed = false;
                }

                // 한 애니메이터를 둘이 공유하면 팔레트는 하나여야 한다.
                // 이 값이 스킨드 드로우 수와 늘 같으면 중복 제거가 죽은 것이다.
                if (2 != skinnedCount || 1 != paletteCount)
                {
                    outLog += "팔레트 중복 제거가 죽었다 — 스킨드 둘에 팔레트가 하나여야 한다\n";
                    passed = false;
                }
                // 애니메이터가 같아도 다르더라도 같은 메시면 한 배치다.
                if (1 != batches)
                {
                    outLog += "스킨드 둘이 배치 하나로 안 묶였다 — 인스턴싱이 깨졌다\n";
                    passed = false;
                }
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

    wireframe.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "와이어프레임 패스 검증 통과\n" : "와이어프레임 패스 검증 실패\n";
    return passed;
}

#endif
