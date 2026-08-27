#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12Encoder.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Passes/Editor/EnhancedWireFramePass.h"
#include "FrameCameraSnapshot.h"
#include "Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <mathematics/transform.hpp>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kWireRhiSize = 256;

    struct WireRhiSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct WireRhiFrame
    {
        RHIReadbackImage image;
        EnhancedRenderGraph::Stats graph{};
        uint32_t drawItems{ 0 };
        uint32_t batches{ 0 };
        uint32_t skinned{ 0 };
        uint32_t palettes{ 0 };
        uint32_t lit{ 0 };
    };

    struct WireRhiCapture
    {
        WireRhiFrame visible;
        WireRhiFrame away;
        WireRhiFrame skinned;
        WireRhiFrame bindPose;

        float edge{ 0.f };
        float secondEdge{ 0.f };
        float inside{ 0.f };
        float skinnedMoved{ 0.f };
        float skinnedBind{ 0.f };
        float plainMoved{ 0.f };
        float plainBind{ 0.f };

        uint32_t meshUploads{ 0 };
        uint32_t meshHits{ 0 };
        uint32_t meshFailures{ 0 };
    };

    bool WireRhiProjectToPixel(const FrameCameraSnapshot& camera,
        float worldX, float worldY, float worldZ,
        uint32_t& outX, uint32_t& outY)
    {
        const math::vector4 clip =
            math::vector4{worldX, worldY, worldZ, 1.f} *
            (camera.view * camera.projection);
        const float w = clip.w;
        if (w <= 1e-6f) return false;

        const float ndcX = clip.x / w;
        const float ndcY = clip.y / w;
        if (ndcX < -1.f || ndcX > 1.f ||
            ndcY < -1.f || ndcY > 1.f)
            return false;

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) *
            static_cast<float>(kWireRhiSize));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) *
            static_cast<float>(kWireRhiSize));
        outX = (std::min)(outX, kWireRhiSize - 1);
        outY = (std::min)(outY, kWireRhiSize - 1);
        return true;
    }

    float WireRhiMaxInWindow(const RHIReadbackImage& image,
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

    uint32_t WireRhiCountLit(const RHIReadbackImage& image)
    {
        uint32_t result = 0;
        for (uint32_t y = 0; y < image.height; ++y)
            for (uint32_t x = 0; x < image.width; ++x)
                if (image.At(x, y, 1) > 0.5f) ++result;
        return result;
    }

    template <typename TResources, typename TMeshCache>
    bool CaptureWireRhiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        TMeshCache& meshes, WireRhiCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshes;
        context.width = kWireRhiSize;
        context.height = kWireRhiSize;

        EnhancedWireFramePass wireframe;
        RHIReadback readback{};
        bool frameOpen = false;
        uint64_t frameIndex = 0;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            wireframe.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!wireframe.Initialize(context, outError)) return fail(outError);
        wireframe.SetKeepAlive(true);
        if (!resources.CreateReadback(kWireRhiSize, kWireRhiSize,
                EnhancedWireFramePass::kOutputFormat, 1, readback, outError))
            return fail(outError);

        const FrameCameraSnapshot front = []
        {
            FrameCameraSnapshot camera{};
            camera.view = math::look_at_lh(
                math::vector3{0.f, 0.f, -8.f},
                math::vector3{0.f, 0.f, 0.f},
                math::vector3{0.f, 1.f, 0.f});
            camera.projection = math::perspective_fov_lh(
                math::quarter_pi, 1.f, 0.1f, 100.f);
            camera.eyePosition = math::vector3{0.f, 0.f, -8.f};
            return camera;
        }();
        FrameCameraSnapshot away = front;
        away.view = math::look_at_lh(
            math::vector3{0.f, 0.f, -8.f},
            math::vector3{0.f, 0.f, -20.f},
            math::vector3{0.f, 1.f, 0.f});

        std::vector<Vertex> vertices(4);
        vertices[0].position = { -1.f, -1.f, 0.f };
        vertices[1].position = { 1.f, -1.f, 0.f };
        vertices[2].position = { 1.f, 1.f, 0.f };
        vertices[3].position = { -1.f, 1.f, 0.f };
        const std::vector<uint32_t> indices{ 0, 1, 2, 0, 2, 3 };
        Mesh quadMesh("rhi_wireframe_quad", vertices, indices);

        std::vector<EnhancedDrawItem> draws(2);
        draws[0].mesh = &quadMesh;
        draws[0].worldMatrix = math::matrix4x4::identity();
        draws[1].mesh = &quadMesh;
        draws[1].worldMatrix = math::translation_matrix(math::vector3{ 3.f, 0.f, 0.f });
        context.draws = &draws;

        const auto render = [&](const FrameCameraSnapshot& camera,
            WireRhiFrame& frame) -> bool
        {
            context.camera = &camera;
            if (!resources.BeginFrame(outError)) return false;
            frameOpen = true;
            meshes.BeginFrame(frameIndex++);
            if (!wireframe.PrepareFrame(context, outError)) return false;

            EnhancedRenderGraph graph(
                static_cast<IRenderDeviceServices&>(resources));
            wireframe.Declare(graph, context);
            const RGHandle output = wireframe.GetOutput();
            if (!output.IsValid())
            {
                outError = "WireFrame 출력이 선언되지 않았다";
                return false;
            }
            graph.AddPass("WireFrameRHI.Readback",
                { { output, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback(
                        readback, executeContext.ResolveHandle(output));
                }, true);
            if (!graph.Compile(outError) || !graph.Execute(outError)) return false;

            frame.graph = graph.GetStats();
            frame.drawItems = wireframe.GetLastDrawItemCount();
            frame.batches = wireframe.GetLastBatchCount();
            frame.skinned = wireframe.GetLastSkinnedCount();
            frame.palettes = wireframe.GetLastBonePaletteCount();

            if (!resources.EndFrame(outError)) return false;
            frameOpen = false;
            resources.WaitForGpu();
            if (!resources.MapReadback(readback, frame.image, outError)) return false;
            frame.lit = WireRhiCountLit(frame.image);
            return true;
        };

        if (!render(front, outCapture.visible)) return fail(outError);
        if (!render(away, outCapture.away)) return fail(outError);

        uint32_t edgeX = 0, edgeY = 0;
        uint32_t secondX = 0, secondY = 0;
        uint32_t insideX = 0, insideY = 0;
        if (!WireRhiProjectToPixel(front, 0.f, 1.f, 0.f, edgeX, edgeY) ||
            !WireRhiProjectToPixel(front, 3.f, 1.f, 0.f, secondX, secondY) ||
            !WireRhiProjectToPixel(front, 0.6f, -0.2f, 0.f, insideX, insideY))
            return fail("WireFrame 일반 표본 투영에 실패했다");
        outCapture.edge = WireRhiMaxInWindow(
            outCapture.visible.image, edgeX, edgeY, 2, 1);
        outCapture.secondEdge = WireRhiMaxInWindow(
            outCapture.visible.image, secondX, secondY, 2, 1);
        outCapture.inside = outCapture.visible.image.At(insideX, insideY, 1);

        std::vector<Vertex> skinnedVertices = vertices;
        for (Vertex& vertex : skinnedVertices)
        {
            vertex.boneIndices = { 0.f, 0.f, 0.f, 0.f };
            vertex.boneWeights = { 1.f, 0.f, 0.f, 0.f };
        }
        Mesh skinnedMesh("rhi_wireframe_skinned_quad", skinnedVertices, indices);
        const math::matrix4x4 palette[1] = {
            math::translation_matrix(math::vector3{ 0.f, 1.5f, 0.f })
        };
        std::vector<EnhancedDrawItem> skinnedDraws(2);
        for (uint32_t i = 0; i < 2; ++i)
        {
            skinnedDraws[i].mesh = &skinnedMesh;
            skinnedDraws[i].worldMatrix = (0 == i)
                ? math::matrix4x4::identity()
                : math::translation_matrix(math::vector3{ 3.f, 0.f, 0.f });
            skinnedDraws[i].bonePalette = palette;
            skinnedDraws[i].animatorKey = 1;
            skinnedDraws[i].boneCount = 1;
        }
        context.draws = &skinnedDraws;
        if (!render(front, outCapture.skinned)) return fail(outError);

        for (EnhancedDrawItem& draw : skinnedDraws)
        {
            draw.bonePalette = nullptr;
            draw.boneCount = 0;
        }
        if (!render(front, outCapture.bindPose)) return fail(outError);

        uint32_t movedX = 0, movedY = 0;
        uint32_t bindX = 0, bindY = 0;
        if (!WireRhiProjectToPixel(front, 0.f, 2.5f, 0.f, movedX, movedY) ||
            !WireRhiProjectToPixel(front, 0.f, -1.f, 0.f, bindX, bindY))
            return fail("WireFrame 스키닝 표본 투영에 실패했다");
        outCapture.skinnedMoved = WireRhiMaxInWindow(
            outCapture.skinned.image, movedX, movedY, 2, 1);
        outCapture.skinnedBind = WireRhiMaxInWindow(
            outCapture.skinned.image, bindX, bindY, 2, 1);
        outCapture.plainMoved = WireRhiMaxInWindow(
            outCapture.bindPose.image, movedX, movedY, 2, 1);
        outCapture.plainBind = WireRhiMaxInWindow(
            outCapture.bindPose.image, bindX, bindY, 2, 1);

        const auto stats = meshes.GetStats();
        outCapture.meshUploads = stats.uploads;
        outCapture.meshHits = stats.hits;
        outCapture.meshFailures = stats.failures;

        wireframe.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }

    bool WireRhiFunctional(const WireRhiCapture& capture)
    {
        const auto graphOk = [](const WireRhiFrame& frame)
        {
            return 2 == frame.graph.passesExecuted &&
                0 == frame.graph.passesCulled &&
                2 == frame.graph.transientCreated;
        };
        return graphOk(capture.visible) && graphOk(capture.away) &&
            graphOk(capture.skinned) && graphOk(capture.bindPose) &&
            2 == capture.visible.drawItems && 1 == capture.visible.batches &&
            capture.edge > 0.9f && capture.secondEdge > 0.9f &&
            capture.inside < 0.05f && capture.visible.lit > 0 &&
            0 == capture.away.lit &&
            2 == capture.skinned.drawItems && 1 == capture.skinned.batches &&
            2 == capture.skinned.skinned && 1 == capture.skinned.palettes &&
            capture.skinnedMoved > 0.9f && capture.skinnedBind < 0.05f &&
            0 == capture.bindPose.skinned && 0 == capture.bindPose.palettes &&
            capture.plainMoved < 0.05f && capture.plainBind > 0.9f &&
            2 == capture.meshUploads && capture.meshHits >= 2 &&
            0 == capture.meshFailures;
    }

    struct WireRhiComparison
    {
        float maxDelta{ 0.f };
        double sumDelta{ 0.0 };
        uint64_t samples{ 0 };
        uint64_t overThreshold{ 0 };
        bool shapeMatches{ true };
    };

    void CompareWireRhiImage(const RHIReadbackImage& lhs,
        const RHIReadbackImage& rhs, WireRhiComparison& comparison)
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

bool RunVulkanWireFrameTest(std::string& outLog)
{
    outLog += "── WireFrame 공용 패스 — DX12/Vulkan wire raster·skinning 대조 ──\n";
    WireRhiCapture dx12Capture{};
    WireRhiCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        if (!resources.Initialize(kWireRhiSize, kWireRhiSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_wireframe.cache", error) ||
            !roots.Initialize(&resources, error) ||
            !meshes.Initialize(&resources, error))
        {
            outLog += "[1/6] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureWireRhiBackend(
            resources, pipelines, roots, meshes, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        meshes.Shutdown();
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
        "[1/6] DX12 — 변/두번째/내부 %.3f/%.3f/%.3f · 점등 %u · away %u · "
        "mesh upload/hit %u/%u\n      skin moved/bind %.3f/%.3f · "
        "plain moved/bind %.3f/%.3f · draw/palette/batch %u/%u/%u\n",
        dx12Capture.edge, dx12Capture.secondEdge, dx12Capture.inside,
        dx12Capture.visible.lit, dx12Capture.away.lit,
        dx12Capture.meshUploads, dx12Capture.meshHits,
        dx12Capture.skinnedMoved, dx12Capture.skinnedBind,
        dx12Capture.plainMoved, dx12Capture.plainBind,
        dx12Capture.skinned.skinned, dx12Capture.skinned.palettes,
        dx12Capture.skinned.batches);
    outLog += dx12Line;

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/6] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanMeshCache meshes;
    if (!resources.Initialize(kWireRhiSize, kWireRhiSize, true, error))
    {
        outLog += "[2/6] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!meshes.Initialize(&resources, error))
    {
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/6] Vulkan mesh cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        WireRhiSpirvScope spirv;
        captured = CaptureWireRhiBackend(
            resources, pipelines, pipelines, meshes, vkCapture, error);
    }

    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char vkLine[416]{};
        std::snprintf(vkLine, sizeof(vkLine),
            "[2/6] Vulkan — 변/두번째/내부 %.3f/%.3f/%.3f · 점등 %u · away %u · "
            "mesh upload/hit/fail %u/%u/%u\n      skin moved/bind %.3f/%.3f · "
            "plain moved/bind %.3f/%.3f · draw/palette/batch %u/%u/%u · "
            "그래프 %u패스/%u transient\n",
            vkCapture.edge, vkCapture.secondEdge, vkCapture.inside,
            vkCapture.visible.lit, vkCapture.away.lit,
            vkCapture.meshUploads, vkCapture.meshHits, vkCapture.meshFailures,
            vkCapture.skinnedMoved, vkCapture.skinnedBind,
            vkCapture.plainMoved, vkCapture.plainBind,
            vkCapture.skinned.skinned, vkCapture.skinned.palettes,
            vkCapture.skinned.batches,
            vkCapture.visible.graph.passesExecuted,
            vkCapture.visible.graph.transientCreated);
        outLog += vkLine;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        WireRhiFunctional(dx12Capture) && WireRhiFunctional(vkCapture);

    WireRhiComparison comparison{};
    const std::array<const WireRhiFrame*, 4> dx12Frames = {
        &dx12Capture.visible, &dx12Capture.away,
        &dx12Capture.skinned, &dx12Capture.bindPose
    };
    const std::array<const WireRhiFrame*, 4> vkFrames = {
        &vkCapture.visible, &vkCapture.away,
        &vkCapture.skinned, &vkCapture.bindPose
    };
    for (size_t i = 0; i < dx12Frames.size(); ++i)
        CompareWireRhiImage(dx12Frames[i]->image, vkFrames[i]->image, comparison);

    const double meanDelta = (0 == comparison.samples) ? 0.0 :
        comparison.sumDelta / static_cast<double>(comparison.samples);
    const uint32_t visibleLitDelta =
        (dx12Capture.visible.lit > vkCapture.visible.lit)
        ? dx12Capture.visible.lit - vkCapture.visible.lit
        : vkCapture.visible.lit - dx12Capture.visible.lit;
    const uint32_t skinnedLitDelta =
        (dx12Capture.skinned.lit > vkCapture.skinned.lit)
        ? dx12Capture.skinned.lit - vkCapture.skinned.lit
        : vkCapture.skinned.lit - dx12Capture.skinned.lit;

    char compareLine[352]{};
    std::snprintf(compareLine, sizeof(compareLine),
        "[3/6] visible/away/skinned/bind 전체 RGBA %llu표본 — 최대 %.6f · "
        "평균 %.8f · 0.05 초과 %llu · 점등 편차 %u/%u\n",
        static_cast<unsigned long long>(comparison.samples),
        comparison.maxDelta, meanDelta,
        static_cast<unsigned long long>(comparison.overThreshold),
        visibleLitDelta, skinnedLitDelta);
    outLog += compareLine;

    // polygonMode=LINE의 경계 포함 규칙은 API별로 일부 픽셀이 다를 수 있다.
    // 전체 네 프레임을 비교하되, 선 주변의 작은 커버리지 차이만 허용한다.
    if (!comparison.shapeMatches || 0 == comparison.samples ||
        meanDelta > 0.0001 || comparison.overThreshold > 64 ||
        visibleLitDelta > 8 || skinnedLitDelta > 8)
    {
        passed = false;
        outLog += "WireFrame 전체 픽셀 또는 wire coverage 대조 허용 범위를 벗어났다\n";
    }

    const float predicateDelta = (std::max)({
        std::fabs(dx12Capture.edge - vkCapture.edge),
        std::fabs(dx12Capture.secondEdge - vkCapture.secondEdge),
        std::fabs(dx12Capture.inside - vkCapture.inside),
        std::fabs(dx12Capture.skinnedMoved - vkCapture.skinnedMoved),
        std::fabs(dx12Capture.skinnedBind - vkCapture.skinnedBind),
        std::fabs(dx12Capture.plainMoved - vkCapture.plainMoved),
        std::fabs(dx12Capture.plainBind - vkCapture.plainBind),
    });
    char predicateLine[288]{};
    std::snprintf(predicateLine, sizeof(predicateLine),
        "[4/6] wire/inside·instance merge·camera·skinning 판정 최대 편차 %.6f · "
        "mesh cache %u upload/%u hit\n",
        predicateDelta, vkCapture.meshUploads, vkCapture.meshHits);
    outLog += predicateLine;
    if (predicateDelta > 0.01f) passed = false;

    outLog += "[5/6] fillModeNonSolid·root storage t0/t1·vertex/index·depth · 미구현 " +
        std::to_string(stubs) + "\n";
    outLog += "[6/6] Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "WireFrame 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "WireFrame 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}
