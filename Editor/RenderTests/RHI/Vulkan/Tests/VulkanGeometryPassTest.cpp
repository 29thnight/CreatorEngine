#include "../VulkanSelfTest.h"
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "Render/Passes/Geometry/EnhancedDecalPass.h"
#include "Render/Passes/Geometry/EnhancedDeferredPass.h"
#include "Render/Passes/Geometry/EnhancedForwardPass.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Passes/Lighting/EnhancedSSAOPass.h"
#include "Render/Passes/Lighting/EnhancedSSGIPass.h"
#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include "FrameCameraSnapshot.h"
#include "Mesh.h"
#include "Texture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kGeometryTestWindow = 64;

    struct ShadowDepthCapture
    {
        uint32_t writtenPixels{ 0 };
        float minDepth{ 1.f };
        float maxWrittenDepth{ 0.f };
        float meanWrittenDepth{ 0.f };
        uint32_t draws{ 0 };
        uint32_t batches{ 0 };
        EnhancedRenderGraph::Stats graph;
    };

    struct ShadowFixture
    {
        std::vector<Vertex> groundVertices;
        std::vector<uint32> groundIndices;
        std::vector<Vertex> blockerVertices;
        std::vector<uint32> blockerIndices;
        std::unique_ptr<Mesh> ground;
        std::unique_ptr<Mesh> blocker;
        FrameCameraSnapshot camera;
        std::vector<EnhancedDrawItem> draws;
        std::vector<EnhancedLight> lights;

        static void Quad(std::vector<Vertex>& vertices, std::vector<uint32>& indices,
            const Mathf::Vector3& origin, const Mathf::Vector3& axisU,
            const Mathf::Vector3& axisV)
        {
            const uint32 base = static_cast<uint32>(vertices.size());
            const Mathf::Vector3 points[] = {
                origin, origin + axisU, origin + axisU + axisV, origin + axisV };
            for (const Mathf::Vector3& point : points)
            {
                Vertex vertex{};
                vertex.position = point;
                vertices.push_back(vertex);
            }
            indices.insert(indices.end(), { base, base + 1, base + 2,
                base, base + 2, base + 3 });
        }

        ShadowFixture()
        {
            Quad(groundVertices, groundIndices,
                { -45.f, 0.f, -45.f }, { 90.f, 0.f, 0.f }, { 0.f, 0.f, 90.f });
            Quad(blockerVertices, blockerIndices,
                { -3.f, 0.f, 8.f }, { 6.f, 0.f, 0.f }, { 0.f, 12.f, 0.f });

            ground = std::make_unique<Mesh>(
                "rhi_shadow_ground", groundVertices, groundIndices);
            blocker = std::make_unique<Mesh>(
                "rhi_shadow_blocker", blockerVertices, blockerIndices);
            ground->RecalculateBounds();
            blocker->RecalculateBounds();

            const Mathf::xVector eye = DirectX::XMVectorSet(0.f, 18.f, -24.f, 1.f);
            const Mathf::xVector at = DirectX::XMVectorSet(0.f, 0.f, 16.f, 1.f);
            const Mathf::xVector up = DirectX::XMVectorSet(0.f, 1.f, 0.f, 0.f);
            camera.view = MathematicsInterop::FromDirectX(DirectX::XMMatrixLookAtLH(eye, at, up));
            camera.projection = math::perspective_fov_lh(
                DirectX::XM_PI / 3.f, 1.f, 0.1f, 180.f);
            camera.inverseView = math::inverse(camera.view);
            camera.inverseProjection = math::inverse(camera.projection);
            camera.eyePosition = MathematicsInterop::FromDirectX3(eye);
            camera.forward = MathematicsInterop::FromDirectX3(DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(at, eye)));
            camera.right = MathematicsInterop::FromDirectX3(DirectX::XMVector3Normalize(DirectX::XMVector3Cross(up, MathematicsInterop::ToDirectXDirection(camera.forward))));
            camera.up = MathematicsInterop::FromDirectX3(DirectX::XMVector3Cross(MathematicsInterop::ToDirectXDirection(camera.forward), MathematicsInterop::ToDirectXDirection(camera.right)));
            camera.fov = 60.f;
            camera.nearPlane = 0.1f;
            camera.farPlane = 180.f;

            EnhancedDrawItem groundDraw{};
            groundDraw.mesh = ground.get();
            groundDraw.worldMatrix = DirectX::XMMatrixIdentity();
            draws.push_back(groundDraw);

            EnhancedDrawItem blockerDraw{};
            blockerDraw.mesh = blocker.get();
            blockerDraw.worldMatrix = DirectX::XMMatrixIdentity();
            draws.push_back(blockerDraw);

            EnhancedLight sun{};
            sun.position = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);
            sun.direction = Mathf::Vector4(Mathf::Vector3(DirectX::XMVector3Normalize(
                DirectX::XMVectorSet(0.65f, -1.f, 0.25f, 0.f))));
            sun.color = Mathf::Color4(1.f, 1.f, 1.f, 4.f);
            lights.push_back(sun);
        }
    };

    void AnalyzeShadowDepth(const RHIReadbackImage& image, ShadowDepthCapture& capture)
    {
        double sum = 0.0;
        capture.minDepth = 1.f;
        capture.maxWrittenDepth = 0.f;
        capture.writtenPixels = 0;
        for (uint32_t y = 0; y < image.height; ++y)
        {
            for (uint32_t x = 0; x < image.width; ++x)
            {
                const float depth = image.At(x, y, 0);
                if (!std::isfinite(depth) || depth >= 0.99999f) continue;
                capture.minDepth = (std::min)(capture.minDepth, depth);
                capture.maxWrittenDepth = (std::max)(capture.maxWrittenDepth, depth);
                sum += depth;
                ++capture.writtenPixels;
            }
        }
        capture.meanWrittenDepth = (0 != capture.writtenPixels)
            ? static_cast<float>(sum / capture.writtenPixels) : 0.f;
    }

    template <typename TResources>
    bool CaptureShadowBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        IRenderMeshCache& meshCache, const std::function<void()>& beginMeshFrame,
        ShadowFixture& fixture, ShadowDepthCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshCache;
        context.camera = &fixture.camera;
        context.draws = &fixture.draws;
        context.lights = &fixture.lights;
        context.width = kGeometryTestWindow;
        context.height = kGeometryTestWindow;

        EnhancedShadowPass shadow;
        RHIReadback readback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error) {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            shadow.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!shadow.Initialize(context, outError)) return fail(outError);
        if (!resources.CreateReadback(EnhancedShadowPass::kShadowMapSize,
            EnhancedShadowPass::kShadowMapSize, EnhancedShadowPass::kShadowFormat,
            1, readback, outError))
        {
            return fail(outError);
        }

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (beginMeshFrame) beginMeshFrame();
        if (!shadow.PrepareFrame(context, outError)) return fail(outError);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        shadow.Declare(graph, context);
        const RGHandle shadowMap = shadow.GetShadowMap();
        if (!shadowMap.IsValid()) return fail("그림자 배열 텍스처가 선언되지 않았다");

        graph.AddPass("ShadowRHI.Readback",
            { { shadowMap, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                // 첫 cascade = destination slice 0, source subresource 0.
                executeContext.encoder->CopyToReadback(readback,
                    executeContext.ResolveHandle(shadowMap), 0, 0);
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
        outCapture.graph = graph.GetStats();
        outCapture.draws = shadow.GetLastDrawCount();
        outCapture.batches = shadow.GetLastBatchCount();

        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        RHIReadbackImage image;
        if (!resources.MapReadback(readback, image, outError)) return fail(outError);
        AnalyzeShadowDepth(image, outCapture);

        shadow.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }

    struct ShadowSpirvScope
    {
        RHIShaderCompiler::ScopedOutput output{ RHIShaderBinary::SpirV };
    };

    struct GBufferCapture
    {
        float center[4][4]{};
        float outside[4][4]{};
        float bitmask{ 0.f };
        float outsideBitmask{ 0.f };
        float depth{ 1.f };
        float outsideDepth{ 1.f };
        uint32_t writtenPixels{ 0 };
        uint32_t draws{ 0 };
        uint32_t meshes{ 0 };
        uint32_t materials{ 0 };
        uint32_t batches{ 0 };
        EnhancedRenderGraph::Stats graph;
    };

    struct GBufferFixture
    {
        std::vector<Vertex> vertices;
        std::vector<uint32> indices{ 0, 1, 2, 0, 2, 3 };
        std::unique_ptr<Mesh> mesh;
        std::vector<EnhancedDrawItem> draws;

        GBufferFixture()
        {
            const Mathf::Vector3 positions[] = {
                { -0.75f, -0.75f, 0.5f }, { -0.75f, 0.75f, 0.5f },
                { 0.75f, 0.75f, 0.5f }, { 0.75f, -0.75f, 0.5f } };
            const Mathf::Vector2 uvs[] = {
                { 0.f, 1.f }, { 0.f, 0.f }, { 1.f, 0.f }, { 1.f, 1.f } };
            for (uint32_t i = 0; i < 4; ++i)
            {
                Vertex vertex{};
                vertex.position = positions[i];
                vertex.normal = { 0.f, 0.f, 1.f };
                vertex.uv0 = uvs[i];
                vertex.tangent = { 1.f, 0.f, 0.f };
                vertex.bitangent = { 0.f, 1.f, 0.f };
                vertices.push_back(vertex);
            }

            mesh = std::make_unique<Mesh>("rhi_gbuffer_quad", vertices, indices);
            mesh->RecalculateBounds();

            EnhancedDrawItem draw{};
            draw.mesh = mesh.get();
            draw.worldMatrix = DirectX::XMMatrixIdentity();
            draw.baseColorFactor = Mathf::Color4(0.25f, 0.5f, 0.75f, 1.f);
            draw.metallic = 0.2f;
            draw.roughness = 0.6f;
            draws.push_back(draw);
        }
    };

    template <typename TResources>
    bool CaptureGBufferBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        IRenderMeshCache& meshCache, IRenderTextureCache& textureCache,
        const std::function<void()>& beginCaches, GBufferFixture& fixture,
        GBufferCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshCache;
        context.textureCache = &textureCache;
        context.draws = &fixture.draws;
        context.width = kGeometryTestWindow;
        context.height = kGeometryTestWindow;

        EnhancedGBufferPass gbuffer;
        gbuffer.SetKeepAlive(false);
        std::array<RHIReadback, 6> readbacks{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error) {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            gbuffer.Shutdown();
            for (RHIReadback& readback : readbacks)
                resources.ReleaseReadback(readback);
            return false;
        };

        if (!gbuffer.Initialize(context, outError)) return fail(outError);
        const RHIFormat formats[] = {
            RHIFormat::RGBA16Float, RHIFormat::RGBA16Float,
            RHIFormat::RGBA16Float, RHIFormat::RGBA16Float,
            RHIFormat::R32Uint, RHIFormat::D32Float };
        for (uint32_t i = 0; i < static_cast<uint32_t>(readbacks.size()); ++i)
        {
            if (!resources.CreateReadback(kGeometryTestWindow, kGeometryTestWindow,
                formats[i], 1, readbacks[i], outError))
                return fail(outError);
        }

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (beginCaches) beginCaches();
        if (!gbuffer.PrepareFrame(context, outError)) return fail(outError);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        gbuffer.Declare(graph, context);
        const EnhancedGBufferPass::Outputs outputs = gbuffer.GetOutputs();
        const std::array<RGHandle, 6> handles = {
            outputs.diffuse, outputs.metalRough, outputs.normal,
            outputs.emissive, outputs.bitmask, outputs.depth };
        for (const RGHandle handle : handles)
            if (!handle.IsValid()) return fail("GBuffer 출력이 선언되지 않았다");

        std::vector<EnhancedRenderGraph::RGPassUsage> readUsages;
        readUsages.reserve(handles.size());
        for (const RGHandle handle : handles)
            readUsages.push_back({ handle, RHIResourceState::CopySource });
        graph.AddPass("GBufferRHI.Readback", readUsages,
            [&, handles](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                for (uint32_t i = 0; i < static_cast<uint32_t>(handles.size()); ++i)
                {
                    executeContext.encoder->CopyToReadback(readbacks[i],
                        executeContext.ResolveHandle(handles[i]));
                }
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
        outCapture.graph = graph.GetStats();
        outCapture.draws = gbuffer.GetLastDrawCount();
        outCapture.meshes = gbuffer.GetLastMeshCount();
        outCapture.materials = gbuffer.GetLastMaterialCount();
        outCapture.batches = gbuffer.GetLastBatchCount();

        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        std::array<RHIReadbackImage, 6> images;
        for (uint32_t i = 0; i < static_cast<uint32_t>(images.size()); ++i)
            if (!resources.MapReadback(readbacks[i], images[i], outError))
                return fail(outError);

        constexpr uint32_t center = kGeometryTestWindow / 2;
        constexpr uint32_t outside = 2;
        for (uint32_t target = 0; target < 4; ++target)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                outCapture.center[target][channel] =
                    images[target].At(center, center, channel);
                outCapture.outside[target][channel] =
                    images[target].At(outside, outside, channel);
            }
        }
        outCapture.bitmask = images[4].At(center, center, 0);
        outCapture.outsideBitmask = images[4].At(outside, outside, 0);
        outCapture.depth = images[5].At(center, center, 0);
        outCapture.outsideDepth = images[5].At(outside, outside, 0);
        for (uint32_t y = 0; y < kGeometryTestWindow; ++y)
            for (uint32_t x = 0; x < kGeometryTestWindow; ++x)
                if (images[0].At(x, y, 3) > 0.5f) ++outCapture.writtenPixels;

        gbuffer.Shutdown();
        for (RHIReadback& readback : readbacks) resources.ReleaseReadback(readback);
        return true;
    }

    struct ForwardCapture
    {
        float center[4]{};
        float outside[4]{};
        uint32_t writtenPixels{ 0 };
        uint32_t centerTileLights{ 0 };
        uint32_t cornerTileLights{ 0 };
        uint32_t minTileLights{ UINT32_MAX };
        uint32_t maxTileLights{ 0 };
        EnhancedRenderGraph::Stats graph;
    };

    struct ForwardFixture
    {
        std::vector<Vertex> vertices;
        std::vector<uint32> indices{ 0, 1, 2, 0, 2, 3 };
        std::unique_ptr<Mesh> mesh;
        std::vector<EnhancedDrawItem> draws;
        std::vector<EnhancedLight> lights;
        FrameCameraSnapshot camera;

        ForwardFixture()
        {
            const Mathf::Vector3 positions[] = {
                { -0.75f, -0.75f, 0.5f }, { -0.75f, 0.75f, 0.5f },
                { 0.75f, 0.75f, 0.5f }, { 0.75f, -0.75f, 0.5f } };
            const Mathf::Vector2 uvs[] = {
                { 0.f, 1.f }, { 0.f, 0.f }, { 1.f, 0.f }, { 1.f, 1.f } };
            for (uint32_t i = 0; i < 4; ++i)
            {
                Vertex vertex{};
                vertex.position = positions[i];
                vertex.normal = { 0.f, 0.f, 1.f };
                vertex.uv0 = uvs[i];
                vertex.tangent = { 1.f, 0.f, 0.f };
                vertex.bitangent = { 0.f, 1.f, 0.f };
                vertices.push_back(vertex);
            }

            mesh = std::make_unique<Mesh>("rhi_forward_quad", vertices, indices);
            mesh->RecalculateBounds();

            EnhancedDrawItem draw{};
            draw.mesh = mesh.get();
            draw.worldMatrix = DirectX::XMMatrixIdentity();
            draw.baseColorFactor = Mathf::Color4(0.8f, 0.4f, 0.2f, 0.5f);
            draw.metallic = 0.f;
            draw.roughness = 0.7f;
            draws.push_back(draw);

            EnhancedLight sun{};
            sun.position = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);
            sun.direction = Mathf::Vector4(0.f, 0.f, -1.f, 0.f);
            sun.color = Mathf::Color4(1.f, 1.f, 1.f, 2.f);
            lights.push_back(sun);

            camera.view = math::matrix4x4::identity();
            camera.projection = math::matrix4x4::identity();
            camera.inverseView = math::matrix4x4::identity();
            camera.inverseProjection = math::matrix4x4::identity();
            camera.eyePosition = math::vector3{0.f, 0.f, 2.f};
            camera.forward = math::vector3{0.f, 0.f, 1.f};
            camera.right = math::vector3{1.f, 0.f, 0.f};
            camera.up = math::vector3{0.f, 1.f, 0.f};
            camera.nearPlane = 0.f;
            camera.farPlane = 1.f;
        }
    };

    template <typename TResources>
    bool CaptureForwardBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        IRenderMeshCache& meshCache, IRenderTextureCache& textureCache,
        const std::function<void()>& beginCaches, ForwardFixture& fixture,
        ForwardCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshCache;
        context.textureCache = &textureCache;
        context.forwardDraws = &fixture.draws;
        context.lights = &fixture.lights;
        context.camera = &fixture.camera;
        context.width = kGeometryTestWindow;
        context.height = kGeometryTestWindow;

        EnhancedForwardPass forward;
        RHIReadback colorReadback{};
        RHIReadback countReadback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error) {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            forward.Shutdown();
            resources.ReleaseReadback(colorReadback);
            resources.ReleaseReadback(countReadback);
            return false;
        };

        if (!forward.Initialize(context, outError)) return fail(outError);
        const uint32_t tileX = (kGeometryTestWindow + EnhancedForwardPass::kTileSize - 1)
            / EnhancedForwardPass::kTileSize;
        const uint32_t tileY = tileX;
        const uint32_t tileTotal = tileX * tileY;
        if (!resources.CreateReadback(kGeometryTestWindow, kGeometryTestWindow,
                EnhancedForwardPass::kOutputFormat, 1, colorReadback, outError) ||
            !resources.CreateBufferReadback(
                static_cast<uint64_t>(tileTotal) * sizeof(uint32_t),
                countReadback, outError))
        {
            return fail(outError);
        }

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (beginCaches) beginCaches();
        if (!forward.PrepareFrame(context, outError)) return fail(outError);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        RGTextureDesc depthDesc{};
        depthDesc.width = kGeometryTestWindow;
        depthDesc.height = kGeometryTestWindow;
        depthDesc.format = EnhancedForwardPass::kDepthFormat;
        depthDesc.allowDepthStencil = true;
        depthDesc.name = "ForwardRHI.Depth";
        const RGHandle depth = graph.CreateTexture(depthDesc);

        graph.AddPass("ForwardRHI.DepthClear",
            { { depth, RHIResourceState::DepthWrite } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                const auto depthTarget = RHIDepthTargetDesc::Depth(
                    executeContext.ResolveHandle(depth), EnhancedForwardPass::kDepthFormat);
                const RHIRenderTargetBinding targets =
        context.resources->CreateRenderTargets(
            std::span<const RHITextureHandle>{}, &depthTarget);
                if (!targets.IsValid()) return;
                executeContext.encoder->BindRenderTargets(targets);
                executeContext.encoder->ClearDepthTarget(targets, 1.f);
            });

        EnhancedForwardPass::Inputs inputs{};
        inputs.depth = depth;
        forward.SetInputs(inputs);
        forward.Declare(graph, context);
        const RGHandle output = forward.GetOutput();
        if (!output.IsValid() || !forward.GetTileCountHandle().IsValid())
            return fail("Forward 출력 또는 타일 카운트 버퍼가 선언되지 않았다");

        graph.AddPass("ForwardRHI.Readback",
            { { output, RHIResourceState::CopySource },
              { forward.GetTileCountHandle(), RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback(
                    colorReadback, executeContext.ResolveHandle(output));
                executeContext.encoder->CopyBufferToReadback(
                    countReadback, forward.GetTileCountBuffer());
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
        outCapture.graph = graph.GetStats();

        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        RHIReadbackImage color;
        RHIReadbackImage counts;
        if (!resources.MapReadback(colorReadback, color, outError) ||
            !resources.MapReadback(countReadback, counts, outError))
        {
            return fail(outError);
        }

        constexpr uint32_t center = kGeometryTestWindow / 2;
        constexpr uint32_t outside = 2;
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            outCapture.center[channel] = color.At(center, center, channel);
            outCapture.outside[channel] = color.At(outside, outside, channel);
        }
        for (uint32_t y = 0; y < kGeometryTestWindow; ++y)
            for (uint32_t x = 0; x < kGeometryTestWindow; ++x)
                if ((std::max)({ std::fabs(color.At(x, y, 0)),
                        std::fabs(color.At(x, y, 1)), std::fabs(color.At(x, y, 2)) }) > 0.001f)
                    ++outCapture.writtenPixels;

        const uint32_t* tileCounts = counts.Elements<uint32_t>();
        if (nullptr == tileCounts) return fail("Forward 타일 카운트 리드백이 비었다");
        outCapture.cornerTileLights = tileCounts[0];
        outCapture.centerTileLights = tileCounts[(tileY / 2) * tileX + tileX / 2];
        for (uint32_t i = 0; i < tileTotal; ++i)
        {
            outCapture.minTileLights = (std::min)(outCapture.minTileLights, tileCounts[i]);
            outCapture.maxTileLights = (std::max)(outCapture.maxTileLights, tileCounts[i]);
        }

        forward.Shutdown();
        resources.ReleaseReadback(colorReadback);
        resources.ReleaseReadback(countReadback);
        return true;
    }

    struct DeferredCapture
    {
        float center[4]{};
        float outside[4]{};
        uint32_t litPixels{ 0 };
        uint32_t gbufferDraws{ 0 };
        uint32_t lightCount{ 0 };
        EnhancedRenderGraph::Stats graph;
    };

    struct DeferredFixture
    {
        GBufferFixture geometry;
        FrameCameraSnapshot camera;
        std::vector<EnhancedLight> lights;

        DeferredFixture()
        {
            camera.view = math::matrix4x4::identity();
            camera.projection = math::matrix4x4::identity();
            camera.inverseView = math::matrix4x4::identity();
            camera.inverseProjection = math::matrix4x4::identity();
            camera.eyePosition = math::vector3{0.f, 0.f, 2.f};
            camera.forward = math::vector3{0.f, 0.f, 1.f};
            camera.right = math::vector3{1.f, 0.f, 0.f};
            camera.up = math::vector3{0.f, 1.f, 0.f};
            camera.nearPlane = 0.f;
            camera.farPlane = 1.f;

            EnhancedLight sun{};
            sun.position = Mathf::Vector4(0.f, 0.f, 0.f, 0.f);
            sun.direction = Mathf::Vector4(0.f, 0.f, -1.f, 0.f);
            sun.color = Mathf::Color4(1.f, 1.f, 1.f, 2.f);
            lights.push_back(sun);
        }
    };

    template <typename TResources>
    bool CaptureDeferredBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        IRenderMeshCache& meshCache, IRenderTextureCache& textureCache,
        const std::function<void()>& beginCaches, DeferredFixture& fixture,
        DeferredCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshCache;
        context.textureCache = &textureCache;
        context.draws = &fixture.geometry.draws;
        context.lights = &fixture.lights;
        context.camera = &fixture.camera;
        context.width = kGeometryTestWindow;
        context.height = kGeometryTestWindow;

        EnhancedGBufferPass gbuffer;
        EnhancedDeferredPass deferred;
        gbuffer.SetKeepAlive(false);
        RHIReadback readback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error) {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            deferred.Shutdown();
            gbuffer.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!gbuffer.Initialize(context, outError) ||
            !deferred.Initialize(context, outError))
            return fail(outError);
        if (!resources.CreateReadback(kGeometryTestWindow, kGeometryTestWindow,
                EnhancedDeferredPass::kOutputFormat, 1, readback, outError))
            return fail(outError);

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (beginCaches) beginCaches();
        if (!gbuffer.PrepareFrame(context, outError) ||
            !deferred.PrepareFrame(context, outError))
            return fail(outError);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        gbuffer.Declare(graph, context);
        const EnhancedGBufferPass::Outputs gbufferOutputs = gbuffer.GetOutputs();
        deferred.SetInputs(gbufferOutputs);
        deferred.Declare(graph, context);
        const RGHandle output = deferred.GetOutput();
        if (!output.IsValid()) return fail("Deferred 출력이 선언되지 않았다");

        graph.AddPass("DeferredRHI.Readback",
            { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback(
                    readback, executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
        outCapture.graph = graph.GetStats();
        outCapture.gbufferDraws = gbuffer.GetLastDrawCount();
        outCapture.lightCount = deferred.GetLastLightCount();

        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        RHIReadbackImage image;
        if (!resources.MapReadback(readback, image, outError)) return fail(outError);
        constexpr uint32_t center = kGeometryTestWindow / 2;
        constexpr uint32_t outside = 2;
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            outCapture.center[channel] = image.At(center, center, channel);
            outCapture.outside[channel] = image.At(outside, outside, channel);
        }
        for (uint32_t y = 0; y < kGeometryTestWindow; ++y)
            for (uint32_t x = 0; x < kGeometryTestWindow; ++x)
                if ((std::max)({ std::fabs(image.At(x, y, 0)),
                        std::fabs(image.At(x, y, 1)), std::fabs(image.At(x, y, 2)) }) > 0.001f)
                    ++outCapture.litPixels;

        deferred.Shutdown();
        gbuffer.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }
}

bool RunVulkanShadowTest(std::string& outLog)
{
    outLog += "── Shadow 패스 — DX12/Vulkan depth-array·mesh 대조 ──\n";
    ShadowFixture fixture;
    ShadowDepthCapture dx12Capture{};
    ShadowDepthCapture vkCapture{};
    std::string error;

    // DX12 기준을 같은 실행·같은 fixture에서 만든다. 고정 숫자를 복사해 두면
    // fixture가 바뀌었을 때 두 검사 중 한쪽만 낡는다.
    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_shadow.cache", error) ||
            !roots.Initialize(&resources, error) || !meshes.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureShadowBackend(resources, pipelines, roots, meshes,
            {}, fixture, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        meshes.Shutdown();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/4] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }

    outLog += "[1/4] DX12 기준 depth-only PSO·mesh upload·cascade draw 통과\n";

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanMeshCache meshes;
    if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!meshes.Initialize(&resources, error))
    {
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/4] Vulkan mesh cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        ShadowSpirvScope spirv;
        captured = CaptureShadowBackend(resources, pipelines, pipelines, meshes,
            [&] { meshes.BeginFrame(0); }, fixture, vkCapture, error);
    }

    const VulkanMeshCache::Stats meshStats = meshes.GetStats();
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char line[320]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Vulkan — 실행 %u·컬링 %u·transient %u · draw %u·batch %u · "
            "mesh upload %u/실패 %u\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated, vkCapture.draws, vkCapture.batches,
            meshStats.uploads, meshStats.failures);
        outLog += line;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        2 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        1 == vkCapture.graph.transientCreated && 0 != vkCapture.draws &&
        0 != vkCapture.batches && 2 == meshStats.uploads && 0 == meshStats.failures;

    const float writtenDelta = (0 != dx12Capture.writtenPixels)
        ? std::fabs(static_cast<float>(vkCapture.writtenPixels) -
            static_cast<float>(dx12Capture.writtenPixels)) /
            static_cast<float>(dx12Capture.writtenPixels)
        : (0 == vkCapture.writtenPixels ? 0.f : 1.f);
    const float minDelta = std::fabs(vkCapture.minDepth - dx12Capture.minDepth);
    const float meanDelta = std::fabs(
        vkCapture.meanWrittenDepth - dx12Capture.meanWrittenDepth);

    char compare[384]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] 첫 cascade depth — DX12/Vulkan 기록 %u/%u(편차 %.2f%%) · "
        "min %.5f/%.5f · mean %.5f/%.5f\n",
        dx12Capture.writtenPixels, vkCapture.writtenPixels, writtenDelta * 100.f,
        dx12Capture.minDepth, vkCapture.minDepth,
        dx12Capture.meanWrittenDepth, vkCapture.meanWrittenDepth);
    outLog += compare;

    if (0 == dx12Capture.writtenPixels || 0 == vkCapture.writtenPixels ||
        writtenDelta > 0.05f || minDelta > 0.02f || meanDelta > 0.02f)
    {
        passed = false;
        outLog += "depth-array 픽셀 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] depth-only stage·slice view·indexed mesh·completion lifetime · "
        "미구현 " + std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();

    outLog += passed
        ? "Shadow 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "Shadow 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

bool RunVulkanGBufferTest(std::string& outLog)
{
    outLog += "── GBuffer 패스 — DX12/Vulkan MRT·texture·sampler·mesh 대조 ──\n";
    GBufferFixture fixture;
    GBufferCapture dx12Capture{};
    GBufferCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        DX12TextureCache textures;
        if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_gbuffer.cache", error) ||
            !roots.Initialize(&resources, error) ||
            !meshes.Initialize(&resources, error) ||
            !textures.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureGBufferBackend(resources, pipelines, roots,
            meshes, textures,
            [&] { meshes.BeginFrame(0); textures.BeginFrame(0); },
            fixture, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        textures.Shutdown();
        meshes.Shutdown();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/4] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }

    outLog += "[1/4] DX12 기준 5 MRT·depth·fallback texture·dynamic sampler 통과\n";

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanMeshCache meshes;
    VulkanTextureCache textures;
    if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!meshes.Initialize(&resources, error) || !textures.Initialize(&resources, error))
    {
        textures.Shutdown();
        meshes.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/4] Vulkan asset cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        ShadowSpirvScope spirv;
        captured = CaptureGBufferBackend(resources, pipelines, pipelines,
            meshes, textures, [&] { meshes.BeginFrame(0); },
            fixture, vkCapture, error);
    }

    const VulkanMeshCache::Stats meshStats = meshes.GetStats();
    const VulkanTextureCache::Stats textureStats = textures.GetStats();
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char line[384]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Vulkan — 실행 %u·컬링 %u·transient %u · "
            "draw/mesh/material/batch %u/%u/%u/%u · mesh upload %u/실패 %u · "
            "texture 실패 %u\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated, vkCapture.draws, vkCapture.meshes,
            vkCapture.materials, vkCapture.batches, meshStats.uploads,
            meshStats.failures, textureStats.failures);
        outLog += line;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        2 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        6 == vkCapture.graph.transientCreated &&
        1 == vkCapture.draws && 1 == vkCapture.meshes &&
        1 == vkCapture.materials && 1 == vkCapture.batches &&
        1 == meshStats.uploads && 0 == meshStats.failures &&
        0 == textureStats.failures;

    float maxCenterDelta = 0.f;
    float maxOutside = 0.f;
    for (uint32_t target = 0; target < 4; ++target)
    {
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            maxCenterDelta = (std::max)(maxCenterDelta, std::fabs(
                vkCapture.center[target][channel] -
                dx12Capture.center[target][channel]));
            maxOutside = (std::max)(maxOutside, std::fabs(
                vkCapture.outside[target][channel]));
        }
    }

    const float writtenDelta = (0 != dx12Capture.writtenPixels)
        ? std::fabs(static_cast<float>(vkCapture.writtenPixels) -
            static_cast<float>(dx12Capture.writtenPixels)) /
            static_cast<float>(dx12Capture.writtenPixels)
        : 1.f;
    const float bitmaskDelta = std::fabs(vkCapture.bitmask - dx12Capture.bitmask);
    const float depthDelta = std::fabs(vkCapture.depth - dx12Capture.depth);

    char compare[512]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] center — diffuse %.3f/%.3f,%.3f/%.3f,%.3f/%.3f · "
        "normal z %.3f/%.3f · bitmask %.0f/%.0f · depth %.3f/%.3f · "
        "coverage %u/%u(편차 %.2f%%) · 최대 채널 편차 %.5f\n",
        dx12Capture.center[0][0], vkCapture.center[0][0],
        dx12Capture.center[0][1], vkCapture.center[0][1],
        dx12Capture.center[0][2], vkCapture.center[0][2],
        dx12Capture.center[2][2], vkCapture.center[2][2],
        dx12Capture.bitmask, vkCapture.bitmask,
        dx12Capture.depth, vkCapture.depth,
        dx12Capture.writtenPixels, vkCapture.writtenPixels,
        writtenDelta * 100.f, maxCenterDelta);
    outLog += compare;

    const float expected[][4] = {
        { 0.25f, 0.5f, 0.75f, 1.f },
        { 1.f, 0.6f, 0.2f, 1.f },
        { 0.5f, 0.5f, 1.f, 1.f },
        { 0.f, 0.f, 0.f, 1.f } };
    float dxExpectedDelta = 0.f;
    for (uint32_t target = 0; target < 4; ++target)
        for (uint32_t channel = 0; channel < 4; ++channel)
            dxExpectedDelta = (std::max)(dxExpectedDelta, std::fabs(
                dx12Capture.center[target][channel] - expected[target][channel]));

    if (0 == dx12Capture.writtenPixels || 0 == vkCapture.writtenPixels ||
        writtenDelta > 0.02f || maxCenterDelta > 0.015f ||
        dxExpectedDelta > 0.015f || bitmaskDelta > 0.5f ||
        std::fabs(dx12Capture.bitmask - 43981.f) > 0.5f ||
        depthDelta > 0.001f || std::fabs(vkCapture.depth - 0.5f) > 0.001f ||
        maxOutside > 0.001f || 0.f != vkCapture.outsideBitmask ||
        std::fabs(vkCapture.outsideDepth - 1.f) > 0.001f)
    {
        passed = false;
        outLog += "MRT·depth 픽셀 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] 5 MRT·2D SRV×4·dynamic sampler·root SRV×2·indexed mesh · "
        "미구현 " + std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();

    outLog += passed
        ? "GBuffer 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "GBuffer 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

bool RunVulkanForwardTest(std::string& outLog)
{
    outLog += "── Forward+ 패스 — DX12/Vulkan compute·buffer·blend·mesh 대조 ──\n";
    ForwardFixture fixture;
    ForwardCapture dx12Capture{};
    ForwardCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        DX12TextureCache textures;
        if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_forward.cache", error) ||
            !roots.Initialize(&resources, error) ||
            !meshes.Initialize(&resources, error) ||
            !textures.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureForwardBackend(resources, pipelines, roots,
            meshes, textures,
            [&] { meshes.BeginFrame(0); textures.BeginFrame(0); },
            fixture, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        textures.Shutdown();
        meshes.Shutdown();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/4] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }
    outLog += "[1/4] DX12 기준 compute cull·structured UAV/SRV·alpha blend 통과\n";

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanMeshCache meshes;
    VulkanTextureCache textures;
    if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!meshes.Initialize(&resources, error) || !textures.Initialize(&resources, error))
    {
        textures.Shutdown();
        meshes.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/4] Vulkan asset cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        ShadowSpirvScope spirv;
        captured = CaptureForwardBackend(resources, pipelines, pipelines,
            meshes, textures, [&] { meshes.BeginFrame(0); },
            fixture, vkCapture, error);
    }

    const VulkanMeshCache::Stats meshStats = meshes.GetStats();
    const VulkanTextureCache::Stats textureStats = textures.GetStats();
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char line[384]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Vulkan — 실행 %u·컬링 %u·transient %u · "
            "tile(center/corner/min/max) %u/%u/%u/%u · "
            "mesh upload %u/실패 %u · texture 실패 %u\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated, vkCapture.centerTileLights,
            vkCapture.cornerTileLights, vkCapture.minTileLights,
            vkCapture.maxTileLights, meshStats.uploads, meshStats.failures,
            textureStats.failures);
        outLog += line;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        4 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        2 == vkCapture.graph.transientCreated &&
        1 == meshStats.uploads && 0 == meshStats.failures &&
        0 == textureStats.failures &&
        1 == dx12Capture.centerTileLights && 1 == dx12Capture.cornerTileLights &&
        1 == dx12Capture.minTileLights && 1 == dx12Capture.maxTileLights &&
        1 == vkCapture.centerTileLights && 1 == vkCapture.cornerTileLights &&
        1 == vkCapture.minTileLights && 1 == vkCapture.maxTileLights;

    float maxCenterDelta = 0.f;
    float maxOutside = 0.f;
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        maxCenterDelta = (std::max)(maxCenterDelta, std::fabs(
            vkCapture.center[channel] - dx12Capture.center[channel]));
        maxOutside = (std::max)(maxOutside, std::fabs(vkCapture.outside[channel]));
    }
    const float writtenDelta = (0 != dx12Capture.writtenPixels)
        ? std::fabs(static_cast<float>(vkCapture.writtenPixels) -
            static_cast<float>(dx12Capture.writtenPixels)) /
            static_cast<float>(dx12Capture.writtenPixels)
        : 1.f;

    char compare[512]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] center RGBA %.4f/%.4f,%.4f/%.4f,%.4f/%.4f,%.4f/%.4f · "
        "coverage %u/%u(편차 %.2f%%) · 최대 채널 편차 %.5f · outside %.5f\n",
        dx12Capture.center[0], vkCapture.center[0],
        dx12Capture.center[1], vkCapture.center[1],
        dx12Capture.center[2], vkCapture.center[2],
        dx12Capture.center[3], vkCapture.center[3],
        dx12Capture.writtenPixels, vkCapture.writtenPixels,
        writtenDelta * 100.f, maxCenterDelta, maxOutside);
    outLog += compare;

    if (0 == dx12Capture.writtenPixels || 0 == vkCapture.writtenPixels ||
        dx12Capture.center[0] <= 0.01f || vkCapture.center[0] <= 0.01f ||
        writtenDelta > 0.02f || maxCenterDelta > 0.02f || maxOutside > 0.001f)
    {
        passed = false;
        outLog += "Forward+ 픽셀·coverage 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] compute PSO·depth SRV·structured UAV table·UAV→SRV/Copy "
        "buffer barrier·root buffer·dynamic sampler×3·indexed mesh · 미구현 " +
        std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();

    outLog += passed
        ? "Forward+ 공용 패스 DX12/Vulkan 픽셀·타일 버퍼 대조 통과\n"
        : "Forward+ 공용 패스 DX12/Vulkan 픽셀·타일 버퍼 대조 실패\n";
    return passed;
}

bool RunVulkanDeferredTest(std::string& outLog)
{
    outLog += "── Deferred 패스 — DX12/Vulkan GBuffer consume·fullscreen 대조 ──\n";
    DeferredFixture fixture;
    DeferredCapture dx12Capture{};
    DeferredCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        DX12TextureCache textures;
        if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_deferred.cache", error) ||
            !roots.Initialize(&resources, error) ||
            !meshes.Initialize(&resources, error) ||
            !textures.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }

        const bool captured = CaptureDeferredBackend(resources, pipelines, roots,
            meshes, textures,
            [&] { meshes.BeginFrame(0); textures.BeginFrame(0); },
            fixture, dx12Capture, error);
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        textures.Shutdown();
        meshes.Shutdown();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems)
        {
            outLog += "[1/4] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }
    outLog += "[1/4] DX12 기준 GBuffer→Deferred graph dependency·9 SRV 통과\n";

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanMeshCache meshes;
    VulkanTextureCache textures;
    if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!meshes.Initialize(&resources, error) || !textures.Initialize(&resources, error))
    {
        textures.Shutdown();
        meshes.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/4] Vulkan asset cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        ShadowSpirvScope spirv;
        captured = CaptureDeferredBackend(resources, pipelines, pipelines,
            meshes, textures, [&] { meshes.BeginFrame(0); },
            fixture, vkCapture, error);
    }

    const VulkanMeshCache::Stats meshStats = meshes.GetStats();
    const VulkanTextureCache::Stats textureStats = textures.GetStats();
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char line[320]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Vulkan — 실행 %u·컬링 %u·transient %u · "
            "GBuffer draw %u · light %u · mesh upload %u/실패 %u · texture 실패 %u\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated, vkCapture.gbufferDraws,
            vkCapture.lightCount, meshStats.uploads, meshStats.failures,
            textureStats.failures);
        outLog += line;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        3 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        7 == vkCapture.graph.transientCreated &&
        1 == vkCapture.gbufferDraws && 1 == vkCapture.lightCount &&
        1 == meshStats.uploads && 0 == meshStats.failures &&
        0 == textureStats.failures;

    float maxCenterDelta = 0.f;
    float maxOutsideRgb = 0.f;
    float maxOutsideDelta = 0.f;
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        maxCenterDelta = (std::max)(maxCenterDelta, std::fabs(
            vkCapture.center[channel] - dx12Capture.center[channel]));
        maxOutsideDelta = (std::max)(maxOutsideDelta, std::fabs(
            vkCapture.outside[channel] - dx12Capture.outside[channel]));
        if (channel < 3)
            maxOutsideRgb = (std::max)(maxOutsideRgb,
                std::fabs(vkCapture.outside[channel]));
    }
    const float litDelta = (0 != dx12Capture.litPixels)
        ? std::fabs(static_cast<float>(vkCapture.litPixels) -
            static_cast<float>(dx12Capture.litPixels)) /
            static_cast<float>(dx12Capture.litPixels)
        : 1.f;

    char compare[512]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] center RGBA %.4f/%.4f,%.4f/%.4f,%.4f/%.4f,%.4f/%.4f · "
        "lit pixels %u/%u(편차 %.2f%%) · 최대 채널 편차 %.5f · "
        "outside RGB/백엔드 편차 %.5f/%.5f\n",
        dx12Capture.center[0], vkCapture.center[0],
        dx12Capture.center[1], vkCapture.center[1],
        dx12Capture.center[2], vkCapture.center[2],
        dx12Capture.center[3], vkCapture.center[3],
        dx12Capture.litPixels, vkCapture.litPixels, litDelta * 100.f,
        maxCenterDelta, maxOutsideRgb, maxOutsideDelta);
    outLog += compare;

    if (0 == dx12Capture.litPixels || 0 == vkCapture.litPixels ||
        dx12Capture.center[0] <= 0.01f || vkCapture.center[0] <= 0.01f ||
        litDelta > 0.02f || maxCenterDelta > 0.02f ||
        maxOutsideRgb > 0.001f || maxOutsideDelta > 0.001f)
    {
        passed = false;
        outLog += "Deferred 픽셀·coverage 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] MRT5 producer→9 SRV consumer·null array/cube descriptors·"
        "dynamic sampler×3·pixel CBV·fullscreen triangle · 미구현 " +
        std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();

    outLog += passed
        ? "Deferred 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "Deferred 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

namespace
{
    struct DecalRhiFixture
    {
        GBufferFixture geometry;
        FrameCameraSnapshot camera{};
        Texture* diffuse{ nullptr };
        Texture* normal{ nullptr };
        Texture* orm{ nullptr };
        std::vector<EnhancedDecalPass::Item> decals;

        DecalRhiFixture()
        {
            camera.view = math::matrix4x4::identity();
            camera.projection = math::matrix4x4::identity();
            camera.inverseView = math::matrix4x4::identity();
            camera.inverseProjection = math::matrix4x4::identity();
            camera.eyePosition = math::vector3{0.f, 0.f, -1.f};
            camera.forward = math::vector3{0.f, 0.f, 1.f};
            camera.right = math::vector3{1.f, 0.f, 0.f};
            camera.up = math::vector3{0.f, 1.f, 0.f};
            camera.nearPlane = 0.f;
            camera.farPlane = 1.f;

            const uint8_t diffusePixel[4] = { 255, 0, 0, 128 };
            const uint8_t normalPixel[4] = { 128, 128, 255, 255 };
            const uint8_t ormPixel[4] = { 0, 0, 255, 255 };
            diffuse = Texture::CreateFromPixels(1, 1, "vk_decal_diffuse",
                DXGI_FORMAT_R8G8B8A8_UNORM, diffusePixel);
            normal = Texture::CreateFromPixels(1, 1, "vk_decal_normal",
                DXGI_FORMAT_R8G8B8A8_UNORM, normalPixel);
            orm = Texture::CreateFromPixels(1, 1, "vk_decal_orm",
                DXGI_FORMAT_R8G8B8A8_UNORM, ormPixel);

            EnhancedDecalPass::Item item{};
            // 화면 중앙 24x24 안팎만 덮고, z=0.5 표면을 관통한다. 화면 가장자리의
            // GBuffer 표면은 데칼 밖 대조군으로 남는다.
            item.worldMatrix = DirectX::XMMatrixScaling(0.75f, 0.75f, 0.8f) *
                DirectX::XMMatrixTranslation(0.f, 0.f, 0.5f);
            item.diffuse = diffuse;
            item.normal = normal;
            item.occRoughMetal = orm;
            decals.push_back(item);
        }

        ~DecalRhiFixture()
        {
            delete diffuse;
            delete normal;
            delete orm;
        }

        bool IsValid() const { return diffuse && normal && orm; }
    };

    struct DecalRhiCapture
    {
        float center[3][4]{};
        float outside[3][4]{};
        uint32_t changedPixels{ 0 };
        uint32_t decals{ 0 };
        uint32_t batches{ 0 };
        EnhancedRenderGraph::Stats graph;
    };

    template <typename TResources>
    bool CaptureDecalBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        IRenderMeshCache& meshCache, IRenderTextureCache& textureCache,
        const std::function<void()>& beginCaches, DecalRhiFixture& fixture,
        DecalRhiCapture& outCapture, std::string& outError)
    {
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshCache;
        context.textureCache = &textureCache;
        context.camera = &fixture.camera;
        context.draws = &fixture.geometry.draws;
        context.width = kGeometryTestWindow;
        context.height = kGeometryTestWindow;

        EnhancedGBufferPass gbuffer;
        EnhancedDecalPass decal;
        gbuffer.SetKeepAlive(false);
        decal.SetKeepAlive(false);
        std::array<RHIReadback, 3> readbacks{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            decal.Shutdown();
            gbuffer.Shutdown();
            for (RHIReadback& readback : readbacks) resources.ReleaseReadback(readback);
            return false;
        };

        if (!gbuffer.Initialize(context, outError) || !decal.Initialize(context, outError))
            return fail(outError);
        for (RHIReadback& readback : readbacks)
        {
            if (!resources.CreateReadback(kGeometryTestWindow, kGeometryTestWindow,
                RHIFormat::RGBA16Float, 1, readback, outError)) return fail(outError);
        }

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (beginCaches) beginCaches();
        decal.SetDecals(fixture.decals);
        if (!gbuffer.PrepareFrame(context, outError) ||
            !decal.PrepareFrame(context, outError)) return fail(outError);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        gbuffer.Declare(graph, context);
        const EnhancedGBufferPass::Outputs outputs = gbuffer.GetOutputs();
        if (!outputs.diffuse.IsValid() || !outputs.normal.IsValid() ||
            !outputs.metalRough.IsValid() || !outputs.depth.IsValid())
            return fail("Decal 입력 GBuffer가 선언되지 않았다");

        decal.SetInputs(outputs);
        decal.Declare(graph, context);
        const RGHandle handles[] = { outputs.diffuse, outputs.normal, outputs.metalRough };
        graph.AddPass("DecalRHI.Readback",
            { { handles[0], RHIResourceState::CopySource },
              { handles[1], RHIResourceState::CopySource },
              { handles[2], RHIResourceState::CopySource } },
            [&, handles](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                for (uint32_t i = 0; i < 3; ++i)
                    executeContext.encoder->CopyToReadback(readbacks[i],
                        executeContext.ResolveHandle(handles[i]));
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
        outCapture.graph = graph.GetStats();
        outCapture.decals = decal.GetLastDecalCount();
        outCapture.batches = decal.GetLastBatchCount();
        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        std::array<RHIReadbackImage, 3> images;
        for (uint32_t i = 0; i < 3; ++i)
            if (!resources.MapReadback(readbacks[i], images[i], outError)) return fail(outError);

        constexpr uint32_t center = kGeometryTestWindow / 2;
        constexpr uint32_t outsideX = 12;
        for (uint32_t target = 0; target < 3; ++target)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                outCapture.center[target][channel] = images[target].At(center, center, channel);
                outCapture.outside[target][channel] = images[target].At(outsideX, center, channel);
            }
        }
        for (uint32_t y = 0; y < kGeometryTestWindow; ++y)
            for (uint32_t x = 0; x < kGeometryTestWindow; ++x)
                if (std::fabs(images[0].At(x, y, 0) - 0.25f) > 0.02f &&
                    images[0].At(x, y, 3) > 0.1f) ++outCapture.changedPixels;

        decal.Shutdown();
        gbuffer.Shutdown();
        for (RHIReadback& readback : readbacks) resources.ReleaseReadback(readback);
        return true;
    }
}

bool RunVulkanDecalTest(std::string& outLog)
{
    outLog += "── Decal 패스 — DX12/Vulkan GBuffer snapshot·depth-read·MRT blend 대조 ──\n";
    DecalRhiFixture fixture;
    if (!fixture.IsValid())
    {
        outLog += "[1/4] 합성 데칼 텍스처 생성 실패\n";
        return false;
    }

    DecalRhiCapture dx12Capture{};
    DecalRhiCapture vkCapture{};
    std::string error;
    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        DX12MeshCache meshes;
        DX12TextureCache textures;
        if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_decal.cache", error) ||
            !roots.Initialize(&resources, error) || !meshes.Initialize(&resources, error) ||
            !textures.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }
        const bool captured = CaptureDecalBackend(resources, pipelines, roots,
            meshes, textures, [&] { meshes.BeginFrame(0); textures.BeginFrame(0); },
            fixture, dx12Capture, error);
        const DX12MeshCache::Stats meshStats = meshes.GetStats();
        const DX12TextureCache::Stats textureStats = textures.GetStats();
        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        resources.WaitForGpu();
        textures.Shutdown();
        meshes.Shutdown();
        roots.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        if (!captured || 0 != problems || 1 != meshStats.uploads ||
            textureStats.uploads < 3 || 0 != textureStats.failures)
        {
            outLog += "[1/4] DX12 기준 캡처 실패: " + error + "\n" + validation;
            return false;
        }
    }
    outLog += "[1/4] DX12 기준 GBuffer→Snapshot→Apply·3채널 데칼 통과\n";

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    VulkanMeshCache meshes;
    VulkanTextureCache textures;
    if (!resources.Initialize(kGeometryTestWindow, kGeometryTestWindow, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);
    if (!meshes.Initialize(&resources, error) || !textures.Initialize(&resources, error))
    {
        textures.Shutdown();
        meshes.Shutdown();
        pipelines.Shutdown();
        resources.Shutdown();
        outLog += "[2/4] Vulkan asset cache 초기화 실패: " + error + "\n";
        return false;
    }

    bool captured = false;
    {
        ShadowSpirvScope spirv;
        captured = CaptureDecalBackend(resources, pipelines, pipelines,
            meshes, textures, [&] { meshes.BeginFrame(0); }, fixture, vkCapture, error);
    }
    const VulkanMeshCache::Stats meshStats = meshes.GetStats();
    const VulkanTextureCache::Stats textureStats = textures.GetStats();
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char line[384]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Vulkan — 실행 %u·컬링 %u·transient %u · decal/batch %u/%u · "
            "mesh upload %u/실패 %u · texture upload %u/실패 %u\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated, vkCapture.decals, vkCapture.batches,
            meshStats.uploads, meshStats.failures, textureStats.uploads,
            textureStats.failures);
        outLog += line;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        4 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        9 == vkCapture.graph.transientCreated && 1 == vkCapture.decals &&
        1 == vkCapture.batches && 1 == meshStats.uploads && 0 == meshStats.failures &&
        textureStats.uploads >= 3 && 0 == textureStats.failures;

    float maxCenterDelta = 0.f;
    float maxOutsideDelta = 0.f;
    for (uint32_t target = 0; target < 3; ++target)
    {
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            maxCenterDelta = (std::max)(maxCenterDelta, std::fabs(
                vkCapture.center[target][channel] - dx12Capture.center[target][channel]));
            maxOutsideDelta = (std::max)(maxOutsideDelta, std::fabs(
                vkCapture.outside[target][channel] - dx12Capture.outside[target][channel]));
        }
    }
    const float coverageDelta = (0 != dx12Capture.changedPixels)
        ? std::fabs(static_cast<float>(vkCapture.changedPixels) -
            static_cast<float>(dx12Capture.changedPixels)) /
            static_cast<float>(dx12Capture.changedPixels) : 1.f;

    char compare[512]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] center diffuse RGB %.4f/%.4f,%.4f/%.4f,%.4f/%.4f · "
        "normal Z %.4f/%.4f · ORM R %.4f/%.4f · changed %u/%u(편차 %.2f%%) · "
        "최대 center/outside 편차 %.5f/%.5f\n",
        dx12Capture.center[0][0], vkCapture.center[0][0],
        dx12Capture.center[0][1], vkCapture.center[0][1],
        dx12Capture.center[0][2], vkCapture.center[0][2],
        dx12Capture.center[1][2], vkCapture.center[1][2],
        dx12Capture.center[2][0], vkCapture.center[2][0],
        dx12Capture.changedPixels, vkCapture.changedPixels, coverageDelta * 100.f,
        maxCenterDelta, maxOutsideDelta);
    outLog += compare;

    // DX12 기준 자체도 데칼이 실제로 적용됐는지 확인한다. 두 백엔드가 함께
    // 아무것도 안 그려 같은 픽셀을 내는 거짓 양성을 막는다.
    if (0 == dx12Capture.changedPixels || 0 == vkCapture.changedPixels ||
        dx12Capture.center[0][0] <= 0.30f || vkCapture.center[0][0] <= 0.30f ||
        coverageDelta > 0.03f || maxCenterDelta > 0.02f || maxOutsideDelta > 0.02f)
    {
        passed = false;
        outLog += "Decal MRT blend·상자 coverage 픽셀 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] GBuffer producer→snapshot copy×3·read-only depth SRV/DSV·"
        "independent MRT blend·root instance buffer · 미구현 " +
        std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "Decal 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "Decal 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

namespace
{
    constexpr uint32_t kSsaoTestSize = 256;
    constexpr const char* kSsaoSceneShader = "SelfTest/SsaoScene.hlsl";

    struct SsaoSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        float nearZ{ 0.f };
        float farZ{ 0.f };
        float leftViewZ{ 0.f };
        float rightViewZ{ 0.f };
        uint32_t pad[2]{};
    };

    struct SsaoRhiCapture
    {
        float flatAO{ 0.f };
        float edgeAO{ 0.f };
        double rawNeighbourDiff{ 0.0 };
        double filteredNeighbourDiff{ 0.0 };
        std::vector<float> raw;
        std::vector<float> filtered;
        EnhancedRenderGraph::Stats graph;
    };

    void AnalyzeSsao(const RHIReadbackImage& image, SsaoRhiCapture& capture)
    {
        const size_t pixelCount = static_cast<size_t>(image.width) * image.height;
        capture.raw.resize(pixelCount);
        capture.filtered.resize(pixelCount);
        for (uint32_t y = 0; y < image.height; ++y)
        {
            for (uint32_t x = 0; x < image.width; ++x)
            {
                const size_t index = static_cast<size_t>(y) * image.width + x;
                capture.raw[index] = image.At(x, y, 0, 0);
                capture.filtered[index] = image.At(x, y, 0, 1);
            }
        }

        const uint32_t sampleY = image.height / 2;
        const uint32_t flatX = image.width / 8;
        const uint32_t edgeX = image.width / 2 - 2;
        capture.flatAO = capture.filtered[static_cast<size_t>(sampleY) * image.width + flatX];
        capture.edgeAO = capture.filtered[static_cast<size_t>(sampleY) * image.width + edgeX];

        double rawSum = 0.0;
        double filteredSum = 0.0;
        for (uint32_t x = 1; x < image.width; ++x)
        {
            const size_t index = static_cast<size_t>(sampleY) * image.width + x;
            rawSum += std::fabs(capture.raw[index] - capture.raw[index - 1]);
            filteredSum += std::fabs(capture.filtered[index] - capture.filtered[index - 1]);
        }
        const double denominator = static_cast<double>((std::max)(1u, image.width - 1));
        capture.rawNeighbourDiff = rawSum / denominator;
        capture.filteredNeighbourDiff = filteredSum / denominator;
    }

    template <typename TResources>
    bool CaptureSsaoBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        SsaoRhiCapture& outCapture, std::string& outError)
    {
        constexpr float kNearZ = 0.1f;
        constexpr float kFarZ = 100.f;

        FrameCameraSnapshot camera{};
        camera.view = math::matrix4x4::identity();
        camera.projection = math::perspective_fov_lh(DirectX::XM_PIDIV2, 1.f, kNearZ, kFarZ);
        camera.inverseView = math::matrix4x4::identity();
        camera.inverseProjection = math::inverse(camera.projection);

        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.width = kSsaoTestSize;
        context.height = kSsaoTestSize;
        context.camera = &camera;

        EnhancedSSAOPass ssao;
        RHIReadback readback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            ssao.Shutdown();
            resources.ReleaseReadback(readback);
            return false;
        };

        if (!ssao.Initialize(context, outError)) return fail(outError);

        const RHIPipelineLayoutParam sceneParams[] = {
            RHILayout::Cbv(0),
            RHILayout::UavTable(2, 0),
        };
        RHIPipelineLayoutDesc sceneLayoutDesc{};
        sceneLayoutDesc.params = sceneParams;
        const RHIPipelineLayoutHandle sceneLayout = roots.GetOrCreate(sceneLayoutDesc, outError);
        if (!sceneLayout.IsValid()) return fail(outError);

        RHIShaderBlob sceneBlob;
        if (!RHIShaderCompiler::CompileFile(
            kSsaoSceneShader, "CSMain", "cs_5_0", sceneBlob, outError))
            return fail(outError);
        RHIComputePipelineDesc scenePipelineDesc{};
        scenePipelineDesc.csBytecode = sceneBlob.Data();
        scenePipelineDesc.csSize = sceneBlob.Size();
        scenePipelineDesc.layout = sceneLayout;
        const RHIPipelineHandle scenePipeline =
            pipelines.GetOrCreateCompute(scenePipelineDesc, outError);
        if (!scenePipeline.IsValid()) return fail(outError);

        const uint32_t aoSize = (kSsaoTestSize + EnhancedSSAOPass::kResolutionDivisor - 1) /
            EnhancedSSAOPass::kResolutionDivisor;
        if (!resources.CreateReadback(aoSize, aoSize,
            EnhancedSSAOPass::kAOFormat, 2, readback, outError)) return fail(outError);

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (!ssao.PrepareFrame(context, outError)) return fail(outError);

        EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
        RGTextureDesc depthDesc{};
        depthDesc.width = kSsaoTestSize;
        depthDesc.height = kSsaoTestSize;
        depthDesc.format = RHIFormat::R32Float;
        depthDesc.allowUnorderedAccess = true;
        depthDesc.name = "SSAORHI.Depth";
        const RGHandle depth = graph.CreateTexture(depthDesc);

        RGTextureDesc normalDesc = depthDesc;
        normalDesc.format = RHIFormat::RGBA16Float;
        normalDesc.name = "SSAORHI.Normal";
        const RGHandle normal = graph.CreateTexture(normalDesc);

        graph.AddPass("SSAORHI.Scene",
            { { depth, RHIResourceState::UnorderedAccess },
              { normal, RHIResourceState::UnorderedAccess } },
            [&, depth, normal](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                SsaoSceneParams params{};
                params.sizeX = kSsaoTestSize;
                params.sizeY = kSsaoTestSize;
                params.nearZ = kNearZ;
                params.farZ = kFarZ;
                params.leftViewZ = 1.0f;
                params.rightViewZ = 0.6f;
                const auto constants = resources.UploadConstants(&params, sizeof(params));
                if (!constants.IsValid()) return;

                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(
                        executeContext.ResolveHandle(depth), RHIFormat::R32Float),
                    RHIBindingDesc::Uav2D(
                        executeContext.ResolveHandle(normal), RHIFormat::RGBA16Float),
                };
                const RHIBindingTable uavTable = resources.CreateBindings(uavs);
                if (!uavTable.IsValid()) return;

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, scenePipeline);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                encoder.SetBindings(RHIBindPoint::Compute, 1, uavTable);
                encoder.Dispatch((kSsaoTestSize + 7) / 8, (kSsaoTestSize + 7) / 8, 1);
            });

        EnhancedSSAOPass::Inputs inputs{};
        inputs.depth = depth;
        inputs.normal = normal;
        ssao.SetInputs(inputs);
        ssao.SetFrameIndex(0);
        ssao.Declare(graph, context);
        const RGHandle raw = ssao.GetRawOutput();
        const RGHandle filtered = ssao.GetOutput();
        if (!raw.IsValid() || !filtered.IsValid()) return fail("SSAO 출력이 선언되지 않았다");

        graph.AddPass("SSAORHI.Readback",
            { { raw, RHIResourceState::CopySource },
              { filtered, RHIResourceState::CopySource } },
            [&, raw, filtered](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback(
                    readback, executeContext.ResolveHandle(raw), 0);
                executeContext.encoder->CopyToReadback(
                    readback, executeContext.ResolveHandle(filtered), 1);
            }, true);

        if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
        outCapture.graph = graph.GetStats();
        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        RHIReadbackImage image{};
        if (!resources.MapReadback(readback, image, outError)) return fail(outError);
        AnalyzeSsao(image, outCapture);

        ssao.Shutdown();
        resources.ReleaseReadback(readback);
        return true;
    }
}

bool RunVulkanSSAOTest(std::string& outLog)
{
    outLog += "── SSAO 패스 — DX12/Vulkan depth·normal compute와 bilateral filter 대조 ──\n";
    SsaoRhiCapture dx12Capture{};
    SsaoRhiCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!resources.Initialize(kSsaoTestSize, kSsaoTestSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_ssao.cache", error) ||
            !roots.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }
        const bool captured = CaptureSsaoBackend(
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
    outLog += "[1/4] DX12 기준 scene→AO→filter→2-slice readback 통과\n";

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    if (!resources.Initialize(kSsaoTestSize, kSsaoTestSize, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);

    bool captured = false;
    {
        ShadowSpirvScope spirv;
        captured = CaptureSsaoBackend(resources, pipelines, pipelines, vkCapture, error);
    }
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Vulkan — 실행 %u·컬링 %u·transient %u · AO %ux%u\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated,
            kSsaoTestSize / EnhancedSSAOPass::kResolutionDivisor,
            kSsaoTestSize / EnhancedSSAOPass::kResolutionDivisor);
        outLog += line;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        4 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        4 == vkCapture.graph.transientCreated &&
        dx12Capture.flatAO > 0.85f && vkCapture.flatAO > 0.85f &&
        dx12Capture.edgeAO + 0.10f < dx12Capture.flatAO &&
        vkCapture.edgeAO + 0.10f < vkCapture.flatAO &&
        dx12Capture.filteredNeighbourDiff < dx12Capture.rawNeighbourDiff &&
        vkCapture.filteredNeighbourDiff < vkCapture.rawNeighbourDiff;

    float maxRawDelta = 0.f;
    float maxFilteredDelta = 0.f;
    double meanFilteredDelta = 0.0;
    if (dx12Capture.raw.size() != vkCapture.raw.size() ||
        dx12Capture.filtered.size() != vkCapture.filtered.size())
    {
        passed = false;
    }
    else
    {
        for (size_t i = 0; i < dx12Capture.raw.size(); ++i)
        {
            maxRawDelta = (std::max)(maxRawDelta,
                std::fabs(dx12Capture.raw[i] - vkCapture.raw[i]));
            const float filteredDelta =
                std::fabs(dx12Capture.filtered[i] - vkCapture.filtered[i]);
            maxFilteredDelta = (std::max)(maxFilteredDelta, filteredDelta);
            meanFilteredDelta += filteredDelta;
        }
        if (!dx12Capture.filtered.empty())
            meanFilteredDelta /= static_cast<double>(dx12Capture.filtered.size());
    }

    const float dx12Contrast = dx12Capture.flatAO - dx12Capture.edgeAO;
    const float vkContrast = vkCapture.flatAO - vkCapture.edgeAO;
    const float contrastDelta = std::fabs(dx12Contrast - vkContrast);

    char compare[512]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] AO flat %.3f/%.3f · edge %.3f/%.3f · 대비 %.3f/%.3f · filter diff "
        "%.5f→%.5f / %.5f→%.5f · 최대 raw/filtered %.5f/%.5f · 평균 %.6f\n",
        dx12Capture.flatAO, vkCapture.flatAO,
        dx12Capture.edgeAO, vkCapture.edgeAO,
        dx12Contrast, vkContrast,
        dx12Capture.rawNeighbourDiff, dx12Capture.filteredNeighbourDiff,
        vkCapture.rawNeighbourDiff, vkCapture.filteredNeighbourDiff,
        maxRawDelta, maxFilteredDelta, meanFilteredDelta);
    outLog += compare;

    // 픽셀 회전 씨앗이 sin/cos라 DXIL과 SPIR-V의 초월함수 근사 차이로 개별
    // 표본은 달라질 수 있다. 확률 패스는 exact pixel이 아니라 flat/edge 대비와
    // 필터 뒤 전체 분포를 단정한다. 허용치는 AO 한 비트(1/32) 수준의 평균 편차다.
    if (std::fabs(dx12Capture.flatAO - vkCapture.flatAO) > 0.05f ||
        std::fabs(dx12Capture.edgeAO - vkCapture.edgeAO) > 0.05f ||
        contrastDelta > 0.02f || maxRawDelta > 0.40f ||
        maxFilteredDelta > 0.15f || meanFilteredDelta > 0.04)
    {
        passed = false;
        outLog += "SSAO 대비·필터 분포 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] R32 depth+RGBA16 normal UAV→SRV·RG16 AO UAV·"
        "compute CBV/table·2-slice readback · 미구현 " + std::to_string(stubs) +
        " · Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "SSAO 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "SSAO 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

namespace
{
    constexpr uint32_t kSsgiTestSize = 64;
    constexpr uint32_t kSsgiTestFrames = 6;
    constexpr const char* kSsgiSceneShader = "SelfTest/SsgiRhiScene.hlsl";

    struct SsgiSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        uint32_t aoSizeX{ 0 };
        uint32_t aoSizeY{ 0 };
    };

    struct SsgiRhiCapture
    {
        double accumMean{ 0.0 };
        float accumMax{ 0.f };
        double resolvedNeighbourDiff{ 0.0 };
        double filteredNeighbourDiff{ 0.0 };
        double filteredGiMean{ 0.0 };
        double indirectMean{ 0.0 };
        float indirectMax{ 0.f };
        uint32_t changedPixels{ 0 };
        std::vector<float> filteredLuma;
        std::vector<float> outputLuma;
        EnhancedRenderGraph::Stats graph;
    };

    float SsgiLuma(float r, float g, float b)
    {
        return r * 0.2126f + g * 0.7152f + b * 0.0722f;
    }

    void AnalyzeSsgi(const RHIReadbackImage& giImage,
        const RHIReadbackImage& fullImage, SsgiRhiCapture& capture)
    {
        const size_t giPixels = static_cast<size_t>(giImage.width) * giImage.height;
        capture.filteredLuma.resize(giPixels);
        double accumSum = 0.0;
        uint32_t accumPixels = 0;
        double resolvedDiff = 0.0;
        double filteredDiff = 0.0;
        uint64_t neighbourCount = 0;

        for (uint32_t y = 0; y < giImage.height; ++y)
        {
            for (uint32_t x = 0; x < giImage.width; ++x)
            {
                const float resolved = SsgiLuma(giImage.At(x, y, 0, 0),
                    giImage.At(x, y, 1, 0), giImage.At(x, y, 2, 0));
                const float filtered = SsgiLuma(giImage.At(x, y, 0, 1),
                    giImage.At(x, y, 1, 1), giImage.At(x, y, 2, 1));
                const float frames = giImage.At(x, y, 3, 0);
                const size_t index = static_cast<size_t>(y) * giImage.width + x;
                capture.filteredLuma[index] = filtered;
                capture.filteredGiMean += filtered;
                if (frames > 0.f)
                {
                    accumSum += frames;
                    ++accumPixels;
                    capture.accumMax = (std::max)(capture.accumMax, frames);
                }
                if (x > 0)
                {
                    const float previousResolved = SsgiLuma(
                        giImage.At(x - 1, y, 0, 0), giImage.At(x - 1, y, 1, 0),
                        giImage.At(x - 1, y, 2, 0));
                    resolvedDiff += std::fabs(resolved - previousResolved);
                    filteredDiff += std::fabs(filtered -
                        capture.filteredLuma[index - 1]);
                    ++neighbourCount;
                }
            }
        }
        capture.accumMean = accumPixels ? accumSum / accumPixels : 0.0;
        if (giPixels) capture.filteredGiMean /= static_cast<double>(giPixels);
        if (neighbourCount)
        {
            capture.resolvedNeighbourDiff = resolvedDiff / neighbourCount;
            capture.filteredNeighbourDiff = filteredDiff / neighbourCount;
        }

        const size_t fullPixels = static_cast<size_t>(fullImage.width) * fullImage.height;
        capture.outputLuma.resize(fullPixels);
        double indirectSum = 0.0;
        for (uint32_t y = 0; y < fullImage.height; ++y)
        {
            for (uint32_t x = 0; x < fullImage.width; ++x)
            {
                const float direct = SsgiLuma(fullImage.At(x, y, 0, 0),
                    fullImage.At(x, y, 1, 0), fullImage.At(x, y, 2, 0));
                const float output = SsgiLuma(fullImage.At(x, y, 0, 1),
                    fullImage.At(x, y, 1, 1), fullImage.At(x, y, 2, 1));
                const float indirect = (std::max)(0.f, output - direct);
                capture.outputLuma[static_cast<size_t>(y) * fullImage.width + x] = output;
                indirectSum += indirect;
                capture.indirectMax = (std::max)(capture.indirectMax, indirect);
                if (indirect > 0.001f) ++capture.changedPixels;
            }
        }
        capture.indirectMean = fullPixels ? indirectSum / fullPixels : 0.0;
    }

    template <typename TResources>
    bool CaptureSsgiBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        SsgiRhiCapture& outCapture, std::string& outError)
    {
        const uint32_t giSize = kSsgiTestSize / EnhancedSSGIPass::kResolutionDivisor;

        FrameCameraSnapshot camera{};
        camera.view = math::matrix4x4::identity();
        camera.projection = math::perspective_fov_lh(DirectX::XM_PIDIV2, 1.f, 0.1f, 100.f);
        camera.inverseView = math::matrix4x4::identity();
        camera.inverseProjection = math::inverse(camera.projection);

        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.width = kSsgiTestSize;
        context.height = kSsgiTestSize;
        context.camera = &camera;

        EnhancedSSGIPass ssgi;
        RHIReadback giReadback{};
        RHIReadback fullReadback{};
        bool frameOpen = false;
        const auto fail = [&](const std::string& error)
        {
            outError = error;
            if (frameOpen) resources.AbortFrame();
            resources.WaitForGpu();
            ssgi.Shutdown();
            resources.ReleaseReadback(giReadback);
            resources.ReleaseReadback(fullReadback);
            return false;
        };

        if (!ssgi.Initialize(context, outError)) return fail(outError);

        const RHIPipelineLayoutParam sceneParams[] = {
            RHILayout::Cbv(0), RHILayout::UavTable(5, 0),
        };
        RHIPipelineLayoutDesc sceneLayoutDesc{};
        sceneLayoutDesc.params = sceneParams;
        const RHIPipelineLayoutHandle sceneLayout = roots.GetOrCreate(
            sceneLayoutDesc, outError);
        if (!sceneLayout.IsValid()) return fail(outError);

        RHIShaderBlob sceneBlob;
        if (!RHIShaderCompiler::CompileFile(kSsgiSceneShader, "CSMain", "cs_5_0",
            sceneBlob, outError)) return fail(outError);
        RHIComputePipelineDesc scenePipelineDesc{};
        scenePipelineDesc.csBytecode = sceneBlob.Data();
        scenePipelineDesc.csSize = sceneBlob.Size();
        scenePipelineDesc.layout = sceneLayout;
        const RHIPipelineHandle scenePipeline = pipelines.GetOrCreateCompute(
            scenePipelineDesc, outError);
        if (!scenePipeline.IsValid()) return fail(outError);

        if (!resources.CreateReadback(giSize, giSize, EnhancedSSGIPass::kGIFormat,
                2, giReadback, outError) ||
            !resources.CreateReadback(kSsgiTestSize, kSsgiTestSize,
                EnhancedSSGIPass::kGIFormat, 2, fullReadback, outError))
            return fail(outError);

        for (uint32_t frame = 0; frame < kSsgiTestFrames; ++frame)
        {
            if (!resources.BeginFrame(outError)) return fail(outError);
            frameOpen = true;
            if (!ssgi.PrepareFrame(context, outError)) return fail(outError);

            EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(resources));
            RGTextureDesc fullDesc{};
            fullDesc.width = kSsgiTestSize;
            fullDesc.height = kSsgiTestSize;
            fullDesc.allowUnorderedAccess = true;

            fullDesc.format = RHIFormat::R32Float;
            fullDesc.name = "SSGIRHI.Depth";
            const RGHandle depth = graph.CreateTexture(fullDesc);
            fullDesc.format = RHIFormat::RGBA16Float;
            fullDesc.name = "SSGIRHI.Normal";
            const RGHandle normal = graph.CreateTexture(fullDesc);
            fullDesc.name = "SSGIRHI.Lighting";
            const RGHandle lighting = graph.CreateTexture(fullDesc);
            fullDesc.name = "SSGIRHI.Diffuse";
            const RGHandle diffuse = graph.CreateTexture(fullDesc);

            RGTextureDesc aoDesc{};
            aoDesc.width = giSize;
            aoDesc.height = giSize;
            aoDesc.format = RHIFormat::RG16Float;
            aoDesc.allowUnorderedAccess = true;
            aoDesc.name = "SSGIRHI.AO";
            const RGHandle ao = graph.CreateTexture(aoDesc);

            graph.AddPass("SSGIRHI.Scene",
                { { depth, RHIResourceState::UnorderedAccess },
                  { normal, RHIResourceState::UnorderedAccess },
                  { lighting, RHIResourceState::UnorderedAccess },
                  { diffuse, RHIResourceState::UnorderedAccess },
                  { ao, RHIResourceState::UnorderedAccess } },
                [&, depth, normal, lighting, diffuse, ao]
                (const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    SsgiSceneParams params{};
                    params.sizeX = kSsgiTestSize;
                    params.sizeY = kSsgiTestSize;
                    params.aoSizeX = giSize;
                    params.aoSizeY = giSize;
                    const auto constants = resources.UploadConstants(&params, sizeof(params));
                    if (!constants.IsValid()) return;

                    const RHIBindingDesc uavs[] = {
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(depth),
                            RHIFormat::R32Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(normal),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(lighting),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(diffuse),
                            RHIFormat::RGBA16Float),
                        RHIBindingDesc::Uav2D(executeContext.ResolveHandle(ao),
                            RHIFormat::RG16Float),
                    };
                    const RHIBindingTable uavTable = resources.CreateBindings(uavs);
                    if (!uavTable.IsValid()) return;

                    RHIEncoder& encoder = *executeContext.encoder;
                    encoder.SetPipeline(RHIBindPoint::Compute, scenePipeline);
                    encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, constants);
                    encoder.SetBindings(RHIBindPoint::Compute, 1, uavTable);
                    encoder.Dispatch((kSsgiTestSize + 7) / 8,
                        (kSsgiTestSize + 7) / 8, 1);
                });

            EnhancedSSGIPass::Inputs inputs{};
            inputs.depth = depth;
            inputs.normal = normal;
            inputs.diffuse = diffuse;
            inputs.lighting = lighting;
            inputs.ambientOcclusion = ao;
            ssgi.SetInputs(inputs);
            ssgi.Declare(graph, context);

            const RGHandle resolved = ssgi.GetResolvedResult();
            const RGHandle filtered = ssgi.GetFilteredResult();
            const RGHandle output = ssgi.GetOutput();
            if (!resolved.IsValid() || !filtered.IsValid() || !output.IsValid())
                return fail("SSGI 출력이 선언되지 않았다");

            if (frame + 1 == kSsgiTestFrames)
            {
                graph.AddPass("SSGIRHI.Readback",
                    { { resolved, RHIResourceState::CopySource },
                      { filtered, RHIResourceState::CopySource },
                      { lighting, RHIResourceState::CopySource },
                      { output, RHIResourceState::CopySource } },
                    [&, resolved, filtered, lighting, output]
                    (const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {
                        RHIEncoder& encoder = *executeContext.encoder;
                        encoder.CopyToReadback(giReadback,
                            executeContext.ResolveHandle(resolved), 0);
                        encoder.CopyToReadback(giReadback,
                            executeContext.ResolveHandle(filtered), 1);
                        encoder.CopyToReadback(fullReadback,
                            executeContext.ResolveHandle(lighting), 0);
                        encoder.CopyToReadback(fullReadback,
                            executeContext.ResolveHandle(output), 1);
                    }, true);
            }

            if (!graph.Compile(outError) || !graph.Execute(outError)) return fail(outError);
            if (frame + 1 == kSsgiTestFrames) outCapture.graph = graph.GetStats();
            if (!resources.EndFrame(outError)) return fail(outError);
            frameOpen = false;
            resources.WaitForGpu();
        }

        RHIReadbackImage giImage{};
        RHIReadbackImage fullImage{};
        if (!resources.MapReadback(giReadback, giImage, outError) ||
            !resources.MapReadback(fullReadback, fullImage, outError))
            return fail(outError);
        AnalyzeSsgi(giImage, fullImage, outCapture);

        ssgi.Shutdown();
        resources.ReleaseReadback(giReadback);
        resources.ReleaseReadback(fullReadback);
        return true;
    }
}

bool RunVulkanSSGITest(std::string& outLog)
{
    outLog += "── SSGI 패스 — DX12/Vulkan Hi-Z·temporal·filter·composite 대조 ──\n";
    SsgiRhiCapture dx12Capture{};
    SsgiRhiCapture vkCapture{};
    std::string error;

    {
        DX12DeviceResources resources;
        DX12PSOManager pipelines;
        DX12RootSignatureCache roots;
        if (!resources.Initialize(kSsgiTestSize, kSsgiTestSize, error) ||
            !pipelines.Initialize(&resources, L"dx12_vk_ssgi.cache", error) ||
            !roots.Initialize(&resources, error))
        {
            outLog += "[1/4] DX12 기준 초기화 실패: " + error + "\n";
            return false;
        }
        const bool captured = CaptureSsgiBackend(
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
    outLog += "[1/4] DX12 기준 6프레임 Hi-Z→누적→filter→composite 통과\n";

    if (!VulkanApi::LoadLoader(error))
    {
        outLog += "[2/4] Vulkan 로더 없음: " + error + "\n";
        return false;
    }

    VulkanDeviceResources resources;
    VulkanPipelineCache pipelines;
    if (!resources.Initialize(kSsgiTestSize, kSsgiTestSize, true, error))
    {
        outLog += "[2/4] Vulkan 초기화 실패: " + error + "\n";
        return false;
    }
    pipelines.Initialize(resources.GetDevice());
    resources.SetPipelineCache(&pipelines);

    bool captured = false;
    {
        ShadowSpirvScope spirv;
        captured = CaptureSsgiBackend(
            resources, pipelines, pipelines, vkCapture, error);
    }
    const uint32_t stubs = resources.GetUnimplementedCount() +
        resources.GetEncoderUnimplementedCount();
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);

    if (captured)
    {
        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Vulkan — 실행 %u·컬링 %u·transient %u · 6-frame temporal\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated);
        outLog += line;
    }

    bool passed = captured && 0 == stubs && 0 == problems &&
        13 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        15 == vkCapture.graph.transientCreated;

    const auto semanticPass = [](const SsgiRhiCapture& capture)
    {
        return capture.accumMean > 3.0 && capture.accumMax >= 5.0f &&
            capture.filteredGiMean > 0.001 && capture.indirectMean > 0.0001 &&
            capture.indirectMax > 0.005f && capture.changedPixels > 16 &&
            capture.filteredNeighbourDiff <= capture.resolvedNeighbourDiff * 1.05;
    };
    if (!semanticPass(dx12Capture) || !semanticPass(vkCapture)) passed = false;

    double meanFilteredDelta = 0.0;
    float maxFilteredDelta = 0.f;
    double meanOutputDelta = 0.0;
    float maxOutputDelta = 0.f;
    if (dx12Capture.filteredLuma.size() != vkCapture.filteredLuma.size() ||
        dx12Capture.outputLuma.size() != vkCapture.outputLuma.size())
    {
        passed = false;
    }
    else
    {
        for (size_t i = 0; i < dx12Capture.filteredLuma.size(); ++i)
        {
            const float delta = std::fabs(
                dx12Capture.filteredLuma[i] - vkCapture.filteredLuma[i]);
            meanFilteredDelta += delta;
            maxFilteredDelta = (std::max)(maxFilteredDelta, delta);
        }
        if (!dx12Capture.filteredLuma.empty())
            meanFilteredDelta /= dx12Capture.filteredLuma.size();
        for (size_t i = 0; i < dx12Capture.outputLuma.size(); ++i)
        {
            const float delta = std::fabs(
                dx12Capture.outputLuma[i] - vkCapture.outputLuma[i]);
            meanOutputDelta += delta;
            maxOutputDelta = (std::max)(maxOutputDelta, delta);
        }
        if (!dx12Capture.outputLuma.empty())
            meanOutputDelta /= dx12Capture.outputLuma.size();
    }

    char compare[640]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] 누적 평균/최대 %.2f/%.1f · %.2f/%.1f · GI 평균 %.5f/%.5f · "
        "filter diff %.5f→%.5f / %.5f→%.5f · 간접광 평균/최대 %.5f/%.4f · "
        "%.5f/%.4f · 변경 %u/%u · 평균 GI/output 편차 %.5f/%.5f\n",
        dx12Capture.accumMean, dx12Capture.accumMax,
        vkCapture.accumMean, vkCapture.accumMax,
        dx12Capture.filteredGiMean, vkCapture.filteredGiMean,
        dx12Capture.resolvedNeighbourDiff, dx12Capture.filteredNeighbourDiff,
        vkCapture.resolvedNeighbourDiff, vkCapture.filteredNeighbourDiff,
        dx12Capture.indirectMean, dx12Capture.indirectMax,
        vkCapture.indirectMean, vkCapture.indirectMax,
        dx12Capture.changedPixels, vkCapture.changedPixels,
        meanFilteredDelta, meanOutputDelta);
    outLog += compare;

    // 해시 방향의 sin/cos 근사는 DXIL과 SPIR-V에서 조금 다르다. 개별 최대값보다
    // 시간 누적·필터·최종 합성의 전체 분포를 주 판정으로 삼는다.
    if (std::fabs(dx12Capture.accumMean - vkCapture.accumMean) > 0.75 ||
        std::fabs(dx12Capture.filteredGiMean - vkCapture.filteredGiMean) > 0.08 ||
        std::fabs(dx12Capture.indirectMean - vkCapture.indirectMean) > 0.04 ||
        meanFilteredDelta > 0.10 || meanOutputDelta > 0.08 ||
        maxFilteredDelta > 1.5f || maxOutputDelta > 1.5f)
    {
        passed = false;
        outLog += "SSGI temporal·filter·composite 분포 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] R32 Hi-Z·RGBA16 temporal history·RG16 AO·persistent import/copy·"
        "2-scale readback · 미구현 " + std::to_string(stubs) +
        " · Vulkan validation " + std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    pipelines.Shutdown();
    resources.Shutdown();
    outLog += passed
        ? "SSGI 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "SSGI 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}
