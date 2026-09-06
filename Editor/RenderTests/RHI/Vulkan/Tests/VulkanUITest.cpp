#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12Encoder.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Passes/UI/EnhancedUIPass.h"
#include "Texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kUiRhiSize = 256;

    struct UiRhiSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct UiRhiFixture
    {
        Texture* redTexture{ nullptr };
        Texture* blueTexture{ nullptr };
        std::vector<EnhancedUIPass::Rect> baseRects;
        std::vector<EnhancedUIPass::Rect> texturedRects;

        UiRhiFixture()
        {
            const uint8_t red[4] = { 255, 0, 0, 255 };
            const uint8_t blue[4] = { 0, 0, 255, 255 };
            redTexture = Texture::CreateFromPixels(1, 1, "rhi_ui_red",
                RHIFormat::RGBA8Unorm, red);
            blueTexture = Texture::CreateFromPixels(1, 1, "rhi_ui_blue",
                RHIFormat::RGBA8Unorm, blue);

            EnhancedUIPass::Rect redRect{};
            redRect.left = 10.f;
            redRect.top = 10.f;
            redRect.right = 60.f;
            redRect.bottom = 60.f;
            redRect.color = { 1.f, 0.f, 0.f, 1.f };
            redRect.layerOrder = 0;
            baseRects.push_back(redRect);

            EnhancedUIPass::Rect blueRect{};
            blueRect.left = 35.f;
            blueRect.top = 35.f;
            blueRect.right = 85.f;
            blueRect.bottom = 85.f;
            blueRect.color = { 0.f, 0.f, 1.f, 1.f };
            blueRect.layerOrder = 1;
            baseRects.push_back(blueRect);

            EnhancedUIPass::Rect greenRect{};
            greenRect.left = 140.f;
            greenRect.top = 10.f;
            greenRect.right = 240.f;
            greenRect.bottom = 110.f;
            greenRect.color = { 0.f, 1.f, 0.f, 1.f };
            greenRect.layerOrder = 0;
            baseRects.push_back(greenRect);

            EnhancedUIPass::Rect blendRect{};
            blendRect.left = 160.f;
            blendRect.top = 30.f;
            blendRect.right = 220.f;
            blendRect.bottom = 90.f;
            blendRect.color = { 1.f, 0.f, 0.f, 0.5f };
            blendRect.layerOrder = 1;
            baseRects.push_back(blendRect);

            // 목록 순서와 layerOrder 순서를 의도적으로 다르게 둔다.
            std::swap(baseRects[1], baseRects[2]);

            for (uint32_t i = 0; i < 4; ++i)
            {
                EnhancedUIPass::Rect rect{};
                rect.left = 10.f + 55.f * static_cast<float>(i);
                rect.top = 150.f;
                rect.right = rect.left + 40.f;
                rect.bottom = 200.f;
                rect.color = { 1.f, 1.f, 1.f, 1.f };
                rect.texture = (0 == (i & 1u)) ? redTexture : blueTexture;
                texturedRects.push_back(rect);
            }
        }

        ~UiRhiFixture()
        {
            delete redTexture;
            delete blueTexture;
        }

        bool IsValid() const
        {
            return nullptr != redTexture && nullptr != blueTexture;
        }
    };

    struct UiRhiFrame
    {
        RHIReadbackImage image;
        EnhancedRenderGraph::Stats graph{};
        uint32_t rects{ 0 };
        uint32_t batches{ 0 };
    };

    struct UiRhiCapture
    {
        UiRhiFrame base;
        UiRhiFrame textured;

        float redOnly{ 0.f };
        float overlapR{ 0.f };
        float overlapB{ 0.f };
        float greenOnly{ 0.f };
        float blendR{ 0.f };
        float blendG{ 0.f };
        float outsideA{ 0.f };
        float textureRed{ 0.f };
        float textureBlue{ 0.f };

        uint32_t textureUploads{ 0 };
        uint32_t textureHits{ 0 };
        uint32_t textureFailures{ 0 };
        uint32_t fromCpuPixels{ 0 };
    };

    template <typename TResources, typename TTextureCache>
    bool CaptureUiRhiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        TTextureCache& textures, const UiRhiFixture& fixture,
        UiRhiCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.textureCache = &textures;
        context.width = kUiRhiSize;
        context.height = kUiRhiSize;

        EnhancedUIPass ui;
        RHIReadback readback{};
        bool frameOpen = false;
        uint64_t frameIndex = 0;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            ui.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!ui.Initialize(context, outError)) return fail(outError);
        if (!resources.CreateReadback(kUiRhiSize, kUiRhiSize,
                EnhancedUIPass::kOutputFormat, 1, readback, outError))
            return fail(outError);

        const auto render = [&](const std::vector<EnhancedUIPass::Rect>& rects,
            UiRhiFrame& frame) -> bool
        {
            ui.SetRects(&rects);
            if (!resources.BeginFrame(outError)) return false;
            frameOpen = true;
            textures.BeginFrame(frameIndex++);
            if (!ui.PrepareFrame(context, outError)) return false;

            EnhancedRenderGraph graph(
                static_cast<IRenderDeviceServices&>(resources));
            ui.Declare(graph, context);
            const RGHandle output = ui.GetOutput();
            if (!output.IsValid())
            {
                outError = "UI 출력이 선언되지 않았다";
                return false;
            }
            graph.AddPass("UIRHI.Readback",
                { { output, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback(
                        readback, executeContext.ResolveHandle(output));
                }, true);
            if (!graph.Compile(outError) || !graph.Execute(outError)) return false;
            frame.graph = graph.GetStats();
            frame.rects = ui.GetLastRectCount();
            frame.batches = ui.GetLastBatchCount();

            if (!resources.EndFrame(outError)) return false;
            frameOpen = false;
            resources.WaitForGpu();
            return resources.MapReadback(readback, frame.image, outError);
        };

        if (!render(fixture.baseRects, outCapture.base)) return fail(outError);
        if (!render(fixture.texturedRects, outCapture.textured)) return fail(outError);

        outCapture.redOnly = outCapture.base.image.At(20, 20, 0);
        outCapture.overlapR = outCapture.base.image.At(50, 50, 0);
        outCapture.overlapB = outCapture.base.image.At(50, 50, 2);
        outCapture.greenOnly = outCapture.base.image.At(150, 20, 1);
        outCapture.blendR = outCapture.base.image.At(190, 60, 0);
        outCapture.blendG = outCapture.base.image.At(190, 60, 1);
        outCapture.outsideA = outCapture.base.image.At(120, 200, 3);
        outCapture.textureRed = outCapture.textured.image.At(30, 175, 0);
        outCapture.textureBlue = outCapture.textured.image.At(85, 175, 2);

        const auto stats = textures.GetStats();
        outCapture.textureUploads = stats.uploads;
        outCapture.textureHits = stats.hits;
        outCapture.textureFailures = stats.failures;
        outCapture.fromCpuPixels = stats.fromCpuPixels;

        ui.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }

    bool UiRhiFunctional(const UiRhiCapture& capture)
    {
        const auto graphOk = [](const UiRhiFrame& frame)
        {
            return 2 == frame.graph.passesExecuted &&
                0 == frame.graph.passesCulled &&
                1 == frame.graph.transientCreated;
        };
        return graphOk(capture.base) && graphOk(capture.textured) &&
            4 == capture.base.rects && 1 == capture.base.batches &&
            4 == capture.textured.rects && 4 == capture.textured.batches &&
            capture.redOnly > 0.9f &&
            capture.overlapR < 0.1f && capture.overlapB > 0.9f &&
            capture.greenOnly > 0.9f &&
            capture.blendR > 0.2f && capture.blendR < 0.8f &&
            capture.blendG > 0.2f && capture.blendG < 0.8f &&
            capture.outsideA < 0.01f &&
            capture.textureRed > 0.9f && capture.textureBlue > 0.9f &&
            2 == capture.textureUploads && capture.textureHits >= 2 &&
            0 == capture.textureFailures && 2 == capture.fromCpuPixels;
    }

    struct UiRhiComparison
    {
        float maxDelta{ 0.f };
        double sumDelta{ 0.0 };
        uint64_t samples{ 0 };
        uint64_t overThreshold{ 0 };
        bool shapeMatches{ true };
    };

    void CompareUiRhiImage(const RHIReadbackImage& lhs,
        const RHIReadbackImage& rhs, UiRhiComparison& comparison)
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
                    if (delta > 0.003f) ++comparison.overThreshold;
                }
            }
        }
    }
}

bool RunVulkanUITest(std::string& outLog)
{
    outLog += "── UI 공용 패스 — DX12/Vulkan order·blend·texture batch 대조 ──\n";
    UiRhiFixture fixture;
    if (!fixture.IsValid())
    {
        outLog += "[1/6] 합성 UI 텍스처 생성 실패\n";
        return false;
    }

    UiRhiCapture dx12Capture{};
    UiRhiCapture vkCapture{};
    std::string error;
    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12TextureCache textures;
        if (!resources.Initialize(kUiRhiSize, kUiRhiSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_ui.cache", error) ||
            !roots.Initialize(&resources, error) ||
            !textures.Initialize(&resources, error))
        {
            outLog += "[1/6] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureUiRhiBackend(
            resources, pipelines, roots, textures, fixture, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        textures.Shutdown();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/6] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }

    char dx12Line[384]{};
    std::snprintf(dx12Line, sizeof(dx12Line),
        "[1/6] DX12 — rect/batch %u/%u · red %.3f · overlap R/B %.3f/%.3f · "
        "green %.3f · blend R/G %.3f/%.3f · outside A %.3f\n"
        "      texture R/B %.3f/%.3f · mixed batch %u · upload/hit %u/%u\n",
        dx12Capture.base.rects, dx12Capture.base.batches,
        dx12Capture.redOnly, dx12Capture.overlapR, dx12Capture.overlapB,
        dx12Capture.greenOnly, dx12Capture.blendR, dx12Capture.blendG,
        dx12Capture.outsideA, dx12Capture.textureRed, dx12Capture.textureBlue,
        dx12Capture.textured.batches,
        dx12Capture.textureUploads, dx12Capture.textureHits);
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/6] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanTextureCache textures;
    if (!resources.Initialize(kUiRhiSize, kUiRhiSize, true, error))
    {
        outLog += "[2/6] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!textures.Initialize(&resources, error))
    {
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/6] Vulkan texture cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        UiRhiSpirvScope spirv;
        captured = CaptureUiRhiBackend(
            resources, pipelines, pipelines, textures, fixture, vkCapture, error);
    }
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[416]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/6] Vulkan — rect/batch %u/%u · red %.3f · overlap R/B %.3f/%.3f · "
            "green %.3f · blend R/G %.3f/%.3f · outside A %.3f\n"
            "      texture R/B %.3f/%.3f · mixed batch %u · upload/hit/fail %u/%u/%u · "
            "그래프 %u패스/%u transient\n",
            vkCapture.base.rects, vkCapture.base.batches,
            vkCapture.redOnly, vkCapture.overlapR, vkCapture.overlapB,
            vkCapture.greenOnly, vkCapture.blendR, vkCapture.blendG,
            vkCapture.outsideA, vkCapture.textureRed, vkCapture.textureBlue,
            vkCapture.textured.batches,
            vkCapture.textureUploads, vkCapture.textureHits,
            vkCapture.textureFailures,
            vkCapture.base.graph.passesExecuted,
            vkCapture.base.graph.transientCreated);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        UiRhiFunctional(dx12Capture) && UiRhiFunctional(vkCapture);

    UiRhiComparison comparison{};
    CompareUiRhiImage(dx12Capture.base.image,
        vkCapture.base.image, comparison);
    CompareUiRhiImage(dx12Capture.textured.image,
        vkCapture.textured.image, comparison);
    const double meanDelta = (0 == comparison.samples) ? 0.0 :
        comparison.sumDelta / static_cast<double>(comparison.samples);

    char compareLine[320]{};
    std::snprintf(compareLine, sizeof(compareLine),
        "[3/6] base/textured 전체 RGBA %llu표본 — 최대 %.6f · 평균 %.8f · "
        "0.003 초과 %llu\n",
        static_cast<unsigned long long>(comparison.samples),
        comparison.maxDelta, meanDelta,
        static_cast<unsigned long long>(comparison.overThreshold));
    outLog += compareLine;
    if (!comparison.shapeMatches || 0 == comparison.samples ||
        comparison.maxDelta > 0.003f || meanDelta > 0.00002 ||
        0 != comparison.overThreshold)
    {
        passed = false;
        outLog += "UI 전체 픽셀 대조 허용 범위를 벗어났다\n";
    }

    const float predicateDelta = (std::max)({
        std::fabs(dx12Capture.redOnly - vkCapture.redOnly),
        std::fabs(dx12Capture.overlapR - vkCapture.overlapR),
        std::fabs(dx12Capture.overlapB - vkCapture.overlapB),
        std::fabs(dx12Capture.greenOnly - vkCapture.greenOnly),
        std::fabs(dx12Capture.blendR - vkCapture.blendR),
        std::fabs(dx12Capture.blendG - vkCapture.blendG),
        std::fabs(dx12Capture.outsideA - vkCapture.outsideA),
        std::fabs(dx12Capture.textureRed - vkCapture.textureRed),
        std::fabs(dx12Capture.textureBlue - vkCapture.textureBlue),
    });
    char predicateLine[288]{};
    std::snprintf(predicateLine, sizeof(predicateLine),
        "[4/6] pixel coordinate·layer order·alpha blend·texture batch 판정 최대 편차 "
        "%.6f · texture %u upload/%u hit/%u failure\n",
        predicateDelta, vkCapture.textureUploads,
        vkCapture.textureHits, vkCapture.textureFailures);
    outLog += predicateLine;
    if (predicateDelta > 0.003f) passed = false;

    outLog += "[5/6] PrepareFrame texture lifetime·root t0·sampled t1·static s0 · 미구현 " +
        std::to_string(stubs) + "\n";
    outLog += "[6/6] Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "UI 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "UI 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}
