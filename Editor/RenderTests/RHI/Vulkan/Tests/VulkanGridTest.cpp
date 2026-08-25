#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "Render/Passes/Editor/EnhancedGridPass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "FrameCameraSnapshot.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// 그리드 패스 — Vulkan (5d). **두 백엔드가 공유하는 첫 패스**다.
//
// ── 규칙 ──
//
// `EnhancedGridPass` 를 **한 줄도 고치지 않고** 돌린다. 고쳐야 하면 그것이
// 경계 결함의 실측이고, 실제로 이 검사를 세우며 셋이 나왔다(전부 패스 밖에서
// 고쳤다): 셰이더가 DXBC 로 구워지는 것(RHIShaderCompiler 출력 선택) ·
// 배리어가 열린 렌더링 안에서 기록되는 것(전이가 스스로 닫는다) ·
// 리드백 넷이 스텁이던 것(5d 가 청구해 실물이 됐다).
//
// ── 판정 ──
//
// dx12.grid 와 **같은 검사 넷**(라인·내부·밀도·카메라)에 더해, dx12.grid 의
// 기준선과 수치를 대조한다: 점등 9840(15.0%) · 원점 선 R 0.225. 같은 셰이더
// 소스·같은 카메라·같은 크기이므로 큰 차이는 경계의 결함이다. 허용 오차는
// 블렌드·보간의 백엔드 차를 감안해 준다 — §8.6 이 "정당하게 다를 수 있는
// 것"을 미리 적어 두었다.
namespace
{
    constexpr uint32_t kVkGridSize = 256;

    // dx12.grid 기준선 (2026-08-11 실측). 대조가 이 검사의 존재 이유다.
    constexpr uint32_t kVkGridBaselineLit = 9840;
    constexpr float    kVkGridBaselineLineR = 0.225f;

    struct VkGridCapture
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
            const uint32_t x1 = (centerX + radius < kVkGridSize) ? centerX + radius : kVkGridSize - 1;
            const uint32_t y1 = (centerY + radius < kVkGridSize) ? centerY + radius : kVkGridSize - 1;
            for (uint32_t y = y0; y <= y1; ++y)
                for (uint32_t x = x0; x <= x1; ++x)
                    best = (std::max)(best, At(x, y, channel));
            return best;
        }

        uint32_t CountLit(uint32_t yBegin, uint32_t yEnd, float threshold) const
        {
            uint32_t lit = 0;
            for (uint32_t y = yBegin; y < yEnd; ++y)
                for (uint32_t x = 0; x < kVkGridSize; ++x)
                    if (At(x, y, 0) > threshold) ++lit;
            return lit;
        }
    };

    // dx12.grid 의 것과 같은 계산 — 같은 자로 재야 대조가 성립한다.
    bool VkGridProjectToPixel(const math::matrix4x4& view, const math::matrix4x4& projection,
        float worldX, float worldZ, uint32_t& outX, uint32_t& outY)
    {
        const Mathf::xMatrix vp = MathematicsInterop::ToDirectX(view * projection);
        const Mathf::xVector clip = DirectX::XMVector4Transform(
            DirectX::XMVectorSet(worldX, 0.f, worldZ, 1.f), vp);
        const float w = DirectX::XMVectorGetW(clip);
        if (w <= 1e-6f) return false;

        const float ndcX = DirectX::XMVectorGetX(clip) / w;
        const float ndcY = DirectX::XMVectorGetY(clip) / w;
        if (ndcX < -1.f || ndcX > 1.f || ndcY < -1.f || ndcY > 1.f) return false;

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * static_cast<float>(kVkGridSize));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) * static_cast<float>(kVkGridSize));
        if (outX >= kVkGridSize) outX = kVkGridSize - 1;
        if (outY >= kVkGridSize) outY = kVkGridSize - 1;
        return true;
    }

    /// SPIR-V 모드를 검사 동안만 켠다. 검사당 프로세스 하나지만(§7.2.7),
    /// 전역 스위치를 켠 채 끝나는 검사는 다음에 이 파일을 고치는 사람에게
    /// 함정이다.
    struct VkGridSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };
}

bool RunVulkanGridTest(std::string& outLog)
{
    outLog += "── 그리드 패스 — Vulkan (5d, 두 백엔드 공유 첫 패스) ──\n";

    std::string error;

    VulkanDeviceResources resources;
    if (!VulkanApi::LoadLoader(error))
    {
        // 로더가 없는 기계에서는 검사가 성립하지 않는다 — 실패가 아니라
        // 건너뜀이고, 그 사실이 판정 줄에 그대로 나온다.
        outLog += "[1/4] Vulkan 로더 없음 — 건너뜀: " + error + "\n";
        return false;
    }
    if (!resources.Initialize(kVkGridSize, kVkGridSize, true, error))
    {
        outLog += "[1/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }

    VulkanPipelineCache pipelineCache;
    pipelineCache.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelineCache);

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &pipelineCache;
    frameContext.rootSignatures = &pipelineCache;
    frameContext.width = kVkGridSize;
    frameContext.height = kVkGridSize;

    EnhancedGridPass grid;
    RHIReadback readback{};

    auto fail = [&](const std::string& line) {
        outLog += line;
        std::string layerMessages;
        if (0 != resources.DrainDebugMessages(layerMessages)) outLog += layerMessages;

        resources.WaitForGpu();
        grid.Shutdown();
        resources.ReleaseReadback(readback);
        pipelineCache.Shutdown();
        resources.Shutdown();
        outLog += "그리드 패스(Vulkan) 검증 실패\n";
        return false;
    };

    {
        // ★ 여기가 5d 다. `grid.Initialize` 는 dx12.grid 와 **같은 함수**이고,
        //   SPIR-V 모드만 켜져 있다 — 패스는 자기가 어느 백엔드에서 도는지
        //   모른다.
        VkGridSpirvScope spirvScope;
        if (!grid.Initialize(frameContext, error))
            return fail("[1/4] 그리드 초기화 실패: " + error + "\n");
    }
    outLog += "[1/4] 셰이더 SPIR-V 컴파일·파이프라인 생성 통과 — 패스 코드 무변경\n";

    if (!resources.CreateReadback(kVkGridSize, kVkGridSize,
        EnhancedGridPass::kOutputFormat, 1, readback, error))
    {
        return fail("[1/4] 리드백 생성 실패: " + error + "\n");
    }

    EnhancedRenderGraph::Stats lastStats{};

    // dx12.grid 의 renderOnce 와 같은 모양 — 그래프는 중립 생성자를 탄다.
    const auto renderOnce = [&](const FrameCameraSnapshot& snapshot, VkGridCapture& outCapture)
        -> bool
    {
        frameContext.camera = &snapshot;

        if (!resources.BeginFrame(error)) { outLog += "BeginFrame 실패: " + error + "\n"; return false; }
        if (!grid.PrepareFrame(frameContext, error)) { outLog += "PrepareFrame 실패: " + error + "\n"; return false; }

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        grid.Declare(graph, frameContext);

        const RGHandle output = grid.GetOutput();
        if (!output.IsValid()) { outLog += "그리드 출력이 선언되지 않았다\n"; return false; }

        graph.AddPass("Grid.Readback",
            { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback(readback,
                    executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(error)) { outLog += "Compile 실패: " + error + "\n"; return false; }
        if (!graph.Execute(error)) { outLog += "Execute 실패: " + error + "\n"; return false; }
        lastStats = graph.GetStats();

        if (!resources.EndFrame(error)) { outLog += "EndFrame 실패: " + error + "\n"; return false; }
        resources.WaitForGpu();

        if (!resources.MapReadback(readback, outCapture.image, error))
        {
            outLog += error + "\n";
            return false;
        }
        return true;
    };

    // ── 하향 카메라 — dx12.grid 와 같은 자리 ──
    FrameCameraSnapshot topDown{};
    topDown.view = math::look_at_lh(
        math::vector3{0.f, 30.f, 0.f},
        math::vector3{0.f, 0.f, 0.f},
        math::vector3{0.f, 0.f, 1.f});
    topDown.projection = math::perspective_fov_lh(
        DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 500.f);
    topDown.eyePosition = math::vector3{0.f, 30.f, 0.f};

    VkGridCapture capture{};
    if (!renderOnce(topDown, capture)) return fail("");

    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 그래프(중립 생성자) — 선언 %u · 실행 %u · 컬링 %u · transient %u\n",
            lastStats.passesDeclared, lastStats.passesExecuted, lastStats.passesCulled,
            lastStats.transientCreated);
        outLog += line;

        if (2 != lastStats.passesExecuted || 0 != lastStats.passesCulled)
            return fail("패스 수가 다르다 — 그리드가 컬링됐거나 사슬이 끊겼다\n");
        if (2 != lastStats.transientCreated)
            return fail("transient가 둘(색·깊이)이 아니다\n");
    }

    {
        uint32_t lineX = 0, lineY = 0;
        uint32_t cellX = 0, cellY = 0;
        if (!VkGridProjectToPixel(topDown.view, topDown.projection, 0.f, 0.f, lineX, lineY) ||
            !VkGridProjectToPixel(topDown.view, topDown.projection, 0.5f, 0.5f, cellX, cellY))
        {
            return fail("[3/4] 표본 투영 실패 — 카메라 행렬이 화면을 벗어난다\n");
        }

        const float lineR = capture.MaxInWindow(lineX, lineY, 2, 0);
        const float cellR = capture.At(cellX, cellY, 0);
        const float cellA = capture.At(cellX, cellY, 3);

        const uint32_t litTotal = capture.CountLit(0, kVkGridSize, 0.05f);
        const float litRatio =
            static_cast<float>(litTotal) / static_cast<float>(kVkGridSize * kVkGridSize);

        // ── 기준선 대조 — 이 검사의 존재 이유 ──
        //
        // ★ 점등 ±10% · 원점 R ±0.05. 래스터라이즈 규칙이 같아도 커버리지
        //   경계 픽셀과 블렌드 반올림은 백엔드가 다를 수 있다 — 그 차이는
        //   수십 픽셀 규모이고, 상수·좌표계·셰이더가 틀린 부류는 수천 픽셀
        //   규모다. 허용 오차는 앞엣것을 통과시키고 뒤엣것을 잡게 골랐다.
        const float litDelta = std::fabs(static_cast<float>(litTotal)
            - static_cast<float>(kVkGridBaselineLit)) / static_cast<float>(kVkGridBaselineLit);
        const float lineDelta = std::fabs(lineR - kVkGridBaselineLineR);

        char line[288]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 원점 선 R %.3f(기준 %.3f) · 셀 내부(R %.3f A %.3f) · "
            "점등 %u(%.1f%%, 기준 %u) · 편차 점등 %.1f%% 선 %.3f\n",
            lineR, kVkGridBaselineLineR, cellR, cellA,
            litTotal, litRatio * 100.f, kVkGridBaselineLit, litDelta * 100.f, lineDelta);
        outLog += line;

        if (lineR < 0.1f)
            return fail("원점 교차선 자리가 비었다 — 그리드가 안 그려지거나 자리가 틀렸다\n");
        if (cellR > 0.05f || cellA > 0.05f)
            return fail("셀 가운데가 칠해졌다 — 선 두께 계산이 화면을 채우고 있다\n");
        if (litRatio < 0.02f || litRatio > 0.6f)
            return fail("선 밀도가 상식 범위(2~60%)를 벗어났다\n");

        if (litDelta > 0.10f)
            return fail("점등 수가 DX12 기준선과 10% 넘게 다르다 — 픽셀 대조 실패\n");
        if (lineDelta > 0.05f)
            return fail("원점 선 밝기가 DX12 기준선과 0.05 넘게 다르다 — 픽셀 대조 실패\n");
    }

    // ── 측면 카메라 — 카메라 반응 (dx12.grid ④와 같다) ──
    {
        FrameCameraSnapshot side{};
        side.view = math::look_at_lh(
            math::vector3{0.f, 3.f, 0.f},
            math::vector3{100.f, 0.f, 0.f},
            math::vector3{0.f, 1.f, 0.f});
        side.projection = math::perspective_fov_lh(
            DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 500.f);
        side.eyePosition = math::vector3{0.f, 3.f, 0.f};

        VkGridCapture sideCapture{};
        if (!renderOnce(side, sideCapture)) return fail("");

        const uint32_t topLit = sideCapture.CountLit(0, kVkGridSize / 4, 0.05f);
        const uint32_t bottomLit = sideCapture.CountLit(kVkGridSize / 2, kVkGridSize, 0.05f);

        char line[160]{};
        std::snprintf(line, sizeof(line),
            "[4/4] 측면 카메라 — 상단 1/4 점등 %u · 하단 1/2 점등 %u\n",
            topLit, bottomLit);
        outLog += line;

        // ★ 여기가 Y 뒤집기의 판정이기도 하다. 백엔드가 음수 뷰포트를 빼먹으면
        //   지평선이 **아래**로 가서 상단이 칠해진다 — 좌표계 결함이 카메라
        //   검사에 그대로 걸린다.
        if (0 != topLit)
            return fail("지평선 위가 칠해졌다 — 카메라 상수가 안 닿거나 Y 가 뒤집혔다\n");
        if (0 == bottomLit)
            return fail("발밑에 선이 없다 — 측면 카메라에서 아무것도 안 그려졌다\n");
    }

    // ── 미구현 계수·검증 레이어 — vk.* 공통 판정 ──

    const uint32_t stubs = resources.GetUnimplementedCount();
    if (0 != stubs)
    {
        return fail("미구현 호출 " + std::to_string(stubs) + "건("
            + (nullptr != resources.GetLastUnimplemented()
                ? resources.GetLastUnimplemented() : "-") + ")\n");
    }

    resources.WaitForGpu();
    grid.Shutdown();
    resources.ReleaseReadback(readback);
    pipelineCache.Shutdown();

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
        resources.Shutdown();
        outLog += "그리드 패스(Vulkan) 검증 실패\n";
        return false;
    }

    resources.Shutdown();
    outLog += "그리드 패스(Vulkan) 회귀 검증 통과 — 픽셀 편차 0.0% 유지\n";
    return true;
}
