#ifndef DYNAMICCPP_EXPORTS
#include "../../EnhancedIBLGenerator.h"
#include "../../../../Render/Passes/Geometry/EnhancedShadowPass.h"
#include "../../../../Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "../../../../Render/Passes/Geometry/EnhancedDeferredPass.h"
#include "../../DX12DeviceResources.h"
#include "../../DX12PSOManager.h"
#include "../../DX12RootSignatureCache.h"
#include "../../DX12MeshCache.h"
#include "../../DX12TextureCache.h"
#include "../../../../Render/Graph/EnhancedRenderGraph.h"
#include "../../../../Render/Scene/EnhancedSceneRenderer.h"
#include "../../Mesh.h"
// DeviceState.h include가 여기 있었다 (E, 2026-08-09).
// 이 파일에서 DirectX11:: 심볼을 쓰는 코드가 0이다.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

// IBL 앰비언트 소비 검증 (PHASE 3-6 — 스카이박스 계열 마지막 조각).
//
// ── 셋을 따로 단정한다 ──
//
//   ① 끔 = 검정 — 광원 0개 + IBL 미설정이면 출력이 0이다(기존 동작 보존).
//      이게 서야 ②의 밝음이 앰비언트 항에서 온 것이라고 말할 수 있다.
//   ② 방향성   — 반구가 갈린 IBL(위 빨강·아래 초록)에서 위를 보는 바닥은
//      빨강 우세, 아래를 보는 천장은 초록 우세여야 한다. 조도를 법선으로
//      읽는 배선이 뒤집히면 두 색이 맞바뀐다 — '밝아졌다'만 보면 못 잡는다.
//   ③ 정반사   — 매끈한 금속 바닥은 반사 방향(위 반구) 프리필터 색을
//      반사한다. 확산이 0(금속)이라 이 밝음은 스플릿섬 두 번째 항에서만
//      올 수 있다 — LUT·프리필터 배선의 증명이다.
//
// 광원을 0개로 두는 것이 설계의 요점이다 — 출력 = 앰비언트 + 방출뿐이라
// 앰비언트 항이 다른 빛에 섞이지 않고 그대로 보인다.
namespace
{
    constexpr uint32_t kIblShadeWidth = 256;
    constexpr uint32_t kIblShadeHeight = 256;

    constexpr uint32_t kIblShadeEquirectWidth = 256;
    constexpr uint32_t kIblShadeEquirectHeight = 128;
    constexpr uint16_t kIblShadeHalfOne = 0x3C00;

    struct IblShadeCapture
    {
        RHIReadbackImage image;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            return image.At(x, y, channel);
        }

        float MaxCenterChannel(uint32_t channel) const
        {
            float best = 0.f;
            for (uint32_t y = kIblShadeHeight / 4; y < kIblShadeHeight * 3 / 4; ++y)
                for (uint32_t x = kIblShadeWidth / 4; x < kIblShadeWidth * 3 / 4; ++x)
                    best = (std::max)(best, At(x, y, channel));
            return best;
        }
    };

    bool IblShadeProject(const Mathf::xMatrix& view, const Mathf::xMatrix& projection,
        float worldX, float worldY, float worldZ, uint32_t& outX, uint32_t& outY)
    {
        const Mathf::xMatrix vp = XMMatrixMultiply(view, projection);
        const Mathf::xVector clip = XMVector4Transform(
            XMVectorSet(worldX, worldY, worldZ, 1.f), vp);
        const float w = XMVectorGetW(clip);
        if (w <= 1e-6f) return false;

        const float ndcX = XMVectorGetX(clip) / w;
        const float ndcY = XMVectorGetY(clip) / w;
        if (ndcX < -1.f || ndcX > 1.f || ndcY < -1.f || ndcY > 1.f) return false;

        outX = static_cast<uint32_t>((ndcX * 0.5f + 0.5f) * static_cast<float>(kIblShadeWidth));
        outY = static_cast<uint32_t>((0.5f - ndcY * 0.5f) * static_cast<float>(kIblShadeHeight));
        if (outX >= kIblShadeWidth) outX = kIblShadeWidth - 1;
        if (outY >= kIblShadeHeight) outY = kIblShadeHeight - 1;
        return true;
    }

    void IblShadeQuad(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices,
        const Mathf::Vector3& origin, const Mathf::Vector3& axisU,
        const Mathf::Vector3& axisV, const Mathf::Vector3& normal)
    {
        const uint32_t base = static_cast<uint32_t>(outVertices.size());
        const Mathf::Vector3 corners[4] = {
            origin, origin + axisU, origin + axisU + axisV, origin + axisV };
        for (const auto& corner : corners)
        {
            Vertex vertex{};
            vertex.position = corner;
            vertex.normal = normal;
            outVertices.push_back(vertex);
        }
        outIndices.push_back(base + 0); outIndices.push_back(base + 1); outIndices.push_back(base + 2);
        outIndices.push_back(base + 0); outIndices.push_back(base + 2); outIndices.push_back(base + 3);
    }
}

bool EnhancedSceneRenderer::RunIBLShadeTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── IBL 앰비언트 소비 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kIblShadeWidth, kIblShadeHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(&resources, L"dx12_iblshade.cache", error) ||
        !rootSignatures.Initialize(&resources, error) ||
        !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.textureCache = &textureCache;
    frameContext.width = kIblShadeWidth;
    frameContext.height = kIblShadeHeight;

    EnhancedShadowPass shadow;
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedIBLGenerator generator;
    if (!shadow.Initialize(frameContext, error) ||
        !gbuffer.Initialize(frameContext, error) ||
        !deferred.Initialize(frameContext, error) ||
        !generator.Initialize(frameContext, error))
    {
        outLog += "[1/4] 패스 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    gbuffer.SetKeepAlive(false);
    outLog += "[1/4] 패스 3종 + IBL 생성기 초기화 통과\n";

    // ── [2/4] 반구 IBL 생성 — 위 빨강 · 아래 초록 ──
    ComPtr<ID3D12Resource> equirect;
    RHITextureHandle equirectHandle;
    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kIblShadeEquirectWidth;
        desc.Height = kIblShadeEquirectHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&equirect))))
        {
            outLog += "[2/4] equirect 생성 실패\n";
            resources.Shutdown();
            return false;
        }
        equirectHandle = resources.RegisterExternalTexture(equirect.Get());
        if (!equirectHandle.IsValid())
        {
            outLog += "[2/4] equirect 핸들 등록 실패\n";
            resources.Shutdown();
            return false;
        }

        constexpr uint32_t kRowPitch = kIblShadeEquirectWidth * 8;

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        const auto upload = resources.AllocateUpload(
            RHIUploadRequest{ kRowPitch * kIblShadeEquirectHeight,
                RHIUploadUsage::TextureCopy, 1 });
        if (!upload.IsValid())
        {
            outLog += "[2/4] 업로드 링 할당 실패\n";
            resources.Shutdown();
            return false;
        }

        for (uint32_t y = 0; y < kIblShadeEquirectHeight; ++y)
        {
            auto* row = reinterpret_cast<uint16_t*>(
                static_cast<uint8_t*>(upload.cpuAddress) + y * kRowPitch);
            const bool top = y < kIblShadeEquirectHeight / 2;
            for (uint32_t x = 0; x < kIblShadeEquirectWidth; ++x)
            {
                row[x * 4 + 0] = top ? kIblShadeHalfOne : 0;
                row[x * 4 + 1] = top ? 0 : kIblShadeHalfOne;
                row[x * 4 + 2] = 0;
                row[x * 4 + 3] = kIblShadeHalfOne;
            }
        }

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = resources.Resolve(upload.buffer);
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = upload.offset;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        src.PlacedFootprint.Footprint.Width = kIblShadeEquirectWidth;
        src.PlacedFootprint.Footprint.Height = kIblShadeEquirectHeight;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = kRowPitch;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = equirect.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = equirect.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        resources.GetCommandList()->ResourceBarrier(1, &barrier);

        if (!generator.Generate(frameContext, equirectHandle,
            RHIFormat::RGBA16Float, 64, 64, error))
        {
            outLog += "[2/4] IBL 생성 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();
        outLog += "[2/4] 반구 IBL(위 빨강·아래 초록) 생성 완료\n";
    }

    // ── 합성 씬 — 위를 보는 바닥, 아래를 보는 천장 ──
    std::vector<Vertex> floorVertices;
    std::vector<uint32_t> floorIndices;
    IblShadeQuad(floorVertices, floorIndices,
        { -40.f, 0.f, -40.f }, { 80.f, 0.f, 0.f }, { 0.f, 0.f, 80.f }, { 0.f, 1.f, 0.f });
    Mesh floorMesh("dx12_iblshade_floor", floorVertices, floorIndices);
    floorMesh.RecalculateBounds();

    std::vector<Vertex> ceilingVertices;
    std::vector<uint32_t> ceilingIndices;
    IblShadeQuad(ceilingVertices, ceilingIndices,
        { -40.f, 4.f, -40.f }, { 80.f, 0.f, 0.f }, { 0.f, 0.f, 80.f }, { 0.f, -1.f, 0.f });
    Mesh ceilingMesh("dx12_iblshade_ceiling", ceilingVertices, ceilingIndices);
    ceilingMesh.RecalculateBounds();

    // 거친 비금속 둘 — 앰비언트가 거의 확산(조도 x 알베도)이라 방향성이 또렷하다.
    // 매끈 금속 검증은 바닥 재질을 바꾼 세 번째 렌더가 한다.
    std::vector<EnhancedDrawItem> draws(2);
    draws[0].mesh = &floorMesh;
    draws[0].worldMatrix = XMMatrixIdentity();
    draws[0].metallic = 0.f;
    draws[0].roughness = 1.f;
    draws[1].mesh = &ceilingMesh;
    draws[1].worldMatrix = XMMatrixIdentity();
    draws[1].metallic = 0.f;
    draws[1].roughness = 1.f;

    const std::vector<EnhancedLight> noLights;   // 광원 0개 — 출력 = 앰비언트뿐

    FrameCameraSnapshot camera{};
    {
        const Mathf::xVector eye = XMVectorSet(0.f, 2.f, -8.f, 1.f);
        const Mathf::xVector at = XMVectorSet(0.f, 2.f, 4.f, 1.f);
        const Mathf::xVector up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
        camera.view = XMMatrixLookAtLH(eye, at, up);
        camera.projection = XMMatrixPerspectiveFovLH(DirectX::XM_PI / 3.f, 1.f, 0.1f, 200.f);
        camera.inverseView = XMMatrixInverse(nullptr, camera.view);
        camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
        camera.eyePosition = eye;
        camera.forward = XMVector3Normalize(XMVectorSubtract(at, eye));
        camera.right = XMVector3Normalize(XMVector3Cross(up, camera.forward));
        camera.up = XMVector3Cross(camera.forward, camera.right);
        camera.fov = DirectX::XM_PI / 3.f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 200.f;
        camera.isOrthographic = false;
    }

    RHIReadback readback{};
    if (!resources.CreateReadback(kIblShadeWidth, kIblShadeHeight,
        EnhancedDeferredPass::kOutputFormat, 1, readback, error))
    {
        outLog += "[2/4] 리드백 생성 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    bool passed = true;

    const auto renderOnce = [&](const std::vector<EnhancedDrawItem>& frameDraws,
        bool useIbl, IblShadeCapture& outCapture) -> bool
    {
        frameContext.camera = &camera;
        frameContext.draws = &frameDraws;
        frameContext.lights = &noLights;

        if (!resources.BeginFrame(error))
        {
            outLog += "BeginFrame 실패: " + error + "\n";
            return false;
        }

        if (!shadow.PrepareFrame(frameContext, error) ||
            !gbuffer.PrepareFrame(frameContext, error) ||
            !deferred.PrepareFrame(frameContext, error))
        {
            outLog += "PrepareFrame 실패: " + error + "\n";
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);

        shadow.Declare(graph, frameContext);
        gbuffer.Declare(graph, frameContext);

        deferred.SetInputs(gbuffer.GetOutputs());
        deferred.SetShadow(shadow.GetShadowMap(), shadow.GetShadowData());
        if (useIbl)
        {
            deferred.SetIBL(generator.GetIrradianceMap(), generator.GetPrefilteredMap(),
                EnhancedIBLGenerator::kPrefilterMips, generator.GetBrdfLut());
        }
        else
        {
            deferred.SetIBL({}, {}, 1, {});
        }
        deferred.Declare(graph, frameContext);

        const RGHandle output = deferred.GetOutput();
        if (!output.IsValid())
        {
            outLog += "Deferred 출력이 없다\n";
            return false;
        }

        graph.AddPass("IBLShade.Readback",
            { { output, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyToReadback( readback,
                    executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(error))
        {
            outLog += "Compile 실패: " + error + "\n";
            return false;
        }
        if (!graph.Execute(error))
        {
            outLog += "Execute 실패: " + error + "\n";
            return false;
        }

        if (!resources.EndFrame(error))
        {
            outLog += "EndFrame 실패: " + error + "\n";
            return false;
        }
        resources.WaitForGpu();

        if (!resources.MapReadback(readback, outCapture.image, error))
        {
            outLog += "리드백 Map 실패: " + error + "\n";
            return false;
        }
        return true;
    };

    uint32_t floorX = 0, floorY = 0, ceilingX = 0, ceilingY = 0;
    if (!IblShadeProject(camera.view, camera.projection, 0.f, 0.f, 2.f, floorX, floorY) ||
        !IblShadeProject(camera.view, camera.projection, 0.f, 4.f, 2.f, ceilingX, ceilingY))
    {
        outLog += "[3/4] 표본 투영 실패\n";
        passed = false;
    }

    // ── [3/4] 끔 = 검정, 켬 = 법선 방향의 조도 ──
    if (passed)
    {
        IblShadeCapture offCapture{};
        IblShadeCapture onCapture{};
        if (!renderOnce(draws, false, offCapture) || !renderOnce(draws, true, onCapture))
        {
            passed = false;
        }
        else
        {
            const float offMax = (std::max)({ offCapture.MaxCenterChannel(0),
                offCapture.MaxCenterChannel(1), offCapture.MaxCenterChannel(2) });

            const float floorR = onCapture.At(floorX, floorY, 0);
            const float floorG = onCapture.At(floorX, floorY, 1);
            const float ceilingR = onCapture.At(ceilingX, ceilingY, 0);
            const float ceilingG = onCapture.At(ceilingX, ceilingY, 1);

            char line[256]{};
            std::snprintf(line, sizeof(line),
                "[3/4] 끔 최대 %.4f · 켬 바닥(R %.3f G %.3f, px %u,%u) · "
                "천장(R %.3f G %.3f, px %u,%u)\n",
                offMax, floorR, floorG, floorX, floorY,
                ceilingR, ceilingG, ceilingX, ceilingY);
            outLog += line;

            // ① 끔 = 검정. 광원 0개에서 밝으면 앰비언트가 아닌 다른 것이 샌 것이다.
            if (offMax > 0.01f)
            {
                outLog += "IBL 없이 밝다 — 앰비언트 외의 빛이 샌다\n";
                passed = false;
            }

            // ② 방향성 — 바닥(법선 +Y)은 빨강 우세, 천장(법선 -Y)은 초록 우세.
            if (floorR < 0.05f || floorR < floorG * 2.f)
            {
                outLog += "바닥이 빨강 우세가 아니다 — 조도 방향 배선이 틀렸다\n";
                passed = false;
            }
            if (ceilingG < 0.05f || ceilingG < ceilingR * 2.f)
            {
                outLog += "천장이 초록 우세가 아니다 — 조도 방향 배선이 틀렸다\n";
                passed = false;
            }
        }
    }

    // ── [4/4] 정반사 — 매끈 금속 바닥은 반사 방향(위 반구)의 프리필터 색 ──
    if (passed)
    {
        std::vector<EnhancedDrawItem> mirrorDraws = draws;
        mirrorDraws[0].metallic = 1.f;
        mirrorDraws[0].roughness = 0.f;

        IblShadeCapture mirrorCapture{};
        if (!renderOnce(mirrorDraws, true, mirrorCapture))
        {
            passed = false;
        }
        else
        {
            const float mirrorR = mirrorCapture.At(floorX, floorY, 0);
            const float mirrorG = mirrorCapture.At(floorX, floorY, 1);

            char line[160]{};
            std::snprintf(line, sizeof(line),
                "[4/4] 매끈 금속 바닥 — R %.3f G %.3f\n", mirrorR, mirrorG);
            outLog += line;

            // 금속은 확산이 0이라 이 밝음은 스플릿섬 정반사 항에서만 온다.
            // 바닥에서 위를 보는 반사는 위 반구(빨강)를 향한다.
            if (mirrorR < 0.05f)
            {
                outLog += "매끈 금속이 어둡다 — 프리필터·LUT 배선이 죽었다\n";
                passed = false;
            }
            if (mirrorR < mirrorG * 2.f)
            {
                outLog += "금속 반사가 빨강 우세가 아니다 — 반사 방향이 틀렸다\n";
                passed = false;
            }
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    generator.Shutdown();
    resources.ReleaseTexture(equirectHandle);
    deferred.Shutdown();
    gbuffer.Shutdown();
    shadow.Shutdown();
    textureCache.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "IBL 앰비언트 소비 검증 통과\n" : "IBL 앰비언트 소비 검증 실패\n";
    return passed;
}

#endif
