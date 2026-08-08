#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedDecalPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../../Texture.h"
// DirectX11::DeviceStates — 데칼 텍스처를 DX11로 만들어 캐시에 태운다.
// 이웃 파일에서 딸려 오던 것이라 유니티 묶음이 바뀌면 끊긴다. 직접 든다.
#include "../../DeviceState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// 데칼 패스 자가 검증 (PHASE 3-6, 미구현 패스 이식 2차).
//
// ── 다섯을 따로 단정한다 ──
//
//   ① 상자 판정 — 데칼이 상자 안 표면에만 얹히는가. 대조군은 상자 밖이다.
//   ② 하늘 게이트 — 깊이 1(얹을 표면 없음)에는 얹지 않는가. 상자가 그
//      화면 영역을 덮고 있는데도 안 얹혀야 한다.
//   ③~⑤ 원본에서 찾은 셋을 각각 잰다. 아래 셋은 '고쳐야 할 것'이 아니라
//      '이식이 원본과 같은가'의 판정 기준이다 — DX11이 기준선이므로 여기서
//      고치면 픽셀 대조가 성립하지 않는다.
//
//        ③ 확산의 알파가 제곱된다(셰이더 lerp + 블렌드 lerp)
//        ④ 노멀 채널이 아무 일도 하지 않는다(출력 알파 0 × SRC_ALPHA)
//        ⑤ ORM이 데칼 불투명도가 아니라 표면 IOR로 섞인다
//
//   ★ ③~⑤는 숫자 하나가 판정한다. ③은 0.25(제곱)냐 0.5(단순)냐, ⑤는
//     1.5(IOR 외삽)냐 1.0(정상 보간)이냐가 갈린다 — 눈으로는 구분되지
//     않지만 숫자로는 한 번에 갈린다.
namespace
{
    constexpr uint32_t kDecalWidth = 256;
    constexpr uint32_t kDecalHeight = 256;

    constexpr uint64_t kDecalRowPitch =
        ((static_cast<uint64_t>(kDecalWidth) * 8ull) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull)
        & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull);

    // 표면의 깊이. 정사영이라 월드 z = 깊이 x 10이다.
    constexpr float kDecalSurfaceDepth = 0.5f;

    // 화면 오른쪽 절반은 하늘로 둔다 — ②의 재료이자 ①의 대조군이다.
    constexpr uint32_t kDecalSkyStartX = 128;

    // 데칼 상자가 덮는 화면 영역. 월드 x·y가 -0.25~0.25이고 화면은 -1~1이라
    // 픽셀로는 96~160이다.
    constexpr uint32_t kDecalMinPixel = 96;
    constexpr uint32_t kDecalMaxPixel = 160;

    // GBuffer ORM의 알파는 IOR이다(GBuffer.ps.hlsl). 엔진 기본값이 1.5다.
    constexpr float kDecalSurfaceIor = 1.5f;

    uint16_t DecalFloatToHalf(float value)
    {
        uint32_t bits;
        memcpy(&bits, &value, 4);
        const uint32_t sign = (bits >> 16) & 0x8000u;
        int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
        uint32_t mantissa = bits & 0x7FFFFFu;

        if (exponent <= 0) return static_cast<uint16_t>(sign);
        if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10)
            | (mantissa >> 13));
    }

    float DecalHalfToFloat(uint16_t bits)
    {
        const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
        uint32_t exponent = (bits >> 10) & 0x1Fu;
        uint32_t mantissa = bits & 0x3FFu;

        if (0 == exponent)
        {
            if (0 == mantissa) { float out; memcpy(&out, &sign, 4); return out; }
            exponent = 1;
            while (0 == (mantissa & 0x400u)) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3FFu;
        }
        else if (31 == exponent)
        {
            const uint32_t infBits = sign | 0x7F800000u | (mantissa << 13);
            float out; memcpy(&out, &infBits, 4); return out;
        }

        const uint32_t outBits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        float out; memcpy(&out, &outBits, 4); return out;
    }

    struct DecalCapture
    {
        std::vector<uint8_t> data;

        float At(uint32_t x, uint32_t y, uint32_t channel) const
        {
            const auto* row = reinterpret_cast<const uint16_t*>(
                data.data() + static_cast<size_t>(y) * kDecalRowPitch);
            return DecalHalfToFloat(row[x * 4 + channel]);
        }

        /// 기준값과 다른 픽셀 수. '어디가 바뀌었나'를 재는 자다.
        uint32_t CountChanged(float reference, uint32_t channel, float tolerance) const
        {
            uint32_t count = 0;
            for (uint32_t y = 0; y < kDecalHeight; ++y)
                for (uint32_t x = 0; x < kDecalWidth; ++x)
                    if (std::fabs(At(x, y, channel) - reference) > tolerance) ++count;
            return count;
        }

        /// 지정한 사각형 밖에서 바뀐 픽셀 수. 새어 나간 것을 잡는다.
        uint32_t CountChangedOutside(float reference, uint32_t channel, float tolerance,
            uint32_t minX, uint32_t maxX, uint32_t minY, uint32_t maxY) const
        {
            uint32_t count = 0;
            for (uint32_t y = 0; y < kDecalHeight; ++y)
            {
                for (uint32_t x = 0; x < kDecalWidth; ++x)
                {
                    const bool inside = (x >= minX && x < maxX && y >= minY && y < maxY);
                    if (inside) continue;
                    if (std::fabs(At(x, y, channel) - reference) > tolerance) ++count;
                }
            }
            return count;
        }
    };

    /// 화면 전체를 한 색으로 채운 반정밀도 GBuffer 타깃 하나를 만든다.
    bool CreateDecalTarget(DX12DeviceResources& resources, const float rgba[4],
        Microsoft::WRL::ComPtr<ID3D12Resource>& out)
    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kDecalWidth;
        desc.Height = kDecalHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&out))))
        {
            return false;
        }

        constexpr uint32_t pitch = kDecalWidth * 8;
        const auto upload = resources.GetUploadRing().Allocate(
            static_cast<uint64_t>(pitch) * kDecalHeight, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (!upload.IsValid()) return false;

        auto* base = static_cast<uint8_t*>(upload.cpuAddress);
        for (uint32_t y = 0; y < kDecalHeight; ++y)
        {
            auto* row = reinterpret_cast<uint16_t*>(base + static_cast<size_t>(y) * pitch);
            for (uint32_t x = 0; x < kDecalWidth; ++x)
                for (uint32_t c = 0; c < 4; ++c)
                    row[x * 4 + c] = DecalFloatToHalf(rgba[c]);
        }

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = upload.resource;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = upload.offset;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        src.PlacedFootprint.Footprint.Width = kDecalWidth;
        src.PlacedFootprint.Footprint.Height = kDecalHeight;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = pitch;

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = out.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        return true;
    }
}

bool EnhancedSceneRenderer::RunDecalTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 데칼 패스 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kDecalWidth, kDecalHeight, error))
    {
        outLog += "[1/5] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_decal.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !textureCache.Initialize(&resources, error))
    {
        outLog += "[1/5] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.textureCache = &textureCache;
    frameContext.width = kDecalWidth;
    frameContext.height = kDecalHeight;

    // 정사영을 쓴다. 화면 좌표에서 월드 좌표가 선형으로 나오므로 데칼 상자를
    // 픽셀 단위로 정확히 겨눌 수 있다 — 원근이면 '대략 이 근처'가 되어
    // 경계를 단정할 수 없다.
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixOrthographicLH(2.f, 2.f, 0.f, 10.f);
    camera.inverseView = XMMatrixInverse(nullptr, camera.view);
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
    camera.fov = DirectX::XM_PIDIV4;
    frameContext.camera = &camera;

    EnhancedDecalPass decal;
    if (!decal.Initialize(frameContext, error))
    {
        outLog += "[1/5] 데칼 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    decal.SetKeepAlive(true);
    outLog += "[1/5] 셰이더 컴파일·채널 조합 7종 PSO 생성 통과\n";

    // ── 합성 GBuffer와 데칼 텍스처 ──
    ComPtr<ID3D12Resource> gbufferDiffuse;
    ComPtr<ID3D12Resource> gbufferNormal;
    ComPtr<ID3D12Resource> gbufferOrm;
    ComPtr<ID3D12Resource> gbufferDepth;

    Texture* decalDiffuse = nullptr;
    Texture* decalNormal = nullptr;
    Texture* decalOrm = nullptr;

    {
        // 데칼 텍스처는 DX11로 만든다 — 캐시가 DX11에서 읽어 DX12로 나른다.
        //
        // 확산: 빨강 · 알파 0.5. 알파가 ③의 재료다(0.5가 제곱되면 0.25).
        // 빨강을 1.0으로 두는 이유는 pow(x, 2.2)가 1에서 항등이라, 감마가
        // ③의 측정에 섞이지 않게 하기 위해서다.
        const uint8_t diffusePixel[4] = { 255, 0, 0, 128 };
        D3D11_SUBRESOURCE_DATA diffuseData{ diffusePixel, 4, 0 };
        decalDiffuse = Texture::Create(1, 1, "decalTestDiffuse",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, &diffuseData);

        // 노멀: 평평한 접선 공간 노멀(0.5, 0.5, 1).
        const uint8_t normalPixel[4] = { 128, 128, 255, 255 };
        D3D11_SUBRESOURCE_DATA normalData{ normalPixel, 4, 0 };
        decalNormal = Texture::Create(1, 1, "decalTestNormal",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, &normalData);

        // ORM: (occ 0 · rough 0 · metal 1). 셰이더가 (b,g,r)로 뒤집어
        // (metal 1, rough 0, occ 0)을 쓴다 — 첫 채널 1이 ⑤의 재료다.
        const uint8_t ormPixel[4] = { 0, 0, 255, 255 };
        D3D11_SUBRESOURCE_DATA ormData{ ormPixel, 4, 0 };
        decalOrm = Texture::Create(1, 1, "decalTestOrm",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, &ormData);

        if (nullptr == decalDiffuse || nullptr == decalNormal || nullptr == decalOrm)
        {
            outLog += "[2/5] 데칼 텍스처 생성 실패\n";
            resources.Shutdown();
            return false;
        }
        decalDiffuse->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);
        decalNormal->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);
        decalOrm->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    if (!resources.BeginFrame(error))
    {
        outLog += "[2/5] BeginFrame 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    {
        // 확산은 검정으로 둔다 — ③의 기대값 lerp(0, 1, a²) = a²이 그대로 읽힌다.
        const float black[4] = { 0.f, 0.f, 0.f, 1.f };

        // 노멀은 (0,1,0)을 인코딩한 것(0.5, 1, 0.5). 위를 향한 표면이다.
        const float upNormal[4] = { 0.5f, 1.f, 0.5f, 1.f };

        // ORM은 전부 0이되 알파에 IOR을 넣는다 — 이것이 ⑤의 재료다.
        const float ormBase[4] = { 0.f, 0.f, 0.f, kDecalSurfaceIor };

        if (!CreateDecalTarget(resources, black, gbufferDiffuse) ||
            !CreateDecalTarget(resources, upNormal, gbufferNormal) ||
            !CreateDecalTarget(resources, ormBase, gbufferOrm))
        {
            outLog += "[2/5] GBuffer 타깃 생성 실패\n";
            resources.Shutdown();
            return false;
        }

        // 깊이 — 왼쪽은 표면(0.5), kDecalSkyStartX부터 하늘(1.0).
        //
        // ALLOW_DEPTH_STENCIL로 만든다. 읽기 전용 DSV로 걸면서 같은 리소스를
        // SRV로도 읽는 것이 이 패스의 요점이라, 그 경로를 실제로 태워야 한다.
        {
            D3D12_HEAP_PROPERTIES defaultHeap{};
            defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = kDecalWidth;
            desc.Height = kDecalHeight;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = EnhancedGBufferPass::kDepthFormat;
            desc.SampleDesc.Count = 1;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE clearValue{};
            clearValue.Format = EnhancedGBufferPass::kDepthFormat;
            clearValue.DepthStencil.Depth = 1.f;

            if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                &clearValue, IID_PPV_ARGS(&gbufferDepth))))
            {
                outLog += "[2/5] 깊이 생성 실패\n";
                resources.Shutdown();
                return false;
            }

            constexpr uint32_t depthPitch = kDecalWidth * 4;
            const auto upload = resources.GetUploadRing().Allocate(
                static_cast<uint64_t>(depthPitch) * kDecalHeight,
                D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            if (!upload.IsValid())
            {
                outLog += "[2/5] 깊이 업로드 할당 실패\n";
                resources.Shutdown();
                return false;
            }

            auto* base = static_cast<uint8_t*>(upload.cpuAddress);
            for (uint32_t y = 0; y < kDecalHeight; ++y)
            {
                auto* row = reinterpret_cast<float*>(base + static_cast<size_t>(y) * depthPitch);
                for (uint32_t x = 0; x < kDecalWidth; ++x)
                    row[x] = (x >= kDecalSkyStartX) ? 1.f : kDecalSurfaceDepth;
            }

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = upload.resource;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Offset = upload.offset;
            src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
            src.PlacedFootprint.Footprint.Width = kDecalWidth;
            src.PlacedFootprint.Footprint.Height = kDecalHeight;
            src.PlacedFootprint.Footprint.Depth = 1;
            src.PlacedFootprint.Footprint.RowPitch = depthPitch;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = gbufferDepth.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            resources.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        }

        D3D12_RESOURCE_BARRIER barriers[4]{};
        ID3D12Resource* const targets[4] = { gbufferDiffuse.Get(), gbufferNormal.Get(),
            gbufferOrm.Get(), gbufferDepth.Get() };
        for (uint32_t i = 0; i < 4; ++i)
        {
            barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[i].Transition.pResource = targets[i];
            barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[i].Transition.StateAfter = (3 == i)
                ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        resources.GetCommandList()->ResourceBarrier(4, barriers);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/5] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();
        outLog += "[2/5] 합성 GBuffer(표면 + 하늘) · 데칼 텍스처 3종 준비 완료\n";
    }

    // 리드백 — 확산·노멀·ORM 셋.
    ComPtr<ID3D12Resource> readback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kDecalRowPitch * kDecalHeight * 3;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&readback))))
        {
            outLog += "[2/5] 리드백 생성 실패\n";
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    DecalCapture diffuseCapture{};
    DecalCapture normalCapture{};
    DecalCapture ormCapture{};
    EnhancedRenderGraph::Stats stats{};

    {
        // 데칼 하나 — 세 채널을 다 켠다. 하나로 ③④⑤를 동시에 잰다.
        //
        // 상자는 월드 (0,0,5) 중심에 x·y 0.5, z 8. 표면(z=5)을 확실히
        // 관통하면서 화면으로는 -0.25~0.25를 덮는다.
        EnhancedDecalPass::Item item{};
        item.worldMatrix = XMMatrixScaling(0.5f, 0.5f, 8.f) *
            XMMatrixTranslation(0.f, 0.f, 5.f);
        item.diffuse = decalDiffuse;
        item.normal = decalNormal;
        item.occRoughMetal = decalOrm;

        decal.SetDecals({ item });

        if (!resources.BeginFrame(error) || !decal.PrepareFrame(frameContext, error))
        {
            outLog += "[3/5] 준비 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph;

        EnhancedGBufferPass::Outputs inputs{};
        inputs.diffuse = graph.ImportTexture(gbufferDiffuse.Get(),
            RGResourceState::RenderTarget, "Decal.GBufferDiffuse");
        inputs.normal = graph.ImportTexture(gbufferNormal.Get(),
            RGResourceState::RenderTarget, "Decal.GBufferNormal");
        inputs.metalRough = graph.ImportTexture(gbufferOrm.Get(),
            RGResourceState::RenderTarget, "Decal.GBufferOrm");
        inputs.depth = graph.ImportTexture(gbufferDepth.Get(),
            RGResourceState::DepthWrite, "Decal.GBufferDepth");

        decal.SetInputs(inputs);
        decal.Declare(graph, frameContext);

        graph.AddPass("Decal.Readback",
            { { inputs.diffuse,    RGResourceState::CopySource },
              { inputs.normal,     RGResourceState::CopySource },
              { inputs.metalRough, RGResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                const auto copyOne = [&](RGHandle handle, uint64_t offset)
                {
                    D3D12_TEXTURE_COPY_LOCATION src{};
                    src.pResource = executeContext.Resolve(handle);
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = readback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Offset = offset;
                    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                    dst.PlacedFootprint.Footprint.Width = kDecalWidth;
                    dst.PlacedFootprint.Footprint.Height = kDecalHeight;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(kDecalRowPitch);

                    executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                };

                const uint64_t slice = kDecalRowPitch * kDecalHeight;
                copyOne(inputs.diffuse, 0);
                copyOne(inputs.normal, slice);
                copyOne(inputs.metalRough, slice * 2);
            }, true);

        if (!graph.Compile(resources.GetDevice(), error) ||
            !graph.Execute(resources.GetCommandList(), error))
        {
            outLog += "[3/5] 그래프 실행 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        stats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "[3/5] EndFrame 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }
        resources.WaitForGpu();

        void* mapped = nullptr;
        const size_t sliceBytes = static_cast<size_t>(kDecalRowPitch) * kDecalHeight;
        D3D12_RANGE range{ 0, sliceBytes * 3 };
        if (FAILED(readback->Map(0, &range, &mapped)))
        {
            outLog += "[3/5] 리드백 Map 실패\n";
            resources.Shutdown();
            return false;
        }

        const auto* base = static_cast<const uint8_t*>(mapped);
        diffuseCapture.data.assign(base, base + sliceBytes);
        normalCapture.data.assign(base + sliceBytes, base + sliceBytes * 2);
        ormCapture.data.assign(base + sliceBytes * 2, base + sliceBytes * 3);
        readback->Unmap(0, nullptr);
    }

    // ── [3/5] 상자 판정과 하늘 게이트 ──
    {
        // 데칼이 닿아야 하는 곳: 상자 안이면서 표면인 곳.
        // 상자는 96~160을 덮지만 128부터는 하늘이므로 96~128만 남는다.
        const uint32_t changed = diffuseCapture.CountChanged(0.f, 0, 0.002f);
        const uint32_t leaked = diffuseCapture.CountChangedOutside(0.f, 0, 0.002f,
            kDecalMinPixel, kDecalSkyStartX, kDecalMinPixel, kDecalMaxPixel);

        // 상자 안이지만 하늘인 쪽 — 얹히면 안 된다.
        uint32_t sky = 0;
        for (uint32_t y = kDecalMinPixel; y < kDecalMaxPixel; ++y)
            for (uint32_t x = kDecalSkyStartX; x < kDecalMaxPixel; ++x)
                if (std::fabs(diffuseCapture.At(x, y, 0)) > 0.002f) ++sky;

        const uint32_t expected = (kDecalSkyStartX - kDecalMinPixel)
            * (kDecalMaxPixel - kDecalMinPixel);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[3/5] 상자·하늘 — 바뀐 픽셀 %u (기대 %u) · 상자 밖 유출 %u"
            " · 하늘에 얹힌 것 %u · 그래프 %u패스\n",
            changed, expected, leaked, sky, stats.passesExecuted);
        outLog += line;

        // ① 상자 판정 — 기대 영역이 실제로 칠해졌는가. 경계 픽셀의 반올림을
        //   감안해 2% 어긋남까지 허용한다.
        if (changed < expected * 98 / 100 || changed > expected * 102 / 100)
        {
            outLog += "칠해진 넓이가 상자의 화면 투영과 다르다 — 상자 판정이 틀렸다\n";
            passed = false;
        }
        if (0 != leaked)
        {
            outLog += "상자 밖으로 새어 나갔다 — 지역 좌표 판정이 죽었다\n";
            passed = false;
        }
        // ② 하늘 게이트 — 상자가 덮고 있는데도 얹히지 않아야 한다.
        if (0 != sky)
        {
            outLog += "하늘에 데칼이 얹혔다 — depth>=1 게이트가 죽었다\n";
            passed = false;
        }
        // 사본 + 덧칠 + 리드백 셋이다. 깊이 사본이 없어야 셋이다 —
        // 넷이면 깊이까지 복사하고 있다는 뜻이다.
        if (3 != stats.passesExecuted)
        {
            outLog += "패스가 셋(사본·덧칠·리드백)이 아니다\n";
            passed = false;
        }
    }

    // ── [4/5] 원본에서 찾은 셋 ──
    if (passed)
    {
        // 데칼이 확실히 닿은 지점 하나를 고른다.
        constexpr uint32_t kSampleX = 112;
        constexpr uint32_t kSampleY = 128;

        const float diffuseRed = diffuseCapture.At(kSampleX, kSampleY, 0);
        const float ormFirst = ormCapture.At(kSampleX, kSampleY, 0);

        // ④ 노멀은 바뀌지 않아야 한다. 기준은 업로드한 (0.5, 1, 0.5).
        const uint32_t normalChanged = normalCapture.CountChanged(0.5f, 0, 0.002f);

        // 데칼 텍스처 알파 128/255 = 0.50196.
        const float alpha = 128.f / 255.f;
        const float squared = alpha * alpha;

        char line[320]{};
        std::snprintf(line, sizeof(line),
            "[4/5] 원본 셋 — 확산 %.4f (제곱 %.4f · 단순 %.4f)"
            " · 노멀 바뀐 픽셀 %u · ORM 첫 채널 %.3f (IOR %.1f · 정상 1.0)\n",
            diffuseRed, squared, alpha, normalChanged, ormFirst, kDecalSurfaceIor);
        outLog += line;

        // ③ 알파가 제곱되는가. 셰이더의 lerp와 블렌드의 lerp가 겹친 결과다.
        //   단순 보간(0.502)과 제곱(0.252)은 두 배 차이라 반올림으로 섞이지 않는다.
        if (std::fabs(diffuseRed - squared) > 0.01f)
        {
            outLog += "확산 혼합이 원본과 다르다 — 셰이더 lerp나 블렌드가 어긋났다\n";
            passed = false;
        }

        // ④ 노멀 채널이 정말로 아무 일도 안 하는가.
        //   대조군은 위에서 이미 성립했다 — 같은 데칼의 확산은 바뀌었다.
        //   즉 '패스가 안 돌아서' 노멀이 그대로인 것이 아니다.
        if (0 != normalChanged)
        {
            outLog += "노멀이 바뀌었다 — 출력 알파 0 x SRC_ALPHA면 그대로여야 한다\n";
            passed = false;
        }

        // ⑤ ORM이 IOR로 섞이는가. 1.5면 외삽(원본과 같다), 1.0이면
        //   정상 보간(원본과 다르게 이식됐다)이다.
        if (std::fabs(ormFirst - kDecalSurfaceIor) > 0.02f)
        {
            outLog += "ORM 혼합 계수가 원본과 다르다 — baseORM.a(IOR)를 쓰지 않는다\n";
            passed = false;
        }
    }

    // ── [5/5] 배칭 — 연속한 것만 묶고 갈리면 늘어난다 ──
    if (passed)
    {
        // 텍스처 자원 없이 포인터의 같고 다름만 본다(기즈모 아이콘 검증과
        // 같은 방식). 캐시를 떼면 업로드를 건너뛰므로 가짜 포인터가 안전하다.
        EnhancedFrameContext batchContext = frameContext;
        batchContext.textureCache = nullptr;

        Texture* const fakeA = reinterpret_cast<Texture*>(0x1);
        Texture* const fakeB = reinterpret_cast<Texture*>(0x2);

        std::vector<EnhancedDecalPass::Item> items(4);
        for (auto& item : items) item.worldMatrix = XMMatrixIdentity();

        std::string dummy;

        // 같은 텍스처 넷이 연속 → 배치 1
        for (auto& item : items) item.diffuse = fakeA;
        decal.SetDecals(items);
        decal.PrepareFrame(batchContext, dummy);
        const uint32_t runBatches = decal.GetLastBatchCount();
        const uint32_t runDecals = decal.GetLastDecalCount();

        // 번갈아 → 배치 4. 이 대조군이 없으면 '항상 1을 내는' 구현도 통과한다.
        items[1].diffuse = fakeB;
        items[3].diffuse = fakeB;
        decal.SetDecals(items);
        decal.PrepareFrame(batchContext, dummy);
        const uint32_t mixedBatches = decal.GetLastBatchCount();

        // 채널이 갈려도 묶이면 안 된다 — 조합마다 PSO가 다르다.
        for (auto& item : items) item.diffuse = fakeA;
        items[2].normal = fakeA;
        decal.SetDecals(items);
        decal.PrepareFrame(batchContext, dummy);
        const uint32_t channelBatches = decal.GetLastBatchCount();

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[5/5] 배칭 — 연속 넷: 데칼 %u·배치 %u · 번갈아: 배치 %u"
            " · 채널 갈림: 배치 %u\n",
            runDecals, runBatches, mixedBatches, channelBatches);
        outLog += line;

        if (4 != runDecals)
        {
            outLog += "데칼 수가 넷이 아니다 — 수집이 빠뜨렸다\n";
            passed = false;
        }
        if (1 != runBatches)
        {
            outLog += "연속한 같은 텍스처가 안 묶였다 — 인스턴싱이 죽었다\n";
            passed = false;
        }
        if (4 != mixedBatches)
        {
            outLog += "다른 텍스처가 묶였다 — 배치 키가 텍스처를 안 본다\n";
            passed = false;
        }
        if (3 != channelBatches)
        {
            outLog += "채널 조합이 다른데 묶였다 — PSO가 섞인다\n";
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

    decal.Shutdown();
    textureCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    delete decalDiffuse;
    delete decalNormal;
    delete decalOrm;

    outLog += passed ? "데칼 패스 검증 통과\n" : "데칼 패스 검증 실패\n";
    return passed;
}

#endif
