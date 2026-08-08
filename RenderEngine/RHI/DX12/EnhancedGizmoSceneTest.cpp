#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGridPass.h"
#include "EnhancedGizmoLinePass.h"
#include "EnhancedGizmoIconPass.h"
#include "EnhancedWireFramePass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../../RenderPassData.h"
#include "../../MeshRendererProxy.h"
#include "../../DeviceState.h"
// 씬 수집은 루트의 바인딩이 한다 — 컴포넌트 헤더는 인클루드 스택 문제로
// RHI/DX12에서 직접 들 수 없다(바인딩 헤더의 설명 참고).
#include "../../EnhancedGizmoSceneBinding.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// Gizmo 계열 씬 연결 검증 (PHASE 3-6, Gizmo 계열 5차 슬라이스).
//
// ── 무엇을 검증하는가 ──
//
//   ① 밀봉 복사 — 씬의 아이콘 대상(카메라·라이트)·선택 기즈모·콜라이더를
//      게임 자료구조를 들지 않는 형태로 복사해 패스에 물린다
//   ② 체인     — 그리드 → 와이어프레임 → 아이콘 → 라인이 한 그래프에서
//      같은 타깃을 이어 그린다(GizmoRenderer::OnDrawGizmos의 순서 그대로)
//   ③ 공유     — transient가 그리드의 색·깊이 둘뿐이어야 한다. 각 패스가
//      각자 타깃을 만들면 체인이 아니라 병렬 그림 넷이다
//
// ★ ③이 이 슬라이스의 요점이다. Inputs{color,depth}는 지금까지 자가 검증
//   에서 늘 비어 있었다(각자 transient) — 채워서 잇는 것은 이번이 처음이고,
//   여기가 어긋나면 '각자 그리는데 마지막 것만 보인다'로 조용히 실패한다.
//
// DX11 수집 로직의 이식 원칙: 아이콘의 y-0.5 보정, 방향광 기즈모의
// 화면 비례 크기(GetGizmoScale의 Rad2Deg quirk 포함)를 그대로 옮긴다.
namespace
{
    constexpr uint32_t kGizmoSceneWidth = 256;
    constexpr uint32_t kGizmoSceneHeight = 256;

}

bool EnhancedSceneRenderer::RunGizmoSceneTest(std::string& outLog)
{
    outLog += "── Gizmo 계열 씬 연결 검증 (PHASE 3-6) ──\n";

    std::string error;

    // ── [1/5] 씬 원천 — 카메라 스냅샷과 기즈모 대상의 밀봉 복사 ──
    Camera* sceneCamera = nullptr;
    for (auto& camera : CameraManagement->GetCameras())
    {
        if (camera && RenderPassData::VaildCheck(camera.get()))
        {
            sceneCamera = camera.get();
            break;
        }
    }

    if (nullptr == sceneCamera)
    {
        outLog += "[1/5] 활성 카메라가 없다(에디터 실행 중에만 의미 있는 검증)\n";
        return false;
    }

    const RenderPassData* renderData = RenderPassData::GetData(sceneCamera);
    const FrameCameraSnapshot cameraSnapshot = renderData->GetFrameSnapshot();

    // 와이어프레임 대상 — 커맨드 빌드가 채워 둔 큐에서 메시·월드만 복사.
    std::vector<EnhancedDrawItem> draws;
    {
        const auto copyQueue = [&](const auto& queue)
        {
            for (auto* proxy : queue)
            {
                if (nullptr == proxy || nullptr == proxy->m_Mesh) continue;
                EnhancedDrawItem item{};
                item.mesh = proxy->m_Mesh.get();
                item.worldMatrix = proxy->m_worldMatrix;
                draws.push_back(item);
            }
        };
        copyQueue(renderData->m_deferredQueue);
        copyQueue(renderData->m_forwardQueue);
    }

    // ── [2/5] DX12 초기화 + 패스 4종 ──
    DX12DeviceResources resources;
    if (!resources.Initialize(kGizmoSceneWidth, kGizmoSceneHeight, error))
    {
        outLog += "[2/5] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_gizmoscene.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[2/5] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.textureCache = &textureCache;
    frameContext.width = kGizmoSceneWidth;
    frameContext.height = kGizmoSceneHeight;
    frameContext.camera = &cameraSnapshot;
    frameContext.draws = &draws;

    EnhancedGridPass grid;
    EnhancedWireFramePass wireframe;
    EnhancedGizmoIconPass icon;
    EnhancedGizmoLinePass line;

    if (!grid.Initialize(frameContext, error) ||
        !wireframe.Initialize(frameContext, error) ||
        !icon.Initialize(frameContext, error) ||
        !line.Initialize(frameContext, error))
    {
        outLog += "[2/5] 패스 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[2/5] 패스 4종 초기화 통과\n";

    // ── 씬 수집 — 밀봉 복사(루트 바인딩) + 주입 내용 ──
    //
    // 콜라이더는 디버그 모드와 무관하게 수집한다 — 경로 검증이 목적이다
    // (실전 조립에서는 EngineSettingInstance->IsDebugMode()가 판정한다).
    line.ResetLines();
    EnhancedGizmoSceneData sceneData{};
    if (!BuildEnhancedGizmoSceneData(cameraSnapshot, true, line, sceneData))
    {
        outLog += "[1/5] 활성 씬이 없다\n";
        resources.Shutdown();
        return false;
    }

    // 주입 내용 — 씬에 선택·콜라이더가 없어도 라인 경로가 그려지게 한다.
    // 방향광 기즈모 도형(9세그 원 + 방향선)의 이식도 이것으로 실행된다.
    line.AddLine({ -2.f, 0.f, 0.f }, { 2.f, 0.f, 0.f }, { 1.f, 0.f, 0.f, 1.f });
    line.AddWireCircleWithDirectionLines({ 0.f, 1.f, 0.f }, 1.f,
        { 0.f, 1.f, 0.f }, { 0.f, -1.f, 0.f }, { 1.f, 0.f, 1.f, 1.f });

    icon.SetIcons(&sceneData.icons);

    char sourceLine[224]{};
    std::snprintf(sourceLine, sizeof(sourceLine),
        "[1/5] 씬 원천 — 아이콘 %zu(카메라 %u · 라이트 %u) · "
        "선택 기즈모 %u · 콜라이더 %u · 주입 2\n",
        sceneData.icons.size(), sceneData.cameraIcons, sceneData.lightIcons,
        sceneData.selectionShapes, sceneData.colliderShapes);
    outLog += sourceLine;

    if (sceneData.icons.empty())
    {
        outLog += "아이콘 대상이 없다 — 기본 씬에도 카메라는 있어야 한다\n";
        resources.Shutdown();
        return false;
    }

    // ── [3/5] 체인 렌더 — Grid → WireFrame → Icon → Line → 리드백 ──
    RHIReadback readback{};
    if (!resources.CreateReadback(kGizmoSceneWidth, kGizmoSceneHeight,
        EnhancedGridPass::kOutputFormat, 1, readback, error))
    {
        outLog += "[3/5] 리드백 생성 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    bool passed = true;
    EnhancedRenderGraph::Stats graphStats{};
    RHIReadbackImage captured{};

    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/5] BeginFrame 실패: " + error + "\n";
            passed = false;
        }

        if (passed &&
            (!grid.PrepareFrame(frameContext, error) ||
             !wireframe.PrepareFrame(frameContext, error) ||
             !icon.PrepareFrame(frameContext, error) ||
             !line.PrepareFrame(frameContext, error)))
        {
            outLog += "[3/5] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);

        if (passed)
        {
            // DX11 GizmoRenderer::OnDrawGizmos의 순서 그대로:
            // Grid → WireFrame → Gizmo(아이콘) → GizmoLine.
            grid.Declare(graph, frameContext);

            EnhancedWireFramePass::Inputs wireInputs{};
            wireInputs.color = grid.GetOutput();
            wireInputs.depth = grid.GetDepth();
            wireframe.SetInputs(wireInputs);
            wireframe.Declare(graph, frameContext);

            EnhancedGizmoIconPass::Inputs iconInputs{};
            iconInputs.color = wireframe.GetOutput();
            icon.SetInputs(iconInputs);
            icon.Declare(graph, frameContext);

            EnhancedGizmoLinePass::Inputs lineInputs{};
            lineInputs.color = icon.GetOutput();
            line.SetInputs(lineInputs);
            line.Declare(graph, frameContext);

            const RGHandle output = line.GetOutput();
            if (!output.IsValid())
            {
                outLog += "[3/5] 체인 끝의 출력이 없다\n";
                passed = false;
            }
            else
            {
                graph.AddPass("GizmoScene.Readback",
                    { { output, RGResourceState::CopySource } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {
                        resources.CopyToReadback(executeContext.commandList, readback,
                            executeContext.Resolve(output));
                    }, true);

                if (!graph.Compile(resources.GetDevice(), error))
                {
                    outLog += "[3/5] Compile 실패: " + error + "\n";
                    passed = false;
                }
                if (passed && !graph.Execute(resources.GetCommandList(), error))
                {
                    outLog += "[3/5] Execute 실패: " + error + "\n";
                    passed = false;
                }
                graphStats = graph.GetStats();
            }
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[3/5] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }

        if (passed)
        {
            if (!resources.MapReadback(readback, captured, error))
            {
                outLog += "[3/5] 리드백 Map 실패: " + error + "\n";
                passed = false;
            }
        }
    }

    // ── [4/5] 그래프·공유·픽셀 단정 ──
    if (passed)
    {
        char line1[256]{};
        std::snprintf(line1, sizeof(line1),
            "[4/5] 그래프 — 선언 %u · 실행 %u · 컬링 %u · transient %u · "
            "배리어 %u건/%u번 · 와이어프레임 드로우 %u·배치 %u · 라인 정점 %u\n",
            graphStats.passesDeclared, graphStats.passesExecuted,
            graphStats.passesCulled, graphStats.transientCreated,
            graphStats.barriersEmitted, graphStats.barrierBatches,
            wireframe.GetLastDrawItemCount(), wireframe.GetLastBatchCount(),
            line.GetLastVertexCount());
        outLog += line1;

        if (5 != graphStats.passesDeclared || 5 != graphStats.passesExecuted ||
            0 != graphStats.passesCulled)
        {
            outLog += "체인 5패스가 전부 실행되지 않았다\n";
            passed = false;
        }

        // ★ 공유 — transient는 그리드의 색·깊이 둘뿐이어야 한다.
        //   각 패스가 각자 타깃을 만들면 여기가 6~8로 뛴다.
        if (2 != graphStats.transientCreated)
        {
            outLog += "transient가 둘이 아니다 — 체인이 타깃을 공유하지 않는다\n";
            passed = false;
        }

        // 주입 라인 최소치: 선 1(2정점) + 방향광 기즈모(36정점) = 38.
        if (line.GetLastVertexCount() < 38)
        {
            outLog += "라인 정점이 주입 최소치(38)에 못 미친다\n";
            passed = false;
        }

        if (icon.GetLastIconCount() != sceneData.icons.size())
        {
            outLog += "아이콘 수가 수집한 수와 다르다\n";
            passed = false;
        }
    }

    if (passed)
    {
        const auto at = [&](uint32_t x, uint32_t y, uint32_t channel)
        {
            return captured.At(x, y, channel);
        };

        uint32_t lit = 0;
        for (uint32_t y = 0; y < kGizmoSceneHeight; ++y)
            for (uint32_t x = 0; x < kGizmoSceneWidth; ++x)
                if (at(x, y, 0) > 0.05f || at(x, y, 1) > 0.05f || at(x, y, 2) > 0.05f)
                    ++lit;

        char line2[96]{};
        std::snprintf(line2, sizeof(line2), "[5/5] 점등 %u\n", lit);
        outLog += line2;

        // 에디터 카메라의 구도를 미리 알 수 없어 표본 자리는 못 박지 않는다.
        // 대신 '무언가는 그려졌다'와 '전부 칠해지진 않았다'를 본다.
        if (0 == lit)
        {
            outLog += "아무것도 안 그려졌다 — 체인 어딘가에서 그림이 사라졌다\n";
            passed = false;
        }
        if (lit >= kGizmoSceneWidth * kGizmoSceneHeight)
        {
            outLog += "화면 전체가 칠해졌다 — 클리어나 블렌딩이 이상하다\n";
            passed = false;
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    line.Shutdown();
    icon.Shutdown();
    wireframe.Shutdown();
    grid.Shutdown();
    textureCache.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "Gizmo 씬 연결 검증 통과\n" : "Gizmo 씬 연결 검증 실패\n";
    return passed;
}

#endif
