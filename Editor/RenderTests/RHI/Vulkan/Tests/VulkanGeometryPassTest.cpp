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
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Render/Passes/Lighting/EnhancedSSAOPass.h"
#include "Render/Passes/Lighting/EnhancedSSGIPass.h"
#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include "FrameCameraSnapshot.h"
#include "DataSystem.h"
#include "Material.h"
#include "Mesh.h"
#include "PrimitiveRenderProxy.h"
#include "Texture.h"
#include "ShaderMeta.h"
#include "ShaderMetaReflection.h"
#include "ShaderPermutationDomain.h"
#include "StandardMaterialProperty.h"
#include "RHI/RHIShaderSource.h"
#include <mathematics/transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <span>
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
            const math::vector3& origin, const math::vector3& axisU,
            const math::vector3& axisV)
        {
            const uint32 base = static_cast<uint32>(vertices.size());
            const math::vector3 points[] = {
                origin, origin + axisU, origin + axisU + axisV, origin + axisV };
            for (const math::vector3& point : points)
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

            const math::vector3 eye{0.f, 18.f, -24.f};
            const math::vector3 at{0.f, 0.f, 16.f};
            const math::vector3 up = math::vector3::unit_y();
            camera.view = math::look_at_lh(eye, at, up);
            camera.projection = math::perspective_fov_lh(
                math::pi / 3.f, 1.f, 0.1f, 180.f);
            camera.inverseView = math::inverse(camera.view);
            camera.inverseProjection = math::inverse(camera.projection);
            camera.eyePosition = eye;
            camera.forward = math::normalize(at - eye);
            camera.right = math::normalize(math::cross(up, camera.forward));
            camera.up = math::cross(camera.forward, camera.right);
            camera.fov = 60.f;
            camera.nearPlane = 0.1f;
            camera.farPlane = 180.f;

            EnhancedDrawItem groundDraw{};
            groundDraw.mesh = ground.get();
            groundDraw.worldMatrix = math::matrix4x4::identity();
            draws.push_back(groundDraw);

            EnhancedDrawItem blockerDraw{};
            blockerDraw.mesh = blocker.get();
            blockerDraw.worldMatrix = math::matrix4x4::identity();
            draws.push_back(blockerDraw);

            EnhancedLight sun{};
            sun.position = math::vector4(0.f, 0.f, 0.f, 0.f);
            const math::vector3 sunDirection = math::normalize(
                math::vector3{ 0.65f, -1.f, 0.25f });
            sun.direction = math::vector4{
                sunDirection.x, sunDirection.y, sunDirection.z, 0.f };
            sun.color = math::color(1.f, 1.f, 1.f, 4.f);
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
        float second[4][4]{};
        float third[4][4]{};
        float fourth[4][4]{};
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
        RHIPipelineHandle previousPipeline{};
        RHIPipelineHandle activePipeline{};
        RHIPipelineHandle alternatePipeline{};
        RHIPipelineHandle secondaryMetaPipeline{};
        RHIPipelineHandle retiredSecondaryMetaPipeline{};
        RHIPipelineHandle retiredVariantPipeline{};
        ShaderMetaHandle shaderMetaHandle{};
        ShaderMetaHandle secondaryMetaHandle{};
        uint32_t shaderVariants{};
        bool previousPipelineStale{ false };
        bool retiredVariantStale{ false };
        bool rejectedReloadPreserved{ false };
        bool rejectedMaterialPreserved{ false };
        bool rejectedTextureBindingPreserved{ false };
        bool rejectedPermutationPreserved{ false };
        bool rejectedSecondaryMetaPreserved{ false };
        bool secondaryMetaSurvivedPrimaryReload{ false };
        bool secondaryMetaGenerationRetired{ false };
        bool secondaryMetaFrameRetired{ false };
    };

    struct GBufferFixture
    {
        std::vector<Vertex> vertices;
        std::vector<uint32> indices{ 0, 1, 2, 0, 2, 3 };
        std::unique_ptr<Mesh> mesh;
        std::vector<EnhancedDrawItem> draws;

        GBufferFixture()
        {
        const math::vector3 positions[] = {
                { -0.25f, -0.75f, 0.5f }, { -0.25f, 0.75f, 0.5f },
                { 0.25f, 0.75f, 0.5f }, { 0.25f, -0.75f, 0.5f } };
        const math::vector2 uvs[] = {
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
            draw.worldMatrix = math::matrix4x4::identity();
            draw.baseColorFactor = math::color(0.25f, 0.5f, 0.75f, 1.f);
            draw.metallic = 0.2f;
            draw.roughness = 0.6f;
            draw.useNormalMap = 1;
            draws.push_back(draw);
        }
    };

    bool PrepareStandardMaterialProbe(ShaderMetaHandle& outHandle,
        ShaderMeta& outMeta, Material& outMaterial, std::string& outError)
    {
        const std::filesystem::path metaPath = RHIShaderSource::Resolve(
            "GBuffer.shadermeta");
        const FileGuid guid = DataSystems->GetFileGuid(metaPath);
        if (FileGuid{} == guid)
        {
            outError = "제품 GBuffer catalog GUID를 찾지 못했다";
            return false;
        }

        outHandle = DataSystems->LoadShaderMetaHandle(guid, outError);
        const std::shared_ptr<const ShaderMeta> snapshot =
            DataSystems->ResolveShaderMeta(outHandle);
        if (!outHandle.IsValid() || !snapshot)
        {
            if (outError.empty()) outError = "제품 GBuffer generation resolve 실패";
            return false;
        }
        outMeta = *snapshot;

        ShaderMetaPermutation permutation;
        constexpr std::array<std::uint16_t, 1> fullQuality{ 0 };
        if (!ShaderPermutationDomain::Resolve(outMeta, 0,
                fullQuality, permutation, outError))
            return false;

        if (1 != outMeta.keywords.size()
            || outMeta.keywords[0].name != "SHADING_QUALITY"
            || outMeta.keywords[0].values
                != std::vector<std::string>{ "full", "reduced" })
        {
            outError = "제품 GBuffer SHADING_QUALITY permutation 축 불일치";
            return false;
        }

        std::error_code pathError;
        const std::filesystem::path sourcePath = outMeta.ResolveSource(metaPath);
        const std::filesystem::path relativeSource = std::filesystem::relative(
            sourcePath, RHIShaderSource::Resolve(""), pathError);
        if (pathError || relativeSource.empty())
        {
            outError = "제품 GBuffer source 상대 경로 계산 실패";
            return false;
        }

        std::vector<RHIShaderReflection> dxilStages;
        std::vector<RHIShaderReflection> spirvStages;
        const ShaderPassDesc& pass = outMeta.passes[0];
        const std::pair<const ShaderStageEntry*, const char*> stages[] = {
            { pass.vertex ? &*pass.vertex : nullptr, "vs_5_0" },
            { pass.pixel ? &*pass.pixel : nullptr, "ps_5_0" },
        };
        for (const auto& [stage, profile] : stages)
        {
            if (nullptr == stage)
            {
                outError = "제품 GBuffer는 VS+PS여야 한다";
                return false;
            }

            RHIShaderReflection dxil;
            RHIShaderReflection spirv;
            const std::string sourceName = relativeSource.generic_string();
            if (!RHIShaderCompiler::ReflectFile(sourceName, stage->entry, profile,
                    RHIShaderBinary::Dxil, permutation.defines, dxil, outError)
                || !RHIShaderCompiler::ReflectFile(sourceName, stage->entry, profile,
                    RHIShaderBinary::SpirV, permutation.defines, spirv, outError)
                || !AreShaderReflectionsEquivalent(dxil, spirv, outError))
                return false;
            dxilStages.push_back(std::move(dxil));
            spirvStages.push_back(std::move(spirv));
        }

        ShaderMetaBindingLayout layout;
        if (!ShaderMetaReflection::Resolve(outMeta, dxilStages, layout, outError))
            return false;

        constexpr std::size_t kNumericPropertyCount = 7;
        const std::string_view expectedNames[kNumericPropertyCount] = {
            standard_material::property::BaseColor,
            standard_material::property::Metallic,
            standard_material::property::Roughness,
            standard_material::property::NormalScale,
            standard_material::property::OcclusionStrength,
            standard_material::property::Emissive,
            standard_material::property::AlphaCutoff,
        };
        constexpr std::uint32_t expectedOffsets[kNumericPropertyCount] = {
            0, 16, 20, 24, 28, 32, 44,
        };
        constexpr std::array<std::string_view, 4> expectedTextures{
            standard_material::property::BaseColorMap,
            standard_material::property::NormalMap,
            standard_material::property::OrmMap,
            standard_material::property::EmissiveMap,
        };
        bool layoutMatches = "MaterialProperties" == layout.constantBufferName
            && 2 == layout.constantBufferRegister
            && 0 == layout.constantBufferSpace
            && 48 == layout.constantBufferByteSize
            && kNumericPropertyCount + expectedTextures.size()
                == layout.properties.size();
        for (std::size_t i = 0; layoutMatches && i < kNumericPropertyCount; ++i)
        {
            layoutMatches = expectedNames[i] == layout.properties[i].name
                && expectedOffsets[i] == layout.properties[i].byteOffset
                && RHIShaderResourceKind::ConstantBuffer ==
                    layout.properties[i].resourceKind;
        }
        for (std::size_t i = 0; layoutMatches && i < expectedTextures.size(); ++i)
        {
            const ShaderMetaPropertyBinding& binding =
                layout.properties[kNumericPropertyCount + i];
            layoutMatches = expectedTextures[i] == binding.name
                && ShaderPropertyType::Texture2D == binding.propertyType
                && RHIShaderResourceKind::Texture == binding.resourceKind
                && i == binding.registerIndex && 0 == binding.registerSpace;
        }
        if (!layoutMatches)
        {
            outError = "제품 GBuffer b2/texture reflection layout 불일치";
            return false;
        }

        if (!outMaterial.ConfigureShaderProperties(outMeta, layout, outError, outHandle)
            || !outMaterial.TrySetVector("MaterialProperties", "baseColor",
                math::vector4{ 0.125f, 0.25f, 0.5f, 1.0f })
            || !outMaterial.TrySetFloat("MaterialProperties", "metallic", 0.75f)
            || !outMaterial.TrySetFloat("MaterialProperties", "roughness", 0.625f)
            || !outMaterial.TrySetFloat("MaterialProperties", "normalScale", 0.5f)
            || !outMaterial.TrySetFloat("MaterialProperties", "occlusionStrength", 0.375f)
            || !outMaterial.TrySetVector("MaterialProperties", "emissive",
                math::vector3{ 0.0625f, 0.125f, 0.25f })
            || !outMaterial.TrySetFloat("MaterialProperties", "alphaCutoff", 0.875f)
            || 48 != outMaterial.GetConstantBufferData().size())
        {
            if (outError.empty()) outError = "제품 GBuffer Standard property pack 실패";
            return false;
        }
        return true;
    }

    template <typename TResources>
    bool CaptureGBufferBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        IRenderMeshCache& meshCache, IRenderTextureCache& textureCache,
        const std::function<void()>& beginCaches, GBufferFixture& fixture,
        const ShaderMeta& materialProbeMeta, ShaderMetaHandle materialProbeHandle,
        const ShaderMeta& secondaryMeta, ShaderMetaHandle secondaryMetaHandle,
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

        // C3b2b2 대표 pass 전환. bootstrap은 lessEqual, catalog 제품 meta는
        // less라 PSO key가 반드시 갈린다. 현재 depth=0.5, clear=1이라 픽셀은
        // 동일하고 다음 draw가 새 handle로 같은 MRT를 내야 한다.
        ShaderMeta shaderMeta{};
        shaderMeta.guid = FileGuid{ "30000000-0000-4000-8000-000000000003" };
        shaderMeta.name = "GBufferReloadProbe";
        shaderMeta.source = "GBuffer.hlsl";
        ShaderPassDesc shaderPass{};
        shaderPass.name = "GBuffer";
        shaderPass.vertex = ShaderStageEntry{ "VSMain" };
        shaderPass.pixel = ShaderStageEntry{ "PSMain" };
        shaderPass.state.cullMode = RHICullMode::None;
        shaderPass.state.depthTest = RHICompareOp::LessEqual;
        shaderPass.queue = ShaderPassQueue::Opaque;
        shaderMeta.passes.push_back(shaderPass);

        constexpr ShaderMetaHandle bootstrapHandle{ 0xFFFFFFFEu, 1u };
        if (!gbuffer.ApplyShaderMeta(context, bootstrapHandle, shaderMeta,
                {}, outError)) return fail(outError);
        outCapture.previousPipeline = gbuffer.GetPipelineHandle();

        if (!gbuffer.ApplyShaderMeta(context, materialProbeHandle, materialProbeMeta,
                RHICompletionPoint{ resources.GetLastSignaledFenceValue() }, outError))
            return fail(outError);
        outCapture.activePipeline = gbuffer.GetPipelineHandle();
        outCapture.shaderMetaHandle = gbuffer.GetShaderMetaHandle();

        for (const EnhancedDrawItem& draw : fixture.draws)
        {
            if (!draw.materialSnapshot) continue;
            const bool usesSecondaryMeta =
                draw.materialSnapshot->shaderMetaHandle == secondaryMetaHandle;
            const ShaderMeta& drawMeta = usesSecondaryMeta
                ? secondaryMeta : materialProbeMeta;
            const ShaderMetaHandle drawMetaHandle = usesSecondaryMeta
                ? secondaryMetaHandle : materialProbeHandle;
            RHIShaderPermutationKey resolvedKey{};
            std::shared_ptr<const ShaderMetaBindingLayout> resolvedLayout;
            if (!gbuffer.EnsureShaderMetaVariant(context, drawMetaHandle,
                    drawMeta, draw.materialSnapshot->keywordSelections,
                    resolvedKey, resolvedLayout, outError))
            {
                return fail(outError);
            }
            if (resolvedKey != draw.materialSnapshot->permutationKey
                || !resolvedLayout
                || *resolvedLayout != draw.materialSnapshot->bindingLayout)
            {
                return fail("material keyword variant identity/layout 불일치");
            }
        }
        outCapture.shaderVariants = gbuffer.GetShaderVariantCount();
        if (fixture.draws.size() >= 3 && fixture.draws[2].materialSnapshot)
        {
            outCapture.alternatePipeline = gbuffer.GetShaderVariantPipeline(
                materialProbeHandle,
                fixture.draws[2].materialSnapshot->permutationKey);
        }
        if (fixture.draws.size() >= 4 && fixture.draws[3].materialSnapshot)
        {
            outCapture.secondaryMetaHandle = secondaryMetaHandle;
            outCapture.secondaryMetaPipeline = gbuffer.GetShaderVariantPipeline(
                secondaryMetaHandle,
                fixture.draws[3].materialSnapshot->permutationKey);
        }
        const std::array<ShaderMetaHandle, 2> activeMetaHandles{
            materialProbeHandle, secondaryMetaHandle };
        if (0 != gbuffer.CommitShaderMetaFrame(context, activeMetaHandles,
                RHICompletionPoint{ resources.GetLastSignaledFenceValue() }))
        {
            return fail("active multi ShaderMeta frame이 variant를 조기 retire했다");
        }

        ShaderMeta invalidMeta = materialProbeMeta;
        invalidMeta.passes[0].name = "NotGBuffer";
        std::string rejectedError;
        const ShaderMetaHandle invalidHandle{
            materialProbeHandle.slot, materialProbeHandle.generation + 1u };
        const bool rejected = !gbuffer.ApplyShaderMeta(context,
            invalidHandle, invalidMeta,
            RHICompletionPoint{ resources.GetLastSignaledFenceValue() }, rejectedError);
        outCapture.rejectedReloadPreserved = rejected && !rejectedError.empty() &&
            outCapture.activePipeline == gbuffer.GetPipelineHandle() &&
            outCapture.shaderMetaHandle == gbuffer.GetShaderMetaHandle() &&
            outCapture.alternatePipeline == gbuffer.GetShaderVariantPipeline(
                materialProbeHandle,
                fixture.draws[2].materialSnapshot->permutationKey) &&
            outCapture.secondaryMetaPipeline == gbuffer.GetShaderVariantPipeline(
                secondaryMetaHandle,
                fixture.draws[3].materialSnapshot->permutationKey);

        ShaderMeta invalidSecondaryMeta = secondaryMeta;
        invalidSecondaryMeta.passes[0].name = "NotGBuffer";
        const ShaderMetaHandle invalidSecondaryHandle{
            secondaryMetaHandle.slot, secondaryMetaHandle.generation + 1u };
        RHIShaderPermutationKey rejectedSecondaryKey{};
        std::shared_ptr<const ShaderMetaBindingLayout> rejectedSecondaryLayout;
        std::string rejectedSecondaryError;
        const bool secondaryRejected = !gbuffer.EnsureShaderMetaVariant(context,
            invalidSecondaryHandle, invalidSecondaryMeta,
            fixture.draws[3].materialSnapshot->keywordSelections,
            rejectedSecondaryKey, rejectedSecondaryLayout,
            rejectedSecondaryError);
        outCapture.rejectedSecondaryMetaPreserved = secondaryRejected
            && !rejectedSecondaryError.empty()
            && outCapture.secondaryMetaPipeline == gbuffer.GetShaderVariantPipeline(
                secondaryMetaHandle,
                fixture.draws[3].materialSnapshot->permutationKey)
            && 3 == gbuffer.GetShaderVariantCount();
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

        const std::shared_ptr<const EnhancedMaterialDrawSnapshot> validMaterial =
            fixture.draws[0].materialSnapshot;
        auto invalidMaterial = std::make_shared<EnhancedMaterialDrawSnapshot>(
            *validMaterial);
        ++invalidMaterial->shaderMetaHandle.generation;
        fixture.draws[0].materialSnapshot = invalidMaterial;
        std::string materialError;
        const bool materialRejected = !gbuffer.PrepareFrame(context, materialError);
        fixture.draws[0].materialSnapshot = validMaterial;
        outCapture.rejectedMaterialPreserved = materialRejected
            && !materialError.empty()
            && outCapture.activePipeline == gbuffer.GetPipelineHandle()
            && outCapture.shaderMetaHandle == gbuffer.GetShaderMetaHandle();
        if (!outCapture.rejectedMaterialPreserved)
            return fail("invalid material snapshot이 fail-closed되지 않았다");

        auto invalidTextureBinding =
            std::make_shared<EnhancedMaterialDrawSnapshot>(*validMaterial);
        invalidTextureBinding->textureBindings[0].registerIndex = 1;
        fixture.draws[0].materialSnapshot = invalidTextureBinding;
        std::string textureBindingError;
        const bool textureBindingRejected =
            !gbuffer.PrepareFrame(context, textureBindingError);
        fixture.draws[0].materialSnapshot = validMaterial;
        outCapture.rejectedTextureBindingPreserved = textureBindingRejected
            && !textureBindingError.empty()
            && outCapture.activePipeline == gbuffer.GetPipelineHandle()
            && outCapture.shaderMetaHandle == gbuffer.GetShaderMetaHandle();
        if (!outCapture.rejectedTextureBindingPreserved)
            return fail("invalid texture register snapshot이 fail-closed되지 않았다");

        auto invalidPermutation =
            std::make_shared<EnhancedMaterialDrawSnapshot>(*validMaterial);
        ++invalidPermutation->permutationKey.lo;
        fixture.draws[0].materialSnapshot = invalidPermutation;
        std::string permutationError;
        const bool permutationRejected =
            !gbuffer.PrepareFrame(context, permutationError);
        fixture.draws[0].materialSnapshot = validMaterial;
        outCapture.rejectedPermutationPreserved = permutationRejected
            && !permutationError.empty()
            && outCapture.activePipeline == gbuffer.GetPipelineHandle()
            && 3 == gbuffer.GetShaderVariantCount();
        if (!outCapture.rejectedPermutationPreserved)
            return fail("invalid permutation snapshot이 fail-closed되지 않았다");

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

        constexpr uint32_t center = kGeometryTestWindow / 8;
        constexpr uint32_t second = kGeometryTestWindow * 3 / 8;
        constexpr uint32_t third = kGeometryTestWindow * 5 / 8;
        constexpr uint32_t fourth = kGeometryTestWindow * 7 / 8;
        constexpr uint32_t outside = 2;
        for (uint32_t target = 0; target < 4; ++target)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                outCapture.center[target][channel] =
                    images[target].At(center, center, channel);
                outCapture.second[target][channel] =
                    images[target].At(second, center, channel);
                outCapture.third[target][channel] =
                    images[target].At(third, center, channel);
                outCapture.fourth[target][channel] =
                    images[target].At(fourth, center, channel);
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

        outCapture.retiredVariantPipeline = outCapture.alternatePipeline;
        const ShaderMetaHandle nextGeneration{
            materialProbeHandle.slot, materialProbeHandle.generation + 2u };
        if (!gbuffer.ApplyShaderMeta(context, nextGeneration, materialProbeMeta,
                RHICompletionPoint{ resources.GetLastSignaledFenceValue() }, outError))
        {
            return fail(outError);
        }
        outCapture.retiredVariantStale = outCapture.retiredVariantPipeline.IsValid()
            && !gbuffer.GetShaderVariantPipeline(materialProbeHandle,
                fixture.draws[2].materialSnapshot->permutationKey).IsValid()
            && 2 == gbuffer.GetShaderVariantCount();
        outCapture.secondaryMetaSurvivedPrimaryReload =
            outCapture.secondaryMetaPipeline == gbuffer.GetShaderVariantPipeline(
                secondaryMetaHandle,
                fixture.draws[3].materialSnapshot->permutationKey);

        const ShaderMetaHandle nextSecondaryGeneration{
            secondaryMetaHandle.slot, secondaryMetaHandle.generation + 2u };
        RHIShaderPermutationKey nextSecondaryKey{};
        std::shared_ptr<const ShaderMetaBindingLayout> nextSecondaryLayout;
        if (!gbuffer.EnsureShaderMetaVariant(context, nextSecondaryGeneration,
                secondaryMeta,
                fixture.draws[3].materialSnapshot->keywordSelections,
                nextSecondaryKey, nextSecondaryLayout, outError))
        {
            return fail(outError);
        }
        const std::array<ShaderMetaHandle, 2> nextActiveMetaHandles{
            nextGeneration, nextSecondaryGeneration };
        const std::uint32_t generationRetired = gbuffer.CommitShaderMetaFrame(
            context, nextActiveMetaHandles,
            RHICompletionPoint{ resources.GetLastSignaledFenceValue() });
        outCapture.secondaryMetaGenerationRetired = 1 == generationRetired
            && !gbuffer.GetShaderVariantPipeline(secondaryMetaHandle,
                fixture.draws[3].materialSnapshot->permutationKey).IsValid()
            && outCapture.secondaryMetaPipeline
                == gbuffer.GetShaderVariantPipeline(nextSecondaryGeneration,
                    nextSecondaryKey)
            && 2 == gbuffer.GetShaderVariantCount();

        const std::array<ShaderMetaHandle, 1> primaryOnly{ nextGeneration };
        outCapture.retiredSecondaryMetaPipeline =
            gbuffer.GetShaderVariantPipeline(nextSecondaryGeneration,
                nextSecondaryKey);
        const std::uint32_t frameRetired = gbuffer.CommitShaderMetaFrame(
            context, primaryOnly,
            RHICompletionPoint{ resources.GetLastSignaledFenceValue() });
        outCapture.secondaryMetaFrameRetired = 1 == frameRetired
            && !gbuffer.GetShaderVariantPipeline(nextSecondaryGeneration,
                nextSecondaryKey).IsValid()
            && 1 == gbuffer.GetShaderVariantCount();

        gbuffer.Shutdown();
        for (RHIReadback& readback : readbacks) resources.ReleaseReadback(readback);
        return true;
    }

    struct ForwardCapture
    {
        float swatches[3][4]{};
        float overlap[4]{};
        float customBefore[4]{};
        float customAfter[4]{};
        float outside[4]{};
        uint32_t writtenPixels{ 0 };
        uint32_t centerTileLights{ 0 };
        uint32_t cornerTileLights{ 0 };
        uint32_t minTileLights{ UINT32_MAX };
        uint32_t maxTileLights{ 0 };
        uint32_t draws{ 0 };
        uint32_t batches{ 0 };
        uint32_t shaderVariants{ 0 };
        RHIPipelineHandle shadePipeline{};
        RHIPipelineHandle referencePipeline{};
        RHIPipelineHandle secondaryShadePipeline{};
        RHIPipelineHandle secondaryReferencePipeline{};
        RHIPipelineHandle tertiaryShadePipeline{};
        RHIPipelineHandle tertiaryReferencePipeline{};
        bool invalidMetaRejected{ false };
        bool invalidMaterialRejected{ false };
        bool invalidPacketRejected{ false };
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
        const math::vector3 positions[] = {
                { -0.75f, -0.75f, 0.5f }, { -0.75f, 0.75f, 0.5f },
                { 0.75f, 0.75f, 0.5f }, { 0.75f, -0.75f, 0.5f } };
        const math::vector2 uvs[] = {
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

            for (float x : { -0.75f, 0.75f })
            {
                EnhancedDrawItem draw{};
                draw.mesh = mesh.get();
                draw.worldMatrix = math::translation_matrix(
                    math::vector3{ x, 0.f, 0.f });
                // P2a gate에서는 두 draw의 legacy 값은 일부러 같다. 실제 색이
                // 갈리려면 Forward가 owning packet의 값/texture를 읽어야 한다.
                draw.baseColorFactor = math::color(0.25f, 0.25f, 0.25f, 0.5f);
                draw.metallic = 0.f;
                draw.roughness = 0.7f;
                draws.push_back(draw);
            }

            EnhancedLight sun{};
            sun.position = math::vector4(0.f, 0.f, 0.f, 0.f);
            sun.direction = math::vector4(0.f, 0.f, -1.f, 0.f);
            sun.color = math::color(1.f, 1.f, 1.f, 2.f);
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

    bool PrepareForwardMaterialProbe(std::string_view metaFile,
        std::span<const std::string_view> tailProperties,
        std::uint32_t expectedConstantBytes, ShaderMetaHandle& outHandle,
        ShaderMeta& outMeta, Material& outMaterial, std::uint32_t& outPassIndex,
        std::string& outError)
    {
        const std::filesystem::path metaPath = RHIShaderSource::Resolve(
            std::string(metaFile));
        const FileGuid guid = DataSystems->GetFileGuid(metaPath);
        if (FileGuid{} == guid)
        {
            outError = "제품 Forward catalog GUID를 찾지 못했다";
            return false;
        }

        outHandle = DataSystems->LoadShaderMetaHandle(guid, outError);
        const std::shared_ptr<const ShaderMeta> snapshot =
            DataSystems->ResolveShaderMeta(outHandle);
        if (!outHandle.IsValid() || !snapshot)
        {
            if (outError.empty()) outError = "제품 Forward generation resolve 실패";
            return false;
        }
        outMeta = *snapshot;

        const auto passIt = std::find_if(outMeta.passes.begin(), outMeta.passes.end(),
            [](const ShaderPassDesc& pass) { return pass.name == "Forward"; });
        if (passIt == outMeta.passes.end() || !passIt->vertex || !passIt->pixel
            || passIt->compute || ShaderPassQueue::Transparent != passIt->queue)
        {
            outError = "제품 Forward는 transparent VS+PS pass여야 한다";
            return false;
        }
        outPassIndex = static_cast<std::uint32_t>(
            std::distance(outMeta.passes.begin(), passIt));

        RHIGraphicsPipelineDesc stateProbe{};
        passIt->state.ApplyTo(stateProbe);
        if (!stateProbe.blendEnable || stateProbe.independentBlend
            || RHIDepthWrite::Zero != stateProbe.depthWriteMask)
        {
            outError = "제품 Forward alpha blend/depth-write state가 다르다";
            return false;
        }

        const std::vector<std::uint16_t> defaultSelections(outMeta.keywords.size(), 0);
        ShaderMetaPermutation permutation;
        if (!ShaderPermutationDomain::Resolve(outMeta, outPassIndex,
                defaultSelections, permutation, outError))
        {
            return false;
        }
        RHIShaderPermutation reflectionDefines = permutation.defines;
        if (!reflectionDefines.Set("TILE_SIZE",
                std::to_string(EnhancedForwardPass::kTileSize), outError)
            || !reflectionDefines.Set("MAX_LIGHTS_PER_TILE",
                std::to_string(EnhancedForwardPass::kMaxLightsPerTile), outError))
        {
            return false;
        }

        std::error_code pathError;
        const std::filesystem::path sourcePath = outMeta.ResolveSource(metaPath);
        const std::filesystem::path relativeSource = std::filesystem::relative(
            sourcePath, RHIShaderSource::Resolve(""), pathError);
        if (pathError || relativeSource.empty())
        {
            outError = "제품 Forward source 상대 경로 계산 실패";
            return false;
        }

        std::vector<RHIShaderReflection> dxilStages;
        std::vector<RHIShaderReflection> spirvStages;
        const std::pair<const ShaderStageEntry*, const char*> stages[] = {
            { &*passIt->vertex, "vs_5_0" },
            { &*passIt->pixel, "ps_5_0" },
        };
        for (const auto& [stage, profile] : stages)
        {
            RHIShaderReflection dxil;
            RHIShaderReflection spirv;
            const std::string sourceName = relativeSource.generic_string();
            if (!RHIShaderCompiler::ReflectFile(sourceName, stage->entry, profile,
                    RHIShaderBinary::Dxil, reflectionDefines, dxil, outError)
                || !RHIShaderCompiler::ReflectFile(sourceName, stage->entry, profile,
                    RHIShaderBinary::SpirV, reflectionDefines, spirv, outError)
                || !AreShaderReflectionsEquivalent(dxil, spirv, outError))
            {
                return false;
            }
            dxilStages.push_back(std::move(dxil));
            spirvStages.push_back(std::move(spirv));
        }

        ShaderMetaBindingLayout layout;
        if (!ShaderMetaReflection::Resolve(outMeta, dxilStages, layout, outError))
            return false;

        constexpr std::size_t kStandardNumericPropertyCount = 7;
        constexpr std::array<std::string_view,
            kStandardNumericPropertyCount> expectedNames{
            standard_material::property::BaseColor,
            standard_material::property::Metallic,
            standard_material::property::Roughness,
            standard_material::property::NormalScale,
            standard_material::property::OcclusionStrength,
            standard_material::property::Emissive,
            standard_material::property::AlphaCutoff,
        };
        constexpr std::array<std::uint32_t,
            kStandardNumericPropertyCount> expectedOffsets{
            0, 16, 20, 24, 28, 32, 44,
        };
        constexpr std::size_t kMaterialTextureSlotCount = 4;
        bool layoutMatches = "MaterialProperties" == layout.constantBufferName
            && 2 == layout.constantBufferRegister
            && 0 == layout.constantBufferSpace
            && expectedConstantBytes == layout.constantBufferByteSize
            // I5-M5 flow 승격 — 표준 7 + variant tail + flow 2 + texture 4.
            && kStandardNumericPropertyCount + tailProperties.size() + 2u
                + kMaterialTextureSlotCount
                == layout.properties.size();
        for (std::size_t index = 0;
            layoutMatches && index < expectedNames.size(); ++index)
        {
            const ShaderMetaPropertyBinding& binding = layout.properties[index];
            layoutMatches = expectedNames[index] == binding.name
                && expectedOffsets[index] == binding.byteOffset
                && RHIShaderResourceKind::ConstantBuffer == binding.resourceKind;
        }
        for (std::size_t index = 0;
            layoutMatches && index < tailProperties.size(); ++index)
        {
            const ShaderMetaPropertyBinding& binding =
                layout.properties[kStandardNumericPropertyCount + index];
            layoutMatches = tailProperties[index] == binding.name
                && 48u + static_cast<std::uint32_t>(index) * 4u
                    == binding.byteOffset
                && ShaderPropertyType::Float == binding.propertyType
                && RHIShaderResourceKind::ConstantBuffer == binding.resourceKind;
        }
        // flow 두 항목 — tail 뒤, texture 앞. float2(uvScroll)가 먼저고
        // float4(windVector)는 다음 16B 레지스터로 정렬된다 — 총합이 16B로
        // 끝나야 pass 검증(%16)을 지나므로 이 순서가 계약이다.
        if (layoutMatches)
        {
            const std::size_t flowIndex =
                kStandardNumericPropertyCount + tailProperties.size();
            const std::uint32_t flowBase = 48u
                + static_cast<std::uint32_t>(tailProperties.size()) * 4u;
            const ShaderMetaPropertyBinding& flowUv =
                layout.properties[flowIndex];
            const ShaderMetaPropertyBinding& flowWind =
                layout.properties[flowIndex + 1];
            layoutMatches =
                standard_material::property::FlowUvScroll == flowUv.name
                && ShaderPropertyType::Float2 == flowUv.propertyType
                && flowBase == flowUv.byteOffset
                && standard_material::property::FlowWindVector == flowWind.name
                && ShaderPropertyType::Float4 == flowWind.propertyType
                && flowBase + 16u == flowWind.byteOffset;
        }
        const std::size_t texturePropertyOffset =
            kStandardNumericPropertyCount + tailProperties.size() + 2u;
        std::array<bool, kMaterialTextureSlotCount> occupied{};
        std::vector<std::string_view> textureNames;
        for (std::size_t index = 0;
            layoutMatches && index < kMaterialTextureSlotCount; ++index)
        {
            const ShaderMetaPropertyBinding& binding =
                layout.properties[texturePropertyOffset + index];
            layoutMatches = !binding.name.empty()
                && ShaderPropertyType::Texture2D == binding.propertyType
                && RHIShaderResourceKind::Texture == binding.resourceKind
                && binding.registerIndex >= 4u && binding.registerIndex < 8u
                && 0 == binding.registerSpace;
            if (layoutMatches)
            {
                const std::size_t slot = binding.registerIndex - 4u;
                layoutMatches = !occupied[slot]
                    && std::find(textureNames.begin(), textureNames.end(), binding.name)
                        == textureNames.end();
                occupied[slot] = true;
                textureNames.push_back(binding.name);
            }
        }
        if (!layoutMatches)
        {
            outError = "제품 Forward b2/t4..t7 reflection layout 불일치";
            return false;
        }

        if (!outMaterial.ConfigureShaderProperties(outMeta, layout, outError, outHandle)
            || !outMaterial.TrySetVector("MaterialProperties", "baseColor",
                math::vector4{ 0.f, 0.f, 0.f, 0.5f })
            || !outMaterial.TrySetFloat("MaterialProperties", "metallic", 0.f)
            || !outMaterial.TrySetFloat("MaterialProperties", "roughness", 1.f)
            || !outMaterial.TrySetFloat("MaterialProperties", "normalScale", 1.f)
            || !outMaterial.TrySetFloat("MaterialProperties", "occlusionStrength", 1.f)
            || !outMaterial.TrySetVector("MaterialProperties", "emissive",
                math::vector3{ 1.f, 1.f, 1.f })
            // 음수도 합법 authored 값이다. legacy 판별 sentinel로 재사용하면
            // product snapshot이 ShadeInstance default로 잘못 전환된다.
            || !outMaterial.TrySetFloat("MaterialProperties", "alphaCutoff", -1.f)
            || expectedConstantBytes != outMaterial.GetConstantBufferData().size())
        {
            if (outError.empty()) outError = "제품 Forward Standard property pack 실패";
            return false;
        }
        return true;
    }

    bool ValidateForwardRepresentativeAsset(const Material& material,
        const ShaderMeta& meta, std::span<const std::string_view> tailProperties,
        std::span<const float> expectedDefaults, std::string& outError)
    {
        if (tailProperties.size() != expectedDefaults.size()
            || material.m_shaderMetaGuid != meta.guid
            || FileGuid{} == material.m_fileGuid
            || MaterialRenderingMode::Transparent != material.m_renderingMode)
        {
            outError = "대표 Forward material의 GUID/mode 계약이 다르다";
            return false;
        }
        for (std::size_t index = 0; index < tailProperties.size(); ++index)
        {
            const std::string_view property = tailProperties[index];
            const auto found = std::find_if(material.m_propertyValues.begin(),
                material.m_propertyValues.end(),
                [property](const MaterialPropertyValue& value)
                {
                    return value.m_name == property;
                });
            if (found == material.m_propertyValues.end()
                || 1u != found->m_numericValue.size()
                || std::fabs(found->m_numericValue[0] - expectedDefaults[index])
                    > 1e-6f)
            {
                outError = "대표 Forward material custom float 불일치: ";
                outError += property;
                return false;
            }
        }
        return true;
    }

    template <typename TResources>
    bool CaptureForwardBackend(TResources& resources,
        IRenderPipelineCache& pipelines, IRenderRootSignatureCache& roots,
        IRenderMeshCache& meshCache, IRenderTextureCache& textureCache,
        const std::function<void()>& beginCaches, ForwardFixture& fixture,
        const std::array<const ShaderMeta*, 3>& shaderMetas,
        const std::array<ShaderMetaHandle, 3>& shaderMetaHandles,
        std::size_t mutationDrawIndex,
        std::shared_ptr<const EnhancedForwardMaterialDrawSnapshot> nextFramePacket,
        ForwardCapture& outCapture, std::string& outError)
    {
        if (std::any_of(shaderMetas.begin(), shaderMetas.end(),
                [](const ShaderMeta* meta) { return nullptr == meta; })
            || std::any_of(shaderMetaHandles.begin(), shaderMetaHandles.end(),
                [](ShaderMetaHandle handle) { return !handle.IsValid(); }))
        {
            outError = "Forward representative ShaderMeta 입력이 invalid다";
            return false;
        }
        const ShaderMeta& primaryMeta = *shaderMetas[0];
        const ShaderMetaHandle primaryHandle = shaderMetaHandles[0];
        const ShaderMetaHandle secondaryHandle = shaderMetaHandles[1];
        const ShaderMetaHandle tertiaryHandle = shaderMetaHandles[2];
        std::vector<EnhancedDrawItem> frameDraws = fixture.draws;
        EnhancedFrameContext context{};
        context.resources = &resources;
        context.psoManager = &pipelines;
        context.rootSignatures = &roots;
        context.meshCache = &meshCache;
        context.textureCache = &textureCache;
        context.forwardDraws = &frameDraws;
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
        if (!forward.ApplyShaderMeta(context, primaryHandle, primaryMeta,
                RHICompletionPoint{ resources.GetLastSignaledFenceValue() }, outError))
        {
            return fail(outError);
        }
        for (const EnhancedDrawItem& draw : frameDraws)
        {
            if (!draw.forwardMaterialSnapshot) continue;
            const EnhancedForwardMaterialDrawSnapshot& material =
                *draw.forwardMaterialSnapshot;
            const auto handleIt = std::find(shaderMetaHandles.begin(),
                shaderMetaHandles.end(), material.shaderMetaHandle);
            if (handleIt == shaderMetaHandles.end())
                return fail("Forward draw가 frame catalog 밖 ShaderMeta를 참조한다");
            const std::size_t metaIndex = static_cast<std::size_t>(
                std::distance(shaderMetaHandles.begin(), handleIt));
            const ShaderMeta& meta = *shaderMetas[metaIndex];
            const ShaderMetaHandle handle = shaderMetaHandles[metaIndex];
            RHIShaderPermutationKey resolvedKey{};
            std::shared_ptr<const ShaderMetaBindingLayout> resolvedLayout;
            if (!forward.EnsureShaderMetaVariant(context, handle, meta,
                    material.keywordSelections, resolvedKey, resolvedLayout, outError))
            {
                return fail(outError);
            }
            if (resolvedKey != material.permutationKey || !resolvedLayout
                || *resolvedLayout != material.bindingLayout)
            {
                return fail("Forward material meta/permutation/layout identity 불일치");
            }
        }
        outCapture.shadePipeline = forward.GetShadePSO();
        outCapture.referencePipeline = forward.GetReferencePSO();
        outCapture.shaderVariants = forward.GetShaderVariantCount();
        const auto secondaryDraw = std::find_if(frameDraws.begin(),
            frameDraws.end(), [secondaryHandle](const EnhancedDrawItem& draw)
            {
                return draw.forwardMaterialSnapshot
                    && draw.forwardMaterialSnapshot->shaderMetaHandle
                        == secondaryHandle;
            });
        if (secondaryDraw != frameDraws.end())
        {
            const EnhancedForwardMaterialDrawSnapshot& material =
                *secondaryDraw->forwardMaterialSnapshot;
            outCapture.secondaryShadePipeline = forward.GetShaderVariantPipeline(
                secondaryHandle, material.permutationKey,
                material.keywordSelections, false);
            outCapture.secondaryReferencePipeline = forward.GetShaderVariantPipeline(
                secondaryHandle, material.permutationKey,
                material.keywordSelections, true);
        }
        const auto tertiaryDraw = std::find_if(frameDraws.begin(),
            frameDraws.end(), [tertiaryHandle](const EnhancedDrawItem& draw)
            {
                return draw.forwardMaterialSnapshot
                    && draw.forwardMaterialSnapshot->shaderMetaHandle
                        == tertiaryHandle;
            });
        if (tertiaryDraw != frameDraws.end())
        {
            const EnhancedForwardMaterialDrawSnapshot& material =
                *tertiaryDraw->forwardMaterialSnapshot;
            outCapture.tertiaryShadePipeline = forward.GetShaderVariantPipeline(
                tertiaryHandle, material.permutationKey,
                material.keywordSelections, false);
            outCapture.tertiaryReferencePipeline = forward.GetShaderVariantPipeline(
                tertiaryHandle, material.permutationKey,
                material.keywordSelections, true);
        }
        if (!outCapture.shadePipeline.IsValid()
            || !outCapture.referencePipeline.IsValid()
            || !outCapture.secondaryShadePipeline.IsValid()
            || !outCapture.secondaryReferencePipeline.IsValid()
            || !outCapture.tertiaryShadePipeline.IsValid()
            || !outCapture.tertiaryReferencePipeline.IsValid()
            || outCapture.shadePipeline == outCapture.secondaryShadePipeline
            || outCapture.shadePipeline == outCapture.tertiaryShadePipeline
            || outCapture.secondaryShadePipeline == outCapture.tertiaryShadePipeline
            || outCapture.referencePipeline == outCapture.secondaryReferencePipeline
            || outCapture.referencePipeline == outCapture.tertiaryReferencePipeline
            || outCapture.secondaryReferencePipeline
                == outCapture.tertiaryReferencePipeline
            || 3 != outCapture.shaderVariants)
        {
            return fail("Forward primary/water/wind ShaderMeta PSO 준비가 불완전하다");
        }

        class FailSecondGraphicsPipelineCache final : public IRenderPipelineCache
        {
        public:
            explicit FailSecondGraphicsPipelineCache(IRenderPipelineCache& inner)
                : m_inner(inner)
            {
            }

            RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc& desc,
                std::string& error) override
            {
                ++graphicsRequests;
                if (2u == graphicsRequests)
                {
                    error = "Forward unpublished pair reference failure injection";
                    return {};
                }
                return m_inner.GetOrCreate(desc, error);
            }

            RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc& desc,
                std::string& error) override
            {
                return m_inner.GetOrCreateCompute(desc, error);
            }

            bool InvalidatePipeline(RHIPipelineHandle handle,
                RHICompletionPoint retireAfter) override
            {
                const bool invalidated = m_inner.InvalidatePipeline(handle, retireAfter);
                if (invalidated) invalidatedHandles.push_back(handle);
                return invalidated;
            }

            std::uint32_t InvalidatePipelines(
                RHICompletionPoint retireAfter) override
            {
                return m_inner.InvalidatePipelines(retireAfter);
            }

            std::uint32_t CollectRetiredPipelines(
                RHICompletionPoint completed) override
            {
                return m_inner.CollectRetiredPipelines(completed);
            }

            std::uint32_t graphicsRequests{};
            std::vector<RHIPipelineHandle> invalidatedHandles;

        private:
            IRenderPipelineCache& m_inner;
        };

        ShaderMeta cleanupProbeMeta = primaryMeta;
        const auto cleanupProbePass = std::find_if(cleanupProbeMeta.passes.begin(),
            cleanupProbeMeta.passes.end(),
            [](const ShaderPassDesc& pass) { return pass.name == "Forward"; });
        if (cleanupProbePass == cleanupProbeMeta.passes.end())
            return fail("Forward unpublished pair cleanup probe pass가 없다");
        cleanupProbePass->state.cullMode = RHICullMode::None;
        const ShaderMetaHandle cleanupProbeHandle{
            tertiaryHandle.slot + 1u, tertiaryHandle.generation + 1u };
        const std::vector<std::uint16_t> cleanupSelections(
            cleanupProbeMeta.keywords.size(), 0u);
        FailSecondGraphicsPipelineCache failingPipelines(pipelines);
        EnhancedFrameContext failingContext = context;
        failingContext.psoManager = &failingPipelines;
        RHIShaderPermutationKey cleanupKey{};
        std::shared_ptr<const ShaderMetaBindingLayout> cleanupLayout;
        std::string cleanupError;
        if (forward.EnsureShaderMetaVariant(failingContext, cleanupProbeHandle,
                cleanupProbeMeta, cleanupSelections, cleanupKey, cleanupLayout,
                cleanupError)
            || cleanupError.empty()
            || !failingPipelines.invalidatedHandles.empty()
            || outCapture.shaderVariants != forward.GetShaderVariantCount())
        {
            return fail("Forward unpublished half-pair가 shared cache handle을 invalidation했다");
        }
        cleanupError.clear();
        if (!forward.EnsureShaderMetaVariant(context, cleanupProbeHandle,
                cleanupProbeMeta, cleanupSelections, cleanupKey, cleanupLayout,
                cleanupError)
            || !cleanupLayout)
        {
            return fail(cleanupError.empty()
                ? "Forward half-pair 실패 뒤 cache reuse retry가 실패했다" : cleanupError);
        }
        const std::array<ShaderMetaHandle, 3> fixtureHandles{
            primaryHandle, secondaryHandle, tertiaryHandle };
        if (1u != forward.CommitShaderMetaFrame(context, fixtureHandles, {})
            || outCapture.shaderVariants != forward.GetShaderVariantCount())
        {
            return fail("Forward cleanup probe variant retirement가 불완전하다");
        }

        ShaderMeta invalidMeta = primaryMeta;
        const auto invalidPass = std::find_if(invalidMeta.passes.begin(),
            invalidMeta.passes.end(),
            [](const ShaderPassDesc& pass) { return pass.name == "Forward"; });
        if (invalidPass == invalidMeta.passes.end())
            return fail("Forward invalid-meta probe pass가 없다");
        invalidPass->name = "NotForward";
        const ShaderMetaHandle invalidHandle{
            primaryHandle.slot, primaryHandle.generation + 1u };
        std::string rejectedMetaError;
        const bool metaRejected = !forward.ApplyShaderMeta(context,
            invalidHandle, invalidMeta,
            RHICompletionPoint{ resources.GetLastSignaledFenceValue() },
            rejectedMetaError);
        outCapture.invalidMetaRejected = metaRejected && !rejectedMetaError.empty()
            && outCapture.shadePipeline == forward.GetShadePSO()
            && outCapture.referencePipeline == forward.GetReferencePSO()
            && outCapture.secondaryShadePipeline == forward.GetShaderVariantPipeline(
                secondaryHandle,
                secondaryDraw->forwardMaterialSnapshot->permutationKey,
                secondaryDraw->forwardMaterialSnapshot->keywordSelections, false)
            && outCapture.secondaryReferencePipeline == forward.GetShaderVariantPipeline(
                secondaryHandle,
                secondaryDraw->forwardMaterialSnapshot->permutationKey,
                secondaryDraw->forwardMaterialSnapshot->keywordSelections, true)
            && outCapture.tertiaryShadePipeline == forward.GetShaderVariantPipeline(
                tertiaryHandle,
                tertiaryDraw->forwardMaterialSnapshot->permutationKey,
                tertiaryDraw->forwardMaterialSnapshot->keywordSelections, false)
            && outCapture.tertiaryReferencePipeline == forward.GetShaderVariantPipeline(
                tertiaryHandle,
                tertiaryDraw->forwardMaterialSnapshot->permutationKey,
                tertiaryDraw->forwardMaterialSnapshot->keywordSelections, true)
            && outCapture.shaderVariants == forward.GetShaderVariantCount();
        if (!outCapture.invalidMetaRejected)
            return fail("Forward invalid ShaderMeta candidate가 current PSO를 보존하지 않았다");

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
        if (frameDraws.empty() || !frameDraws[0].forwardMaterialSnapshot)
            return fail("Forward P2b owning material packet이 없다");
        {
            const auto valid = frameDraws[0].forwardMaterialSnapshot;
            auto invalidMaterial =
                std::make_shared<EnhancedForwardMaterialDrawSnapshot>(*valid);
            ++invalidMaterial->shaderMetaHandle.generation;
            frameDraws[0].forwardMaterialSnapshot = invalidMaterial;
            std::string rejectedMaterialError;
            outCapture.invalidMaterialRejected =
                !forward.PrepareFrame(context, rejectedMaterialError)
                && !rejectedMaterialError.empty()
                && outCapture.shadePipeline == forward.GetShadePSO()
                && outCapture.referencePipeline == forward.GetReferencePSO()
                && outCapture.shaderVariants == forward.GetShaderVariantCount();
            frameDraws[0].forwardMaterialSnapshot = valid;
            if (!outCapture.invalidMaterialRejected)
                return fail("Forward invalid material generation이 fail-closed되지 않았다");

            auto invalid =
                std::make_shared<EnhancedForwardMaterialDrawSnapshot>(*valid);
            invalid->textureBindings[0].registerIndex = 5;
            frameDraws[0].forwardMaterialSnapshot = invalid;
            std::string rejectedError;
            outCapture.invalidPacketRejected =
                !forward.PrepareFrame(context, rejectedError)
                && !rejectedError.empty();
            frameDraws[0].forwardMaterialSnapshot = valid;
            if (!outCapture.invalidPacketRejected)
                return fail("Forward invalid material packet이 fail-closed되지 않았다");
            outError.clear();
        }
        if (!forward.PrepareFrame(context, outError)) return fail(outError);
        outCapture.draws = forward.GetLastDrawCount();
        outCapture.batches = forward.GetLastBatchCount();

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

        constexpr std::array<uint32_t, 3> swatchX{
            kGeometryTestWindow * 7 / 40,
            kGeometryTestWindow / 2,
            kGeometryTestWindow * 33 / 40,
        };
        constexpr uint32_t swatchY = kGeometryTestWindow / 4;
        constexpr uint32_t overlapX = kGeometryTestWindow / 2;
        constexpr uint32_t overlapY = kGeometryTestWindow * 3 / 4;
        constexpr uint32_t customX = kGeometryTestWindow * 33 / 40;
        constexpr uint32_t customY = kGeometryTestWindow * 3 / 4;
        constexpr uint32_t outside = 2;
        for (std::size_t material = 0; material < swatchX.size(); ++material)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                outCapture.swatches[material][channel] =
                    color.At(swatchX[material], swatchY, channel);
            }
        }
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            outCapture.overlap[channel] = color.At(overlapX, overlapY, channel);
            outCapture.customBefore[channel] = color.At(customX, customY, channel);
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

        // P2c: 같은 pass/variant를 유지한 채 immutable wind packet만 다음 프레임
        // 값으로 바꾼다. 새 pass를 만들면 property 갱신이 아니라 초기화 차이도
        // 섞이므로, 반드시 같은 EnhancedForwardPass의 연속 두 프레임으로 본다.
        if (mutationDrawIndex >= frameDraws.size() || !nextFramePacket
            || !nextFramePacket->IsValid())
        {
            return fail("Forward custom-float 다음-frame packet이 invalid다");
        }
        frameDraws[mutationDrawIndex].forwardMaterialSnapshot =
            std::move(nextFramePacket);

        if (!resources.BeginFrame(outError)) return fail(outError);
        frameOpen = true;
        if (beginCaches) beginCaches();
        if (!forward.PrepareFrame(context, outError)) return fail(outError);
        if (outCapture.draws != forward.GetLastDrawCount()
            || outCapture.batches != forward.GetLastBatchCount()
            || outCapture.shaderVariants != forward.GetShaderVariantCount())
        {
            return fail("Forward custom-float 갱신이 draw/batch/variant identity를 바꿨다");
        }

        EnhancedRenderGraph nextGraph(
            static_cast<IRenderDeviceServices&>(resources));
        RGTextureDesc nextDepthDesc{};
        nextDepthDesc.width = kGeometryTestWindow;
        nextDepthDesc.height = kGeometryTestWindow;
        nextDepthDesc.format = EnhancedForwardPass::kDepthFormat;
        nextDepthDesc.allowDepthStencil = true;
        nextDepthDesc.name = "ForwardRHI.NextDepth";
        const RGHandle nextDepth = nextGraph.CreateTexture(nextDepthDesc);
        nextGraph.AddPass("ForwardRHI.NextDepthClear",
            { { nextDepth, RHIResourceState::DepthWrite } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                const auto depthTarget = RHIDepthTargetDesc::Depth(
                    executeContext.ResolveHandle(nextDepth),
                    EnhancedForwardPass::kDepthFormat);
                const RHIRenderTargetBinding targets =
                    context.resources->CreateRenderTargets(
                        std::span<const RHITextureHandle>{}, &depthTarget);
                if (!targets.IsValid()) return;
                executeContext.encoder->BindRenderTargets(targets);
                executeContext.encoder->ClearDepthTarget(targets, 1.f);
            });

        EnhancedForwardPass::Inputs nextInputs{};
        nextInputs.depth = nextDepth;
        forward.SetInputs(nextInputs);
        forward.Declare(nextGraph, context);
        const RGHandle nextOutput = forward.GetOutput();
        if (!nextOutput.IsValid())
            return fail("Forward custom-float 다음-frame 출력이 없다");
        nextGraph.AddPass("ForwardRHI.NextReadback",
            { { nextOutput, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback(
                    colorReadback, executeContext.ResolveHandle(nextOutput));
            }, true);
        if (!nextGraph.Compile(outError) || !nextGraph.Execute(outError))
            return fail(outError);
        if (!resources.EndFrame(outError)) return fail(outError);
        frameOpen = false;
        resources.WaitForGpu();

        RHIReadbackImage nextColor;
        if (!resources.MapReadback(colorReadback, nextColor, outError))
            return fail(outError);
        for (uint32_t channel = 0; channel < 4; ++channel)
            outCapture.customAfter[channel] =
                nextColor.At(customX, customY, channel);

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
            sun.position = math::vector4(0.f, 0.f, 0.f, 0.f);
            sun.direction = math::vector4(0.f, 0.f, -1.f, 0.f);
            sun.color = math::color(1.f, 1.f, 1.f, 2.f);
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
    outLog += "── 제품 GBuffer — DX12/Vulkan Standard Material batch b2·MRT 대조 ──\n";
    GBufferFixture fixture;
    GBufferCapture dx12Capture{};
    GBufferCapture vkCapture{};
    std::string error;
    ShaderMetaHandle materialProbeHandle{};
    ShaderMeta materialProbeMeta{};
    Material materialProbe{};
    if (!PrepareStandardMaterialProbe(materialProbeHandle, materialProbeMeta,
            materialProbe, error))
    {
        outLog += "[1/4] Standard Material probe 준비 실패: " + error + "\n";
        return false;
    }
    const ShaderMetaBindingLayout* materialLayout =
        materialProbe.GetShaderBindingLayout();
    if (!materialLayout)
    {
        outLog += "[1/4] Standard Material snapshot layout이 없다\n";
        return false;
    }
    ShaderMeta secondaryMeta = materialProbeMeta;
    secondaryMeta.guid = FileGuid{ "50000000-0000-4000-8000-000000000005" };
    secondaryMeta.name = "GBufferSecondaryMetaProbe";
    secondaryMeta.passes[0].state.depthTest = RHICompareOp::LessEqual;
    constexpr ShaderMetaHandle secondaryMetaHandle{ 0xFFFFFFFDu, 1u };
    EnhancedLiveFramePacket shaderMetaFrame{};
    auto primaryFrameOwner = std::make_shared<const ShaderMeta>(materialProbeMeta);
    auto secondaryFrameOwner = std::make_shared<const ShaderMeta>(secondaryMeta);
    std::weak_ptr<const ShaderMeta> primaryFrameLifetime = primaryFrameOwner;
    std::weak_ptr<const ShaderMeta> secondaryFrameLifetime = secondaryFrameOwner;
    EnhancedShaderMetaFrameSnapshot primaryFrameSnapshot{};
    primaryFrameSnapshot.guid = materialProbeMeta.guid;
    primaryFrameSnapshot.handle = materialProbeHandle;
    primaryFrameSnapshot.value = primaryFrameOwner;
    EnhancedShaderMetaFrameSnapshot secondaryFrameSnapshot{};
    secondaryFrameSnapshot.guid = secondaryMeta.guid;
    secondaryFrameSnapshot.handle = secondaryMetaHandle;
    secondaryFrameSnapshot.value = secondaryFrameOwner;
    shaderMetaFrame.gbufferShaderMetas.push_back(std::move(primaryFrameSnapshot));
    shaderMetaFrame.gbufferShaderMetas.push_back(std::move(secondaryFrameSnapshot));
    primaryFrameOwner.reset();
    secondaryFrameOwner.reset();
    const bool shaderMetaOwnedByFrame = !primaryFrameLifetime.expired()
        && !secondaryFrameLifetime.expired()
        && shaderMetaFrame.FindGBufferShaderMeta(materialProbeMeta.guid)
        && shaderMetaFrame.FindGBufferShaderMeta(secondaryMeta.guid);
    const std::array<std::uint8_t, 4> firstBaseColorPixel{
        128u, 255u, 64u, 255u };
    const std::array<std::uint8_t, 4> secondBaseColorPixel{
        64u, 128u, 255u, 255u };
    const std::array<std::uint8_t, 4> sharedNormalPixel{
        255u, 128u, 128u, 255u };
    std::shared_ptr<Texture> firstBaseColorOwner{ Texture::CreateFromPixels(
        1, 1, "m6_p1b2a_first_base_color", DXGI_FORMAT_R8G8B8A8_UNORM,
        firstBaseColorPixel.data(), firstBaseColorPixel.size()) };
    std::shared_ptr<Texture> secondBaseColorOwner{ Texture::CreateFromPixels(
        1, 1, "m6_p1b2a_second_base_color", DXGI_FORMAT_R8G8B8A8_UNORM,
        secondBaseColorPixel.data(), secondBaseColorPixel.size()) };
    std::shared_ptr<Texture> sharedNormalOwner{ Texture::CreateFromPixels(
        1, 1, "m6_p1b2b1_shared_normal", DXGI_FORMAT_R8G8B8A8_UNORM,
        sharedNormalPixel.data(), sharedNormalPixel.size()) };
    if (!firstBaseColorOwner || !secondBaseColorOwner || !sharedNormalOwner)
    {
        outLog += "[1/4] GUID/register probe texture 생성 실패\n";
        return false;
    }
    const FileGuid firstBaseColorGuid{
        "10000000-0000-4000-8000-000000000001" };
    const FileGuid secondBaseColorGuid{
        "20000000-0000-4000-8000-000000000002" };
    const FileGuid sharedNormalGuid{
        "40000000-0000-4000-8000-000000000004" };
    std::weak_ptr<Texture> firstBaseColorLifetime = firstBaseColorOwner;
    std::weak_ptr<Texture> secondBaseColorLifetime = secondBaseColorOwner;
    std::weak_ptr<Texture> sharedNormalLifetime = sharedNormalOwner;
    materialProbe.UseBaseColorMap(firstBaseColorOwner);
    if (!materialProbe.TrySetTextureGuid(
            standard_material::property::BaseColorMap, firstBaseColorGuid))
    {
        outLog += "[1/4] 첫 baseColorMap GUID 설정 실패\n";
        return false;
    }

    const auto makeSnapshot = [&](const Material& material,
        const ShaderMeta& shaderMeta, ShaderMetaHandle shaderMetaHandle)
    {
        auto snapshot = std::make_shared<EnhancedMaterialDrawSnapshot>();
        snapshot->shaderMetaHandle = shaderMetaHandle;
        snapshot->bindingLayout = *materialLayout;
        const std::span<const std::uint16_t> keywords =
            material.GetKeywordSelections();
        snapshot->keywordSelections.assign(keywords.begin(), keywords.end());
        ShaderMetaPermutation permutation;
        if (!ShaderPermutationDomain::Resolve(shaderMeta, 0,
                snapshot->keywordSelections, permutation, error))
        {
            return snapshot;
        }
        snapshot->permutationKey = permutation.key;
        if (!material.BuildShaderPropertyBlock(shaderMeta, *materialLayout,
                snapshot->propertyBytes, error))
        {
            return snapshot;
        }
        constexpr std::array<std::string_view, 4> textureProperties{
            standard_material::property::BaseColorMap,
            standard_material::property::NormalMap,
            standard_material::property::OrmMap,
            standard_material::property::EmissiveMap,
        };
        const std::array<std::shared_ptr<Texture>, 4> textureOwners{
            material.GetBaseColorMapShared(), material.GetNormalMapShared(),
            material.GetOccRoughMetalMapShared(), material.GetEmissiveMapShared(),
        };
        snapshot->textureBindings.resize(textureProperties.size());
        for (std::size_t index = 0; index < textureProperties.size(); ++index)
        {
            const std::string_view property = textureProperties[index];
            const auto reflected = std::find_if(materialLayout->properties.begin(),
                materialLayout->properties.end(),
                [property](const ShaderMetaPropertyBinding& binding)
                {
                    return binding.name == property;
                });
            const auto logical = std::find_if(material.m_propertyValues.begin(),
                material.m_propertyValues.end(),
                [property](const MaterialPropertyValue& value)
                {
                    return value.m_name == property;
                });
            if (reflected == materialLayout->properties.end()
                || logical == material.m_propertyValues.end())
            {
                error = "Standard Material texture GUID/register snapshot 실패: ";
                error += property;
                return snapshot;
            }
            EnhancedMaterialTextureBinding& texture =
                snapshot->textureBindings[index];
            texture.propertyName = std::string(property);
            texture.textureGuid = logical->m_textureGuid;
            texture.registerIndex = reflected->registerIndex;
            texture.registerSpace = reflected->registerSpace;
            texture.textureOwner = textureOwners[index];
        }
        return snapshot;
    };

    fixture.draws[0].worldMatrix = math::translation_matrix(
        math::vector3{ -0.75f, 0.f, 0.f });
    fixture.draws[0].materialSnapshot = makeSnapshot(
        materialProbe, materialProbeMeta, materialProbeHandle);

    Material secondMaterial = materialProbe;
    if (!secondMaterial.TrySetVector("MaterialProperties", "baseColor",
            math::vector4{ 0.75f, 0.125f, 0.25f, 1.0f })
        || !secondMaterial.TrySetFloat("MaterialProperties", "metallic", 0.125f)
        || !secondMaterial.TrySetFloat("MaterialProperties", "roughness", 0.25f)
        || !secondMaterial.TrySetTextureGuid(
            standard_material::property::BaseColorMap, secondBaseColorGuid)
        || !secondMaterial.TrySetTextureGuid(
            standard_material::property::NormalMap, sharedNormalGuid))
    {
        outLog += "[1/4] 두 번째 Standard Material property/GUID 설정 실패\n";
        return false;
    }
    secondMaterial.UseBaseColorMap(secondBaseColorOwner);
    secondMaterial.UseNormalMap(sharedNormalOwner);
    EnhancedDrawItem secondDraw = fixture.draws[0];
    secondDraw.worldMatrix = math::translation_matrix(
        math::vector3{ -0.25f, 0.f, 0.f });
    secondDraw.materialSnapshot = makeSnapshot(
        secondMaterial, materialProbeMeta, materialProbeHandle);
    fixture.draws.push_back(std::move(secondDraw));

    Material reducedMaterial = secondMaterial;
    if (!reducedMaterial.TrySetKeywordSelection("SHADING_QUALITY", "reduced"))
    {
        outLog += "[1/4] reduced SHADING_QUALITY 선택 실패\n";
        return false;
    }
    EnhancedDrawItem thirdDraw = fixture.draws[1];
    thirdDraw.worldMatrix = math::translation_matrix(
        math::vector3{ 0.25f, 0.f, 0.f });
    thirdDraw.materialSnapshot = makeSnapshot(
        reducedMaterial, materialProbeMeta, materialProbeHandle);
    fixture.draws.push_back(std::move(thirdDraw));

    EnhancedDrawItem fourthDraw = fixture.draws[1];
    fourthDraw.worldMatrix = math::translation_matrix(
        math::vector3{ 0.75f, 0.f, 0.f });
    fourthDraw.materialSnapshot = makeSnapshot(
        secondMaterial, secondaryMeta, secondaryMetaHandle);
    fixture.draws.push_back(std::move(fourthDraw));
    if (!fixture.draws[0].materialSnapshot->IsValid()
        || !fixture.draws[1].materialSnapshot->IsValid()
        || !fixture.draws[2].materialSnapshot->IsValid()
        || !fixture.draws[3].materialSnapshot->IsValid()
        || fixture.draws[1].materialSnapshot->permutationKey
            == fixture.draws[2].materialSnapshot->permutationKey
        || fixture.draws[1].materialSnapshot->shaderMetaHandle
            == fixture.draws[3].materialSnapshot->shaderMetaHandle)
    {
        outLog += "[1/4] Standard Material draw snapshot이 invalid다: "
            + error + "\n";
        return false;
    }
    materialProbe.UseBaseColorMap(std::shared_ptr<Texture>{});
    secondMaterial.UseBaseColorMap(std::shared_ptr<Texture>{});
    secondMaterial.UseNormalMap(std::shared_ptr<Texture>{});
    reducedMaterial.UseBaseColorMap(std::shared_ptr<Texture>{});
    reducedMaterial.UseNormalMap(std::shared_ptr<Texture>{});
    firstBaseColorOwner.reset();
    secondBaseColorOwner.reset();
    sharedNormalOwner.reset();
    const EnhancedMaterialTextureBinding& firstTexture =
        fixture.draws[0].materialSnapshot->textureBindings[0];
    const EnhancedMaterialTextureBinding& secondTexture =
        fixture.draws[1].materialSnapshot->textureBindings[0];
    const EnhancedMaterialTextureBinding& thirdTexture =
        fixture.draws[2].materialSnapshot->textureBindings[0];
    const EnhancedMaterialTextureBinding& fourthTexture =
        fixture.draws[3].materialSnapshot->textureBindings[0];
    const EnhancedMaterialTextureBinding& secondNormal =
        fixture.draws[1].materialSnapshot->textureBindings[1];
    const EnhancedMaterialTextureBinding& thirdNormal =
        fixture.draws[2].materialSnapshot->textureBindings[1];
    const EnhancedMaterialTextureBinding& fourthNormal =
        fixture.draws[3].materialSnapshot->textureBindings[1];
    const bool textureOwnedByPacket = !firstBaseColorLifetime.expired()
        && !secondBaseColorLifetime.expired() && !sharedNormalLifetime.expired()
        && firstTexture.textureOwner && secondTexture.textureOwner
        && thirdTexture.textureOwner && fourthTexture.textureOwner
        && secondNormal.textureOwner && thirdNormal.textureOwner
        && fourthNormal.textureOwner
        && firstTexture.textureOwner.get() != secondTexture.textureOwner.get()
        && secondTexture.textureOwner.get() == thirdTexture.textureOwner.get()
        && thirdTexture.textureOwner.get() == fourthTexture.textureOwner.get()
        && secondNormal.textureOwner.get() == thirdNormal.textureOwner.get()
        && thirdNormal.textureOwner.get() == fourthNormal.textureOwner.get()
        && firstTexture.textureGuid == firstBaseColorGuid
        && secondTexture.textureGuid == secondBaseColorGuid
        && thirdTexture.textureGuid == secondBaseColorGuid
        && fourthTexture.textureGuid == secondBaseColorGuid
        && secondNormal.textureGuid == sharedNormalGuid
        && thirdNormal.textureGuid == sharedNormalGuid
        && fourthNormal.textureGuid == sharedNormalGuid
        && firstTexture.propertyName == standard_material::property::BaseColorMap
        && secondTexture.propertyName == standard_material::property::BaseColorMap
        && 0 == firstTexture.registerIndex && 0 == firstTexture.registerSpace
        && 0 == secondTexture.registerIndex && 0 == secondTexture.registerSpace
        && fixture.draws[1].materialSnapshot->keywordSelections
            == std::vector<std::uint16_t>{ 0 }
        && fixture.draws[2].materialSnapshot->keywordSelections
            == std::vector<std::uint16_t>{ 1 }
        && fixture.draws[3].materialSnapshot->keywordSelections
            == std::vector<std::uint16_t>{ 0 }
        && fixture.draws[3].materialSnapshot->shaderMetaHandle
            == secondaryMetaHandle;
    if (!textureOwnedByPacket)
    {
        outLog += "[1/4] texture GUID→t0 binding/packet owner 계약 불일치\n";
        return false;
    }

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
            fixture, materialProbeMeta, materialProbeHandle,
            secondaryMeta, secondaryMetaHandle,
            dx12Capture, error);
        dx12Capture.previousPipelineStale =
            !resources.Resolve(dx12Capture.previousPipeline).IsValid();
        dx12Capture.retiredVariantStale = dx12Capture.retiredVariantStale
            && !resources.Resolve(dx12Capture.retiredVariantPipeline).IsValid();
        dx12Capture.secondaryMetaFrameRetired =
            dx12Capture.secondaryMetaFrameRetired
            && !resources.Resolve(
                dx12Capture.retiredSecondaryMetaPipeline).IsValid();
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

    outLog += "[1/4] DX12 기준 Material property→reflection b2→PSO→5 MRT"
        " · targeted next-use 통과\n";

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
            fixture, materialProbeMeta, materialProbeHandle,
            secondaryMeta, secondaryMetaHandle,
            vkCapture, error);
    }
    vkCapture.previousPipelineStale =
        !pipelines.Resolve(vkCapture.previousPipeline).IsValid();
    vkCapture.retiredVariantStale = vkCapture.retiredVariantStale
        && !pipelines.Resolve(vkCapture.retiredVariantPipeline).IsValid();
    vkCapture.secondaryMetaFrameRetired =
        vkCapture.secondaryMetaFrameRetired
        && !pipelines.Resolve(vkCapture.retiredSecondaryMetaPipeline).IsValid();

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

    bool passed = captured && textureOwnedByPacket && shaderMetaOwnedByFrame
        && 0 == stubs && 0 == problems &&
        2 == vkCapture.graph.passesExecuted && 0 == vkCapture.graph.passesCulled &&
        6 == vkCapture.graph.transientCreated &&
        4 == dx12Capture.draws && 1 == dx12Capture.meshes &&
        4 == dx12Capture.materials && 4 == dx12Capture.batches &&
        4 == vkCapture.draws && 1 == vkCapture.meshes &&
        4 == vkCapture.materials && 4 == vkCapture.batches &&
        dx12Capture.previousPipeline.IsValid() && dx12Capture.activePipeline.IsValid() &&
        dx12Capture.previousPipeline != dx12Capture.activePipeline &&
        dx12Capture.alternatePipeline.IsValid() &&
        dx12Capture.alternatePipeline != dx12Capture.activePipeline &&
        dx12Capture.secondaryMetaPipeline.IsValid() &&
        dx12Capture.secondaryMetaPipeline != dx12Capture.activePipeline &&
        3 == dx12Capture.shaderVariants && dx12Capture.retiredVariantStale &&
        dx12Capture.previousPipelineStale &&
        dx12Capture.rejectedReloadPreserved &&
        dx12Capture.rejectedMaterialPreserved &&
        dx12Capture.rejectedTextureBindingPreserved &&
        dx12Capture.rejectedPermutationPreserved &&
        dx12Capture.rejectedSecondaryMetaPreserved &&
        dx12Capture.secondaryMetaSurvivedPrimaryReload &&
        dx12Capture.secondaryMetaGenerationRetired &&
        dx12Capture.secondaryMetaFrameRetired &&
        materialProbeHandle == dx12Capture.shaderMetaHandle &&
        secondaryMetaHandle == dx12Capture.secondaryMetaHandle &&
        vkCapture.previousPipeline.IsValid() && vkCapture.activePipeline.IsValid() &&
        vkCapture.previousPipeline != vkCapture.activePipeline &&
        vkCapture.alternatePipeline.IsValid() &&
        vkCapture.alternatePipeline != vkCapture.activePipeline &&
        vkCapture.secondaryMetaPipeline.IsValid() &&
        vkCapture.secondaryMetaPipeline != vkCapture.activePipeline &&
        3 == vkCapture.shaderVariants && vkCapture.retiredVariantStale &&
        vkCapture.previousPipelineStale &&
        vkCapture.rejectedReloadPreserved &&
        vkCapture.rejectedMaterialPreserved &&
        vkCapture.rejectedTextureBindingPreserved &&
        vkCapture.rejectedPermutationPreserved &&
        vkCapture.rejectedSecondaryMetaPreserved &&
        vkCapture.secondaryMetaSurvivedPrimaryReload &&
        vkCapture.secondaryMetaGenerationRetired &&
        vkCapture.secondaryMetaFrameRetired &&
        materialProbeHandle == vkCapture.shaderMetaHandle &&
        secondaryMetaHandle == vkCapture.secondaryMetaHandle &&
        1 == meshStats.uploads && 0 == meshStats.failures &&
        0 == textureStats.failures;

    float maxCenterDelta = 0.f;
    float maxSecondDelta = 0.f;
    float maxThirdDelta = 0.f;
    float maxFourthDelta = 0.f;
    float maxOutside = 0.f;
    for (uint32_t target = 0; target < 4; ++target)
    {
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            maxCenterDelta = (std::max)(maxCenterDelta, std::fabs(
                vkCapture.center[target][channel] -
                dx12Capture.center[target][channel]));
            maxSecondDelta = (std::max)(maxSecondDelta, std::fabs(
                vkCapture.second[target][channel] -
                dx12Capture.second[target][channel]));
            maxThirdDelta = (std::max)(maxThirdDelta, std::fabs(
                vkCapture.third[target][channel] -
                dx12Capture.third[target][channel]));
            maxFourthDelta = (std::max)(maxFourthDelta, std::fabs(
                vkCapture.fourth[target][channel] -
                dx12Capture.fourth[target][channel]));
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
        "coverage %u/%u(편차 %.2f%%) · 첫/둘째/셋째/넷째 최대 편차 %.5f/%.5f/%.5f/%.5f\n",
        dx12Capture.center[0][0], vkCapture.center[0][0],
        dx12Capture.center[0][1], vkCapture.center[0][1],
        dx12Capture.center[0][2], vkCapture.center[0][2],
        dx12Capture.center[2][2], vkCapture.center[2][2],
        dx12Capture.bitmask, vkCapture.bitmask,
        dx12Capture.depth, vkCapture.depth,
        dx12Capture.writtenPixels, vkCapture.writtenPixels,
        writtenDelta * 100.f, maxCenterDelta, maxSecondDelta, maxThirdDelta,
        maxFourthDelta);
    outLog += compare;

    const float expected[][4] = {
        { 0.0627451f, 0.25f, 0.12549f, 1.f },
        { 0.375f, 0.625f, 0.75f, 1.f },
        { 0.5f, 0.5f, 1.f, 1.f },
        { 0.f, 0.f, 0.f, 1.f } };
    float dxExpectedDelta = 0.f;
    const float secondExpected[][4] = {
        { 0.188235f, 0.0627451f, 0.25f, 1.f },
        { 0.375f, 0.25f, 0.125f, 1.f },
        { 0.5f, 0.5f, 1.f, 1.f },
        { 0.f, 0.f, 0.f, 1.f } };
    for (uint32_t target = 0; target < 4; ++target)
    {
        if (2 == target) continue;
        for (uint32_t channel = 0; channel < 4; ++channel)
            dxExpectedDelta = (std::max)(dxExpectedDelta, std::fabs(
                dx12Capture.center[target][channel] - expected[target][channel]));
    }
    float secondExpectedDelta = 0.f;
    float thirdExpectedDelta = 0.f;
    float fourthExpectedDelta = 0.f;
    const float thirdExpected[][4] = {
        { 0.188235f, 0.0627451f, 0.25f, 1.f },
        { 0.375f, 0.25f, 0.125f, 1.f },
        { 0.f, 0.f, 0.f, 0.f },
        { 0.f, 0.f, 0.f, 1.f } };
    for (uint32_t target = 0; target < 4; ++target)
    {
        if (2 == target) continue;
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            secondExpectedDelta = (std::max)(secondExpectedDelta, std::fabs(
                dx12Capture.second[target][channel] -
                secondExpected[target][channel]));
            thirdExpectedDelta = (std::max)(thirdExpectedDelta, std::fabs(
                dx12Capture.third[target][channel] -
                thirdExpected[target][channel]));
            fourthExpectedDelta = (std::max)(fourthExpectedDelta, std::fabs(
                dx12Capture.fourth[target][channel] -
                secondExpected[target][channel]));
        }
    }

    float dxPermutationNormalDelta = 0.f;
    float vkPermutationNormalDelta = 0.f;
    for (uint32_t channel = 0; channel < 3; ++channel)
    {
        dxPermutationNormalDelta = (std::max)(dxPermutationNormalDelta,
            std::fabs(dx12Capture.second[2][channel]
                - dx12Capture.third[2][channel]));
        vkPermutationNormalDelta = (std::max)(vkPermutationNormalDelta,
            std::fabs(vkCapture.second[2][channel]
                - vkCapture.third[2][channel]));
    }

    if (0 == dx12Capture.writtenPixels || 0 == vkCapture.writtenPixels ||
        writtenDelta > 0.02f || maxCenterDelta > 0.015f ||
        maxSecondDelta > 0.015f || maxThirdDelta > 0.015f ||
        maxFourthDelta > 0.015f ||
        dxExpectedDelta > 0.015f || secondExpectedDelta > 0.015f ||
        thirdExpectedDelta > 0.015f || fourthExpectedDelta > 0.015f ||
        dxPermutationNormalDelta < 0.1f ||
        vkPermutationNormalDelta < 0.1f || bitmaskDelta > 0.5f ||
        std::fabs(dx12Capture.bitmask - 43981.f) > 0.5f ||
        depthDelta > 0.001f || std::fabs(vkCapture.depth - 0.5f) > 0.001f ||
        maxOutside > 0.001f || 0.f != vkCapture.outsideBitmask ||
        std::fabs(vkCapture.outsideDepth - 1.f) > 0.001f)
    {
        passed = false;
        outLog += "MRT·depth 픽셀 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] Standard numeric 7+texture 4·b2 48B"
        " · baseColorMap GUID 2개→reflection t0/space0·서로 다른 owned texture 2 batch"
        " · 같은 texture/property+SHADING_QUALITY full/reduced→PSO 2개·normal 픽셀 분리"
        " · secondary ShaderMeta generation 동시 draw·candidate-first 교체·frame retirement"
        " · GT frame packet primary/secondary generation owner 유지"
        " · variant generation reload targeted retire"
        " · old handle stale · new handle next draw"
        " · invalid primary/secondary ShaderMeta·material generation·texture register/permutation rejected/current preserved"
        " · Material 해제 뒤 packet texture owner 유지"
        " · 5 MRT·CBV b2·2D SRV×4·dynamic sampler·root SRV×2·indexed mesh · "
        "미구현 " + std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();
    fixture.draws.clear();
    shaderMetaFrame.gbufferShaderMetas.clear();
    if (!firstBaseColorLifetime.expired() || !secondBaseColorLifetime.expired()
        || !sharedNormalLifetime.expired() || !primaryFrameLifetime.expired()
        || !secondaryFrameLifetime.expired())
    {
        passed = false;
        outLog += "draw packet 해제 뒤 texture owner가 반환되지 않았다\n";
    }

    outLog += passed
        ? "GBuffer 공용 패스 DX12/Vulkan 픽셀 대조 통과\n"
        : "GBuffer 공용 패스 DX12/Vulkan 픽셀 대조 실패\n";
    return passed;
}

bool RunVulkanForwardTest(std::string& outLog)
{
    outLog += "── Forward+ 패스 — DX12/Vulkan P2d-e legacy retirement + required assets 대조 ──\n";
    ForwardFixture fixture;
    ForwardCapture dx12Capture{};
    ForwardCapture vkCapture{};
    std::string error;

    ShaderMetaHandle primaryHandle{};
    ShaderMeta primaryMeta{};
    Material primaryMaterial{};
    std::uint32_t forwardPassIndex = 0;
    if (!PrepareForwardMaterialProbe("Forward.shadermeta", {}, 80u,
            primaryHandle, primaryMeta, primaryMaterial, forwardPassIndex, error))
    {
        outLog += "[1/4] 제품 Forward ShaderMeta 준비 실패: " + error + "\n";
        return false;
    }
    constexpr std::array<std::string_view, 4> waterTail{
        "waveSpeed", "waveAmplitude", "waveFrequency", "waterTint" };
    constexpr std::array<float, 4> waterDefaults{ 0.35f, 0.08f, 2.0f, 0.8f };
    constexpr std::array<std::string_view, 4> windTail{
        "windSpeed", "windStrength", "windFrequency", "windTint" };
    constexpr std::array<float, 4> windDefaults{ 1.2f, 0.35f, 2.5f, 0.7f };

    // 제품 GT sealing 자체를 material cache 로드보다 먼저 태운다. Scene/proxy가
    // 소유한 Material은 cache와 별개일 수 있으므로 Host required-asset packet의
    // 임의 GUID가 generation owner로 frame에 들어오는지를 고정한다.
    const FileGuid waterCatalogGuid = DataSystems->GetFileGuid(
        RHIShaderSource::Resolve("ForwardWater.shadermeta"));
    const FileGuid windCatalogGuid = DataSystems->GetFileGuid(
        RHIShaderSource::Resolve("ForwardWind.shadermeta"));
    auto waterRequiredMaterial = std::make_shared<Material>();
    waterRequiredMaterial->m_shaderMetaGuid = waterCatalogGuid;
    waterRequiredMaterial->m_renderingMode = MaterialRenderingMode::Transparent;
    auto windRequiredMaterial = std::make_shared<Material>();
    windRequiredMaterial->m_shaderMetaGuid = windCatalogGuid;
    windRequiredMaterial->m_renderingMode = MaterialRenderingMode::Transparent;
    const std::weak_ptr<Material> waterRequiredLifetime = waterRequiredMaterial;
    const std::weak_ptr<Material> windRequiredLifetime = windRequiredMaterial;
    std::vector<std::shared_ptr<Material>> requiredMaterials{
        windRequiredMaterial, waterRequiredMaterial, windRequiredMaterial, nullptr };
    EnhancedRequiredAssetPacket requiredAssets =
        EnhancedSceneRenderer::BuildRequiredAssetPacket(requiredMaterials);
    requiredMaterials.clear();
    waterRequiredMaterial.reset();
    windRequiredMaterial.reset();
    const bool requiredMaterialOwnersReleased =
        waterRequiredLifetime.expired() && windRequiredLifetime.expired();
    // 명시 API도 중복/빈 GUID를 허용하되 canonical set에는 남기지 않는다.
    requiredAssets.RequireShaderMeta(
        EnhancedShaderMetaDomain::Forward, windCatalogGuid);
    requiredAssets.RequireShaderMeta(
        EnhancedShaderMetaDomain::Forward, FileGuid{});
    const EnhancedLiveFramePacket productFrame =
        EnhancedSceneRenderer::BuildLiveFramePacket(
            0.125f, nullptr, 0, false, requiredAssets);
    const EnhancedLiveFramePacket productNextFrame =
        EnhancedSceneRenderer::BuildLiveFramePacket(
            0.25f, nullptr, 0, false, requiredAssets);
    const bool productFrameTimeSealed =
        std::fabs(productFrame.deltaSeconds - 0.125f) <= 1e-6f
        && std::fabs(productNextFrame.deltaSeconds - 0.25f) <= 1e-6f
        && std::fabs(productNextFrame.totalSeconds
            - productFrame.totalSeconds - 0.25f) <= 1e-5f;
    const FileGuid primaryCatalogGuid = DataSystems->GetFileGuid(
        RHIShaderSource::Resolve("Forward.shadermeta"));
    const EnhancedShaderMetaFrameSnapshot* productWater =
        productFrame.FindForwardShaderMeta(waterCatalogGuid);
    const EnhancedShaderMetaFrameSnapshot* productWind =
        productFrame.FindForwardShaderMeta(windCatalogGuid);
    const bool productSecondariesSortedUnique = std::adjacent_find(
        productFrame.forwardShaderMetas.begin() +
            (productFrame.forwardShaderMetas.empty() ? 0 : 1),
        productFrame.forwardShaderMetas.end(),
        [](const EnhancedShaderMetaFrameSnapshot& left,
            const EnhancedShaderMetaFrameSnapshot& right)
        {
            return !(left.guid < right.guid);
        }) == productFrame.forwardShaderMetas.end();
    const bool productFrameOwnsRequiredMetas =
        FileGuid{} != primaryCatalogGuid
        && FileGuid{} != waterCatalogGuid && FileGuid{} != windCatalogGuid
        && requiredMaterialOwnersReleased
        && productFrame.requiredAssets.shaderMetas.size() == 2u
        && productFrame.requiredAssets.ContainsShaderMeta(
            EnhancedShaderMetaDomain::Forward, waterCatalogGuid)
        && productFrame.requiredAssets.ContainsShaderMeta(
            EnhancedShaderMetaDomain::Forward, windCatalogGuid)
        && productFrame.gbufferShaderMetas.size() == 1u
        && productFrame.forwardShaderMetas.size() == 3u
        && productFrame.forwardShaderMetas[0].guid == primaryCatalogGuid
        && productFrame.forwardShaderMetas[0].IsValid()
        && productFrameTimeSealed
        && productSecondariesSortedUnique
        && nullptr != productWater && productWater->IsValid()
        && nullptr != productWind && productWind->IsValid();
    if (!productFrameOwnsRequiredMetas)
    {
        outLog += "[1/4] 제품 required-asset packet의 임의 GUID owner sealing 실패\n";
        return false;
    }

    const std::shared_ptr<Material> waterAsset =
        DataSystems->LoadMaterialShared("ForwardWater");
    const std::shared_ptr<Material> windAsset =
        DataSystems->LoadMaterialShared("ForwardWind");
    if (!waterAsset || !windAsset
        || waterAsset->m_shaderMetaGuid != waterCatalogGuid
        || windAsset->m_shaderMetaGuid != windCatalogGuid)
    {
        outLog += "[1/4] ForwardWater/ForwardWind material asset GUID 로드 실패\n";
        return false;
    }
    const EnhancedLiveFramePacket cacheIsolationFrame =
        EnhancedSceneRenderer::BuildLiveFramePacket(
            0.f, nullptr, 0, false, EnhancedRequiredAssetPacket{});
    if (cacheIsolationFrame.gbufferShaderMetas.size() != 1u
        || cacheIsolationFrame.forwardShaderMetas.size() != 1u
        || nullptr != cacheIsolationFrame.FindForwardShaderMeta(waterCatalogGuid)
        || nullptr != cacheIsolationFrame.FindForwardShaderMeta(windCatalogGuid))
    {
        outLog += "[1/4] 빈 required-asset packet에 material cache ShaderMeta가 누출됐다\n";
        return false;
    }
    Material waterMaterial(*waterAsset);
    Material windMaterial(*windAsset);

    // P2d-a: 실제 FoliageRenderProxy가 타입별 인스턴스를 owning draw source로
    // 펼친다. m_isCulled는 한 카메라의 파생값이므로 여기서 버리지 않고,
    // worldBounds를 제품 CaptureFromView의 카메라별 절두체 판정으로 넘긴다.
    bool foliageDrawSourceValid = false;
    {
        auto foliageMeshOwner = std::shared_ptr<Mesh>(
            fixture.mesh.get(), [](Mesh*) noexcept {});
        auto foliageMaterialOwner = std::make_shared<Material>(windMaterial);
        const std::weak_ptr<Material> foliageMaterialLifetime =
            foliageMaterialOwner;

        FoliageRenderProxy foliage;
        foliage.m_foliageTypes.emplace_back(
            foliageMeshOwner, foliageMaterialOwner, true, "P2dWind");

        FoliageInstance first{};
        first.m_position = { -0.4f, 0.f, 0.f };
        first.m_foliageTypeID = 0;
        first.RebuildWorldMatrix();
        FoliageInstance second{};
        second.m_position = { 0.4f, 0.f, 0.f };
        second.m_foliageTypeID = 0;
        second.m_isCulled = true;
        second.RebuildWorldMatrix();
        FoliageInstance invalid{};
        invalid.m_foliageTypeID = 7;
        invalid.RebuildWorldMatrix();
        foliage.m_foliageInstances = { first, second, invalid };
        foliage.RebuildInstanceMap();

        std::vector<FoliageRenderProxy::DrawSource> sources =
            foliage.CaptureDrawSources();
        foliageDrawSourceValid = 2u == sources.size()
            && std::all_of(sources.begin(), sources.end(),
                [&](const FoliageRenderProxy::DrawSource& source)
                {
                    return source.mesh.get() == fixture.mesh.get()
                        && source.material == foliageMaterialOwner
                        && 0u == source.foliageTypeID
                        && !source.worldBounds.is_empty();
                })
            && math::near_equal(sources[0].worldMatrix, first.m_worldMatrix)
            && math::near_equal(sources[1].worldMatrix, second.m_worldMatrix);

        // 프록시 원본을 놓아도 frame draw source가 owner를 유지하고, source를
        // 놓은 뒤에는 반환되는지까지 같이 고정한다.
        foliage.m_foliageTypes.clear();
        foliageMaterialOwner.reset();
        foliageDrawSourceValid = foliageDrawSourceValid
            && !foliageMaterialLifetime.expired();
        sources.clear();
        foliageDrawSourceValid = foliageDrawSourceValid
            && foliageMaterialLifetime.expired();
    }
    if (!foliageDrawSourceValid)
    {
        outLog += "[1/4] FoliageRenderProxy owning draw source/culling 경계 실패\n";
        return false;
    }

    ShaderMetaHandle waterHandle{};
    ShaderMeta waterMeta{};
    std::uint32_t waterPassIndex = 0;
    ShaderMetaHandle windHandle{};
    ShaderMeta windMeta{};
    std::uint32_t windPassIndex = 0;
    if (!PrepareForwardMaterialProbe("ForwardWater.shadermeta", waterTail, 96u,
            waterHandle, waterMeta, waterMaterial, waterPassIndex, error)
        || !PrepareForwardMaterialProbe("ForwardWind.shadermeta", windTail, 96u,
            windHandle, windMeta, windMaterial, windPassIndex, error)
        || waterPassIndex != forwardPassIndex || windPassIndex != forwardPassIndex)
    {
        outLog += "[1/4] 대표 Water/Wind ShaderMeta 준비 실패: " + error + "\n";
        return false;
    }
    // ConfigureShaderProperties는 runtime schema를 붙이면서 authored logical
    // 값을 보존한다. asset 원본으로 먼저 검사해 GUID 선택과 default가 실제
    // catalog 파일에서 왔음을 고정한다(.meta GUID 하드코딩 금지).
    if (!ValidateForwardRepresentativeAsset(*waterAsset, waterMeta,
            waterTail, waterDefaults, error)
        || !ValidateForwardRepresentativeAsset(*windAsset, windMeta,
            windTail, windDefaults, error))
    {
        outLog += "[1/4] 대표 Water/Wind material 계약 실패: " + error + "\n";
        return false;
    }

    const std::array<const ShaderMeta*, 3> shaderMetas{
        &primaryMeta, &waterMeta, &windMeta };
    const std::array<ShaderMetaHandle, 3> shaderMetaHandles{
        primaryHandle, waterHandle, windHandle };

    EnhancedLiveFramePacket shaderMetaFrame{};
    std::array<std::shared_ptr<const ShaderMeta>, 3> frameOwners{
        std::make_shared<const ShaderMeta>(primaryMeta),
        std::make_shared<const ShaderMeta>(waterMeta),
        std::make_shared<const ShaderMeta>(windMeta),
    };
    std::array<std::weak_ptr<const ShaderMeta>, 3> frameLifetimes{
        frameOwners[0], frameOwners[1], frameOwners[2] };
    for (std::size_t index = 0; index < frameOwners.size(); ++index)
    {
        EnhancedShaderMetaFrameSnapshot snapshot{};
        snapshot.guid = shaderMetas[index]->guid;
        snapshot.handle = shaderMetaHandles[index];
        snapshot.value = frameOwners[index];
        shaderMetaFrame.forwardShaderMetas.push_back(std::move(snapshot));
    }
    frameOwners = {};
    const bool shaderMetaOwnedByFrame = 3 == shaderMetaFrame.forwardShaderMetas.size()
        && std::all_of(frameLifetimes.begin(), frameLifetimes.end(),
            [](const std::weak_ptr<const ShaderMeta>& owner)
            {
                return !owner.expired();
            })
        && std::all_of(shaderMetaFrame.forwardShaderMetas.begin(),
            shaderMetaFrame.forwardShaderMetas.end(),
            [](const EnhancedShaderMetaFrameSnapshot& snapshot)
            {
                return snapshot.IsValid();
            })
        && shaderMetaFrame.FindForwardShaderMeta(primaryMeta.guid)
        && shaderMetaFrame.FindForwardShaderMeta(waterMeta.guid)
        && shaderMetaFrame.FindForwardShaderMeta(windMeta.guid);

    constexpr std::array<std::array<std::uint8_t, 4>, 3> emissionPixels{
        std::array<std::uint8_t, 4>{ 255u, 0u, 0u, 255u },
        std::array<std::uint8_t, 4>{ 0u, 255u, 0u, 255u },
        std::array<std::uint8_t, 4>{ 0u, 0u, 255u, 255u },
    };
    constexpr std::array<const char*, 3> textureNames{
        "m6_p2b_forward_far", "m6_p2b_forward_middle",
        "m6_p2b_forward_near",
    };
    const std::array<FileGuid, 3> emissionGuids{
        FileGuid{ "81000000-0000-4000-8000-000000000001" },
        FileGuid{ "82000000-0000-4000-8000-000000000002" },
        FileGuid{ "83000000-0000-4000-8000-000000000003" },
    };
    std::array<std::shared_ptr<Texture>, 3> emissionOwners{};
    std::array<std::weak_ptr<Texture>, 3> emissionLifetimes{};
    for (std::size_t index = 0; index < emissionOwners.size(); ++index)
    {
        emissionOwners[index].reset(Texture::CreateFromPixels(1, 1,
            textureNames[index], DXGI_FORMAT_R8G8B8A8_UNORM,
            emissionPixels[index].data(), emissionPixels[index].size()));
        if (!emissionOwners[index])
        {
            outLog += "[1/4] Forward P2b emission texture 생성 실패\n";
            return false;
        }
        emissionLifetimes[index] = emissionOwners[index];
    }
    constexpr std::array<std::uint8_t, 4> genericWindPixel{
        255u, 255u, 255u, 255u };
    std::shared_ptr<Texture> genericWindOwner(Texture::CreateFromPixels(1, 1,
        "m6_p2d_c_wind_map", DXGI_FORMAT_R8G8B8A8_UNORM,
        genericWindPixel.data(), genericWindPixel.size()));
    if (!genericWindOwner)
    {
        outLog += "[1/4] P2d-c generic windMap texture 생성 실패\n";
        return false;
    }
    std::weak_ptr<Texture> genericWindLifetime = genericWindOwner;
    const FileGuid genericWindGuid{
        "84000000-0000-4000-8000-000000000004" };

    Material primaryNearMaterial(primaryMaterial);
    const auto configureMaterial = [&](Material& material,
        const std::shared_ptr<Texture>& emission, const FileGuid& emissionGuid)
    {
        material.m_renderingMode = MaterialRenderingMode::Transparent;
        const bool configured = material.TrySetVector("MaterialProperties", "baseColor",
                math::vector4{ 0.f, 0.f, 0.f, 0.5f })
            && material.TrySetFloat("MaterialProperties", "metallic", 0.f)
            && material.TrySetFloat("MaterialProperties", "roughness", 1.f)
            && material.TrySetFloat("MaterialProperties", "normalScale", 1.f)
            && material.TrySetFloat("MaterialProperties", "occlusionStrength", 1.f)
            && material.TrySetVector("MaterialProperties", "emissive",
                math::vector3{ 1.f, 1.f, 1.f })
            && material.TrySetFloat("MaterialProperties", "alphaCutoff", 0.f)
            && material.TrySetTextureGuid(
                standard_material::property::EmissiveMap, emissionGuid);
        if (configured) material.UseEmissiveMap(emission);
        return configured;
    };
    if (!configureMaterial(primaryMaterial, emissionOwners[0], emissionGuids[0])
        || !configureMaterial(waterMaterial, emissionOwners[1], emissionGuids[1])
        || !configureMaterial(primaryNearMaterial,
            emissionOwners[2], emissionGuids[2])
        || !configureMaterial(windMaterial, {}, {})
        // A/B/A 순서 probe에서는 Water tail의 고정 emission을 끄고 기존
        // red/green/blue alpha 식을 그대로 지킨다. Wind tail은 별도 swatch의
        // 연속 두 프레임에서 0→1로 바꿔 실제 64B b2 소비를 판정한다.
        || !waterMaterial.TrySetFloat("MaterialProperties", "waveAmplitude", 0.f)
        || !waterMaterial.TrySetFloat("MaterialProperties", "waterTint", 0.f)
        || !windMaterial.TrySetFloat("MaterialProperties", "windStrength", 0.f)
        || !windMaterial.TrySetFloat("MaterialProperties", "windTint", 0.f)
        || !windMaterial.TrySetTextureGuid("windMap", genericWindGuid))
    {
        outLog += "[1/4] Forward P2c material property 구성 실패: " + error + "\n";
        return false;
    }
    windMaterial.UseTextureMap("windMap", genericWindOwner);
    // P2d-b는 legacy m_flowInfo를 Material 주소가 아닌 값 snapshot으로 넘긴다.
    // Water는 구조 보존을, Wind mutation swatch는 실제 time/flow 픽셀 소비를 본다.
    //
    // I5-M5 flow 승격 — snapshot draw의 wind/uvScroll 정본은 b2 논리 값이다.
    // 이 테스트는 sealing 브리지(m_flowInfo 폴백 합성)를 거치지 않고 수동으로
    // packet을 만들므로, 논리 값을 직접 저작한다. m_flowInfo는 인스턴스 채널
    // (legacy 폴백) 보존 검증용으로 함께 남긴다.
    waterMaterial.SetWindVector(math::vector4{ 0.15f, -0.10f, 0.05f, 0.20f })
        .SetUVScroll(math::vector2{ 0.03f, -0.02f });
    windMaterial.SetWindVector(math::vector4{ -0.25f, 0.35f, 0.10f, 0.40f })
        .SetUVScroll(math::vector2{ 0.05f, 0.025f });
    if (!waterMaterial.TrySetVector("MaterialProperties", "flowWindVector",
            math::vector4{ 0.15f, -0.10f, 0.05f, 0.20f })
        || !waterMaterial.TrySetVector("MaterialProperties", "flowUvScroll",
            math::vector2{ 0.03f, -0.02f })
        || !windMaterial.TrySetVector("MaterialProperties", "flowWindVector",
            math::vector4{ -0.25f, 0.35f, 0.10f, 0.40f })
        || !windMaterial.TrySetVector("MaterialProperties", "flowUvScroll",
            math::vector2{ 0.05f, 0.025f }))
    {
        outLog += "[1/4] Forward flow 논리 값 저작 실패\n";
        return false;
    }
    Material mutatedWindMaterial(windMaterial);
    constexpr float kPi = 3.14159265358979323846f;
    if (!mutatedWindMaterial.TrySetFloat(
            "MaterialProperties", "windSpeed", -0.5f * kPi)
        || !mutatedWindMaterial.TrySetFloat(
            "MaterialProperties", "windStrength", 0.7f)
        || !mutatedWindMaterial.TrySetFloat(
            "MaterialProperties", "windFrequency", 0.f)
        || !mutatedWindMaterial.TrySetFloat(
            "MaterialProperties", "windTint", 0.1f))
    {
        outLog += "[1/4] Forward Wind custom float mutation 실패\n";
        return false;
    }
    mutatedWindMaterial.SetWindVector(
            math::vector4{ 0.f, 0.f, 0.f, 1.5f * kPi })
        .SetUVScroll(math::vector2{ 0.25f, -0.125f });
    if (!mutatedWindMaterial.TrySetVector("MaterialProperties", "flowWindVector",
            math::vector4{ 0.f, 0.f, 0.f, 1.5f * kPi })
        || !mutatedWindMaterial.TrySetVector("MaterialProperties", "flowUvScroll",
            math::vector2{ 0.25f, -0.125f }))
    {
        outLog += "[1/4] Forward Wind flow mutation 논리 값 저작 실패\n";
        return false;
    }

    const auto makePacket = [&](const Material& material,
        const ShaderMeta& meta, ShaderMetaHandle handle)
    {
        auto packet = std::make_shared<EnhancedForwardMaterialDrawSnapshot>();
        const ShaderMetaBindingLayout* materialLayout =
            material.GetShaderBindingLayout();
        if (nullptr == materialLayout)
        {
            error = "Forward material runtime binding layout이 없다";
            return packet;
        }
        // P2b shader는 b2의 alpha=0.5를 써야 한다. 이 P2a 값(0.125)을 읽으면
        // swatch/overlap의 정확한 blend 판정이 즉시 달라진다.
        packet->baseColorFactor = math::color(0.f, 0.f, 0.f, 0.125f);
        packet->metallic = 0.f;
        packet->roughness = 1.f;
        packet->shaderMetaHandle = handle;
        packet->bindingLayout = *materialLayout;
        const std::span<const std::uint16_t> keywords =
            material.GetKeywordSelections();
        packet->keywordSelections.assign(keywords.begin(), keywords.end());
        ShaderMetaPermutation permutation;
        if (!ShaderPermutationDomain::Resolve(meta, forwardPassIndex,
                packet->keywordSelections, permutation, error))
        {
            return packet;
        }
        packet->permutationKey = permutation.key;
        packet->flow.windVector = material.m_flowInfo.m_windVector;
        packet->flow.uvScroll = material.m_flowInfo.m_uvScroll;
        if (!material.BuildShaderPropertyBlock(meta, *materialLayout,
                packet->propertyBytes, error))
        {
            return packet;
        }

        for (const ShaderPropertyDesc& desc : meta.properties)
        {
            if (ShaderPropertyType::Texture2D != desc.type) continue;
            const auto reflected = std::find_if(materialLayout->properties.begin(),
                materialLayout->properties.end(),
                [&desc](const ShaderMetaPropertyBinding& binding)
                {
                    return binding.name == desc.name;
                });
            const auto logical = std::find_if(material.m_propertyValues.begin(),
                material.m_propertyValues.end(),
                [&desc](const MaterialPropertyValue& value)
                {
                    return value.m_name == desc.name;
                });
            if (reflected == materialLayout->properties.end()
                || logical == material.m_propertyValues.end())
            {
                error = "Forward texture GUID/register snapshot 실패: ";
                error += desc.name;
                return packet;
            }
            EnhancedMaterialTextureBinding binding{};
            binding.propertyName = desc.name;
            binding.textureGuid = logical->m_textureGuid;
            binding.registerIndex = reflected->registerIndex;
            binding.registerSpace = reflected->registerSpace;
            binding.textureOwner = material.GetTextureMapShared(desc.name);
            packet->textureBindings.push_back(std::move(binding));
        }
        return packet;
    };

    std::array<std::shared_ptr<EnhancedForwardMaterialDrawSnapshot>, 4> packets{
        makePacket(primaryMaterial, primaryMeta, primaryHandle),
        makePacket(waterMaterial, waterMeta, waterHandle),
        makePacket(primaryNearMaterial, primaryMeta, primaryHandle),
        makePacket(windMaterial, windMeta, windHandle),
    };
    std::shared_ptr<EnhancedForwardMaterialDrawSnapshot> mutatedWindPacket =
        makePacket(mutatedWindMaterial, windMeta, windHandle);
    mutatedWindPacket->flow.totalSeconds = 1.f;
    mutatedWindPacket->flow.deltaSeconds = 0.25f;
    auto invalidFlowPacket =
        std::make_shared<EnhancedForwardMaterialDrawSnapshot>(*mutatedWindPacket);
    invalidFlowPacket->flow.totalSeconds =
        std::numeric_limits<float>::quiet_NaN();
    const bool invalidFlowRejected = !invalidFlowPacket->IsValid();
    if (std::any_of(packets.begin(), packets.end(),
            [](const auto& packet) { return !packet || !packet->IsValid(); })
        || !mutatedWindPacket || !mutatedWindPacket->IsValid()
        || !invalidFlowRejected
        || 80u != packets[0]->propertyBytes.size()
        || 96u != packets[1]->propertyBytes.size()
        || 96u != packets[3]->propertyBytes.size()
        || 4u != packets[3]->textureBindings.size()
        || "windMap" != packets[3]->textureBindings.front().propertyName
        || 4u != packets[3]->textureBindings.front().registerIndex
        || packets[3]->shaderMetaHandle != mutatedWindPacket->shaderMetaHandle
        || packets[3]->permutationKey != mutatedWindPacket->permutationKey
        || packets[3]->bindingLayout != mutatedWindPacket->bindingLayout
        || packets[3]->propertyBytes == mutatedWindPacket->propertyBytes
        || mutatedWindPacket->flow.windVector.w != 1.5f * kPi
        || mutatedWindPacket->flow.uvScroll.x != 0.25f
        || mutatedWindPacket->flow.uvScroll.y != -0.125f
        || mutatedWindPacket->flow.totalSeconds != 1.f
        || mutatedWindPacket->flow.deltaSeconds != 0.25f)
    {
        outLog += "[1/4] Forward P2c 48/64B draw packet이 invalid다: " + error + "\n";
        return false;
    }

    EnhancedDrawItem drawTemplate = fixture.draws.front();
    drawTemplate.baseColorFactor = math::color(1.f, 1.f, 1.f, 0.125f);
    fixture.draws.clear();
    const auto appendDraw = [&](std::size_t material, float scale,
        float x, float y, float z)
    {
        EnhancedDrawItem draw = drawTemplate;
        draw.worldMatrix = math::scaling_matrix(
            math::vector3{ scale, scale, 1.f }) *
            math::translation_matrix(math::vector3{ x, y, z });
        draw.forwardMaterialSnapshot = packets[material];
        fixture.draws.push_back(std::move(draw));
    };
    // 이미 back-to-front로 정렬된 A/B/A/C PSO 순서다. 앞의 6개는 기존 P2b
    // order gate를 그대로 보존하고, 마지막 C는 겹치지 않는 Wind mutation
    // swatch다. A를 전역 그룹화하면 overlap의 alpha 식이 달라진다.
    appendDraw(0, 0.25f, -0.65f, 0.55f, 0.20f);
    appendDraw(0, 0.55f,  0.00f, -0.45f, 0.20f);
    appendDraw(1, 0.25f,  0.00f, 0.55f, 0.00f);
    appendDraw(1, 0.55f,  0.00f, -0.45f, 0.00f);
    appendDraw(2, 0.25f,  0.65f, 0.55f, -0.20f);
    appendDraw(2, 0.55f,  0.00f, -0.45f, -0.20f);
    constexpr std::size_t windMutationDrawIndex = 6;
    appendDraw(3, 0.25f, 0.65f, -0.55f, -0.30f);
    // 타일 컬링은 계속 광원 1개를 검증하되 픽셀은 emission만 남겨 blend 식을
    // 정확히 판정한다.
    fixture.lights[0].color = math::color(0.f, 0.f, 0.f, 0.f);

    for (Material* material : { &primaryMaterial, &waterMaterial,
            &primaryNearMaterial, &windMaterial, &mutatedWindMaterial })
    {
        material->UseEmissiveMap(std::shared_ptr<Texture>{});
    }
    windMaterial.UseTextureMap("windMap", {});
    mutatedWindMaterial.UseTextureMap("windMap", {});
    genericWindOwner.reset();
    for (std::shared_ptr<Texture>& owner : emissionOwners) owner.reset();
    const bool packetOwnsTextures = std::all_of(emissionLifetimes.begin(),
        emissionLifetimes.end(), [](const std::weak_ptr<Texture>& owner)
        {
            return !owner.expired();
        }) && !genericWindLifetime.expired()
        && packets[3]->textureBindings.front().textureOwner
        && packets[3]->textureBindings.front().textureGuid == genericWindGuid;

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
            fixture, shaderMetas, shaderMetaHandles, windMutationDrawIndex,
            mutatedWindPacket,
            dx12Capture, error);
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
    outLog += "[1/4] DX12 기준 Forward ShaderMeta·material PSO/b2/t4..t7·blend order 통과\n";

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
            fixture, shaderMetas, shaderMetaHandles, windMutationDrawIndex,
            mutatedWindPacket,
            vkCapture, error);
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
            "draw/batch/meta %u/%u/%u · tile(center/corner/min/max) %u/%u/%u/%u · "
            "mesh upload %u/실패 %u · texture 실패 %u\n",
            vkCapture.graph.passesExecuted, vkCapture.graph.passesCulled,
            vkCapture.graph.transientCreated, vkCapture.draws, vkCapture.batches,
            vkCapture.shaderVariants, vkCapture.centerTileLights,
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
        packetOwnsTextures && shaderMetaOwnedByFrame
        && dx12Capture.invalidMetaRejected
        && dx12Capture.invalidMaterialRejected
        && dx12Capture.invalidPacketRejected
        && vkCapture.invalidMetaRejected
        && vkCapture.invalidMaterialRejected
        && vkCapture.invalidPacketRejected
        && 7 == dx12Capture.draws && 4 == dx12Capture.batches
        && 3 == dx12Capture.shaderVariants
        && 7 == vkCapture.draws && 4 == vkCapture.batches
        && 3 == vkCapture.shaderVariants
        && dx12Capture.shadePipeline.IsValid()
        && dx12Capture.referencePipeline.IsValid()
        && dx12Capture.secondaryShadePipeline.IsValid()
        && dx12Capture.secondaryReferencePipeline.IsValid()
        && dx12Capture.tertiaryShadePipeline.IsValid()
        && dx12Capture.tertiaryReferencePipeline.IsValid()
        && dx12Capture.shadePipeline != dx12Capture.secondaryShadePipeline
        && dx12Capture.shadePipeline != dx12Capture.tertiaryShadePipeline
        && dx12Capture.secondaryShadePipeline != dx12Capture.tertiaryShadePipeline
        && dx12Capture.referencePipeline != dx12Capture.secondaryReferencePipeline
        && dx12Capture.referencePipeline != dx12Capture.tertiaryReferencePipeline
        && dx12Capture.secondaryReferencePipeline
            != dx12Capture.tertiaryReferencePipeline
        && vkCapture.shadePipeline.IsValid()
        && vkCapture.referencePipeline.IsValid() &&
        vkCapture.secondaryShadePipeline.IsValid()
        && vkCapture.secondaryReferencePipeline.IsValid()
        && vkCapture.tertiaryShadePipeline.IsValid()
        && vkCapture.tertiaryReferencePipeline.IsValid()
        && vkCapture.shadePipeline != vkCapture.secondaryShadePipeline
        && vkCapture.shadePipeline != vkCapture.tertiaryShadePipeline
        && vkCapture.secondaryShadePipeline != vkCapture.tertiaryShadePipeline
        && vkCapture.referencePipeline != vkCapture.secondaryReferencePipeline
        && vkCapture.referencePipeline != vkCapture.tertiaryReferencePipeline
        && vkCapture.secondaryReferencePipeline
            != vkCapture.tertiaryReferencePipeline &&
        1 == dx12Capture.centerTileLights && 1 == dx12Capture.cornerTileLights &&
        1 == dx12Capture.minTileLights && 1 == dx12Capture.maxTileLights &&
        1 == vkCapture.centerTileLights && 1 == vkCapture.cornerTileLights &&
        1 == vkCapture.minTileLights && 1 == vkCapture.maxTileLights;

    float maxRegionDelta = 0.f;
    float maxExpectedDelta = 0.f;
    float maxOutside = 0.f;
    constexpr float expectedSwatches[3][3] = {
        { 0.5f, 0.f, 0.f },
        { 0.f, 0.5f, 0.f },
        { 0.f, 0.f, 0.5f },
    };
    constexpr float expectedOverlap[3] = { 0.125f, 0.25f, 0.5f };
    // windSpeed=-pi/2, total=1, flow phase=3pi/2 => phase=pi/2,
    // response=0.1+0.7*1=0.8. alpha 0.5 합성까지 포함한 정확한 기대값이다.
    constexpr float expectedWindAfter[3] = { 0.032f, 0.26f, 0.056f };
    float maxCustomBackendDelta = 0.f;
    float maxCustomExpectedDelta = 0.f;
    float maxCustomBefore = 0.f;
    float customMutationMagnitude = 0.f;
    for (std::size_t material = 0; material < 3; ++material)
    {
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            maxRegionDelta = (std::max)(maxRegionDelta, std::fabs(
                vkCapture.swatches[material][channel]
                    - dx12Capture.swatches[material][channel]));
            if (channel < 3)
            {
                maxExpectedDelta = (std::max)(maxExpectedDelta, std::fabs(
                    dx12Capture.swatches[material][channel]
                        - expectedSwatches[material][channel]));
                maxExpectedDelta = (std::max)(maxExpectedDelta, std::fabs(
                    vkCapture.swatches[material][channel]
                        - expectedSwatches[material][channel]));
            }
        }
    }
    for (uint32_t channel = 0; channel < 4; ++channel)
    {
        maxRegionDelta = (std::max)(maxRegionDelta, std::fabs(
            vkCapture.overlap[channel] - dx12Capture.overlap[channel]));
        maxOutside = (std::max)(maxOutside, std::fabs(vkCapture.outside[channel]));
        if (channel < 3)
        {
            maxExpectedDelta = (std::max)(maxExpectedDelta, std::fabs(
                dx12Capture.overlap[channel] - expectedOverlap[channel]));
            maxExpectedDelta = (std::max)(maxExpectedDelta, std::fabs(
                vkCapture.overlap[channel] - expectedOverlap[channel]));
            maxCustomBackendDelta = (std::max)(maxCustomBackendDelta,
                std::fabs(dx12Capture.customBefore[channel]
                    - vkCapture.customBefore[channel]));
            maxCustomBackendDelta = (std::max)(maxCustomBackendDelta,
                std::fabs(dx12Capture.customAfter[channel]
                    - vkCapture.customAfter[channel]));
            maxCustomBefore = (std::max)(maxCustomBefore,
                std::fabs(dx12Capture.customBefore[channel]));
            maxCustomBefore = (std::max)(maxCustomBefore,
                std::fabs(vkCapture.customBefore[channel]));
            maxCustomExpectedDelta = (std::max)(maxCustomExpectedDelta,
                std::fabs(dx12Capture.customAfter[channel]
                    - expectedWindAfter[channel]));
            maxCustomExpectedDelta = (std::max)(maxCustomExpectedDelta,
                std::fabs(vkCapture.customAfter[channel]
                    - expectedWindAfter[channel]));
            customMutationMagnitude = (std::max)(customMutationMagnitude,
                std::fabs(dx12Capture.customAfter[channel]
                    - dx12Capture.customBefore[channel]));
            customMutationMagnitude = (std::max)(customMutationMagnitude,
                std::fabs(vkCapture.customAfter[channel]
                    - vkCapture.customBefore[channel]));
        }
    }
    const float writtenDelta = (0 != dx12Capture.writtenPixels)
        ? std::fabs(static_cast<float>(vkCapture.writtenPixels) -
            static_cast<float>(dx12Capture.writtenPixels)) /
            static_cast<float>(dx12Capture.writtenPixels)
        : 1.f;

    char compare[1024]{};
    std::snprintf(compare, sizeof(compare),
        "[3/4] overlap RGB %.4f/%.4f,%.4f/%.4f,%.4f/%.4f · "
        "wind flow/time before→after G %.4f→%.4f/%.4f→%.4f · "
        "coverage %u/%u(편차 %.2f%%) · backend 최대 편차 %.5f · "
        "기대식 편차 %.5f/custom %.5f · outside %.5f\n",
        dx12Capture.overlap[0], vkCapture.overlap[0],
        dx12Capture.overlap[1], vkCapture.overlap[1],
        dx12Capture.overlap[2], vkCapture.overlap[2],
        dx12Capture.customBefore[1], dx12Capture.customAfter[1],
        vkCapture.customBefore[1], vkCapture.customAfter[1],
        dx12Capture.writtenPixels, vkCapture.writtenPixels,
        writtenDelta * 100.f, (std::max)(maxRegionDelta, maxCustomBackendDelta),
        maxExpectedDelta, maxCustomExpectedDelta, maxOutside);
    outLog += compare;

    if (0 == dx12Capture.writtenPixels || 0 == vkCapture.writtenPixels ||
        writtenDelta > 0.02f || maxRegionDelta > 0.02f ||
        maxCustomBackendDelta > 0.02f || maxExpectedDelta > 0.025f ||
        maxCustomBefore > 0.001f || maxCustomExpectedDelta > 0.025f ||
        customMutationMagnitude < 0.20f || maxOutside > 0.001f)
    {
        passed = false;
        outLog += "Forward+ 픽셀·coverage 대조 허용 범위를 벗어났다\n";
    }

    outLog += "[4/4] P2d-e material-cache ShaderMeta scan 0·빈 packet primary-only 1/1·raw Material texture alias/setter 0·"
        "required-asset GUID packet 2개·non-cache Material owner 해제 뒤 유지·cache 로드 전 Water/Wind generation owner·canonical seed 0·"
        "P2d-c windMap@t4 generic schema/owner vector·P2d-b m_flowInfo 32B+frame total/delta immutable snapshot·NaN fail-closed·"
        "Foliage type 1개·view-culling draw source 2개·owner 반환·"
        "Forward frame ShaderMeta owner 3개·48/64B material packet 5개·"
        "primary/water/wind PSO A/B/A/C·인접 7 draw→4 batch·"
        "wind property+flow/time 다음-frame 픽셀·invalid meta/generation/register fail-closed·"
        "owner 원본 해제 뒤 양 backend 유지 · compute PSO·depth SRV·structured UAV table·"
        "UAV→SRV/Copy buffer barrier·root buffer·dynamic sampler×3·indexed mesh · 미구현 " +
        std::to_string(stubs) + " · Vulkan validation " +
        std::to_string(problems) + "건\n";
    if (!captured && !error.empty()) outLog += error + "\n";
    if (!validation.empty()) outLog += validation;

    resources.WaitForGpu();
    textures.Shutdown();
    meshes.Shutdown();
    pipelines.Shutdown();
    resources.Shutdown();

    for (EnhancedDrawItem& draw : fixture.draws)
        draw.forwardMaterialSnapshot.reset();
    packets = {};
    mutatedWindPacket.reset();
    invalidFlowPacket.reset();
    shaderMetaFrame.forwardShaderMetas.clear();
    const bool packetReleasedTextures = std::all_of(emissionLifetimes.begin(),
        emissionLifetimes.end(), [](const std::weak_ptr<Texture>& owner)
        {
            return owner.expired();
        }) && genericWindLifetime.expired();
    const bool frameReleasedShaderMetas = std::all_of(
        frameLifetimes.begin(), frameLifetimes.end(),
        [](const std::weak_ptr<const ShaderMeta>& owner)
        {
            return owner.expired();
        });
    if (!packetReleasedTextures || !frameReleasedShaderMetas)
    {
        passed = false;
        outLog += "Forward packet 해제 뒤 texture/ShaderMeta owner가 반환되지 않았다\n";
    }

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
            item.worldMatrix = math::scaling_matrix(math::vector3{ 0.75f, 0.75f, 0.8f }) *
                math::translation_matrix(math::vector3{ 0.f, 0.f, 0.5f });
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
        camera.projection = math::perspective_fov_lh(math::half_pi, 1.f, kNearZ, kFarZ);
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
        camera.projection = math::perspective_fov_lh(math::half_pi, 1.f, 0.1f, 100.f);
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
