// METEOR blended stream check: light tiles + METEOR.Scene.Blended over the
// composed opaque scene, on the real cooked fixture route.
#include "GraphSceneFixture.h"
#include "Render/Passes/Lighting/EnhancedLightTilePass.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12MeshCache.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "Mesh.h"

#include "../../../../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    constexpr uint32_t kBlendSize = 64;

    // Camera-facing quad on the z = depth plane.
    void BuildQuad(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, float half, float depth)
    {
        vertices.assign(4, Vertex{});
        const float corners[4][2]{{-half, -half}, {half, -half}, {half, half}, {-half, half}};
        for (uint32_t i = 0; i < 4; ++i)
        {
            vertices[i].position = {corners[i][0], corners[i][1], depth};
            vertices[i].normal = {0.f, 0.f, -1.f};
            vertices[i].tangent = {1.f, 0.f, 0.f};
            vertices[i].bitangent = {0.f, 1.f, 0.f};
            vertices[i].uv0 = {corners[i][0] > 0.f ? 1.f : 0.f, corners[i][1] > 0.f ? 1.f : 0.f};
        }
        indices = {0, 2, 1, 0, 3, 2};
    }

    struct BlendCapture
    {
        RHIReadbackImage color;
        RHIReadbackImage depth;
        size_t lightTilePasses{};
        size_t blendedPasses{};
        size_t blendedDraws{};
        size_t preparedDraws{};
    };
}

// Two frames on the same opaque ground: without and with a 50% blended plane in
// front. The plane must change only its covered pixels, keep the opaque depth,
// and run exactly through the light-tile and blended-stream passes.
bool DX12Test::RunBlendedStreamTest(std::string& outLog)
{
    outLog += "── METEOR 반투명 스트림 검증 ──\n";
    std::string error;
    DX12DeviceResources resources;
    if (!resources.Initialize(kBlendSize, kBlendSize, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }
    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(&resources, L"dx12_blended.cache", error) ||
        !rootSignatures.Initialize(&resources, error) || !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    EnhancedFrameContext context{};
    context.resources = &resources;
    context.psoManager = &psoManager;
    context.rootSignatures = &rootSignatures;
    context.meshCache = &meshCache;
    context.textureCache = &textureCache;
    context.width = context.height = kBlendSize;

    FrameCameraSnapshot camera{};
    const math::vector3 eye{0.f, 0.f, -10.f};
    camera.view = math::look_at_lh(eye, math::vector3{}, math::vector3::unit_y());
    camera.projection = math::perspective_fov_lh(math::pi * 0.25f, 1.f, 0.1f, 100.f);
    camera.inverseView = math::inverse(camera.view);
    camera.inverseProjection = math::inverse(camera.projection);
    camera.eyePosition = eye;
    camera.forward = {0.f, 0.f, 1.f};
    camera.right = {1.f, 0.f, 0.f};
    camera.up = {0.f, 1.f, 0.f};
    camera.fov = 45.f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.f;
    context.camera = &camera;

    EnhancedLight light{};
    light.position = {0.f, 0.f, 0.f, 0.f};
    light.direction = {0.f, 0.f, 1.f, 0.f};
    light.color = math::color(1.f, 1.f, 1.f, 1.f);
    const std::vector<EnhancedLight> lights{light};
    context.lights = &lights;

    GraphSceneFixture fixture;
    EnhancedLightTilePass tiles;
    if (!fixture.Initialize(context, error, 0.5f) || !tiles.Initialize(context, error))
    {
        outLog += "[1/4] 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    std::vector<Vertex> groundVertices, planeVertices;
    std::vector<uint32_t> groundIndices, planeIndices;
    BuildQuad(groundVertices, groundIndices, 6.f, 0.f);
    BuildQuad(planeVertices, planeIndices, 1.5f, 0.f);
    const auto groundMesh = fixture.AddMesh(groundVertices, groundIndices);
    const auto planeMesh = fixture.AddMesh(planeVertices, planeIndices);
    outLog += "[1/4] 고정 머티리얼·타일 컬링 초기화 통과\n";

    RHIReadback colorReadback{}, depthReadback{};
    if (!resources.CreateReadback(kBlendSize, kBlendSize, RHIFormat::RGBA16Float, 1, colorReadback, error) ||
        !resources.CreateReadback(kBlendSize, kBlendSize, RHIFormat::D32Float, 1, depthReadback, error))
    {
        outLog += "[1/4] 리드백 생성 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    const auto render = [&](bool withPlane, BlendCapture& capture) -> bool
    {
        std::vector<EnhancedDrawItem> draws{fixture.Draw(groundMesh, math::matrix4x4::identity())};
        // Product rule: light tiles run only for frames with blended draws.
        context.forwardLightingConsumer = withPlane;
        if (withPlane)
        {
            // Same material as the ground; a 60-degree tilt gives the plane a
            // different N.L so a 50% blend is visible.
            auto world = math::matrix4x4::identity();
            const float c = std::cos(math::pi / 3.f), s = std::sin(math::pi / 3.f);
            world.m[0][0] = c; world.m[0][2] = -s;
            world.m[2][0] = s; world.m[2][2] = c;
            world.m[3][2] = -2.f;
            auto plane = fixture.Draw(planeMesh, world);
            plane.coverage.flags |= EnhancedMaterialCoverage::Blended;
            draws.push_back(plane);
        }
        context.draws = &draws;
        ++context.frameId;
        if (!resources.BeginFrame(error) || !fixture.PrepareFrame(context, error) || !tiles.PrepareFrame(context, error))
        {
            outLog += "준비 실패: " + error + "\n";
            return false;
        }
        capture.blendedDraws = fixture.BlendedDrawCount();
        capture.preparedDraws = fixture.PreparedDrawCount();
        EnhancedRenderGraph graph(resources, RGSchedulingMode::ExplicitVersioned, RGOrderPolicy::DependencyOrder);
        fixture.DeclareSurface(graph, context);
        const auto depth = fixture.GetOutputs().depth;
        const auto opaque = fixture.DeclareColor(graph, context,
            material_graph::SceneHost::DeclareHdrTarget(graph, context), {});
        tiles.SetDepth(depth);
        tiles.Declare(graph, context);
        const auto color = fixture.DeclareBlended(graph, opaque, {}, tiles.GetLighting());
        graph.AddPass("METEOR.BlendTest.ColorReadback", {{color, RHIResourceState::CopySource, RGAccessMode::Read}},
            [&, color](const auto& execution) { execution.encoder->CopyToReadback(colorReadback, execution.ResolveHandle(color)); },
            true);
        graph.AddPass("METEOR.BlendTest.DepthReadback", {{depth, RHIResourceState::CopySource, RGAccessMode::Read}},
            [&, depth](const auto& execution) { execution.encoder->CopyToReadback(depthReadback, execution.ResolveHandle(depth)); },
            true);
        EnhancedRenderGraph::DiagnosticSnapshot snapshot;
        if (!graph.Compile(error) || !graph.CaptureDiagnosticSnapshot(snapshot))
        {
            outLog += "Compile 실패: " + error + "\n";
            return false;
        }
        for (const auto& pass : snapshot.passes)
        {
            if (pass.culled) continue;
            capture.lightTilePasses += pass.name == "METEOR.Scene.LightTiles";
            capture.blendedPasses += pass.name.rfind("METEOR.Blended.", 0) == 0;
        }
        if (!graph.Execute(error) || !resources.EndFrame(error) ||
            !fixture.Publish(resources, context.frameId, error) ||
            !GraphSceneFixture::CompleteSubmission(resources, error))
        {
            outLog += "실행·발행 실패: " + error + "\n";
            return false;
        }
        return resources.MapReadback(colorReadback, capture.color, error) &&
               resources.MapReadback(depthReadback, capture.depth, error);
    };

    bool passed = true;
    BlendCapture opaqueOnly, blended;
    if (!render(false, opaqueOnly) || !render(true, blended))
    {
        passed = false;
    }
    else
    {
        outLog += "[2/4] 두 프레임 렌더·발행 통과 — 투명 드로우 " + std::to_string(opaqueOnly.blendedDraws) + " → " +
            std::to_string(blended.blendedDraws) + " · 준비 드로우 " + std::to_string(opaqueOnly.preparedDraws) + " → " +
            std::to_string(blended.preparedDraws) + " · 타일 패스 " + std::to_string(blended.lightTilePasses) +
            " · 투명 패스 " + std::to_string(blended.blendedPasses) + "\n";
        if (opaqueOnly.lightTilePasses != 0 || opaqueOnly.blendedPasses != 0 ||
            blended.lightTilePasses != 1 || blended.blendedPasses == 0)
        {
            outLog += "반투명 스트림 패스 구성이 틀렸다\n";
            passed = false;
        }
        const uint32_t center = kBlendSize / 2, corner = 2;
        float centerDelta = 0.f, cornerDelta = 0.f;
        bool finite = true;
        for (uint32_t c = 0; c < 3; ++c)
        {
            const float a = opaqueOnly.color.At(center, center, c), b = blended.color.At(center, center, c);
            finite = finite && std::isfinite(a) && std::isfinite(b);
            centerDelta = (std::max)(centerDelta, std::abs(a - b));
            cornerDelta = (std::max)(cornerDelta,
                std::abs(opaqueOnly.color.At(corner, corner, c) - blended.color.At(corner, corner, c)));
        }
        const float depthA = opaqueOnly.depth.At(center, center, 0), depthB = blended.depth.At(center, center, 0);
        char line[256]{};
        std::snprintf(line, sizeof(line), "[3/4] 중앙 변화 %.5f · 구석 변화 %.6f · 중앙 깊이 %.6f → %.6f\n",
            centerDelta, cornerDelta, depthA, depthB);
        outLog += line;
        if (!finite || centerDelta < 1e-3f)
        {
            outLog += "반투명 평면이 덮은 픽셀이 바뀌지 않았다\n";
            passed = false;
        }
        if (cornerDelta > 1e-4f)
        {
            outLog += "반투명 평면 밖 픽셀이 바뀌었다\n";
            passed = false;
        }
        if (depthA != depthB || depthA >= 1.f)
        {
            outLog += "반투명 스트림이 불투명 깊이를 바꿨다\n";
            passed = false;
        }
    }
    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (problems)
    {
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
        passed = false;
    }
    else outLog += "[4/4] GPU 검증 문제 0건\n";
    resources.WaitForGpu();
    resources.ReleaseReadback(colorReadback);
    resources.ReleaseReadback(depthReadback);
    tiles.Shutdown();
    fixture.Shutdown();
    textureCache.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();
    outLog += passed ? "METEOR 반투명 스트림 검증 통과\n" : "METEOR 반투명 스트림 검증 실패\n";
    return passed;
}
