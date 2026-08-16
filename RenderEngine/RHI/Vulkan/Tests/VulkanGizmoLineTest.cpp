#ifndef DYNAMICCPP_EXPORTS
#include "../VulkanSelfTest.h"
#include "../VulkanDeviceResources.h"
#include "../VulkanPipelineCache.h"
#include "../../RHIShaderCompiler.h"
#include "../../DX12/DX12DeviceResources.h"
#include "../../DX12/DX12Encoder.h"
#include "../../DX12/DX12PSOManager.h"
#include "../../DX12/DX12RootSignatureCache.h"
#include "../../../Render/Graph/EnhancedRenderGraph.h"
#include "../../../Render/Passes/Editor/EnhancedGizmoLinePass.h"
#include "../../FrameCameraSnapshot.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    constexpr uint32_t kGizmoLineRhiSize = 256;

    struct GizmoLineRhiSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct GizmoLineRhiFrame
    {
        RHIReadbackImage image;
        EnhancedRenderGraph::Stats graph{};
        uint32_t vertices{ 0 };
        uint32_t draws{ 0 };
        uint32_t lit{ 0 };
        float red{ 0.f };
        float blue{ 0.f };
        float empty{ 0.f };
    };

    struct GizmoLineRhiCapture
    {
        GizmoLineRhiFrame visible;
        GizmoLineRhiFrame farAway;
        bool shapeCountsMatch{ false };
    };

    bool GizmoLineRhiProjectToPixel(const FrameCameraSnapshot& camera,
        float worldX, float worldY, float worldZ,
        uint32_t& outX, uint32_t& outY)
    {
        const Mathf::xMatrix vp = XMMatrixMultiply(camera.view, camera.projection);
        const Mathf::xVector clip = XMVector4Transform(
            XMVectorSet(worldX, worldY, worldZ, 1.f), vp);
        const float w = XMVectorGetW(clip);
        if (w <= 1e-6f) return false;

        const float ndcX = XMVectorGetX(clip) / w;
        const float ndcY = XMVectorGetY(clip) / w;
        if (ndcX < -1.f || ndcX > 1.f ||
            ndcY < -1.f || ndcY > 1.f)
            return false;

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) *
            static_cast<float>(kGizmoLineRhiSize));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) *
            static_cast<float>(kGizmoLineRhiSize));
        outX = (std::min)(outX, kGizmoLineRhiSize - 1);
        outY = (std::min)(outY, kGizmoLineRhiSize - 1);
        return true;
    }

    float GizmoLineRhiMaxInWindow(const RHIReadbackImage& image,
        uint32_t centerX, uint32_t centerY, uint32_t radius, uint32_t channel)
    {
        float result = 0.f;
        const uint32_t x0 = (centerX > radius) ? centerX - radius : 0;
        const uint32_t y0 = (centerY > radius) ? centerY - radius : 0;
        const uint32_t x1 = (std::min)(centerX + radius, image.width - 1);
        const uint32_t y1 = (std::min)(centerY + radius, image.height - 1);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x = x0; x <= x1; ++x)
                result = (std::max)(result, image.At(x, y, channel));
        return result;
    }

    uint32_t GizmoLineRhiCountLit(const RHIReadbackImage& image)
    {
        uint32_t result = 0;
        for (uint32_t y = 0; y < image.height; ++y)
        {
            for (uint32_t x = 0; x < image.width; ++x)
            {
                if (image.At(x, y, 0) > 0.1f ||
                    image.At(x, y, 1) > 0.1f ||
                    image.At(x, y, 2) > 0.1f)
                    ++result;
            }
        }
        return result;
    }

    bool GizmoLineRhiCheckShapeCounts(EnhancedGizmoLinePass& gizmo,
        const EnhancedFrameContext& context, std::string& outError)
    {
        const auto countOf = [&](auto&& add) -> uint32_t
        {
            gizmo.ResetLines();
            add();
            if (!gizmo.PrepareFrame(context, outError)) return 0;
            return gizmo.GetLastVertexCount();
        };

        const std::array<uint32_t, 6> actual = {
            countOf([&] { gizmo.AddWireCircle(
                { 0, 0, 0 }, 1.f, { 0, 1, 0 }, { 1, 1, 1, 1 }); }),
            countOf([&] { gizmo.AddWireSphere(
                { 0, 0, 0 }, 1.f, { 1, 1, 1, 1 }); }),
            countOf([&] { gizmo.AddWireBox(
                Mathf::Matrix::Identity, { 1, 1, 1 }, { 1, 1, 1, 1 }); }),
            countOf([&] { gizmo.AddWireCapsule(
                Mathf::Matrix::Identity, 0.5f, 2.f, { 1, 1, 1, 1 }); }),
            countOf([&] { gizmo.AddWireCone(
                { 0, 0, 0 }, { 0, -1, 0 }, 2.f, 45.f, { 1, 1, 1, 1 }); }),
            countOf([&]
            {
                const DirectX::BoundingFrustum frustum(
                    XMMatrixPerspectiveFovLH(
                        DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 10.f));
                gizmo.AddBoundingFrustum(frustum, { 1, 1, 1, 1 });
            }),
        };
        constexpr std::array<uint32_t, 6> expected = {
            128, 384, 24, 1056, 128, 24
        };
        return actual == expected;
    }

    bool GizmoLineRhiAnalyzeVisible(const FrameCameraSnapshot& camera,
        GizmoLineRhiFrame& frame)
    {
        uint32_t redX = 0, redY = 0;
        uint32_t blueX = 0, blueY = 0;
        uint32_t emptyX = 0, emptyY = 0;
        const float diagonal = 4.f * 0.70710678f;
        if (!GizmoLineRhiProjectToPixel(camera,
                2.5f, 0.f, 0.f, redX, redY) ||
            !GizmoLineRhiProjectToPixel(camera,
                diagonal, 0.f, diagonal, blueX, blueY) ||
            !GizmoLineRhiProjectToPixel(camera,
                2.f, 0.f, 2.f, emptyX, emptyY))
            return false;

        frame.red = GizmoLineRhiMaxInWindow(
            frame.image, redX, redY, 2, 0);
        frame.blue = GizmoLineRhiMaxInWindow(
            frame.image, blueX, blueY, 2, 2);
        frame.empty = (std::max)({
            frame.image.At(emptyX, emptyY, 0),
            frame.image.At(emptyX, emptyY, 1),
            frame.image.At(emptyX, emptyY, 2),
        });
        frame.lit = GizmoLineRhiCountLit(frame.image);
        return true;
    }

    template <typename TResources>
    bool CaptureGizmoLineRhiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        GizmoLineRhiCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.width = kGizmoLineRhiSize;
        context.height = kGizmoLineRhiSize;

        EnhancedGizmoLinePass gizmo;
        RHIReadback readback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            gizmo.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!gizmo.Initialize(context, outError)) return fail(outError);
        gizmo.SetKeepAlive(true);
        outCapture.shapeCountsMatch =
            GizmoLineRhiCheckShapeCounts(gizmo, context, outError);
        if (!outCapture.shapeCountsMatch)
            return fail("GizmoLine 도형 정점 수가 기준과 다르다");

        if (!resources.CreateReadback(kGizmoLineRhiSize, kGizmoLineRhiSize,
                EnhancedGizmoLinePass::kOutputFormat, 1, readback, outError))
            return fail(outError);

        FrameCameraSnapshot visibleCamera{};
        visibleCamera.view = XMMatrixLookAtLH(
            XMVectorSet(0.f, 30.f, 0.f, 1.f),
            XMVectorSet(0.f, 0.f, 0.f, 1.f),
            XMVectorSet(0.f, 0.f, 1.f, 0.f));
        visibleCamera.projection = XMMatrixPerspectiveFovLH(
            DirectX::XM_PIDIV2 * 0.5f, 1.f, 0.1f, 500.f);
        visibleCamera.eyePosition = XMVectorSet(0.f, 30.f, 0.f, 1.f);

        FrameCameraSnapshot farCamera{};
        farCamera.view = XMMatrixLookAtLH(
            XMVectorSet(200.f, 30.f, 200.f, 1.f),
            XMVectorSet(200.f, 0.f, 200.f, 1.f),
            XMVectorSet(0.f, 0.f, 1.f, 0.f));
        farCamera.projection = visibleCamera.projection;
        farCamera.eyePosition = XMVectorSet(200.f, 30.f, 200.f, 1.f);

        gizmo.ResetLines();
        gizmo.AddLine({ -5.f, 0.f, 0.f }, { 5.f, 0.f, 0.f },
            { 1.f, 0.f, 0.f, 1.f });
        gizmo.AddLine({ 0.f, 0.f, -5.f }, { 0.f, 0.f, 5.f },
            { 0.f, 1.f, 0.f, 1.f });
        gizmo.AddWireCircle({ 0.f, 0.f, 0.f }, 4.f, { 0.f, 1.f, 0.f },
            { 0.f, 0.f, 1.f, 1.f });

        const auto render = [&](const FrameCameraSnapshot& camera,
            GizmoLineRhiFrame& frame) -> bool
        {
            context.camera = &camera;
            if (!resources.BeginFrame(outError)) return false;
            frameOpen = true;
            if (!gizmo.PrepareFrame(context, outError)) return false;

            EnhancedRenderGraph graph(
                static_cast<IRenderDeviceServices&>(resources));
            gizmo.Declare(graph, context);
            const RGHandle output = gizmo.GetOutput();
            if (!output.IsValid())
            {
                outError = "GizmoLine 출력이 선언되지 않았다";
                return false;
            }
            graph.AddPass("GizmoLineRHI.Readback",
                { { output, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback(
                        readback, executeContext.ResolveHandle(output));
                }, true);
            if (!graph.Compile(outError) || !graph.Execute(outError)) return false;
            frame.graph = graph.GetStats();
            frame.vertices = gizmo.GetLastVertexCount();
            frame.draws = gizmo.GetLastDrawCount();

            if (!resources.EndFrame(outError)) return false;
            frameOpen = false;
            resources.WaitForGpu();
            if (!resources.MapReadback(readback, frame.image, outError)) return false;
            frame.lit = GizmoLineRhiCountLit(frame.image);
            return true;
        };

        if (!render(visibleCamera, outCapture.visible)) return fail(outError);
        if (!GizmoLineRhiAnalyzeVisible(visibleCamera, outCapture.visible))
            return fail("GizmoLine 표본 투영에 실패했다");
        if (!render(farCamera, outCapture.farAway)) return fail(outError);

        gizmo.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }

    bool GizmoLineRhiFunctional(const GizmoLineRhiCapture& capture)
    {
        const GizmoLineRhiFrame& visible = capture.visible;
        const GizmoLineRhiFrame& farAway = capture.farAway;
        return capture.shapeCountsMatch &&
            2 == visible.graph.passesExecuted &&
            0 == visible.graph.passesCulled &&
            1 == visible.graph.transientCreated &&
            132 == visible.vertices && 1 == visible.draws &&
            visible.red > 0.9f && visible.blue > 0.9f &&
            visible.empty < 0.05f && visible.lit > 0 &&
            2 == farAway.graph.passesExecuted &&
            0 == farAway.graph.passesCulled &&
            1 == farAway.graph.transientCreated &&
            132 == farAway.vertices && 1 == farAway.draws &&
            0 == farAway.lit;
    }

    struct GizmoLineRhiComparison
    {
        float maxDelta{ 0.f };
        double sumDelta{ 0.0 };
        uint64_t samples{ 0 };
        uint64_t overThreshold{ 0 };
        bool shapeMatches{ true };
    };

    void CompareGizmoLineRhiImage(const RHIReadbackImage& lhs,
        const RHIReadbackImage& rhs, GizmoLineRhiComparison& comparison)
    {
        if (!lhs.IsValid() || !rhs.IsValid() ||
            lhs.width != rhs.width || lhs.height != rhs.height)
        {
            comparison.shapeMatches = false;
            return;
        }

        for (uint32_t y = 0; y < lhs.height; ++y)
        {
            for (uint32_t x = 0; x < lhs.width; ++x)
            {
                for (uint32_t channel = 0; channel < 4; ++channel)
                {
                    const float delta = std::fabs(
                        lhs.At(x, y, channel) - rhs.At(x, y, channel));
                    comparison.maxDelta =
                        (std::max)(comparison.maxDelta, delta);
                    comparison.sumDelta += delta;
                    ++comparison.samples;
                    if (delta > 0.05f) ++comparison.overThreshold;
                }
            }
        }
    }
}

bool RunVulkanGizmoLineTest(std::string& outLog)
{
    outLog += "── GizmoLine 공용 패스 — DX12/Vulkan line-list 전체 픽셀 대조 ──\n";
    GizmoLineRhiCapture dx12Capture{};
    GizmoLineRhiCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!resources.Initialize(kGizmoLineRhiSize, kGizmoLineRhiSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_gizmoline.cache", error) ||
            !roots.Initialize(&resources, error))
        {
            outLog += "[1/5] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureGizmoLineRhiBackend(
            resources, pipelines, roots, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/5] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }

    char dx12Line[320]{};
    std::snprintf(dx12Line, sizeof(dx12Line),
        "[1/5] DX12 — 정점/드로우 %u/%u · R/B/빈 곳 %.3f/%.3f/%.3f · "
        "점등 %u · 먼 카메라 %u\n",
        dx12Capture.visible.vertices, dx12Capture.visible.draws,
        dx12Capture.visible.red, dx12Capture.visible.blue,
        dx12Capture.visible.empty, dx12Capture.visible.lit,
        dx12Capture.farAway.lit);
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/5] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    if (!resources.Initialize(kGizmoLineRhiSize, kGizmoLineRhiSize, true, error))
    {
        outLog += "[2/5] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);

    bool captured = false;
    {
        GizmoLineRhiSpirvScope spirv;
        captured = CaptureGizmoLineRhiBackend(
            resources, pipelines, pipelines, vkCapture, error);
    }

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[352]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/5] Vulkan — 정점/드로우 %u/%u · R/B/빈 곳 %.3f/%.3f/%.3f · "
            "점등 %u · 먼 카메라 %u · 그래프 %u패스/%u transient\n",
            vkCapture.visible.vertices, vkCapture.visible.draws,
            vkCapture.visible.red, vkCapture.visible.blue,
            vkCapture.visible.empty, vkCapture.visible.lit,
            vkCapture.farAway.lit,
            vkCapture.visible.graph.passesExecuted,
            vkCapture.visible.graph.transientCreated);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        GizmoLineRhiFunctional(dx12Capture) &&
        GizmoLineRhiFunctional(vkCapture);

    GizmoLineRhiComparison comparison{};
    CompareGizmoLineRhiImage(dx12Capture.visible.image,
        vkCapture.visible.image, comparison);
    CompareGizmoLineRhiImage(dx12Capture.farAway.image,
        vkCapture.farAway.image, comparison);
    const double meanDelta = (0 == comparison.samples) ? 0.0 :
        comparison.sumDelta / static_cast<double>(comparison.samples);
    const uint32_t litDelta =
        (dx12Capture.visible.lit > vkCapture.visible.lit)
        ? dx12Capture.visible.lit - vkCapture.visible.lit
        : vkCapture.visible.lit - dx12Capture.visible.lit;

    char compareLine[320]{};
    std::snprintf(compareLine, sizeof(compareLine),
        "[3/5] visible/far 전체 RGBA %llu표본 — 최대 편차 %.6f · "
        "평균 %.8f · 0.05 초과 %llu · 점등 수 편차 %u\n",
        static_cast<unsigned long long>(comparison.samples),
        comparison.maxDelta, meanDelta,
        static_cast<unsigned long long>(comparison.overThreshold), litDelta);
    outLog += compareLine;

    // 선 래스터화의 끝점·diamond-exit 규칙은 API마다 경계 픽셀에서 다를 수
    // 있다. 전체 RGBA와 점등 수를 모두 재되, 수십 개 경계 픽셀 차이만 허용한다.
    if (!comparison.shapeMatches || 0 == comparison.samples ||
        meanDelta > 0.0001 || comparison.overThreshold > 64 || litDelta > 8)
    {
        passed = false;
        outLog += "GizmoLine 전체 픽셀 또는 선 커버리지 대조 허용 범위를 벗어났다\n";
    }

    const float predicateDelta = (std::max)({
        std::fabs(dx12Capture.visible.red - vkCapture.visible.red),
        std::fabs(dx12Capture.visible.blue - vkCapture.visible.blue),
        std::fabs(dx12Capture.visible.empty - vkCapture.visible.empty),
    });
    char predicateLine[224]{};
    std::snprintf(predicateLine, sizeof(predicateLine),
        "[4/5] 도형 6종·line-list upload·단일 드로우·카메라 반응 — "
        "표본 최대 편차 %.6f · 미구현 %u\n",
        predicateDelta, stubs);
    outLog += predicateLine;
    if (predicateDelta > 0.01f) passed = false;

    outLog += "[5/5] Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "GizmoLine 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "GizmoLine 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

#endif
