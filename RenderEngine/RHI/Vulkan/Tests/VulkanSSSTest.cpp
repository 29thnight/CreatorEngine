#ifndef DYNAMICCPP_EXPORTS
#include "../VulkanSelfTest.h"
#include "../VulkanDeviceResources.h"
#include "../VulkanPipelineCache.h"
#include "../VulkanShaderCompiler.h"
#include "../../DX12/DX12DeviceResources.h"
#include "../../DX12/DX12PSOManager.h"
#include "../../DX12/DX12RootSignatureCache.h"
#include "../../DX12/DX12ShaderCompiler.h"
#include "../../../Render/Graph/EnhancedRenderGraph.h"
#include "../../../Render/Passes/Lighting/EnhancedSSSPass.h"
#include "../../../Render/Scene/EnhancedSceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kSssTestSize = 256;
    constexpr uint32_t kSssLeftX = 64;
    constexpr uint32_t kSssRightX = 180;
    constexpr uint32_t kSssPointY = 128;
    constexpr uint32_t kSssStepX = 190;
    constexpr uint32_t kSssHorizontalSlice = 0;
    constexpr uint32_t kSssFinalSlice = 1;
    constexpr const char* kSssSceneShader = "SelfTest/SssScene.hlsl";

    struct SssSpirvScope
    {
        SssSpirvScope() { VulkanShaderCompiler::SetActive(true); }
        ~SssSpirvScope() { VulkanShaderCompiler::SetActive(false); }
    };

    struct SssSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        uint32_t leftX{ 0 };
        uint32_t rightX{ 0 };
        uint32_t pointY{ 0 };
        uint32_t stepX{ 0 };
        uint32_t pad[2]{};
    };

    struct SssCapture
    {
        uint32_t horizontalRow{ 0 };
        uint32_t horizontalColumn{ 0 };
        uint32_t finalRow{ 0 };
        uint32_t finalColumn{ 0 };
        float beyondStep{ 0.f };
        float towardFlat{ 0.f };
        double energy{ 0.0 };
        std::vector<float> rgba;
        EnhancedRenderGraph::Stats graph{};
    };

    uint32_t CountRow(const RHIReadbackImage& image, uint32_t slice,
        uint32_t y, uint32_t channel, float threshold)
    {
        uint32_t count = 0;
        for (uint32_t x = 0; x < image.width; ++x)
            if (image.At(x, y, channel, slice) > threshold) ++count;
        return count;
    }

    uint32_t CountColumn(const RHIReadbackImage& image, uint32_t slice,
        uint32_t x, uint32_t channel, float threshold)
    {
        uint32_t count = 0;
        for (uint32_t y = 0; y < image.height; ++y)
            if (image.At(x, y, channel, slice) > threshold) ++count;
        return count;
    }

    void AnalyzeSss(const RHIReadbackImage& image, SssCapture& capture)
    {
        capture.horizontalRow = CountRow(
            image, kSssHorizontalSlice, kSssPointY, 0, 0.001f);
        capture.horizontalColumn = CountColumn(
            image, kSssHorizontalSlice, kSssLeftX, 0, 0.001f);
        capture.finalRow = CountRow(
            image, kSssFinalSlice, kSssPointY, 0, 0.001f);
        capture.finalColumn = CountColumn(
            image, kSssFinalSlice, kSssLeftX, 0, 0.001f);

        for (uint32_t x = kSssStepX; x < image.width; ++x)
        {
            capture.beyondStep = (std::max)(capture.beyondStep,
                image.At(x, kSssPointY, 1, kSssFinalSlice));
        }
        for (uint32_t x = kSssRightX - (kSssStepX - kSssRightX);
            x < kSssRightX; ++x)
        {
            capture.towardFlat = (std::max)(capture.towardFlat,
                image.At(x, kSssPointY, 1, kSssFinalSlice));
        }

        constexpr uint32_t kSlices = 2;
        constexpr uint32_t kChannels = 4;
        capture.rgba.resize(static_cast<size_t>(kSlices) * image.width *
            image.height * kChannels);
        size_t destination = 0;
        for (uint32_t slice = 0; slice < kSlices; ++slice)
        {
            for (uint32_t y = 0; y < image.height; ++y)
            {
                for (uint32_t x = 0; x < image.width; ++x)
                {
                    for (uint32_t channel = 0; channel < kChannels; ++channel)
                        capture.rgba[destination++] = image.At(x, y, channel, slice);

                    if (slice == kSssFinalSlice)
                    {
                        capture.energy += image.At(x, y, 0, slice);
                        capture.energy += image.At(x, y, 1, slice);
                    }
                }
            }
        }
    }

    bool IsFunctional(const SssCapture& capture)
    {
        return 4 == capture.graph.passesExecuted &&
            0 == capture.graph.passesCulled &&
            4 == capture.graph.transientCreated &&
            capture.horizontalRow >= 3 &&
            capture.horizontalColumn <= 1 &&
            capture.finalRow >= 3 &&
            capture.finalColumn >= 3 &&
            capture.towardFlat >= 0.001f &&
            capture.beyondStep <= capture.towardFlat * 0.5f &&
            capture.energy >= 1.0 && capture.energy <= 4.0;
    }

    template <typename TResources>
    bool CaptureSssBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        SssCapture& outCapture, std::string& outError)
    {
        FrameCameraSnapshot camera{};
        camera.fov = DirectX::XM_PIDIV4;

        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.width = kSssTestSize;
        context.height = kSssTestSize;
        context.camera = &camera;

        EnhancedSSSPass sss;
        RHIReadback readback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            sss.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!sss.Initialize(context, outError)) return fail(outError);
        sss.SetKeepAlive(true);

        const RHIPipelineLayoutParam sceneParams[] = {
            RHILayout::Cbv(0),
            RHILayout::UavTable(2, 0),
        };
        RHIPipelineLayoutDesc sceneLayoutDesc{};
        sceneLayoutDesc.params = sceneParams;
        const RHIPipelineLayoutHandle sceneLayout =
            roots.GetOrCreate(sceneLayoutDesc, outError);
        if (!sceneLayout.IsValid()) return fail(outError);

        RHIShaderBlob sceneBlob;
        if (!DX12ShaderCompiler::CompileFile(
            kSssSceneShader, "CSMain", "cs_5_0", sceneBlob, outError))
            return fail(outError);

        RHIComputePipelineDesc scenePipelineDesc{};
        scenePipelineDesc.csBytecode = sceneBlob.Data();
        scenePipelineDesc.csSize = sceneBlob.Size();
        scenePipelineDesc.layout = sceneLayout;
        const RHIPipelineHandle scenePipeline =
            pipelines.GetOrCreateCompute(scenePipelineDesc, outError);
        if (!scenePipeline.IsValid()) return fail(outError);

        if (!resources.CreateReadback(kSssTestSize, kSssTestSize,
            EnhancedSSSPass::kOutputFormat, 2, readback, outError))
            return fail(outError);

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (!sss.PrepareFrame(context, outError)) return fail(outError);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        RGTextureDesc colorDesc{};
        colorDesc.width = kSssTestSize;
        colorDesc.height = kSssTestSize;
        colorDesc.format = EnhancedSSSPass::kOutputFormat;
        colorDesc.allowUnorderedAccess = true;
        colorDesc.name = "SSSRHI.SourceColor";
        const RGHandle color = graph.CreateTexture(colorDesc);

        RGTextureDesc depthDesc = colorDesc;
        depthDesc.format = RHIFormat::R32Float;
        depthDesc.name = "SSSRHI.SourceDepth";
        const RGHandle depth = graph.CreateTexture(depthDesc);

        graph.AddPass("SSSRHI.Scene",
            { { color, RHIResourceState::UnorderedAccess },
              { depth, RHIResourceState::UnorderedAccess } },
            [&, color, depth](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                SssSceneParams params{};
                params.sizeX = kSssTestSize;
                params.sizeY = kSssTestSize;
                params.leftX = kSssLeftX;
                params.rightX = kSssRightX;
                params.pointY = kSssPointY;
                params.stepX = kSssStepX;
                const RHIBufferSlice constants =
                    resources.UploadConstants(&params, sizeof(params));
                if (!constants.IsValid()) return;

                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(
                        executeContext.ResolveHandle(color), RHIFormat::RGBA16Float),
                    RHIBindingDesc::Uav2D(
                        executeContext.ResolveHandle(depth), RHIFormat::R32Float),
                };
                const RHIBindingTable bindings = resources.CreateBindings(uavs);
                if (!bindings.IsValid()) return;

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, scenePipeline);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                encoder.SetBindings(RHIBindPoint::Compute, 1, bindings);
                encoder.Dispatch((kSssTestSize + 7) / 8,
                    (kSssTestSize + 7) / 8, 1);
            });

        EnhancedSSSPass::Inputs inputs{};
        inputs.color = color;
        inputs.depth = depth;
        sss.SetInputs(inputs);
        sss.Declare(graph, context);

        const RGHandle horizontal = sss.GetHorizontal();
        const RGHandle output = sss.GetOutput();
        if (!horizontal.IsValid() || !output.IsValid())
            return fail("SSS 출력이 선언되지 않았다");

        graph.AddPass("SSSRHI.Readback",
            { { horizontal, RHIResourceState::CopySource },
              { output, RHIResourceState::CopySource } },
            [&, horizontal, output](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback(readback,
                    executeContext.ResolveHandle(horizontal), kSssHorizontalSlice);
                executeContext.encoder->CopyToReadback(readback,
                    executeContext.ResolveHandle(output), kSssFinalSlice);
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
        outCapture.graph = graph.GetStats();

        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        RHIReadbackImage image{};
        if (!resources.MapReadback(readback, image, outError)) return fail(outError);
        AnalyzeSss(image, outCapture);

        sss.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }
}

bool RunVulkanSSSTest(std::string& outLog)
{
    outLog += "── SSS 공용 패스 — DX12/Vulkan 2축 blur·depth gate 픽셀 대조 ──\n";
    SssCapture dx12Capture{};
    SssCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!resources.Initialize(kSssTestSize, kSssTestSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_sss.cache", error) ||
            !roots.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureSssBackend(
            resources, pipelines, roots, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/4] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }

    char dx12Line[256]{};
    std::snprintf(dx12Line, sizeof(dx12Line),
        "[1/4] DX12 — 가로 %u/%u · 최종 %u/%u · gate %.4f/%.4f · 에너지 %.2f\n",
        dx12Capture.horizontalRow, dx12Capture.horizontalColumn,
        dx12Capture.finalRow, dx12Capture.finalColumn,
        dx12Capture.beyondStep, dx12Capture.towardFlat, dx12Capture.energy);
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    if (!resources.Initialize(kSssTestSize, kSssTestSize, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);

    bool captured = false;
    {
        SssSpirvScope spirv;
        captured = CaptureSssBackend(
            resources, pipelines, pipelines, vkCapture, error);
    }

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[256]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/4] Vulkan — 가로 %u/%u · 최종 %u/%u · gate %.4f/%.4f · "
            "에너지 %.2f · 그래프 %u패스/%u transient\n",
            vkCapture.horizontalRow, vkCapture.horizontalColumn,
            vkCapture.finalRow, vkCapture.finalColumn,
            vkCapture.beyondStep, vkCapture.towardFlat, vkCapture.energy,
            vkCapture.graph.passesExecuted, vkCapture.graph.transientCreated);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        IsFunctional(dx12Capture) && IsFunctional(vkCapture);

    float maxPixelDelta = 0.f;
    double meanPixelDelta = 0.0;
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
        }
        meanPixelDelta /= static_cast<double>(dx12Capture.rgba.size());
    }

    char compare[256]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] 전체 2-slice RGBA 픽셀 — 최대 편차 %.6f · 평균 %.8f · "
        "에너지 편차 %.6f\n",
        maxPixelDelta, meanPixelDelta,
        std::fabs(dx12Capture.energy - vkCapture.energy));
    outLog += compare;

    if (maxPixelDelta > 0.003f || meanPixelDelta > 0.00002 ||
        std::fabs(dx12Capture.energy - vkCapture.energy) > 0.01 ||
        dx12Capture.horizontalRow != vkCapture.horizontalRow ||
        dx12Capture.horizontalColumn != vkCapture.horizontalColumn ||
        dx12Capture.finalRow != vkCapture.finalRow ||
        dx12Capture.finalColumn != vkCapture.finalColumn)
    {
        passed = false;
        outLog += "SSS 전체 픽셀 또는 축별 번짐 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] RGBA16/R32 UAV→SRV·2축 RT·CBV/table·2-slice readback · "
        "미구현 " + std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "SSS 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "SSS 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

#endif
