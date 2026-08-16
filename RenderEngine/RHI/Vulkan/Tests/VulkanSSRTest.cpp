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
#include "../../../Render/Passes/Lighting/EnhancedSSRPass.h"
#include "../../../Render/Scene/EnhancedSceneRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kSsrRhiSize = 256;
    constexpr uint32_t kSsrRhiGreenStartX = 128;
    constexpr uint32_t kSsrRhiBandMinX = 116;
    constexpr uint32_t kSsrRhiBandMaxX = 128;
    constexpr uint32_t kSsrRhiMetalRowY = 64;
    constexpr uint32_t kSsrRhiPlainRowY = 192;
    constexpr uint32_t kSsrRhiVariantCount = 4;
    constexpr const char* kSsrRhiSceneShader = "SelfTest/SsrScene.hlsl";

    enum SsrRhiVariant : uint32_t
    {
        SsrRhiBase = 0,
        SsrRhiNoThickness = 1,
        SsrRhiCornerMask = 2,
        SsrRhiExceptCornerMask = 3,
    };

    struct SsrRhiSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct SsrRhiSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        uint32_t greenStartX{ 0 };
        uint32_t maskMode{ 0 };
    };

    struct SsrRhiCapture
    {
        float metalBand{ 0.f };
        float plainBand{ 0.f };
        float noThicknessBand{ 0.f };
        float cornerMaskBand{ 0.f };
        float exceptCornerMaskBand{ 0.f };
        bool passThrough{ false };
        std::array<EnhancedRenderGraph::Stats, kSsrRhiVariantCount> graphs{};
        std::vector<float> rgba;
    };

    float SsrRhiBandMax(const RHIReadbackImage& image, uint32_t slice,
        uint32_t y, uint32_t channel)
    {
        float peak = 0.f;
        for (uint32_t x = kSsrRhiBandMinX; x < kSsrRhiBandMaxX; ++x)
            peak = (std::max)(peak, image.At(x, y, channel, slice));
        return peak;
    }

    void AnalyzeSsrRhi(const RHIReadbackImage& image, SsrRhiCapture& capture)
    {
        capture.metalBand = SsrRhiBandMax(
            image, SsrRhiBase, kSsrRhiMetalRowY, 1);
        capture.plainBand = SsrRhiBandMax(
            image, SsrRhiBase, kSsrRhiPlainRowY, 1);
        capture.noThicknessBand = SsrRhiBandMax(
            image, SsrRhiNoThickness, kSsrRhiMetalRowY, 1);
        capture.cornerMaskBand = SsrRhiBandMax(
            image, SsrRhiCornerMask, kSsrRhiMetalRowY, 1);
        capture.exceptCornerMaskBand = SsrRhiBandMax(
            image, SsrRhiExceptCornerMask, kSsrRhiMetalRowY, 1);

        constexpr uint32_t kChannels = 4;
        capture.rgba.resize(static_cast<size_t>(kSsrRhiVariantCount) *
            image.width * image.height * kChannels);
        size_t destination = 0;
        for (uint32_t slice = 0; slice < kSsrRhiVariantCount; ++slice)
        {
            for (uint32_t y = 0; y < image.height; ++y)
            {
                for (uint32_t x = 0; x < image.width; ++x)
                {
                    for (uint32_t channel = 0; channel < kChannels; ++channel)
                        capture.rgba[destination++] = image.At(x, y, channel, slice);
                }
            }
        }
    }

    bool IsSsrRhiFunctional(const SsrRhiCapture& capture)
    {
        for (const EnhancedRenderGraph::Stats& stats : capture.graphs)
        {
            if (3 != stats.passesExecuted || 0 != stats.passesCulled ||
                6 != stats.transientCreated)
                return false;
        }

        return capture.metalBand >= 0.2f &&
            capture.plainBand <= 0.01f &&
            capture.noThicknessBand <= 0.01f &&
            capture.cornerMaskBand <= 0.01f &&
            capture.exceptCornerMaskBand >= 0.2f &&
            capture.passThrough;
    }

    template <typename TResources>
    bool CaptureSsrRhiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        SsrRhiCapture& outCapture, std::string& outError)
    {
        FrameCameraSnapshot camera{};
        camera.view = XMMatrixIdentity();
        camera.projection = XMMatrixOrthographicLH(2.f, 2.f, 0.f, 10.f);
        camera.inverseView = XMMatrixInverse(nullptr, camera.view);
        camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
        camera.eyePosition = XMVectorSet(0.f, 0.f, -1000.f, 1.f);
        camera.fov = DirectX::XM_PIDIV4;

        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.width = kSsrRhiSize;
        context.height = kSsrRhiSize;
        context.camera = &camera;

        EnhancedSSRPass ssr;
        RHIReadback readback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            ssr.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!ssr.Initialize(context, outError)) return fail(outError);
        ssr.SetTime(0.f);
        ssr.SetKeepAlive(true);

        const RHIPipelineLayoutParam sceneParams[] = {
            RHILayout::Cbv(0),
            RHILayout::UavTable(5, 0),
        };
        RHIPipelineLayoutDesc sceneLayoutDesc{};
        sceneLayoutDesc.params = sceneParams;
        const RHIPipelineLayoutHandle sceneLayout =
            roots.GetOrCreate(sceneLayoutDesc, outError);
        if (!sceneLayout.IsValid()) return fail(outError);

        RHIShaderBlob sceneBlob;
        if (!RHIShaderCompiler::CompileFile(
            kSsrRhiSceneShader, "CSMain", "cs_5_0", sceneBlob, outError))
            return fail(outError);

        RHIComputePipelineDesc scenePipelineDesc{};
        scenePipelineDesc.csBytecode = sceneBlob.Data();
        scenePipelineDesc.csSize = sceneBlob.Size();
        scenePipelineDesc.layout = sceneLayout;
        const RHIPipelineHandle scenePipeline =
            pipelines.GetOrCreateCompute(scenePipelineDesc, outError);
        if (!scenePipeline.IsValid()) return fail(outError);

        if (!resources.CreateReadback(kSsrRhiSize, kSsrRhiSize,
            EnhancedSSRPass::kOutputFormat, kSsrRhiVariantCount,
            readback, outError))
            return fail(outError);

        const auto renderVariant = [&](uint32_t variant,
            const EnhancedSSRPass::Tuning& tuning, uint32_t maskMode) -> bool
        {
            if (!resources.BeginFrame(outError)) return false;
            frameOpen = true;

            ssr.SetTuning(tuning);
            ssr.SetEnabled(true);
            if (!ssr.PrepareFrame(context, outError))
            {
                resources.AbortFrame();
                frameOpen = false;
                return false;
            }

            EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
            RGTextureDesc halfDesc{};
            halfDesc.width = kSsrRhiSize;
            halfDesc.height = kSsrRhiSize;
            halfDesc.format = RHIFormat::RGBA16Float;
            halfDesc.allowUnorderedAccess = true;

            halfDesc.name = "SSRRHI.Color";
            const RGHandle color = graph.CreateTexture(halfDesc);
            halfDesc.name = "SSRRHI.MetalRough";
            const RGHandle metalRough = graph.CreateTexture(halfDesc);
            halfDesc.name = "SSRRHI.Normal";
            const RGHandle normal = graph.CreateTexture(halfDesc);

            RGTextureDesc scalarDesc = halfDesc;
            scalarDesc.format = RHIFormat::R32Float;
            scalarDesc.name = "SSRRHI.Depth";
            const RGHandle depth = graph.CreateTexture(scalarDesc);
            scalarDesc.format = RHIFormat::R32Uint;
            scalarDesc.name = "SSRRHI.Bitmask";
            const RGHandle bitmask = graph.CreateTexture(scalarDesc);

            graph.AddPass("SSRRHI.Scene",
                { { color, RHIResourceState::UnorderedAccess },
                  { depth, RHIResourceState::UnorderedAccess },
                  { metalRough, RHIResourceState::UnorderedAccess },
                  { normal, RHIResourceState::UnorderedAccess },
                  { bitmask, RHIResourceState::UnorderedAccess } },
                [&, color, depth, metalRough, normal, bitmask, maskMode](
                    const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    SsrRhiSceneParams params{};
                    params.sizeX = kSsrRhiSize;
                    params.sizeY = kSsrRhiSize;
                    params.greenStartX = kSsrRhiGreenStartX;
                    params.maskMode = maskMode;
                    const RHIBufferSlice constants =
                        resources.UploadConstants(&params, sizeof(params));
                    if (!constants.IsValid()) return;

                    const RHIBindingDesc uavs[] = {
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(color),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(depth),
                            RHIFormat::R32Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(metalRough),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(normal),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(bitmask),
                            RHIFormat::R32Uint),
                    };
                    const RHIBindingTable bindings = resources.CreateBindings(uavs);
                    if (!bindings.IsValid()) return;

                    RHIEncoder& encoder = *executeContext.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, scenePipeline);
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                    encoder.SetBindings(RHIBindPoint::Compute, 1, bindings);
                    encoder.Dispatch((kSsrRhiSize + 7) / 8,
                        (kSsrRhiSize + 7) / 8, 1);
                });

            EnhancedSSRPass::Inputs inputs{};
            inputs.color = color;
            inputs.depth = depth;
            inputs.metalRough = metalRough;
            inputs.normal = normal;
            inputs.bitmask = bitmask;
            ssr.SetInputs(inputs);
            ssr.Declare(graph, context);

            const RGHandle output = ssr.GetOutput();
            if (!output.IsValid())
            {
                outError = "SSR 출력이 선언되지 않았다";
                resources.AbortFrame();
                frameOpen = false;
                return false;
            }

            graph.AddPass("SSRRHI.Readback",
                { { output, RHIResourceState::CopySource } },
                [&, output, variant](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback(
                        readback, executeContext.ResolveHandle(output), variant);
                }, true);

            if (!graph.Compile(outError) || !graph.Execute(outError))
            {
                resources.AbortFrame();
                frameOpen = false;
                return false;
            }
            outCapture.graphs[variant] = graph.GetStats();

            if (!resources.EndFrame(outError))
            {
                resources.AbortFrame();
                frameOpen = false;
                return false;
            }
            frameOpen = false;
            resources.WaitForGpu();
            return true;
        };

        const EnhancedSSRPass::Tuning defaults{};
        EnhancedSSRPass::Tuning noThickness = defaults;
        noThickness.maxThickness = 0.f;
        if (!renderVariant(SsrRhiBase, defaults, 0) ||
            !renderVariant(SsrRhiNoThickness, noThickness, 0) ||
            !renderVariant(SsrRhiCornerMask, defaults, 1) ||
            !renderVariant(SsrRhiExceptCornerMask, defaults, 2))
            return fail(outError);

        ssr.SetEnabled(false);
        EnhancedRenderGraph offGraph(static_cast<IRenderDeviceServices&>(resources));
        RGTextureDesc offDesc{};
        offDesc.width = kSsrRhiSize;
        offDesc.height = kSsrRhiSize;
        offDesc.format = RHIFormat::RGBA16Float;
        offDesc.name = "SSRRHI.OffColor";
        EnhancedSSRPass::Inputs offInputs{};
        offInputs.color = offGraph.CreateTexture(offDesc);
        ssr.SetInputs(offInputs);
        ssr.Declare(offGraph, context);
        outCapture.passThrough =
            ssr.GetOutput().index == offInputs.color.index;

        RHIReadbackImage image{};
        if (!resources.MapReadback(readback, image, outError)) return fail(outError);
        AnalyzeSsrRhi(image, outCapture);

        ssr.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }
}

bool RunVulkanSSRTest(std::string& outLog)
{
    outLog += "── SSR 공용 패스 — DX12/Vulkan ray hit·gate 전체 픽셀 대조 ──\n";
    SsrRhiCapture dx12Capture{};
    SsrRhiCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!resources.Initialize(kSsrRhiSize, kSsrRhiSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_ssr.cache", error) ||
            !roots.Initialize(&resources, error))
        {
            outLog += "[1/5] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureSsrRhiBackend(
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
        "[1/5] DX12 — 금속/비금속 %.4f/%.4f · 두께0 %.4f · "
        "corner/except %.4f/%.4f · 꺼짐 %s\n",
        dx12Capture.metalBand, dx12Capture.plainBand,
        dx12Capture.noThicknessBand, dx12Capture.cornerMaskBand,
        dx12Capture.exceptCornerMaskBand,
        dx12Capture.passThrough ? "통과" : "실패");
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/5] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    if (!resources.Initialize(kSsrRhiSize, kSsrRhiSize, true, error))
    {
        outLog += "[2/5] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);

    bool captured = false;
    {
        SsrRhiSpirvScope spirv;
        captured = CaptureSsrRhiBackend(
            resources, pipelines, pipelines, vkCapture, error);
    }

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[360]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/5] Vulkan — 금속/비금속 %.4f/%.4f · 두께0 %.4f · "
            "corner/except %.4f/%.4f · 꺼짐 %s · 그래프 %u패스/%u transient\n",
            vkCapture.metalBand, vkCapture.plainBand,
            vkCapture.noThicknessBand, vkCapture.cornerMaskBand,
            vkCapture.exceptCornerMaskBand,
            vkCapture.passThrough ? "통과" : "실패",
            vkCapture.graphs[0].passesExecuted,
            vkCapture.graphs[0].transientCreated);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        IsSsrRhiFunctional(dx12Capture) && IsSsrRhiFunctional(vkCapture);

    float maxPixelDelta = 0.f;
    double meanPixelDelta = 0.0;
    uint64_t differingPixels = 0;
    if (dx12Capture.rgba.size() != vkCapture.rgba.size() ||
        dx12Capture.rgba.empty())
    {
        passed = false;
    }
    else
    {
        for (size_t i = 0; i < dx12Capture.rgba.size(); ++i)
        {
            const float delta = std::fabs(
                dx12Capture.rgba[i] - vkCapture.rgba[i]);
            maxPixelDelta = (std::max)(maxPixelDelta, delta);
            meanPixelDelta += delta;
            if (delta > 0.01f) ++differingPixels;
        }
        meanPixelDelta /= static_cast<double>(dx12Capture.rgba.size());
    }

    char compare[320]{};
    std::snprintf(compare, sizeof(compare),
        "[3/5] 전체 4-slice RGBA 픽셀 — 최대 편차 %.6f · 평균 %.8f · "
        "0.01 초과 %llu/%llu\n",
        maxPixelDelta, meanPixelDelta,
        static_cast<unsigned long long>(differingPixels),
        static_cast<unsigned long long>(dx12Capture.rgba.size()));
    outLog += compare;

    const float predicateDelta = (std::max)({
        std::fabs(dx12Capture.metalBand - vkCapture.metalBand),
        std::fabs(dx12Capture.plainBand - vkCapture.plainBand),
        std::fabs(dx12Capture.noThicknessBand - vkCapture.noThicknessBand),
        std::fabs(dx12Capture.cornerMaskBand - vkCapture.cornerMaskBand),
        std::fabs(dx12Capture.exceptCornerMaskBand - vkCapture.exceptCornerMaskBand),
    });
    outLog += "[4/5] 반사·금속·두께·비트플래그 판정 최대 편차 " +
        std::to_string(predicateDelta) + "\n";

    // 광선 잡음이 sin 기반이므로 backend의 초월함수 근사 차이는 경계 표본을
    // 한두 픽셀 움직일 수 있다. 전체 평균과 핵심 띠 판정은 더 엄격하게 묶는다.
    if (maxPixelDelta > 0.31f || meanPixelDelta > 0.0005 ||
        differingPixels > dx12Capture.rgba.size() / 100 ||
        predicateDelta > 0.01f)
    {
        passed = false;
        outLog += "SSR 전체 픽셀 또는 gate 판정 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[5/5] RGBA16/R32/R32U UAV→SRV·CBV/table·4-slice readback · "
        "미구현 " + std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "SSR 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "SSR 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

#endif
