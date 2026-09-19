#include "Render/Passes/Lighting/EnhancedSSAOPass.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "RHI/RHIEncoder.h"
#include "RHI/RHIShaderCompiler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>

namespace
{
    // Odd, non-square dimensions exercise depth texel centers and dispatch bounds.
    constexpr uint32_t kTestWidth = 257;
    constexpr uint32_t kTestHeight = 193;
    struct SceneParams
    {
        uint32_t sizeX{ kTestWidth }, sizeY{ kTestHeight };
        float nearZ{ 0.1f }, farZ{ 100.f };
        float leftViewZ{ 1.f }, rightViewZ{ 0.6f };
        uint32_t mode{}, pad{};
        float slopeX{}, slopeY{}, pad2[2]{};
        math::matrix4x4 inverseProjection{};
        math::matrix4x4 inverseView{};
    };
    static_assert(offsetof(SceneParams, inverseProjection) == 48);
    static_assert(sizeof(SceneParams) == 176);
}

bool DX12Test::RunSSAOTest(std::string& outLog)
{
    outLog += "SSAO: world normals, signed hemispheres and texel-center regression\n";
    std::string error;
    DX12DeviceResources resources;
    if (!resources.Initialize(kTestWidth, kTestHeight, error))
    {
        outLog += "DX12 initialization failed: " + error + "\n";
        return false;
    }
    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_ssao.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "Cache initialization failed: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    FrameCameraSnapshot camera{};
    camera.projection = math::perspective_fov_lh(math::half_pi,
        float(kTestWidth) / float(kTestHeight), 0.1f, 100.f);
    camera.inverseProjection = math::inverse(camera.projection);
    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kTestWidth;
    frameContext.height = kTestHeight;
    frameContext.camera = &camera;
    EnhancedSSAOPass ssao;
    if (!ssao.Initialize(frameContext, error))
    {
        outLog += "SSAO initialization failed: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    const RHIPipelineLayoutParam layoutParams[] = { RHILayout::Cbv(0), RHILayout::UavTable(2, 0) };
    RHIPipelineLayoutDesc layoutDesc{};
    layoutDesc.params = layoutParams;
    const auto layout = rootSignatures.GetOrCreate(layoutDesc, error);
    RHIShaderBlob sceneBlob;
    if (!layout.IsValid() || !RHIShaderCompiler::CompileFile(
        "SelfTest/SsaoScene.slang", "CSMain", "cs_5_0", sceneBlob, error))
    {
        outLog += "Fixture compilation failed: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    RHIComputePipelineDesc sceneDesc{};
    sceneDesc.csBytecode = sceneBlob.Data();
    sceneDesc.csSize = sceneBlob.Size();
    sceneDesc.layout = layout;
    const auto scenePSO = psoManager.GetOrCreateCompute(sceneDesc, error);
    constexpr uint32_t halfWidth = (kTestWidth + 1) / 2;
    constexpr uint32_t halfHeight = (kTestHeight + 1) / 2;
    RHIReadback readback{};
    if (!scenePSO.IsValid() || !resources.CreateReadback(halfWidth, halfHeight,
        EnhancedSSAOPass::kAOFormat, 2, readback, error))
    {
        outLog += "Fixture/readback initialization failed: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    const auto renderCase = [&](const char* name, uint32_t mode, bool rotated,
        const EnhancedSSAOPass::Tuning& tuning, RHIReadbackImage& captured) -> bool
    {
        // Keep the visible geometry fixed, but represent normals in the rotated
        // world frame. AO must be invariant under this rigid camera/scene rotation.
        camera.view = rotated ? math::look_at_lh(math::vector3{ 0.f, 0.f, 0.f },
            math::vector3{ 1.f, 0.f, 0.f }, math::vector3::unit_y()) : math::matrix4x4::identity();
        camera.inverseView = math::inverse(camera.view);
        ssao.SetTuning(tuning);
        ssao.SetFrameIndex(0);
        if (!resources.BeginFrame(error)) return false;
        bool ok = ssao.PrepareFrame(frameContext, error);
        // Keep transient resources alive until submission and GPU completion.
        EnhancedRenderGraph graph(resources);
        RGTextureDesc inputDesc{};
        inputDesc.width = kTestWidth;
        inputDesc.height = kTestHeight;
        inputDesc.allowUnorderedAccess = true;
        inputDesc.format = RHIFormat::R32Float;
        inputDesc.name = "SSAO.TestDepth";
        EnhancedSSAOPass::Inputs inputs{};
        inputs.depth = graph.CreateTexture(inputDesc);
        inputDesc.format = RHIFormat::RGBA16Float;
        inputDesc.name = "SSAO.TestNormal";
        inputs.normal = graph.CreateTexture(inputDesc);
        graph.AddPass("SSAO.TestScene",
            { { inputs.depth, RHIResourceState::UnorderedAccess },
              { inputs.normal, RHIResourceState::UnorderedAccess } },
            [&](const EnhancedRenderGraph::ExecuteContext& context)
            {
                SceneParams params{};
                params.mode = mode;
                params.slopeX = 0.4f;
                params.slopeY = -0.2f;
                params.inverseProjection = math::transpose(camera.inverseProjection);
                params.inverseView = math::transpose(camera.inverseView);
                const auto cb = resources.UploadConstants(&params, sizeof(params));
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(context.ResolveHandle(inputs.depth), RHIFormat::R32Float),
                    RHIBindingDesc::Uav2D(context.ResolveHandle(inputs.normal), RHIFormat::RGBA16Float),
                };
                const auto table = resources.CreateBindings(uavs);
                if (!cb.IsValid() || !table.IsValid()) { ok = false; return; }
                context.encoder->SetPipeline(RHIBindPoint::Compute, scenePSO);
                context.encoder->SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                context.encoder->SetBindings(RHIBindPoint::Compute, 1, table);
                context.encoder->Dispatch((kTestWidth + 7) / 8, (kTestHeight + 7) / 8, 1);
            });
        ssao.SetInputs(inputs);
        ssao.Declare(graph, frameContext);
        const auto raw = ssao.GetRawOutput();
        const auto filtered = ssao.GetOutput();
        if (!raw.IsValid() || !filtered.IsValid()) ok = false;
        graph.AddPass("SSAO.Readback",
            { { raw, RHIResourceState::CopySource }, { filtered, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& context)
            {
                context.encoder->CopyToReadback(readback, context.ResolveHandle(raw), 0);
                context.encoder->CopyToReadback(readback, context.ResolveHandle(filtered), 1);
            }, true);
        if (ok) ok = graph.Compile(error);
        if (ok) ok = graph.Execute(error) && ok;
        if (graph.GetStats().passesExecuted != 4) ok = false;
        if (!resources.EndFrame(error)) ok = false;
        resources.WaitForGpu();
        if (ok) ok = resources.MapReadback(readback, captured, error);
        if (ok)
        {
            for (uint32_t slice = 0; slice < 2; ++slice)
                for (uint32_t y = 0; y < halfHeight; ++y)
                    for (uint32_t x = 0; x < halfWidth; ++x)
                    {
                        const float ao = captured.At(x, y, 0, slice);
                        const float z = captured.At(x, y, 1, slice);
                        if (!std::isfinite(ao) || ao < 0.f || ao > 1.f || !std::isfinite(z) || z <= 0.f)
                            ok = false;
                    }
        }
        outLog += std::string(name) + (ok ? ": GPU execution/readback finite PASS\n" : ": FAILED " + error + "\n");
        return ok;
    };
    const auto minimumAO = [&](const RHIReadbackImage& image)
    {
        float result = 1.f;
        for (uint32_t slice = 0; slice < 2; ++slice)
            for (uint32_t y = 0; y < halfHeight; ++y)
                for (uint32_t x = 0; x < halfWidth; ++x)
                    result = std::min(result, image.At(x, y, 0, slice));
        return result;
    };
    bool passed = true;
    const auto check = [&](bool condition, const char* name)
    {
        outLog += std::string(name) + (condition ? ": PASS\n" : ": FAIL\n");
        passed &= condition;
    };
    EnhancedSSAOPass::Tuning tuning{};
    RHIReadbackImage baseline, rotated, plane, oneSide, bothSides, disabled, sky;
    if (renderCase("step", 0, false, tuning, baseline) &&
        renderCase("rotated step", 0, true, tuning, rotated))
    {
        float maxDelta = 0.f;
        for (uint32_t slice = 0; slice < 2; ++slice)
            for (uint32_t y = 0; y < halfHeight; ++y)
                for (uint32_t x = 0; x < halfWidth; ++x)
                    maxDelta = std::max(maxDelta, std::fabs(baseline.At(x, y, 0, slice) - rotated.At(x, y, 0, slice)));
        const float flat = baseline.At(halfWidth / 8, halfHeight / 2, 0, 1);
        const float edge = baseline.At(halfWidth / 2 - 2, halfHeight / 2, 0, 1);
        double rawDiff = 0., filteredDiff = 0.;
        for (uint32_t y = 1; y + 1 < halfHeight; ++y)
            for (uint32_t x = 1; x < halfWidth; ++x)
            {
                rawDiff += std::fabs(baseline.At(x, y, 0, 0) - baseline.At(x - 1, y, 0, 0));
                filteredDiff += std::fabs(baseline.At(x, y, 0, 1) - baseline.At(x - 1, y, 0, 1));
            }
        char line[256]{};
        std::snprintf(line, sizeof(line), "flat=%.5f edge=%.5f rotation max=%.8f filter variation ratio=%.5f\n",
            flat, edge, maxDelta, filteredDiff / std::max(rawDiff, 1e-9));
        outLog += line;
        check(maxDelta < 0.002f, "90-degree camera/world rotation invariance");
        check(flat >= 0.99f && edge < flat - 0.05f, "Unoccluded plane and contact visibility");
        check(rawDiff > 0. && filteredDiff < rawDiff, "Spatial filter reduces variation");
    }
    else passed = false;
    if (renderCase("tilted plane", 1, true, tuning, plane))
    {
        outLog += "tilted plane minimum=" + std::to_string(minimumAO(plane)) + "\n";
        check(minimumAO(plane) >= 0.984f, "Odd-size tilted plane does not self-occlude");
    }
    else passed = false;

    tuning.radius = 0.8f;
    tuning.thickness = 1.f;
    if (renderCase("one-sided occluder", 0, false, tuning, oneSide) &&
        renderCase("two-sided recess", 2, false, tuning, bothSides))
    {
        // Only the optical center has the symmetric slice geometry needed for
        // the analytic 50% bound; off-axis perspective slices need not have it.
        const float oneCenter = oneSide.At(halfWidth / 2, halfHeight / 2, 0, 0);
        double recess = 0.;
        for (uint32_t y = halfHeight / 4; y < halfHeight * 3 / 4; ++y)
            recess += bothSides.At(halfWidth / 2, y, 0, 0);
        recess /= double(halfHeight * 3 / 4 - halfHeight / 4);
        outLog += "one-sided center=" + std::to_string(oneCenter) + " two-sided center=" + std::to_string(recess) + "\n";
        check(oneCenter >= 0.499f && oneCenter < 0.65f, "One-sided wall covers at most half at optical center");
        check(recess < 0.4, "Opposite occluders cover distinct sectors");
    }
    else passed = false;
    tuning.intensity = 0.f;
    if (renderCase("intensity zero", 0, true, tuning, disabled))
        check(minimumAO(disabled) == 1.f, "Intensity zero produces visibility one");
    else passed = false;
    tuning.intensity = 1.f;
    if (renderCase("sky", 3, false, tuning, sky))
        check(minimumAO(sky) == 1.f, "Sky remains unoccluded");
    else passed = false;

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    outLog += "DX12 validation problems=" + std::to_string(problems) + "\n" + validation;
    passed &= (problems == 0);
    ssao.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();
    outLog += passed ? "SSAO regression PASS\n" : "SSAO regression FAIL\n";
    return passed;
}
