#ifndef DYNAMICCPP_EXPORTS
#include "../../../../Render/Passes/Lighting/EnhancedVolumetricFogPass.h"
#include "../../DX12DeviceResources.h"
#include "../../DX12PSOManager.h"
#include "../../DX12RootSignatureCache.h"
#include "../DX12TestTextureRegistration.h"
#include "../../../../Render/Graph/EnhancedRenderGraph.h"
#include "../../../../Render/Scene/EnhancedSceneRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

// 볼류메트릭 포그 패스 자가 검증 (PHASE 3-6, 미구현 패스 이식 4차).
//
// ── 다섯을 따로 단정한다 ──
//
//   ① 산란 — 격자에 빛이 실리는가. 대조군은 광원 0개다.
//   ② 누적 — z를 따라 투과율이 단조 감소하는가. 이것이 누적의 정체성이다.
//      앞에서 뒤로 훑으며 곱해 나가는 것이라 뒤로 갈수록 반드시 줄어든다.
//   ③ ★ 시간축 히스토리 — 프레임을 넘겨 격자가 이어지는가.
//      광원을 다 끄고 히스토리 100%로 한 프레임 더 돌린다. 이번 프레임에
//      빛이 하나도 없는데 포그가 남아 있다면, 그 값은 지난 프레임에서만
//      올 수 있다. 핑퐁이 실제로 도는지를 이 뒤집힌 조건이 증명한다.
//   ④ 합성 — 포그가 씬 색에 얹히는가. 대조군은 혼합 계수 0이다
//      (그때는 입력이 한 치도 바뀌면 안 된다).
//   ⑤ 패스 넷 — 산란·누적·합성·리드백. 복사가 없다는 증거다.
namespace
{
    constexpr uint32_t kFogScreen = 256;
    constexpr uint32_t kFogVolumeW = EnhancedVolumetricFogPass::kVolumeWidth;
    constexpr uint32_t kFogVolumeH = EnhancedVolumetricFogPass::kVolumeHeight;
    constexpr uint32_t kFogVolumeD = EnhancedVolumetricFogPass::kVolumeDepth;

    // 씬 색은 회색으로 둔다 — 포그가 얹히면 밝기가 움직인다.
    constexpr float kFogSceneGray = 0.5f;

    // 깊이 0.99는 near 0.5 / far 1000 원근에서 약 48유닛이다.
    constexpr float kFogSceneDepth = 0.99f;

    uint16_t FogFloatToHalf(float value)
    {
        uint32_t bits;
        memcpy(&bits, &value, 4);
        const uint32_t sign = (bits >> 16) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
        const uint32_t mantissa = bits & 0x7FFFFFu;

        if (exponent <= 0) return static_cast<uint16_t>(sign);
        if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10)
            | (mantissa >> 13));
    }

    // 격자는 깊이 한 켜가 장 하나다(R2c-b2) — 3D 리드백의 배치가 그 모양이라
    // 캡처 타입 둘이 RHIReadbackImage 하나로 합쳐졌다. 격자는 At(x, y, 채널, z),
    // 화면은 At(x, y, 채널)이다.
}

bool EnhancedSceneRenderer::RunVolumetricFogTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 볼류메트릭 포그 패스 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kFogScreen, kFogScreen, error))
    {
        outLog += "[1/5] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_fog.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/5] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kFogScreen;
    frameContext.height = kFogScreen;

    // 포그 수식이 원근을 전제한다(슬라이스가 지수 분포다). 커스텀 근/원
    // 평면과 투영을 맞춰 두어야 격자와 화면이 같은 공간을 가리킨다.
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixPerspectiveFovLH(DirectX::XM_PIDIV4, 1.f, 0.5f, 1000.f);
    camera.inverseView = XMMatrixInverse(nullptr, camera.view);
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
    camera.eyePosition = XMVectorSet(0.f, 0.f, 0.f, 1.f);
    camera.nearPlane = 0.5f;
    camera.farPlane = 1000.f;
    camera.fov = DirectX::XM_PIDIV4;
    frameContext.camera = &camera;

    EnhancedVolumetricFogPass fog;
    if (!fog.Initialize(frameContext, error))
    {
        outLog += "[1/5] 포그 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    // 그림자 행렬은 볼륨 전체를 덮어야 한다.
    //
    // 처음에 단위행렬을 넣었다가 [4/5]가 잡았다: 광원공간 깊이가 곧 월드 z가
    // 되어 z가 1을 넘는 순간(슬라이스 22쯤) 비교가 전부 '가려짐'으로 떨어지고,
    // 투과율이 그 지점부터 멈춰 버린다. 카메라 원평면(1000)까지 [0,1]에
    // 들어오도록 넉넉한 정사영을 쓴다.
    fog.SetShadowMatrix(XMMatrixOrthographicLH(2000.f, 2000.f, 0.f, 2000.f));
    outLog += "[1/5] 셰이더 3종 컴파일·격자 3장(160x90x128) 생성 통과\n";

    // ── 합성 입력 ──
    ComPtr<ID3D12Resource> sceneColor;
    ComPtr<ID3D12Resource> sceneDepth;
    ComPtr<ID3D12Resource> shadowMap;
    ComPtr<ID3D12Resource> cloudMap;
    ComPtr<ID3D12Resource> blueNoise;
    DX12TestTextureRegistration colorRegistration;
    DX12TestTextureRegistration depthRegistration;
    DX12TestTextureRegistration shadowRegistration;
    DX12TestTextureRegistration cloudRegistration;
    DX12TestTextureRegistration noiseRegistration;

    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        const auto makeTexture = [&](DXGI_FORMAT format, uint32_t width, uint32_t height,
            uint32_t arraySize, ComPtr<ID3D12Resource>& out) -> bool
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width;
            desc.Height = height;
            desc.DepthOrArraySize = static_cast<UINT16>(arraySize);
            desc.MipLevels = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;

            return SUCCEEDED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&out)));
        };

        if (!makeTexture(DXGI_FORMAT_R16G16B16A16_FLOAT, kFogScreen, kFogScreen, 1, sceneColor) ||
            !makeTexture(DXGI_FORMAT_R32_FLOAT, kFogScreen, kFogScreen, 1, sceneDepth) ||
            !makeTexture(DXGI_FORMAT_R32_FLOAT, 64, 64, kShadowCascadeCount, shadowMap) ||
            !makeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 1, cloudMap) ||
            !makeTexture(DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, blueNoise))
        {
            outLog += "[2/5] 입력 텍스처 생성 실패\n";
            resources.Shutdown();
            return false;
        }

        colorRegistration.Register(resources, sceneColor.Get());
        depthRegistration.Register(resources, sceneDepth.Get());
        shadowRegistration.Register(resources, shadowMap.Get());
        cloudRegistration.Register(resources, cloudMap.Get());
        noiseRegistration.Register(resources, blueNoise.Get());
        if (!colorRegistration.IsValid() || !depthRegistration.IsValid() ||
            !shadowRegistration.IsValid() || !cloudRegistration.IsValid() ||
            !noiseRegistration.IsValid())
        {
            outLog += "[2/5] 입력 텍스처 핸들 등록 실패\n";
            resources.Shutdown();
            return false;
        }

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/5] BeginFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // 한 장(또는 배열 한 슬라이스)을 바이트 단위로 채워 올린다.
        const auto upload = [&](ID3D12Resource* target, DXGI_FORMAT format,
            uint32_t width, uint32_t height, uint32_t bytesPerTexel, uint32_t subresource,
            const std::function<void(uint32_t, uint32_t, void*)>& fill) -> bool
        {
            const uint32_t rawPitch = width * bytesPerTexel;
            const uint64_t pitch = (rawPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

            const auto allocation = resources.AllocateUpload(
                RHIUploadRequest{ pitch * height, RHIUploadUsage::TextureCopy, 1 });
            if (!allocation.IsValid()) return false;

            auto* base = static_cast<uint8_t*>(allocation.cpuAddress);
            for (uint32_t y = 0; y < height; ++y)
            {
                auto* row = base + static_cast<size_t>(y) * pitch;
                for (uint32_t x = 0; x < width; ++x)
                    fill(x, y, row + static_cast<size_t>(x) * bytesPerTexel);
            }

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = resources.Resolve(allocation.buffer);
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = allocation.offset;
            src.PlacedFootprint.Footprint.Format = format;
            src.PlacedFootprint.Footprint.Width = width;
            src.PlacedFootprint.Footprint.Height = height;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(pitch);

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = target;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.SubresourceIndex = subresource;
            resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            return true;
        };

        bool ok = true;

        // 씬 색 — 회색.
        ok = ok && upload(sceneColor.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
            kFogScreen, kFogScreen, 8, 0, [](uint32_t, uint32_t, void* texel)
        {
            auto* rgba = static_cast<uint16_t*>(texel);
            rgba[0] = rgba[1] = rgba[2] = FogFloatToHalf(kFogSceneGray);
            rgba[3] = FogFloatToHalf(1.f);
        });

        // 깊이 — 화면 전체가 같은 거리.
        ok = ok && upload(sceneDepth.Get(), DXGI_FORMAT_R32_FLOAT,
            kFogScreen, kFogScreen, 4, 0, [](uint32_t, uint32_t, void* texel)
        {
            *static_cast<float*>(texel) = kFogSceneDepth;
        });

        // 그림자맵 — 슬라이스 셋을 다 1.0(가장 먼 곳)으로 둔다. 비교가
        // LESS_EQUAL이라 어떤 좌표든 '가려지지 않음'이 나온다.
        for (uint32_t slice = 0; slice < kShadowCascadeCount && ok; ++slice)
        {
            ok = upload(shadowMap.Get(), DXGI_FORMAT_R32_FLOAT, 64, 64, 4, slice,
                [](uint32_t, uint32_t, void* texel) { *static_cast<float*>(texel) = 1.f; });
        }

        // 구름 — 흰색. 원본 ②대로 이 값이 포그 밀도를 그대로 좌우한다.
        ok = ok && upload(cloudMap.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, 4, 0,
            [](uint32_t, uint32_t, void* texel)
        {
            auto* rgba = static_cast<uint8_t*>(texel);
            rgba[0] = rgba[1] = rgba[2] = rgba[3] = 255;
        });

        // 블루 노이즈 — 지터의 씨앗. 내용은 아무거나 되되 고르게 퍼져야 한다.
        ok = ok && upload(blueNoise.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 4, 0,
            [](uint32_t x, uint32_t y, void* texel)
        {
            auto* rgba = static_cast<uint8_t*>(texel);
            const uint8_t value = static_cast<uint8_t>((x * 37u + y * 101u) & 0xFFu);
            rgba[0] = rgba[1] = rgba[2] = value;
            rgba[3] = 255;
        });

        if (!ok)
        {
            outLog += "[2/5] 입력 업로드 실패\n";
            resources.Shutdown();
            return false;
        }

        ID3D12Resource* const sources[5] = { sceneColor.Get(), sceneDepth.Get(),
            shadowMap.Get(), cloudMap.Get(), blueNoise.Get() };
        D3D12_RESOURCE_BARRIER barriers[5]{};
        for (uint32_t i = 0; i < 5; ++i)
        {
            barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource = sources[i];
            barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        resources.GetCommandList()->ResourceBarrier(5, barriers);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/5] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();
        outLog += "[2/5] 합성 입력(씬 색·깊이·캐스케이드 3장·구름·블루노이즈) 완료\n";
    }

    // 리드백 둘 — 격자(3D, 깊이만큼 장)와 합성 결과(2D, 장 하나).
    RHIReadback voxelReadback{};
    RHIReadback screenReadback{};
    {
        std::string readbackError;
        if (!resources.CreateReadback(kFogVolumeW, kFogVolumeH,
                EnhancedVolumetricFogPass::kVoxelFormat, kFogVolumeD,
                voxelReadback, readbackError) ||
            !resources.CreateReadback(kFogScreen, kFogScreen,
                EnhancedVolumetricFogPass::kOutputFormat, 1,
                screenReadback, readbackError))
        {
            outLog += "[2/5] 리드백 생성 실패: " + readbackError + "\n";
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    uint32_t lastPassCount = 0;

    // 한 프레임을 돌려 격자와 합성 결과를 받아 온다. 그래프는 매번 새로 만든다.
    const auto renderFrame = [&](const EnhancedVolumetricFogPass::Tuning& tuning,
        uint32_t lightCount, RHIReadbackImage& outVoxel, RHIReadbackImage& outScreen) -> bool
    {
        // 방향광 하나(또는 없음). 방향광은 감쇠가 1이라 격자 전체가 빛을 받는다.
        std::vector<EnhancedLight> lights;
        for (uint32_t i = 0; i < lightCount; ++i)
        {
            EnhancedLight light{};
            light.position = { 0.f, 0.f, 0.f, 0.f };        // w = 타입(0 방향광)
            light.direction = { 0.f, -1.f, 0.f, 0.f };
            light.color = { 1.f, 1.f, 1.f, 1.f };
            light.attenuation = { 1.f, 0.f, 0.f, 100.f };
            lights.push_back(light);
        }
        frameContext.lights = &lights;

        if (!resources.BeginFrame(error)) return false;

        fog.SetTuning(tuning);
        fog.SetEnabled(true);
        fog.SetFrameIndex(0);   // 지터를 프레임에 묶지 않는다 — 재현되어야 한다

        EnhancedVolumetricFogPass::CloudShadow cloud{};
        cloud.viewProjection = XMMatrixIdentity();
        cloud.alpha = 1.f;       // 원본 ②대로 0이면 포그가 통째로 사라진다
        cloud.size[0] = cloud.size[1] = 1.f;
        cloud.cloudMapSize[0] = cloud.cloudMapSize[1] = 4.f;
        fog.SetCloudShadow(cloud);

        if (!fog.PrepareFrame(frameContext, error)) return false;

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);

        EnhancedVolumetricFogPass::Inputs inputs{};
        inputs.color = graph.ImportTexture(colorRegistration.Handle(),
            RHIResourceState::ShaderResource, "Fog.SceneColor");
        inputs.depth = graph.ImportTexture(depthRegistration.Handle(),
            RHIResourceState::ShaderResource, "Fog.SceneDepth");
        inputs.shadowMap = graph.ImportTexture(shadowRegistration.Handle(),
            RHIResourceState::ShaderResource, "Fog.ShadowMap");
        inputs.cloudShadow = graph.ImportTexture(cloudRegistration.Handle(),
            RHIResourceState::ShaderResource, "Fog.CloudMap");
        inputs.blueNoise = graph.ImportTexture(noiseRegistration.Handle(),
            RHIResourceState::ShaderResource, "Fog.BlueNoise");

        fog.SetInputs(inputs);
        fog.Declare(graph, frameContext);

        const RGHandle output = fog.GetOutput();
        const RGHandle voxelGrid = fog.GetVoxelGrid();
        if (!output.IsValid() || !voxelGrid.IsValid()) return false;

        graph.AddPass("Fog.Readback",
            { { output,    RHIResourceState::CopySource },
              { voxelGrid, RHIResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                executeContext.encoder->CopyVolumeToReadback(
                    voxelReadback, executeContext.ResolveHandle(voxelGrid));
                executeContext.encoder->CopyToReadback(
                    screenReadback, executeContext.ResolveHandle(output));
            }, true);

        if (!graph.Compile(error)) return false;
        if (!graph.Execute(error)) return false;
        lastPassCount = graph.GetStats().passesExecuted;

        if (!resources.EndFrame(error)) return false;
        resources.WaitForGpu();

        return resources.MapReadback(voxelReadback, outVoxel, error)
            && resources.MapReadback(screenReadback, outScreen, error);
    };

    // 격자 한가운데를 본다.
    constexpr uint32_t kProbeX = kFogVolumeW / 2;
    constexpr uint32_t kProbeY = kFogVolumeH / 2;

    EnhancedVolumetricFogPass::Tuning fresh{};
    fresh.previousFrameBlendFactor = 0.f;   // 히스토리를 빼고 순수 산란만 본다

    RHIReadbackImage litVoxel{};
    RHIReadbackImage litScreen{};
    if (!renderFrame(fresh, 1, litVoxel, litScreen))
    {
        outLog += "[3/5] 광원 있는 프레임 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // ── [3/5] 산란과 패스 수 ──
    const float litScatter = litVoxel.At(kProbeX, kProbeY, 1, kFogVolumeD - 1);
    {
        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[3/5] 산란 — 광원 1개일 때 격자 누적광 %.5f · 그래프 %u패스\n",
            litScatter, lastPassCount);
        outLog += line;

        // ① 빛이 실렸는가. 방향광은 감쇠가 1이라 격자 전체가 받는다.
        if (litScatter < 0.001f)
        {
            outLog += "격자에 빛이 실리지 않았다 — 산란이 광원을 못 받는다\n";
            passed = false;
        }
        // ⑤ 산란·누적·합성·리드백 넷이다. 복사가 있었다면 다섯이다.
        if (4 != lastPassCount)
        {
            outLog += "패스가 넷(산란·누적·합성·리드백)이 아니다\n";
            passed = false;
        }
    }

    // ── [4/5] 누적 — z를 따라 투과율이 단조 감소하는가 ──
    if (passed)
    {
        const uint32_t slices[5] = { 0, 32, 64, 96, kFogVolumeD - 1 };
        float transmittance[5]{};
        for (uint32_t i = 0; i < 5; ++i)
            transmittance[i] = litVoxel.At(kProbeX, kProbeY, 3, slices[i]);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[4/5] 누적 투과율 — z0 %.4f · z32 %.4f · z64 %.4f · z96 %.4f · z127 %.4f\n",
            transmittance[0], transmittance[1], transmittance[2],
            transmittance[3], transmittance[4]);
        outLog += line;

        // ② 앞에서 뒤로 훑으며 곱해 나가므로 반드시 줄어든다. 한 번이라도
        //   늘거나 같으면 누적이 z를 안 따라간 것이다.
        for (uint32_t i = 1; i < 5; ++i)
        {
            if (transmittance[i] >= transmittance[i - 1])
            {
                outLog += "투과율이 z를 따라 줄지 않는다 — 누적이 앞뒤로 쌓이지 않는다\n";
                passed = false;
                break;
            }
        }
        // 시작이 1에 가깝지 않으면 초기값이 틀린 것이다(result.a는 1에서 출발한다).
        if (transmittance[0] < 0.99f)
        {
            outLog += "첫 슬라이스 투과율이 1 근처가 아니다 — 누적 초기값이 틀렸다\n";
            passed = false;
        }
    }

    // ── [5/5] 시간축 히스토리와 합성 ──
    if (passed)
    {
        // ★ 광원을 다 끄고 히스토리 100%로 한 프레임 더 돈다.
        //   이번 프레임에는 빛이 없으므로, 남아 있는 값은 지난 프레임에서만
        //   올 수 있다 — 핑퐁이 실제로 프레임을 넘긴다는 증거다.
        EnhancedVolumetricFogPass::Tuning historyOnly{};
        historyOnly.previousFrameBlendFactor = 1.f;

        RHIReadbackImage historyVoxel{};
        RHIReadbackImage historyScreen{};
        if (!renderFrame(historyOnly, 0, historyVoxel, historyScreen))
        {
            outLog += "[5/5] 히스토리 프레임 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // 대조군 — 히스토리를 빼고 광원도 없으면 격자가 비어야 한다.
        RHIReadbackImage darkVoxel{};
        RHIReadbackImage darkScreen{};
        if (!renderFrame(fresh, 0, darkVoxel, darkScreen))
        {
            outLog += "[5/5] 무광원 프레임 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // 합성 대조군 — 혼합 계수 0이면 입력이 한 치도 바뀌면 안 된다.
        EnhancedVolumetricFogPass::Tuning noBlend{};
        noBlend.previousFrameBlendFactor = 0.f;
        noBlend.blendingWithSceneColorFactor = 0.f;

        RHIReadbackImage plainVoxel{};
        RHIReadbackImage plainScreen{};
        if (!renderFrame(noBlend, 1, plainVoxel, plainScreen))
        {
            outLog += "[5/5] 무합성 프레임 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        const float historyScatter = historyVoxel.At(kProbeX, kProbeY, 1, kFogVolumeD - 1);
        const float darkScatter = darkVoxel.At(kProbeX, kProbeY, 1, kFogVolumeD - 1);
        const float litPixel = litScreen.At(kFogScreen / 2, kFogScreen / 2, 1);
        const float plainPixel = plainScreen.At(kFogScreen / 2, kFogScreen / 2, 1);

        char line[320]{};
        std::snprintf(line, sizeof(line),
            "[5/5] 히스토리 — 광원 0 + 히스토리 100%%: %.5f (지난 프레임 %.5f)"
            " · 광원 0 + 히스토리 0%%: %.5f\n      합성 — 계수 0.851: %.4f"
            " · 계수 0: %.4f (입력 %.4f)\n",
            historyScatter, litScatter, darkScatter, litPixel, plainPixel, kFogSceneGray);
        outLog += line;

        // ③ 빛이 없는데 포그가 남았는가. 지난 프레임에서 온 것이다.
        if (historyScatter < litScatter * 0.5f)
        {
            outLog += "히스토리가 이어지지 않는다 — 핑퐁이 프레임을 넘기지 못한다\n";
            passed = false;
        }
        // 대조군이 없으면 '항상 값이 남는' 구현도 위를 통과한다.
        if (darkScatter > 0.0005f)
        {
            outLog += "광원도 히스토리도 없는데 포그가 남았다 — 대조군이 성립하지 않는다\n";
            passed = false;
        }
        // ④ 합성이 실제로 씬 색을 움직이는가.
        if (std::fabs(litPixel - kFogSceneGray) < 0.001f)
        {
            outLog += "합성이 씬 색을 바꾸지 않았다 — 포그가 화면에 안 얹힌다\n";
            passed = false;
        }
        // 계수 0이면 입력 그대로여야 한다.
        if (std::fabs(plainPixel - kFogSceneGray) > 0.002f)
        {
            outLog += "혼합 계수가 0인데 씬 색이 바뀌었다 — 계수가 안 먹는다\n";
            passed = false;
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    fog.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    noiseRegistration.Reset();
    cloudRegistration.Reset();
    shadowRegistration.Reset();
    depthRegistration.Reset();
    colorRegistration.Reset();
    resources.Shutdown();

    outLog += passed ? "볼류메트릭 포그 패스 검증 통과\n" : "볼류메트릭 포그 패스 검증 실패\n";
    return passed;
}

#endif
