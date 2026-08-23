#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12Encoder.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Passes/PostProcess/EnhancedPostChainPass.h"
#include "Render/Scene/EnhancedSceneRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace
{
    constexpr uint32_t kPostRhiWidth = 256;
    constexpr uint32_t kPostRhiHeight = 256;
    constexpr uint32_t kPostRhiBloomWidth =
        kPostRhiWidth / EnhancedPostChainPass::kBloomStartDivisor;
    constexpr uint32_t kPostRhiBloomHeight =
        kPostRhiHeight / EnhancedPostChainPass::kBloomStartDivisor;
    constexpr uint32_t kPostRhiFinalSlice = 0;
    constexpr uint32_t kPostRhiPreAaSlice = 1;
    constexpr uint32_t kPostRhiVariantCount = 3;
    constexpr const char* kPostRhiSceneShader =
        "SelfTest/PostChainScene.hlsl";

    enum PostRhiVariant : uint32_t
    {
        PostRhiAces = 0,
        PostRhiAgx = 1,
        PostRhiSeparateAces = 2,
    };

    struct PostRhiSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct PostRhiSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        uint32_t padding[2]{};
    };

    struct PostRhiFrame
    {
        RHIReadbackImage output;
        RHIReadbackImage bloom;
    };

    struct PostRhiCapture
    {
        std::array<PostRhiFrame, kPostRhiVariantCount> frames;
        std::array<EnhancedRenderGraph::Stats, kPostRhiVariantCount> graphs{};
        uint32_t bloomMipCount{ 0 };
        bool fxaaPassThrough{ false };

        float bright{ 0.f };
        float nearGlow{ 0.f };
        float farBackground{ 0.f };
        float corner{ 0.f };
        float bloomCenter{ 0.f };
        double fxaaBefore{ 0.0 };
        double fxaaAfter{ 0.0 };
        float acesSaturation{ 0.f };
        float agxSaturation{ 0.f };
        uint32_t toneMapperDifferences{ 0 };
    };

    double PostRhiStripeDifference(const RHIReadbackImage& image,
        uint32_t slice)
    {
        double sum = 0.0;
        uint32_t count = 0;
        for (uint32_t x = 42; x < 102; ++x)
        {
            double peak = 0.0;
            for (uint32_t y = 106; y < 154; ++y)
            {
                peak = (std::max)(peak, static_cast<double>(std::fabs(
                    image.At(x, y, 0, slice) - image.At(x, y - 1, 0, slice))));
            }
            sum += peak;
            ++count;
        }
        return (0 == count) ? 0.0 : sum / count;
    }

    float PostRhiSaturation(const RHIReadbackImage& image,
        uint32_t x, uint32_t y)
    {
        const float r = image.At(x, y, 0, kPostRhiFinalSlice);
        const float g = image.At(x, y, 1, kPostRhiFinalSlice);
        const float b = image.At(x, y, 2, kPostRhiFinalSlice);
        const float hi = (std::max)({ r, g, b });
        const float lo = (std::min)({ r, g, b });
        return (hi > 1e-4f) ? ((hi - lo) / hi) : 0.f;
    }

    void AnalyzePostRhi(PostRhiCapture& capture)
    {
        const RHIReadbackImage& aces = capture.frames[PostRhiAces].output;
        const RHIReadbackImage& agx = capture.frames[PostRhiAgx].output;
        const uint32_t centerX = kPostRhiWidth / 2;
        const uint32_t centerY = kPostRhiHeight / 2;

        capture.bright = aces.At(
            centerX, centerY, 0, kPostRhiFinalSlice);
        capture.nearGlow = aces.At(
            centerX + 20, centerY, 0, kPostRhiFinalSlice);
        capture.farBackground = aces.At(
            centerX, centerY - 90, 0, kPostRhiFinalSlice);
        capture.corner = aces.At(4, 4, 0, kPostRhiFinalSlice);
        capture.bloomCenter = capture.frames[PostRhiAces].bloom.At(
            kPostRhiBloomWidth / 2, kPostRhiBloomHeight / 2, 0);
        capture.fxaaBefore = PostRhiStripeDifference(
            aces, kPostRhiPreAaSlice);
        capture.fxaaAfter = PostRhiStripeDifference(
            aces, kPostRhiFinalSlice);
        capture.acesSaturation = PostRhiSaturation(aces, 180, 60);
        capture.agxSaturation = PostRhiSaturation(agx, 180, 60);

        for (uint32_t y = 0; y < kPostRhiHeight; ++y)
        {
            for (uint32_t x = 0; x < kPostRhiWidth; ++x)
            {
                if (std::fabs(aces.At(x, y, 0, kPostRhiFinalSlice) -
                    agx.At(x, y, 0, kPostRhiFinalSlice)) > 1.5f / 255.f)
                    ++capture.toneMapperDifferences;
            }
        }
    }

    bool IsPostRhiFunctional(const PostRhiCapture& capture)
    {
        for (uint32_t variant = 0; variant < kPostRhiVariantCount; ++variant)
        {
            const EnhancedRenderGraph::Stats& stats = capture.graphs[variant];
            const uint32_t expectedPasses =
                (PostRhiSeparateAces == variant) ? 16u : 13u;
            const uint32_t expectedTransients =
                (PostRhiSeparateAces == variant) ? 11u : 8u;
            if (expectedPasses != stats.passesExecuted ||
                0 != stats.passesCulled ||
                expectedTransients != stats.transientCreated ||
                !capture.frames[variant].output.IsValid() ||
                !capture.frames[variant].bloom.IsValid())
                return false;
        }

        return 5 == capture.bloomMipCount &&
            capture.bright < 0.999f &&
            capture.nearGlow > capture.farBackground + 0.01f &&
            capture.corner < capture.farBackground - 0.01f &&
            capture.bloomCenter > 0.01f &&
            capture.fxaaBefore > 1e-9 &&
            capture.fxaaAfter < capture.fxaaBefore &&
            capture.toneMapperDifferences > 0 &&
            capture.agxSaturation < capture.acesSaturation &&
            capture.frames[PostRhiAgx].output.At(
                180, 60, 0, kPostRhiFinalSlice) < 0.999f &&
            capture.fxaaPassThrough;
    }

    template <typename TResources>
    bool CapturePostRhiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        PostRhiCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.width = kPostRhiWidth;
        context.height = kPostRhiHeight;

        EnhancedPostChainPass post;
        RHIReadback outputReadback{};
        RHIReadback bloomReadback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            post.Shutdown();
            resources.ReleaseReadback(outputReadback);
            resources.ReleaseReadback(bloomReadback);
            return false;
        };

        if (!post.Initialize(context, outError)) return fail(outError);

        const RHIPipelineLayoutParam sceneParams[] = {
            RHILayout::Cbv(0),
            RHILayout::UavTable(1, 0),
        };
        RHIPipelineLayoutDesc sceneLayoutDesc{};
        sceneLayoutDesc.params = sceneParams;
        const RHIPipelineLayoutHandle sceneLayout =
            roots.GetOrCreate(sceneLayoutDesc, outError);
        if (!sceneLayout.IsValid()) return fail(outError);

        RHIShaderBlob sceneBlob;
        if (!RHIShaderCompiler::CompileFile(
            kPostRhiSceneShader, "CSMain", "cs_5_0", sceneBlob, outError))
            return fail(outError);

        RHIComputePipelineDesc scenePipelineDesc{};
        scenePipelineDesc.csBytecode = sceneBlob.Data();
        scenePipelineDesc.csSize = sceneBlob.Size();
        scenePipelineDesc.layout = sceneLayout;
        const RHIPipelineHandle scenePipeline =
            pipelines.GetOrCreateCompute(scenePipelineDesc, outError);
        if (!scenePipeline.IsValid()) return fail(outError);

        if (!resources.CreateReadback(kPostRhiWidth, kPostRhiHeight,
                EnhancedPostChainPass::kLDRFormat, 2,
                outputReadback, outError) ||
            !resources.CreateReadback(kPostRhiBloomWidth, kPostRhiBloomHeight,
                EnhancedPostChainPass::kHDRFormat, 1,
                bloomReadback, outError))
            return fail(outError);

        const EnhancedPostChainPass::ToneMapper mappers[] = {
            EnhancedPostChainPass::ToneMapper::ACES,
            EnhancedPostChainPass::ToneMapper::AgX,
            EnhancedPostChainPass::ToneMapper::ACES,
        };
        for (uint32_t variant = 0; variant < kPostRhiVariantCount; ++variant)
        {
            if (!resources.BeginFrame(outError)) return fail(outError);
            frameOpen = true;

            EnhancedPostChainPass::Tuning tuning{};
            tuning.toneMapper = mappers[variant];
            post.SetTuning(tuning);
            post.SetUseSeparatePasses(PostRhiSeparateAces == variant);
            if (!post.PrepareFrame(context, outError)) return fail(outError);

            EnhancedRenderGraph graph(
                static_cast<IRenderDeviceServices&>(resources));
            RGTextureDesc hdrDesc{};
            hdrDesc.width = kPostRhiWidth;
            hdrDesc.height = kPostRhiHeight;
            hdrDesc.format = EnhancedPostChainPass::kHDRFormat;
            hdrDesc.allowUnorderedAccess = true;
            hdrDesc.name = "PostRHI.SceneHDR";
            const RGHandle hdr = graph.CreateTexture(hdrDesc);

            graph.AddPass("PostRHI.Scene",
                { { hdr, RHIResourceState::UnorderedAccess } },
                [&, hdr](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    PostRhiSceneParams params{};
                    params.sizeX = kPostRhiWidth;
                    params.sizeY = kPostRhiHeight;
                    const RHIBufferSlice constants =
                        resources.UploadConstants(&params, sizeof(params));
                    if (!constants.IsValid()) return;

                    const RHIBindingDesc uavs[] = {
                        RHIBindingDesc::Uav2D(
                            executeContext.ResolveHandle(hdr),
                            EnhancedPostChainPass::kHDRFormat),
                    };
                    const RHIBindingTable bindings = resources.CreateBindings(uavs);
                    if (!bindings.IsValid()) return;

                    RHIEncoder& encoder = *executeContext.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, scenePipeline);
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                    encoder.SetBindings(RHIBindPoint::Compute, 1, bindings);
                    encoder.Dispatch((kPostRhiWidth + 7) / 8,
                        (kPostRhiHeight + 7) / 8, 1);
                });

            EnhancedPostChainPass::Inputs inputs{};
            inputs.color = hdr;
            post.SetInputs(inputs);
            post.Declare(graph, context);

            const RGHandle finalOutput = post.GetOutput();
            const RGHandle preAaOutput = post.GetPreAAOutput();
            const RGHandle bloomOutput = post.GetBloomOutput();
            if (!finalOutput.IsValid() || !preAaOutput.IsValid() ||
                !bloomOutput.IsValid())
            {
                outError = "PostChain 출력이 선언되지 않았다";
                return fail(outError);
            }

            graph.AddPass("PostRHI.Readback",
                { { finalOutput, RHIResourceState::CopySource },
                  { preAaOutput, RHIResourceState::CopySource },
                  { bloomOutput, RHIResourceState::CopySource } },
                [&, finalOutput, preAaOutput, bloomOutput](
                    const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback(
                        outputReadback,
                        executeContext.ResolveHandle(finalOutput),
                        kPostRhiFinalSlice);
                    executeContext.encoder->CopyToReadback(
                        outputReadback,
                        executeContext.ResolveHandle(preAaOutput),
                        kPostRhiPreAaSlice);
                    executeContext.encoder->CopyToReadback(
                        bloomReadback,
                        executeContext.ResolveHandle(bloomOutput));
                }, true);

            if (!graph.Compile(outError) || !graph.Execute(outError))
                return fail(outError);
            outCapture.graphs[variant] = graph.GetStats();
            outCapture.bloomMipCount = post.GetBloomMipCount();

            if (!resources.EndFrame(outError)) return fail(outError);
            frameOpen = false;
            resources.WaitForGpu();

            if (!resources.MapReadback(outputReadback,
                    outCapture.frames[variant].output, outError) ||
                !resources.MapReadback(bloomReadback,
                    outCapture.frames[variant].bloom, outError))
                return fail(outError);
        }

        EnhancedPostChainPass::Tuning offTuning{};
        offTuning.fxaaEnabled = false;
        post.SetTuning(offTuning);
        EnhancedRenderGraph offGraph(
            static_cast<IRenderDeviceServices&>(resources));
        RGTextureDesc offDesc{};
        offDesc.width = kPostRhiWidth;
        offDesc.height = kPostRhiHeight;
        offDesc.format = EnhancedPostChainPass::kHDRFormat;
        offDesc.allowUnorderedAccess = true;
        offDesc.name = "PostRHI.OffHDR";
        EnhancedPostChainPass::Inputs offInputs{};
        offInputs.color = offGraph.CreateTexture(offDesc);
        post.SetInputs(offInputs);
        post.Declare(offGraph, context);
        outCapture.fxaaPassThrough = post.GetOutput().IsValid() &&
            post.GetPreAAOutput().IsValid() &&
            post.GetOutput().index == post.GetPreAAOutput().index;

        AnalyzePostRhi(outCapture);
        post.Shutdown();
        resources.ReleaseReadback(outputReadback);
        resources.ReleaseReadback(bloomReadback);
        return true;
    }

    struct PostRhiComparison
    {
        float maxDelta{ 0.f };
        double sumDelta{ 0.0 };
        uint64_t samples{ 0 };
        uint64_t overThreshold{ 0 };
        bool shapeMatches{ true };
    };

    void ComparePostRhiImage(const RHIReadbackImage& lhs,
        const RHIReadbackImage& rhs, PostRhiComparison& comparison)
    {
        if (!lhs.IsValid() || !rhs.IsValid() || lhs.width != rhs.width ||
            lhs.height != rhs.height || lhs.sliceCount != rhs.sliceCount)
        {
            comparison.shapeMatches = false;
            return;
        }

        for (uint32_t slice = 0; slice < lhs.sliceCount; ++slice)
        {
            for (uint32_t y = 0; y < lhs.height; ++y)
            {
                for (uint32_t x = 0; x < lhs.width; ++x)
                {
                    for (uint32_t channel = 0; channel < 4; ++channel)
                    {
                        const float delta = std::fabs(
                            lhs.At(x, y, channel, slice) -
                            rhs.At(x, y, channel, slice));
                        comparison.maxDelta =
                            (std::max)(comparison.maxDelta, delta);
                        comparison.sumDelta += delta;
                        ++comparison.samples;
                        if (delta > 1.5f / 255.f)
                            ++comparison.overThreshold;
                    }
                }
            }
        }
    }
}

bool RunVulkanPostChainTest(std::string& outLog)
{
    outLog += "── PostChain 공용 패스 — DX12/Vulkan bloom·tone map·FXAA 대조 ──\n";
    PostRhiCapture dx12Capture{};
    PostRhiCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!resources.Initialize(kPostRhiWidth, kPostRhiHeight, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_post.cache", error) ||
            !roots.Initialize(&resources, error))
        {
            outLog += "[1/6] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CapturePostRhiBackend(
            resources, pipelines, roots, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/6] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }

    char dx12Line[448]{};
    std::snprintf(dx12Line, sizeof(dx12Line),
        "[1/6] DX12 — 밝음/번짐/배경/구석 %.3f/%.3f/%.3f/%.3f · "
        "bloom %.3f\n      FXAA %.5f→%.5f · 채도 ACES/AgX %.3f/%.3f · "
        "톤 차이 %u\n",
        dx12Capture.bright, dx12Capture.nearGlow,
        dx12Capture.farBackground, dx12Capture.corner,
        dx12Capture.bloomCenter, dx12Capture.fxaaBefore,
        dx12Capture.fxaaAfter, dx12Capture.acesSaturation,
        dx12Capture.agxSaturation, dx12Capture.toneMapperDifferences);
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/6] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    if (!resources.Initialize(kPostRhiWidth, kPostRhiHeight, true, error))
    {
        outLog += "[2/6] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);

    bool captured = false;
    {
        PostRhiSpirvScope spirv;
        captured = CapturePostRhiBackend(
            resources, pipelines, pipelines, vkCapture, error);
    }

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[512]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/6] Vulkan — 밝음/번짐/배경/구석 %.3f/%.3f/%.3f/%.3f · "
            "bloom %.3f\n      FXAA %.5f→%.5f · 채도 ACES/AgX %.3f/%.3f · "
            "톤 차이 %u · 그래프 %u패스/%u transient\n",
            vkCapture.bright, vkCapture.nearGlow,
            vkCapture.farBackground, vkCapture.corner,
            vkCapture.bloomCenter, vkCapture.fxaaBefore,
            vkCapture.fxaaAfter, vkCapture.acesSaturation,
            vkCapture.agxSaturation, vkCapture.toneMapperDifferences,
            vkCapture.graphs[0].passesExecuted,
            vkCapture.graphs[0].transientCreated);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        IsPostRhiFunctional(dx12Capture) && IsPostRhiFunctional(vkCapture);

    PostRhiComparison comparison{};
    for (uint32_t variant = 0; variant < kPostRhiVariantCount; ++variant)
    {
        ComparePostRhiImage(dx12Capture.frames[variant].output,
            vkCapture.frames[variant].output, comparison);
        ComparePostRhiImage(dx12Capture.frames[variant].bloom,
            vkCapture.frames[variant].bloom, comparison);
    }
    const double meanDelta = (0 == comparison.samples)
        ? 0.0 : comparison.sumDelta / static_cast<double>(comparison.samples);

    char compareLine[320]{};
    std::snprintf(compareLine, sizeof(compareLine),
        "[3/6] ACES/AgX/분리형의 bloom·preAA·final RGBA — 최대 편차 %.6f · "
        "평균 %.8f · 1.5/255 초과 %llu/%llu\n",
        comparison.maxDelta, meanDelta,
        static_cast<unsigned long long>(comparison.overThreshold),
        static_cast<unsigned long long>(comparison.samples));
    outLog += compareLine;

    const float predicateDelta = (std::max)({
        std::fabs(dx12Capture.bright - vkCapture.bright),
        std::fabs(dx12Capture.nearGlow - vkCapture.nearGlow),
        std::fabs(dx12Capture.farBackground - vkCapture.farBackground),
        std::fabs(dx12Capture.corner - vkCapture.corner),
        std::fabs(dx12Capture.bloomCenter - vkCapture.bloomCenter),
        std::fabs(dx12Capture.acesSaturation - vkCapture.acesSaturation),
        std::fabs(dx12Capture.agxSaturation - vkCapture.agxSaturation),
        static_cast<float>(std::fabs(
            dx12Capture.fxaaBefore - vkCapture.fxaaBefore)),
        static_cast<float>(std::fabs(
            dx12Capture.fxaaAfter - vkCapture.fxaaAfter)),
    });
    const uint32_t toneCountDelta =
        (dx12Capture.toneMapperDifferences > vkCapture.toneMapperDifferences)
        ? dx12Capture.toneMapperDifferences - vkCapture.toneMapperDifferences
        : vkCapture.toneMapperDifferences - dx12Capture.toneMapperDifferences;
    char predicateLine[256]{};
    std::snprintf(predicateLine, sizeof(predicateLine),
        "[4/6] bloom·tone·vignette·FXAA 판정 최대 편차 %.6f · "
        "톤 차이 수 편차 %u · FXAA-off %s/%s · 분리 그래프 %u/%u\n",
        predicateDelta, toneCountDelta,
        dx12Capture.fxaaPassThrough ? "통과" : "실패",
        vkCapture.fxaaPassThrough ? "통과" : "실패",
        vkCapture.graphs[PostRhiSeparateAces].passesExecuted,
        vkCapture.graphs[PostRhiSeparateAces].transientCreated);
    outLog += predicateLine;

    if (!comparison.shapeMatches || 0 == comparison.samples ||
        comparison.maxDelta > 0.012f || meanDelta > 0.0002 ||
        comparison.overThreshold > comparison.samples / 100 ||
        predicateDelta > 0.008f || toneCountDelta > 64)
    {
        passed = false;
        outLog += "PostChain 전체 픽셀 또는 단계별 판정 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[5/6] RGBA16/RGBA8 storage image·down/up 누적·2-slice readback · "
        "미구현 " + std::to_string(stubs) + "\n";
    outLog += "[6/6] Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "PostChain 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "PostChain 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}
