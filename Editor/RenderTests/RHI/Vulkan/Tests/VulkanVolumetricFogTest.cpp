#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12Encoder.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Passes/Lighting/EnhancedVolumetricFogPass.h"
#include "Render/Scene/EnhancedSceneRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kFogRhiScreen = 256;
    constexpr uint32_t kFogRhiVolumeW = EnhancedVolumetricFogPass::kVolumeWidth;
    constexpr uint32_t kFogRhiVolumeH = EnhancedVolumetricFogPass::kVolumeHeight;
    constexpr uint32_t kFogRhiVolumeD = EnhancedVolumetricFogPass::kVolumeDepth;
    constexpr uint32_t kFogRhiProbeX = kFogRhiVolumeW / 2;
    constexpr uint32_t kFogRhiProbeY = kFogRhiVolumeH / 2;
    constexpr uint32_t kFogRhiVariantCount = 4;
    constexpr float kFogRhiSceneGray = 0.5f;
    constexpr const char* kFogRhiSceneShader = "SelfTest/FogScene.hlsl";

    enum FogRhiVariant : uint32_t
    {
        FogRhiLit = 0,
        FogRhiHistory = 1,
        FogRhiDark = 2,
        FogRhiPlain = 3,
    };

    struct FogRhiSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct FogRhiSceneParams
    {
        uint32_t screenSize[2]{};
        uint32_t noiseSize[2]{};
        uint32_t cloudSize[2]{};
        uint32_t padding[2]{};
    };

    struct FogRhiCapture
    {
        float litScatter{ 0.f };
        float historyScatter{ 0.f };
        float darkScatter{ 0.f };
        float litPixel{ 0.f };
        float plainPixel{ 0.f };
        std::array<float, 5> transmittance{};
        bool passThrough{ false };
        std::array<EnhancedRenderGraph::Stats, kFogRhiVariantCount> graphs{};
        RHIReadbackImage litVoxel;
        RHIReadbackImage historyVoxel;
        std::array<RHIReadbackImage, kFogRhiVariantCount> screens;
    };

    bool IsFogRhiFunctional(const FogRhiCapture& capture)
    {
        for (const EnhancedRenderGraph::Stats& stats : capture.graphs)
        {
            if (6 != stats.passesExecuted || 0 != stats.passesCulled ||
                6 != stats.transientCreated)
                return false;
        }

        if (capture.litScatter < 0.001f ||
            capture.historyScatter < capture.litScatter * 0.5f ||
            capture.darkScatter > 0.0005f ||
            std::fabs(capture.litPixel - kFogRhiSceneGray) < 0.001f ||
            std::fabs(capture.plainPixel - kFogRhiSceneGray) > 0.002f ||
            capture.transmittance[0] < 0.99f || !capture.passThrough)
            return false;

        for (size_t i = 1; i < capture.transmittance.size(); ++i)
        {
            if (capture.transmittance[i] >= capture.transmittance[i - 1])
                return false;
        }
        return true;
    }

    template <typename TResources>
    bool CaptureFogRhiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        FogRhiCapture& outCapture, std::string& outError)
    {
        FrameCameraSnapshot camera{};
        camera.view = math::matrix4x4::identity();
        camera.projection = math::perspective_fov_lh(
            DirectX::XM_PIDIV4, 1.f, 0.5f, 1000.f);
        camera.inverseView = math::inverse(camera.view);
        camera.inverseProjection = math::inverse(camera.projection);
        camera.eyePosition = math::vector3{0.f, 0.f, 0.f};
        camera.nearPlane = 0.5f;
        camera.farPlane = 1000.f;
        camera.fov = 45.f;

        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.width = kFogRhiScreen;
        context.height = kFogRhiScreen;
        context.camera = &camera;

        EnhancedVolumetricFogPass fog;
        RHIReadback voxelReadback{};
        RHIReadback screenReadback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            fog.Shutdown();
            resources.ReleaseReadback(voxelReadback);
            resources.ReleaseReadback(screenReadback);
            return false;
        };

        if (!fog.Initialize(context, outError)) return fail(outError);
        fog.SetShadowMatrix(math::orthographic_lh(
            2000.f, 2000.f, 0.f, 2000.f));
        fog.SetKeepAlive(true);

        const RHIPipelineLayoutParam sceneParams[] = {
            RHILayout::Cbv(0),
            RHILayout::UavTable(4, 0),
        };
        RHIPipelineLayoutDesc sceneLayoutDesc{};
        sceneLayoutDesc.params = sceneParams;
        const RHIPipelineLayoutHandle sceneLayout =
            roots.GetOrCreate(sceneLayoutDesc, outError);
        if (!sceneLayout.IsValid()) return fail(outError);

        RHIShaderBlob sceneBlob;
        if (!RHIShaderCompiler::CompileFile(
            kFogRhiSceneShader, "CSMain", "cs_5_0", sceneBlob, outError))
            return fail(outError);

        RHIComputePipelineDesc scenePipelineDesc{};
        scenePipelineDesc.csBytecode = sceneBlob.Data();
        scenePipelineDesc.csSize = sceneBlob.Size();
        scenePipelineDesc.layout = sceneLayout;
        const RHIPipelineHandle scenePipeline =
            pipelines.GetOrCreateCompute(scenePipelineDesc, outError);
        if (!scenePipeline.IsValid()) return fail(outError);

        if (!resources.CreateReadback(kFogRhiVolumeW, kFogRhiVolumeH,
                EnhancedVolumetricFogPass::kVoxelFormat, kFogRhiVolumeD,
                voxelReadback, outError) ||
            !resources.CreateReadback(kFogRhiScreen, kFogRhiScreen,
                EnhancedVolumetricFogPass::kOutputFormat, 1,
                screenReadback, outError))
            return fail(outError);

        const auto renderVariant = [&](uint32_t variant,
            const EnhancedVolumetricFogPass::Tuning& tuning,
            uint32_t lightCount) -> bool
        {
            std::vector<EnhancedLight> lights;
            for (uint32_t i = 0; i < lightCount; ++i)
            {
                EnhancedLight light{};
                light.position = { 0.f, 0.f, 0.f, 0.f };
                light.direction = { 0.f, -1.f, 0.f, 0.f };
                light.color = { 1.f, 1.f, 1.f, 1.f };
                light.attenuation = { 1.f, 0.f, 0.f, 100.f };
                lights.push_back(light);
            }
            context.lights = &lights;

            if (!resources.BeginFrame(outError)) return false;
            frameOpen = true;

            fog.SetTuning(tuning);
            fog.SetEnabled(true);
            fog.SetFrameIndex(0);

            EnhancedVolumetricFogPass::CloudShadow cloud{};
            cloud.viewProjection = math::matrix4x4::identity();
            cloud.alpha = 1.f;
            cloud.size[0] = cloud.size[1] = 1.f;
            cloud.cloudMapSize[0] = cloud.cloudMapSize[1] = 4.f;
            fog.SetCloudShadow(cloud);
            if (!fog.PrepareFrame(context, outError)) return false;

            EnhancedRenderGraph graph(
                static_cast<IRenderDeviceServices&>(resources));

            RGTextureDesc colorDesc{};
            colorDesc.width = kFogRhiScreen;
            colorDesc.height = kFogRhiScreen;
            colorDesc.format = RHIFormat::RGBA16Float;
            colorDesc.allowUnorderedAccess = true;
            colorDesc.name = "FogRHI.SceneColor";
            const RGHandle color = graph.CreateTexture(colorDesc);

            RGTextureDesc depthDesc = colorDesc;
            depthDesc.format = RHIFormat::R32Float;
            depthDesc.name = "FogRHI.SceneDepth";
            const RGHandle depth = graph.CreateTexture(depthDesc);

            RGTextureDesc cloudDesc = colorDesc;
            cloudDesc.width = 4;
            cloudDesc.height = 4;
            cloudDesc.name = "FogRHI.Cloud";
            const RGHandle cloudMap = graph.CreateTexture(cloudDesc);

            RGTextureDesc noiseDesc = colorDesc;
            noiseDesc.width = 64;
            noiseDesc.height = 64;
            noiseDesc.name = "FogRHI.BlueNoise";
            const RGHandle blueNoise = graph.CreateTexture(noiseDesc);

            RGTextureDesc shadowDesc{};
            shadowDesc.width = 64;
            shadowDesc.height = 64;
            shadowDesc.arraySize = kShadowCascadeCount;
            shadowDesc.format = RHIFormat::D32Float;
            shadowDesc.allowDepthStencil = true;
            shadowDesc.name = "FogRHI.Shadow";
            const RGHandle shadowMap = graph.CreateTexture(shadowDesc);

            graph.AddPass("FogRHI.Scene",
                { { color, RHIResourceState::UnorderedAccess },
                  { depth, RHIResourceState::UnorderedAccess },
                  { cloudMap, RHIResourceState::UnorderedAccess },
                  { blueNoise, RHIResourceState::UnorderedAccess } },
                [&, color, depth, cloudMap, blueNoise](
                    const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    FogRhiSceneParams params{};
                    params.screenSize[0] = params.screenSize[1] = kFogRhiScreen;
                    params.noiseSize[0] = params.noiseSize[1] = 64;
                    params.cloudSize[0] = params.cloudSize[1] = 4;
                    const RHIBufferSlice constants =
                        resources.UploadConstants(&params, sizeof(params));
                    if (!constants.IsValid()) return;

                    const RHIBindingDesc uavs[] = {
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(color),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(depth),
                            RHIFormat::R32Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(cloudMap),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(blueNoise),
                            RHIFormat::RGBA16Float),
                    };
                    const RHIBindingTable bindings = resources.CreateBindings(uavs);
                    if (!bindings.IsValid()) return;

                    RHIEncoder& encoder = *executeContext.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, scenePipeline);
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                    encoder.SetBindings(RHIBindPoint::Compute, 1, bindings);
                    encoder.Dispatch((kFogRhiScreen + 7) / 8,
                        (kFogRhiScreen + 7) / 8, 1);
                });

            graph.AddPass("FogRHI.ShadowClear",
                { { shadowMap, RHIResourceState::DepthWrite } },
                [&, shadowMap](
                    const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    RHIDepthTargetDesc shadowTarget{};
                    shadowTarget.resource = executeContext.ResolveHandle(shadowMap);
                    shadowTarget.format = RHIFormat::D32Float;
                    shadowTarget.firstSlice = 0;
                    shadowTarget.sliceCount = kShadowCascadeCount;
                    const RHIRenderTargetBinding targets =
                        resources.CreateRenderTargets(
                            std::span<const RHITextureHandle>{}, &shadowTarget);
                    if (!targets.IsValid()) return;

                    RHIEncoder& encoder = *executeContext.encoder;
                    encoder.SetViewportAndScissor(64, 64);
                    encoder.BindRenderTargets(targets);
                    encoder.ClearDepthTarget(targets, 1.f);
                });

            EnhancedVolumetricFogPass::Inputs inputs{};
            inputs.color = color;
            inputs.depth = depth;
            inputs.shadowMap = shadowMap;
            inputs.cloudShadow = cloudMap;
            inputs.blueNoise = blueNoise;
            fog.SetInputs(inputs);
            fog.Declare(graph, context);

            const RGHandle output = fog.GetOutput();
            const RGHandle voxelGrid = fog.GetVoxelGrid();
            if (!output.IsValid() || !voxelGrid.IsValid())
            {
                outError = "VolumetricFog 출력이 선언되지 않았다";
                return false;
            }

            graph.AddPass("FogRHI.Readback",
                { { output, RHIResourceState::CopySource },
                  { voxelGrid, RHIResourceState::CopySource } },
                [&, output, voxelGrid](
                    const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyVolumeToReadback(
                        voxelReadback, executeContext.ResolveHandle(voxelGrid));
                    executeContext.encoder->CopyToReadback(
                        screenReadback, executeContext.ResolveHandle(output));
                }, true);

            if (!graph.Compile(outError) || !graph.Execute(outError)) return false;
            outCapture.graphs[variant] = graph.GetStats();

            if (!resources.EndFrame(outError)) return false;
            frameOpen = false;
            resources.WaitForGpu();

            RHIReadbackImage voxelImage{};
            if (!resources.MapReadback(voxelReadback, voxelImage, outError) ||
                !resources.MapReadback(
                    screenReadback, outCapture.screens[variant], outError))
                return false;

            const float scatter = voxelImage.At(
                kFogRhiProbeX, kFogRhiProbeY, 1, kFogRhiVolumeD - 1);
            if (FogRhiLit == variant)
            {
                outCapture.litScatter = scatter;
                const uint32_t slices[5] = { 0, 32, 64, 96, kFogRhiVolumeD - 1 };
                for (size_t i = 0; i < outCapture.transmittance.size(); ++i)
                {
                    outCapture.transmittance[i] = voxelImage.At(
                        kFogRhiProbeX, kFogRhiProbeY, 3, slices[i]);
                }
                outCapture.litVoxel = std::move(voxelImage);
            }
            else if (FogRhiHistory == variant)
            {
                outCapture.historyScatter = scatter;
                outCapture.historyVoxel = std::move(voxelImage);
            }
            else if (FogRhiDark == variant)
            {
                outCapture.darkScatter = scatter;
            }

            return true;
        };

        EnhancedVolumetricFogPass::Tuning fresh{};
        fresh.previousFrameBlendFactor = 0.f;
        EnhancedVolumetricFogPass::Tuning history{};
        history.previousFrameBlendFactor = 1.f;
        EnhancedVolumetricFogPass::Tuning plain{};
        plain.previousFrameBlendFactor = 0.f;
        plain.blendingWithSceneColorFactor = 0.f;

        if (!renderVariant(FogRhiLit, fresh, 1) ||
            !renderVariant(FogRhiHistory, history, 0) ||
            !renderVariant(FogRhiDark, fresh, 0) ||
            !renderVariant(FogRhiPlain, plain, 1))
            return fail(outError);

        outCapture.litPixel = outCapture.screens[FogRhiLit].At(
            kFogRhiScreen / 2, kFogRhiScreen / 2, 1);
        outCapture.plainPixel = outCapture.screens[FogRhiPlain].At(
            kFogRhiScreen / 2, kFogRhiScreen / 2, 1);

        fog.SetEnabled(false);
        EnhancedRenderGraph offGraph(
            static_cast<IRenderDeviceServices&>(resources));
        RGTextureDesc offDesc{};
        offDesc.width = kFogRhiScreen;
        offDesc.height = kFogRhiScreen;
        offDesc.format = RHIFormat::RGBA16Float;
        offDesc.name = "FogRHI.OffColor";
        EnhancedVolumetricFogPass::Inputs offInputs{};
        offInputs.color = offGraph.CreateTexture(offDesc);
        fog.SetInputs(offInputs);
        fog.Declare(offGraph, context);
        outCapture.passThrough =
            fog.GetOutput().index == offInputs.color.index;

        fog.Shutdown();
        resources.ReleaseReadback(voxelReadback);
        resources.ReleaseReadback(screenReadback);
        return true;
    }

    struct FogRhiComparison
    {
        float maxDelta{ 0.f };
        double sumDelta{ 0.0 };
        uint64_t samples{ 0 };
        uint64_t overThreshold{ 0 };
        bool shapeMatches{ true };
    };

    void CompareFogRhiImage(const RHIReadbackImage& lhs,
        const RHIReadbackImage& rhs, FogRhiComparison& comparison)
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
                        if (delta > 0.005f) ++comparison.overThreshold;
                    }
                }
            }
        }
    }
}

bool RunVulkanVolumetricFogTest(std::string& outLog)
{
    outLog += "── VolumetricFog 공용 패스 — DX12/Vulkan 3D 산란·히스토리·합성 대조 ──\n";
    FogRhiCapture dx12Capture{};
    FogRhiCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!resources.Initialize(kFogRhiScreen, kFogRhiScreen, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_fog.cache", error) ||
            !roots.Initialize(&resources, error))
        {
            outLog += "[1/6] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureFogRhiBackend(
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

    char dx12Line[384]{};
    std::snprintf(dx12Line, sizeof(dx12Line),
        "[1/6] DX12 — 산란/히스토리/암부 %.5f/%.5f/%.5f · 합성/원본 %.4f/%.4f\n"
        "      투과율 %.4f/%.4f/%.4f/%.4f/%.4f\n",
        dx12Capture.litScatter, dx12Capture.historyScatter,
        dx12Capture.darkScatter, dx12Capture.litPixel,
        dx12Capture.plainPixel, dx12Capture.transmittance[0],
        dx12Capture.transmittance[1], dx12Capture.transmittance[2],
        dx12Capture.transmittance[3], dx12Capture.transmittance[4]);
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/6] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    if (!resources.Initialize(kFogRhiScreen, kFogRhiScreen, true, error))
    {
        outLog += "[2/6] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);

    bool captured = false;
    {
        FogRhiSpirvScope spirv;
        captured = CaptureFogRhiBackend(
            resources, pipelines, pipelines, vkCapture, error);
    }

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[448]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/6] Vulkan — 산란/히스토리/암부 %.5f/%.5f/%.5f · "
            "합성/원본 %.4f/%.4f · 그래프 %u패스/%u transient\n"
            "      투과율 %.4f/%.4f/%.4f/%.4f/%.4f\n",
            vkCapture.litScatter, vkCapture.historyScatter,
            vkCapture.darkScatter, vkCapture.litPixel, vkCapture.plainPixel,
            vkCapture.graphs[0].passesExecuted,
            vkCapture.graphs[0].transientCreated,
            vkCapture.transmittance[0], vkCapture.transmittance[1],
            vkCapture.transmittance[2], vkCapture.transmittance[3],
            vkCapture.transmittance[4]);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        IsFogRhiFunctional(dx12Capture) && IsFogRhiFunctional(vkCapture);

    FogRhiComparison comparison{};
    CompareFogRhiImage(dx12Capture.litVoxel, vkCapture.litVoxel, comparison);
    CompareFogRhiImage(
        dx12Capture.historyVoxel, vkCapture.historyVoxel, comparison);
    for (uint32_t i = 0; i < kFogRhiVariantCount; ++i)
        CompareFogRhiImage(dx12Capture.screens[i], vkCapture.screens[i], comparison);

    const double meanDelta = (0 == comparison.samples)
        ? 0.0 : comparison.sumDelta / static_cast<double>(comparison.samples);
    char compareLine[320]{};
    std::snprintf(compareLine, sizeof(compareLine),
        "[3/6] 2개 3D 격자 + 4개 화면 RGBA — 최대 편차 %.6f · "
        "평균 %.8f · 0.005 초과 %llu/%llu\n",
        comparison.maxDelta, meanDelta,
        static_cast<unsigned long long>(comparison.overThreshold),
        static_cast<unsigned long long>(comparison.samples));
    outLog += compareLine;

    const float predicateDelta = (std::max)({
        std::fabs(dx12Capture.litScatter - vkCapture.litScatter),
        std::fabs(dx12Capture.historyScatter - vkCapture.historyScatter),
        std::fabs(dx12Capture.darkScatter - vkCapture.darkScatter),
        std::fabs(dx12Capture.litPixel - vkCapture.litPixel),
        std::fabs(dx12Capture.plainPixel - vkCapture.plainPixel),
    });
    float transmittanceDelta = 0.f;
    for (size_t i = 0; i < dx12Capture.transmittance.size(); ++i)
    {
        transmittanceDelta = (std::max)(transmittanceDelta,
            std::fabs(dx12Capture.transmittance[i] -
                vkCapture.transmittance[i]));
    }
    char predicateLine[256]{};
    std::snprintf(predicateLine, sizeof(predicateLine),
        "[4/6] 산란·히스토리·합성 판정 편차 %.6f · 투과율 편차 %.6f · 꺼짐 %s/%s\n",
        predicateDelta, transmittanceDelta,
        dx12Capture.passThrough ? "통과" : "실패",
        vkCapture.passThrough ? "통과" : "실패");
    outLog += predicateLine;

    // exp/pow와 3D 선형 보간은 backend별 근사가 다를 수 있다. 핵심 표본과
    // 평균은 엄격히 묶고, 극소수 경계 표본만 넓은 최대 편차를 허용한다.
    if (!comparison.shapeMatches || 0 == comparison.samples ||
        comparison.maxDelta > 0.02f || meanDelta > 0.0002 ||
        comparison.overThreshold > comparison.samples / 100 ||
        predicateDelta > 0.005f || transmittanceDelta > 0.005f)
    {
        passed = false;
        outLog += "VolumetricFog 전체 픽셀 또는 핵심 판정 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[5/6] 3D clear·UAV→SRV·Texture2DArray·3D/2D readback · 미구현 " +
        std::to_string(stubs) + "\n";
    outLog += "[6/6] Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "VolumetricFog 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "VolumetricFog 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}
