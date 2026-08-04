#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSceneRenderer.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedGBufferPass.h"
#include "EnhancedDeferredPass.h"
#include "EnhancedSSGIPass.h"
#include "EnhancedForwardPass.h"
#include "DX12GpuProfiler.h"
#include "DX12CommandListPool.h"
#include "DX12MeshCache.h"
#include "DX12TextureCache.h"
#include "../../Material.h"
#include "../../RenderScene.h"
#include "../../LightController.h"
#include "../../Texture.h"
#include "../../RenderPassData.h"
#include "../../MeshRendererProxy.h"

#include <DirectXTex.h>
#include <d3dcompiler.h>
#include <array>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

namespace
{
    // 브링업 셰이더는 파일 의존을 만들지 않으려고 소스에 담는다. 실제 씬 셰이더는
    // PSOManager(3-4)가 ShaderSystem과 함께 관리한다.
    constexpr const char* kTriangleShader = R"(
struct VSOut { float4 pos : SV_POSITION; float4 color : COLOR; };

VSOut VSMain(uint id : SV_VertexID)
{
    // 정점 버퍼 없이 SV_VertexID로 삼각형 — 입력 조립 검증은 이후 슬라이스에서.
    float2 positions[3] = { float2(0.0f, 0.6f), float2(0.6f, -0.6f), float2(-0.6f, -0.6f) };
    float4 colors[3] = {
        float4(1, 0.25f, 0.25f, 1), float4(0.25f, 1, 0.25f, 1), float4(0.25f, 0.5f, 1, 1) };

    VSOut output;
    output.pos = float4(positions[id], 0, 1);
    output.color = colors[id];
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.color;
}

// ── 텍스처 블릿(완료 기준 후반부) ──
Texture2D    gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VSQuadOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSQuadOut VSQuad(uint id : SV_VertexID)
{
    // 좌하단 사분면에 트라이앵글 스트립 쿼드. 삼각형과 겹치지 않는 위치라
    // 픽셀 검증이 서로를 오염시키지 않는다.
    float2 corners[4] = {
        float2(-0.9f, -0.3f), float2(-0.3f, -0.3f),
        float2(-0.9f, -0.9f), float2(-0.3f, -0.9f) };
    float2 uvs[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };

    VSQuadOut output;
    output.pos = float4(corners[id], 0, 1);
    output.uv = uvs[id];
    return output;
}

float4 PSQuad(VSQuadOut input) : SV_TARGET
{
    return gTexture.Sample(gSampler, input.uv);
}

// 컴퓨트 PSO 검증용 — 내용은 중요하지 않고, 컴퓨트 경로가 캐시를 타는지가 목적.
RWTexture2D<float4> gOutput : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    gOutput[id.xy] = float4(id.x / 64.0f, id.y / 64.0f, 0, 1);
}
)";

    // 체커보드 텍스처 파라미터. 픽셀 검증이 같은 상수로 기대값을 계산한다.
    constexpr uint32_t kTexSize = 64;      // 64*4 = 256바이트 행 — 업로드 정렬과 정확히 일치
    constexpr uint32_t kCheckerCells = 4;
    constexpr uint8_t  kColorA[4] = { 230, 40, 200, 255 };  // 마젠타
    constexpr uint8_t  kColorB[4] = { 250, 220, 40, 255 };  // 노랑

    bool CompileShader(const char* entry, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outLog)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kTriangleShader, strlen(kTriangleShader), nullptr,
            nullptr, nullptr, entry, target, 0, 0, &outBlob, &errors);
        if (FAILED(hr))
        {
            outLog += "셰이더 컴파일 실패(";
            outLog += entry;
            outLog += "): ";
            if (errors) outLog += static_cast<const char*>(errors->GetBufferPointer());
            outLog += "\n";
            return false;
        }
        return true;
    }
}

bool EnhancedSceneRenderer::RunSelfTest(const std::string& outputPngPath,
    uint32_t frameCount, std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 640;
    constexpr uint32_t kHeight = 360;

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/4] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }
    outLog += "[1/4] 디바이스·큐·펜스·타깃 생성 완료\n";

    // ── 루트 시그니처(비어 있음)와 PSO ──
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileShader("VSMain", "vs_5_0", vsBlob, outLog)) return false;
    if (!CompileShader("PSMain", "ps_5_0", psBlob, outLog)) return false;

    // 루트 시그니처는 캐시를 통해 얻는다(PHASE 3-4). 식별자를 손으로 붙이지 않는
    // 것이 요점 — 두 패스가 같은 번호를 다른 레이아웃에 붙이면 PSO 캐시가 엉뚱한
    // 파이프라인을 돌려주는데, 원인이 '캐시 히트'라 추적이 어렵다.
    DX12RootSignatureCache rootSignatureCache;
    if (!rootSignatureCache.Initialize(resources.GetDevice(), error))
    {
        outLog += "[2/4] 루트 시그니처 캐시 초기화 실패: " + error + "\n";
        return false;
    }

    DX12RootSignatureCache::Entry triangleRoot;
    {
        D3D12_ROOT_SIGNATURE_DESC desc{};
        triangleRoot = rootSignatureCache.GetOrCreate(desc, error);
        if (!triangleRoot.IsValid())
        {
            outLog += "[2/4] 루트 시그니처 생성 실패: " + error + "\n";
            return false;
        }
    }

    // PSO는 매니저를 통해 얻는다(PHASE 3-4). 자가 검증도 실전과 같은 경로를 타야
    // 캐시·해시가 실제로 동작하는지 확인된다.
    DX12PSOManager psoManager;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_pso_selftest.cache", error))
    {
        outLog += "[2/4] PSO 매니저 초기화 실패: " + error + "\n";
        return false;
    }

    DX12GraphicsPipelineDesc triangleDesc{};
    triangleDesc.vsBytecode = vsBlob->GetBufferPointer();
    triangleDesc.vsSize = vsBlob->GetBufferSize();
    triangleDesc.psBytecode = psBlob->GetBufferPointer();
    triangleDesc.psSize = psBlob->GetBufferSize();
    triangleDesc.rootSignature = triangleRoot.signature;
    triangleDesc.rootSignatureId = triangleRoot.id;

    ID3D12PipelineState* pso = psoManager.GetOrCreate(triangleDesc, error);
    if (!pso)
    {
        outLog += "[2/4] 삼각형 PSO 생성 실패: " + error + "\n";
        return false;
    }
    // ── 텍스처 블릿 파이프라인: SRV 힙 + 정적 샘플러 루트 시그니처 + 쿼드 PSO ──
    ComPtr<ID3DBlob> quadVsBlob;
    ComPtr<ID3DBlob> quadPsBlob;
    if (!CompileShader("VSQuad", "vs_5_0", quadVsBlob, outLog)) return false;
    if (!CompileShader("PSQuad", "ps_5_0", quadPsBlob, outLog)) return false;

    // SRV는 프레임 디스크립터 링에서, 샘플러는 샘플러 힙에서 얻는다(PHASE 3-4).
    //
    // 예전에는 단발 SRV 힙 하나를 만들고 샘플러를 루트에 정적으로 박아 두었다.
    // 그건 브링업에서 '텍스처가 보인다'를 증명하기 위한 최소 구성이었고, 실제
    // 패스 이식에는 못 쓴다 — 패스마다 힙을 만들면 힙 교체가 패스 경계마다
    // 일어나고, 정적 샘플러는 머티리얼마다 다른 필터를 감당하지 못한다.
    DX12RootSignatureCache::Entry quadRoot;
    {
        // 테이블 둘: SRV 하나, 샘플러 하나. 샘플러가 루트에서 빠지면서
        // 파라미터가 하나 늘었고, 그만큼 레이아웃이 달라져 루트 시그니처 캐시의
        // id도 달라진다(그래서 PSO 캐시도 자동으로 새 키를 쓴다).
        D3D12_DESCRIPTOR_RANGE srvRange{};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;

        D3D12_DESCRIPTOR_RANGE samplerRange{};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable.NumDescriptorRanges = 1;
        params[0].DescriptorTable.pDescriptorRanges = &srvRange;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &samplerRange;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 2;
        desc.pParameters = params;

        quadRoot = rootSignatureCache.GetOrCreate(desc, error);
        if (!quadRoot.IsValid())
        {
            outLog += "[2/4] 쿼드 루트 시그니처 생성 실패: " + error + "\n";
            return false;
        }
    }

    // 샘플러는 프레임마다 바뀌지 않으므로 한 번만 만들어 둔다.
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHandle{};
    {
        D3D12_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;

        samplerHandle = resources.GetSamplerHeap().GetOrCreate(sampler);
        if (0 == samplerHandle.ptr)
        {
            outLog += "[2/4] 샘플러 생성 실패\n";
            return false;
        }
    }

    DX12GraphicsPipelineDesc quadDesc{};
    quadDesc.vsBytecode = quadVsBlob->GetBufferPointer();
    quadDesc.vsSize = quadVsBlob->GetBufferSize();
    quadDesc.psBytecode = quadPsBlob->GetBufferPointer();
    quadDesc.psSize = quadPsBlob->GetBufferSize();
    quadDesc.rootSignature = quadRoot.signature;
    quadDesc.rootSignatureId = quadRoot.id;

    ID3D12PipelineState* quadPso = psoManager.GetOrCreate(quadDesc, error);
    if (!quadPso)
    {
        outLog += "[2/4] 쿼드 PSO 생성 실패: " + error + "\n";
        return false;
    }

    // 같은 desc를 다시 요청하면 메모리 캐시가 받아야 한다 — 중복 제거 확인.
    if (psoManager.GetOrCreate(triangleDesc, error) != pso)
    {
        outLog += "[2/4] 메모리 캐시 실패 — 같은 desc가 다른 PSO를 돌려줬다\n";
        return false;
    }

    {
        const auto stats = psoManager.GetStats();
        outLog += "[2/4] 루트 시그니처·PSO 생성 완료(삼각형 + 텍스처 쿼드) — 컴파일 "
            + std::to_string(stats.compiles) + " · 라이브러리 히트 "
            + std::to_string(stats.libraryHits) + " · 메모리 히트 "
            + std::to_string(stats.memoryHits) + "\n";
    }

    // ── 체커보드 텍스처 생성·업로드 ──
    //
    // 스테이징은 업로드 링에서 잘라 쓴다. 예전에는 텍스처마다 업로드 버퍼를
    // 새로 만들고 GPU가 다 읽을 때까지 살려 뒀는데(그래서 여기 ComPtr가 하나
    // 더 있었다), 씬을 이식하면 그 방식이 프레임당 수십~수백 건이 된다.
    ComPtr<ID3D12Resource> texture;
    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC texDesc{};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = kTexSize;
        texDesc.Height = kTexSize;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&texture))))
        {
            outLog += "[2/4] 텍스처 생성 실패\n";
            return false;
        }

        // 64px * 4바이트 = 256 = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT — 패딩 불필요.
        constexpr uint32_t rowPitch = kTexSize * 4;
        constexpr uint64_t uploadBytes = static_cast<uint64_t>(rowPitch) * kTexSize;

        // 업로드는 전용 사이클로 제출 — 렌더 프레임과 섞지 않아 실패 지점이 분리된다.
        // BeginFrame이 업로드 링의 이 프레임 구간을 되감아 주므로, 링에서 잘라내는
        // 것은 그 뒤여야 한다.
        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] 업로드 Begin 실패: " + error + "\n";
            return false;
        }

        // 텍스처 스테이징은 512바이트 정렬이 필요하다(CopyTextureRegion의 요구).
        const auto staging = resources.GetUploadRing().Allocate(
            uploadBytes, DX12UploadRing::kTexturePlacementAlignment);
        if (!staging.IsValid())
        {
            outLog += "[2/4] 업로드 링 할당 실패(구간 부족)\n";
            return false;
        }

        auto* dst = static_cast<uint8_t*>(staging.cpuAddress);
        constexpr uint32_t cellSize = kTexSize / kCheckerCells;
        for (uint32_t y = 0; y < kTexSize; ++y)
        {
            for (uint32_t x = 0; x < kTexSize; ++x)
            {
                const bool isA = (((x / cellSize) + (y / cellSize)) % 2) == 0;
                const uint8_t* color = isA ? kColorA : kColorB;
                memcpy(&dst[y * rowPitch + x * 4], color, 4);
            }
        }

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = staging.resource;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = staging.offset;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        src.PlacedFootprint.Footprint.Width = kTexSize;
        src.PlacedFootprint.Footprint.Height = kTexSize;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = texture.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        resources.GetCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        resources.GetCommandList()->ResourceBarrier(1, &barrier);

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] 업로드 End 실패: " + error + "\n";
            return false;
        }
        resources.WaitForGpu();

    }
    outLog += "[2/4] 체커보드 텍스처 업로드 완료\n";

    // ── 프레임 루프: 얼로케이터 3개가 frameCount 동안 회전한다 ──
    for (uint32_t frame = 0; frame < frameCount; ++frame)
    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/4] 프레임 " + std::to_string(frame) + " Begin 실패: " + error + "\n";
            return false;
        }

        auto* commandList = resources.GetCommandList();

        const D3D12_VIEWPORT viewport{ 0.f, 0.f,
            static_cast<float>(kWidth), static_cast<float>(kHeight), 0.f, 1.f };
        const D3D12_RECT scissor{ 0, 0,
            static_cast<LONG>(kWidth), static_cast<LONG>(kHeight) };
        commandList->RSSetViewports(1, &viewport);
        commandList->RSSetScissorRects(1, &scissor);

        const auto rtvHandle = resources.GetRtvHandle();
        commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // 클리어 색은 생성 시 힌트와 반드시 일치해야 검증 레이어가 조용하다.
        // (프레임 번호를 배경에 실어 봤다가 경고 6건을 실측하고 고정으로 돌렸다 —
        //  얼로케이터 회전은 BeginFrame의 펜스 대기 6사이클이 이미 증명한다.)
        commandList->ClearRenderTargetView(rtvHandle, DX12DeviceResources::kClearColor, 0, nullptr);

        commandList->SetGraphicsRootSignature(triangleRoot.signature);
        commandList->SetPipelineState(pso);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(3, 1, 0, 0);

        // 텍스처 쿼드 — SRV를 이번 프레임 디스크립터 링에서 잘라 쓴다.
        //
        // 프레임마다 새로 자르고 뷰를 다시 만드는 것이 요점이다. 실제 씬에서는
        // 프레임마다 보이는 텍스처 집합이 달라지므로 고정 힙으로는 감당이 안 된다.
        // (뷰 생성 대신 CPU 전용 힙에서 CopyDescriptorsSimple로 가져오는 최적화가
        //  있지만, 그건 뷰가 재사용될 때의 얘기라 이식 뒤에 판단한다.)
        const auto srvSlot = resources.GetDescriptorRing().Allocate(1);
        if (!srvSlot.IsValid())
        {
            outLog += "[3/4] 디스크립터 링 할당 실패(구간 부족)\n";
            return false;
        }
        resources.GetDevice()->CreateShaderResourceView(texture.Get(), nullptr, srvSlot.cpu);

        // 힙 바인딩은 루트 테이블 설정보다 먼저(검증 레이어 규칙).
        // CBV/SRV/UAV 힙과 샘플러 힙은 종류가 달라 동시에 하나씩 바인딩된다.
        ID3D12DescriptorHeap* heaps[] = {
            resources.GetDescriptorRing().GetHeap(),
            resources.GetSamplerHeap().GetHeap() };
        commandList->SetDescriptorHeaps(2, heaps);
        commandList->SetGraphicsRootSignature(quadRoot.signature);
        commandList->SetPipelineState(quadPso);
        commandList->SetGraphicsRootDescriptorTable(0, srvSlot.gpu);
        commandList->SetGraphicsRootDescriptorTable(1, samplerHandle);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        commandList->DrawInstanced(4, 1, 0, 0);

        // 마지막 프레임만 리드백으로 복사 — RT ↔ COPY_SOURCE 상태 전이 검증을 겸한다.
        if (frame == frameCount - 1)
        {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resources.GetRenderTarget();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(1, &barrier);

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = resources.GetRenderTarget();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = resources.GetReadbackBuffer();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width = kWidth;
            dst.PlacedFootprint.Footprint.Height = kHeight;
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = resources.GetRowPitch();

            commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

            std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
            commandList->ResourceBarrier(1, &barrier);
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[3/4] 프레임 " + std::to_string(frame) + " End 실패: " + error + "\n";
            return false;
        }
    }

    resources.WaitForGpu();
    outLog += "[3/4] " + std::to_string(frameCount) + "프레임 제출·완료(얼로케이터 "
        + std::to_string(DX12DeviceResources::kFrameCount) + "개 회전)\n";

    // ── 리드백 → 픽셀 검증 → PNG ──
    {
        void* mapped = nullptr;
        const D3D12_RANGE readRange{ 0,
            static_cast<SIZE_T>(resources.GetRowPitch()) * kHeight };
        if (FAILED(resources.GetReadbackBuffer()->Map(0, &readRange, &mapped)))
        {
            outLog += "[4/4] 리드백 Map 실패\n";
            return false;
        }

        const auto* pixels = static_cast<const uint8_t*>(mapped);
        const uint32_t rowPitch = resources.GetRowPitch();

        // 중앙(삼각형 내부)은 클리어 색이 아니어야 하고, 좌상단 구석은 클리어 색이어야 한다.
        auto pixelAt = [&](uint32_t x, uint32_t y)
        {
            return &pixels[static_cast<size_t>(y) * rowPitch + static_cast<size_t>(x) * 4];
        };
        const uint8_t* center = pixelAt(kWidth / 2, kHeight / 2);
        const uint8_t* corner = pixelAt(4, 4);

        // 구석은 클리어 색과 정확히 일치해야 한다. (처음엔 '파랑 우세'라는 느슨한
        // 단정을 썼다가 렌더는 맞는데 검사가 틀리는 오탐을 냈다 — 기대값 비교로 교체.)
        const auto expected = static_cast<uint8_t>(DX12DeviceResources::kClearColor[0] * 255.f + 0.5f);
        const auto expectedBlue = static_cast<uint8_t>(DX12DeviceResources::kClearColor[2] * 255.f + 0.5f);
        // 주의: near/far는 windef.h 매크로라 식별자로 못 쓴다.
        auto isNear = [](uint8_t a, uint8_t b) { return (a > b ? a - b : b - a) <= 2; };

        const bool centerIsTriangle = center[0] != corner[0] || center[1] != corner[1] || center[2] != corner[2];
        const bool cornerIsClear = isNear(corner[0], expected) && isNear(corner[1], expected)
            && isNear(corner[2], expectedBlue);

        // 텍스처 쿼드: NDC [-0.9,-0.3]²이 640x360에서 (32,234)-(224,342)로 맵핑된다.
        // 4x4 체커의 (0,0)·(1,0) 셀 중앙을 찍는다 — 포인트 샘플링이라 결정적이다.
        auto matches = [&isNear](const uint8_t* pixel, const uint8_t* expectedColor)
        {
            return isNear(pixel[0], expectedColor[0]) && isNear(pixel[1], expectedColor[1])
                && isNear(pixel[2], expectedColor[2]);
        };
        const uint8_t* checkerA = pixelAt(56, 247);
        const uint8_t* checkerB = pixelAt(104, 247);
        const bool quadIsTextured = matches(checkerA, kColorA) && matches(checkerB, kColorB);

        if (!centerIsTriangle || !cornerIsClear || !quadIsTextured)
        {
            outLog += "[4/4] 픽셀 검증 실패 — 중앙("
                + std::to_string(center[0]) + "," + std::to_string(center[1]) + "," + std::to_string(center[2])
                + ") 구석(" + std::to_string(corner[0]) + "," + std::to_string(corner[1]) + "," + std::to_string(corner[2])
                + ") 체커A(" + std::to_string(checkerA[0]) + "," + std::to_string(checkerA[1]) + "," + std::to_string(checkerA[2])
                + ") 체커B(" + std::to_string(checkerB[0]) + "," + std::to_string(checkerB[1]) + "," + std::to_string(checkerB[2]) + ")\n";
            resources.GetReadbackBuffer()->Unmap(0, nullptr);
            return false;
        }

        DirectX::Image image{};
        image.width = kWidth;
        image.height = kHeight;
        image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        image.rowPitch = rowPitch;
        image.slicePitch = static_cast<size_t>(rowPitch) * kHeight;
        image.pixels = const_cast<uint8_t*>(pixels);

        const std::wstring widePath(outputPngPath.begin(), outputPngPath.end());
        const HRESULT hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
            DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), widePath.c_str());

        resources.GetReadbackBuffer()->Unmap(0, nullptr);

        if (FAILED(hr))
        {
            outLog += "[4/4] PNG 저장 실패\n";
            return false;
        }
    }

    // 검증 레이어에 WARNING 이상이 없어야 진짜 통과다(INFO는 로그에만 남는다).
    std::string debugMessages;
    const uint32_t problems = resources.DrainDebugMessages(debugMessages);
    if (problems > 0)
    {
        outLog += "[4/4] 검증 레이어 문제 " + std::to_string(problems) + "건:\n" + debugMessages;
        return false;
    }

    // PSO 캐시를 남긴다 — 다음 실행/다음 매니저가 컴파일 없이 복원해야 한다.
    if (!psoManager.SaveCache(error))
    {
        outLog += "[4/4] PSO 캐시 저장 실패: " + error + "\n";
        return false;
    }

    {
        const auto stats = psoManager.GetStats();
        outLog += "[4/4] PSO 캐시 저장 — 컴파일 " + std::to_string(stats.compiles)
            + " · 라이브러리 히트 " + std::to_string(stats.libraryHits) + "\n";
    }

    psoManager.Shutdown();
    outLog += "[4/4] 픽셀 검증·PNG 저장·검증 레이어 클린 — 통과\n";
    resources.Shutdown();
    return true;
}

bool EnhancedSceneRenderer::RunPsoCacheTest(const std::string& cacheFilePath, std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    // 렌더는 하지 않는다 — 디바이스만 있으면 PSO 생성·캐시는 검증된다.
    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(64, 64, error))
    {
        outLog += "디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (!CompileShader("VSMain", "vs_5_0", vsBlob, outLog)) return false;
    if (!CompileShader("PSMain", "ps_5_0", psBlob, outLog)) return false;

    DX12RootSignatureCache rootSignatureCache;
    if (!rootSignatureCache.Initialize(resources.GetDevice(), error))
    {
        outLog += "루트 시그니처 캐시 초기화 실패: " + error + "\n";
        return false;
    }

    DX12RootSignatureCache::Entry emptyRoot;
    {
        D3D12_ROOT_SIGNATURE_DESC desc{};
        emptyRoot = rootSignatureCache.GetOrCreate(desc, error);
        if (!emptyRoot.IsValid())
        {
            outLog += "루트 시그니처 준비 실패: " + error + "\n";
            return false;
        }
    }

    // ── 루트 시그니처 캐시 자체 검증 ──
    //
    // PSO 캐시의 키에 rootSignatureId가 들어가므로, 이 식별자가 레이아웃을
    // 제대로 구분하지 못하면 PSO 캐시가 엉뚱한 파이프라인을 돌려준다.
    // 예전에는 호출부가 번호를 손으로 붙였고(1, 2, 10), 그 규율이 깨지는 순간
    // 원인이 '캐시 히트'인 버그가 된다. 구조가 막는지 여기서 확인한다.
    {
        // 같은 레이아웃을 다시 요청하면 같은 객체·같은 id여야 한다.
        D3D12_ROOT_SIGNATURE_DESC sameDesc{};
        const auto again = rootSignatureCache.GetOrCreate(sameDesc, error);

        // 레이아웃이 다르면 id가 달라야 한다 — 이것이 손번호가 못 하던 일이다.
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC tableDesc{};
        tableDesc.NumParameters = 1;
        tableDesc.pParameters = &param;
        const auto tableRoot = rootSignatureCache.GetOrCreate(tableDesc, error);

        // 같은 내용을 다른 주소에 담아도 같은 id여야 한다. 구조체를 통째로
        // 바이트 해시하면 포인터를 해시하게 되어 여기서 걸린다 — 그러면 캐시가
        // 통째로 놀고, 증상은 '왜인지 매번 컴파일한다'로만 보인다.
        D3D12_DESCRIPTOR_RANGE rangeCopy = range;
        D3D12_ROOT_PARAMETER paramCopy = param;
        paramCopy.DescriptorTable.pDescriptorRanges = &rangeCopy;
        D3D12_ROOT_SIGNATURE_DESC tableCopyDesc{};
        tableCopyDesc.NumParameters = 1;
        tableCopyDesc.pParameters = &paramCopy;
        const auto tableCopyRoot = rootSignatureCache.GetOrCreate(tableCopyDesc, error);

        const bool sameReused = again.IsValid() && again.id == emptyRoot.id
            && again.signature == emptyRoot.signature;
        const bool differentSeparated = tableRoot.IsValid() && tableRoot.id != emptyRoot.id;
        const bool contentHashed = tableCopyRoot.IsValid()
            && tableCopyRoot.id == tableRoot.id
            && tableCopyRoot.signature == tableRoot.signature;

        if (!sameReused || !differentSeparated || !contentHashed)
        {
            outLog += "루트 시그니처 캐시 검증 실패 (재사용 ";
            outLog += sameReused ? "O" : "X";
            outLog += " · 구분 ";
            outLog += differentSeparated ? "O" : "X";
            outLog += " · 내용해시 ";
            outLog += contentHashed ? "O" : "X";
            outLog += ")\n";
            return false;
        }

        const auto rootStats = rootSignatureCache.GetStats();
        outLog += "루트 시그니처 캐시 검증 통과 — 생성 " + std::to_string(rootStats.creates)
            + " · 히트 " + std::to_string(rootStats.hits)
            + " · 보관 " + std::to_string(rootSignatureCache.GetCachedCount()) + "\n";
    }

    // 상태만 다른 변형 3종 — 해시가 상태를 실제로 구분하는지 확인한다.
    // (셰이더가 같아도 다른 PSO여야 한다)
    DX12GraphicsPipelineDesc base{};
    base.vsBytecode = vsBlob->GetBufferPointer();
    base.vsSize = vsBlob->GetBufferSize();
    base.psBytecode = psBlob->GetBufferPointer();
    base.psSize = psBlob->GetBufferSize();
    base.rootSignature = emptyRoot.signature;
    base.rootSignatureId = emptyRoot.id;

    DX12GraphicsPipelineDesc variants[3] = { base, base, base };
    variants[1].cullMode = D3D12_CULL_MODE_BACK;
    variants[2].blendEnable = true;

    if (variants[0].ComputeHash() == variants[1].ComputeHash() ||
        variants[0].ComputeHash() == variants[2].ComputeHash())
    {
        outLog += "해시가 상태 차이를 구분하지 못한다\n";
        return false;
    }

    // 캐시 파일을 지우고 시작 — 1회차의 '컴파일 N건'을 결정적으로 만든다.
    std::remove(cacheFilePath.c_str());
    const std::wstring widePath(cacheFilePath.begin(), cacheFilePath.end());

    // ── 1회차: 캐시 없음 → 전부 컴파일 ──
    {
        DX12PSOManager manager;
        if (!manager.Initialize(resources.GetDevice(), widePath, error))
        {
            outLog += "1회차 초기화 실패: " + error + "\n";
            return false;
        }

        for (auto& variant : variants)
        {
            if (!manager.GetOrCreate(variant, error))
            {
                outLog += "1회차 PSO 생성 실패: " + error + "\n";
                return false;
            }
        }

        const auto stats = manager.GetStats();
        if (stats.compiles != 3 || stats.libraryHits != 0)
        {
            outLog += "1회차 기대와 다름 — 컴파일 " + std::to_string(stats.compiles)
                + "(기대 3) · 라이브러리 히트 " + std::to_string(stats.libraryHits) + "(기대 0)\n";
            return false;
        }

        if (!manager.SaveCache(error))
        {
            outLog += "1회차 캐시 저장 실패: " + error + "\n";
            return false;
        }
        manager.Shutdown();
        outLog += "1회차: 컴파일 3 · 캐시 저장 완료\n";
    }

    // ── 2회차: 캐시 복원 → 컴파일 0 ──
    {
        DX12PSOManager manager;
        if (!manager.Initialize(resources.GetDevice(), widePath, error))
        {
            outLog += "2회차 초기화 실패: " + error + "\n";
            return false;
        }

        if (!manager.IsLibraryLoaded())
        {
            outLog += "2회차: 캐시 파일을 라이브러리로 복원하지 못했다\n";
            return false;
        }

        for (auto& variant : variants)
        {
            if (!manager.GetOrCreate(variant, error))
            {
                outLog += "2회차 PSO 취득 실패: " + error + "\n";
                return false;
            }
        }

        // 메모리 캐시 확인 — 같은 요청을 반복해도 컴파일이 늘지 않아야 한다.
        for (auto& variant : variants)
        {
            manager.GetOrCreate(variant, error);
        }

        const auto stats = manager.GetStats();
        if (stats.compiles != 0 || stats.libraryHits != 3 || stats.memoryHits != 3)
        {
            outLog += "2회차 기대와 다름 — 컴파일 " + std::to_string(stats.compiles)
                + "(기대 0) · 라이브러리 히트 " + std::to_string(stats.libraryHits)
                + "(기대 3) · 메모리 히트 " + std::to_string(stats.memoryHits) + "(기대 3)\n";
            return false;
        }

        manager.Shutdown();
        outLog += "2회차: 컴파일 0 · 라이브러리 히트 3 · 메모리 히트 3 — 캐시가 컴파일을 없앴다\n";
    }

    // ── 비동기 + 폴백: 컴파일 중에도 프레임이 그릴 것을 갖는다 ──
    {
        DX12PSOManager manager;
        if (!manager.Initialize(resources.GetDevice(), L"", error))
        {
            outLog += "비동기 초기화 실패: " + error + "\n";
            return false;
        }

        // 폴백을 먼저 세운다(동기). 이후 다른 desc를 비동기 요청하면 그 사이
        // 프레임은 폴백으로 그려져야 한다.
        if (!manager.SetFallback(variants[0], error))
        {
            outLog += "폴백 PSO 준비 실패: " + error + "\n";
            return false;
        }

        ID3D12PipelineState* resolved = nullptr;
        const auto first = manager.Resolve(variants[1], &resolved);
        if (first != DX12PSOManager::DrawDecision::UseFallback || nullptr == resolved)
        {
            outLog += "컴파일 중인데 폴백이 나오지 않았다 — 프레임이 그릴 것을 잃는다\n";
            return false;
        }

        // 완료까지 폴링. 실전에서는 이 사이의 프레임이 폴백으로 그려진다.
        DX12PSOManager::DrawDecision decision = DX12PSOManager::DrawDecision::UseFallback;
        for (int attempt = 0; attempt < 500 && decision != DX12PSOManager::DrawDecision::UseRequested; ++attempt)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            decision = manager.Resolve(variants[1], &resolved);
        }

        if (decision != DX12PSOManager::DrawDecision::UseRequested || nullptr == resolved)
        {
            outLog += "비동기 컴파일이 완료되지 않았다\n";
            return false;
        }

        const auto stats = manager.GetStats();
        if (stats.fallbackDraws == 0)
        {
            outLog += "폴백 카운터가 0이다 — 통계가 실제 동작을 반영하지 않는다\n";
            return false;
        }
        if (stats.skippedDraws != 0)
        {
            outLog += "폴백이 있는데 Skip이 발생했다\n";
            return false;
        }

        outLog += "비동기+폴백: 컴파일 중 폴백 " + std::to_string(stats.fallbackDraws)
            + "회 → 완료 후 요청 PSO로 전환 · 스킵 0\n";

        // ── 셰이더 리로드: 메모리 캐시가 비워지는가 ──
        manager.OnShaderReloaded();
        ID3D12PipelineState* afterReload = nullptr;
        // 리로드 직후에는 메모리 캐시가 비어 있으므로 다시 Pending이어야 한다.
        if (manager.Resolve(variants[1], &afterReload) == DX12PSOManager::DrawDecision::UseRequested)
        {
            outLog += "리로드 후에도 옛 캐시가 살아 있다\n";
            return false;
        }
        outLog += "셰이더 리로드: 메모리 캐시 비움 확인(디스크 라이브러리는 유지)\n";

        manager.Shutdown();
    }

    // ── 컴퓨트 PSO: 같은 캐시 2층을 타는가 ──
    {
        ComPtr<ID3DBlob> csBlob;
        if (!CompileShader("CSMain", "cs_5_0", csBlob, outLog)) return false;

        DX12RootSignatureCache::Entry computeRoot;
        {
            D3D12_DESCRIPTOR_RANGE range{};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            range.NumDescriptors = 1;

            D3D12_ROOT_PARAMETER param{};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = &range;

            D3D12_ROOT_SIGNATURE_DESC desc{};
            desc.NumParameters = 1;
            desc.pParameters = &param;

            computeRoot = rootSignatureCache.GetOrCreate(desc, error);
            if (!computeRoot.IsValid())
            {
                outLog += "컴퓨트 루트 시그니처 준비 실패: " + error + "\n";
                return false;
            }
        }

        DX12ComputePipelineDesc computeDesc{};
        computeDesc.csBytecode = csBlob->GetBufferPointer();
        computeDesc.csSize = csBlob->GetBufferSize();
        computeDesc.rootSignature = computeRoot.signature;
        computeDesc.rootSignatureId = computeRoot.id;

        const std::string computeCachePath = cacheFilePath + ".compute";
        std::remove(computeCachePath.c_str());
        const std::wstring computeWidePath(computeCachePath.begin(), computeCachePath.end());

        {
            DX12PSOManager manager;
            if (!manager.Initialize(resources.GetDevice(), computeWidePath, error))
            {
                outLog += "컴퓨트 1회차 초기화 실패: " + error + "\n";
                return false;
            }
            if (!manager.GetOrCreateCompute(computeDesc, error))
            {
                outLog += "컴퓨트 PSO 생성 실패: " + error + "\n";
                return false;
            }
            if (manager.GetStats().compiles != 1)
            {
                outLog += "컴퓨트 1회차 컴파일이 1이 아니다\n";
                return false;
            }
            if (!manager.SaveCache(error))
            {
                outLog += "컴퓨트 캐시 저장 실패: " + error + "\n";
                return false;
            }
            manager.Shutdown();
        }

        {
            DX12PSOManager manager;
            if (!manager.Initialize(resources.GetDevice(), computeWidePath, error))
            {
                outLog += "컴퓨트 2회차 초기화 실패: " + error + "\n";
                return false;
            }
            if (!manager.GetOrCreateCompute(computeDesc, error))
            {
                outLog += "컴퓨트 2회차 취득 실패: " + error + "\n";
                return false;
            }
            const auto stats = manager.GetStats();
            if (stats.compiles != 0 || stats.libraryHits != 1)
            {
                outLog += "컴퓨트 2회차 기대와 다름 — 컴파일 " + std::to_string(stats.compiles)
                    + "(기대 0) · 라이브러리 히트 " + std::to_string(stats.libraryHits) + "(기대 1)\n";
                return false;
            }
            manager.Shutdown();
        }

        outLog += "컴퓨트 PSO: 1회차 컴파일 1 → 2회차 컴파일 0 · 라이브러리 히트 1\n";
    }

    resources.Shutdown();
    outLog += "PSO 캐시 검증 통과\n";
    return true;
}

bool EnhancedSceneRenderer::RunUploadRingTest(std::string& outLog)
{
    DX12DeviceResources resources;
    std::string error;

    if (!resources.Initialize(64, 64, error))
    {
        outLog += "[1/5] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    DX12UploadRing& ring = resources.GetUploadRing();
    const uint64_t bytesPerFrame = ring.GetBytesPerFrame();
    const uint32_t frameCount = ring.GetFrameCount();
    bool passed = true;

    // ── [1/5] 정렬 ──
    //
    // 상수 버퍼 뷰는 256 정렬이 아니면 생성 자체가 실패하고, 텍스처 복사는
    // 512 정렬이 아니면 검증 레이어가 잡는다. 어긋난 크기를 일부러 섞어
    // 요청해도 반환 오프셋이 정렬돼 있어야 한다.
    {
        if (!resources.BeginFrame(error)) { outLog += "[1/5] Begin 실패: " + error + "\n"; return false; }

        const uint64_t sizes[] = { 1, 17, 100, 255, 257, 1000 };
        uint32_t misaligned = 0;
        for (const uint64_t size : sizes)
        {
            const auto cbv = ring.Allocate(size, DX12UploadRing::kConstantBufferAlignment);
            const auto tex = ring.Allocate(size, DX12UploadRing::kTexturePlacementAlignment);
            if (!cbv.IsValid() || !tex.IsValid()) { ++misaligned; continue; }
            if (0 != (cbv.offset % DX12UploadRing::kConstantBufferAlignment)) ++misaligned;
            if (0 != (tex.offset % DX12UploadRing::kTexturePlacementAlignment)) ++misaligned;

            // GPU 주소도 같은 정렬이어야 한다 — 오프셋만 맞고 기준 주소가
            // 어긋나면 상수 버퍼 뷰가 런타임에 거절된다.
            if (0 != (cbv.gpuAddress % DX12UploadRing::kConstantBufferAlignment)) ++misaligned;
        }

        if (!resources.EndFrame(error)) { outLog += "[1/5] End 실패: " + error + "\n"; return false; }

        if (0 != misaligned) { passed = false; }
        outLog += "[1/5] 정렬 " + std::string(0 == misaligned ? "통과" : "실패")
            + " (어긋남 " + std::to_string(misaligned) + "건)\n";
    }

    // ── [2/5] 프레임 구간 분리 ──
    //
    // 프레임 i의 할당은 [i*구간, (i+1)*구간) 안에 있어야 한다. 겹치면 GPU가
    // 아직 읽는 중인 데이터를 덮어쓰게 된다 — 링에서 가장 치명적인 실수다.
    {
        uint32_t outOfRange = 0;
        for (uint32_t frame = 0; frame < frameCount; ++frame)
        {
            if (!resources.BeginFrame(error)) { outLog += "[2/5] Begin 실패\n"; return false; }

            const auto allocation = ring.Allocate(1024, DX12UploadRing::kConstantBufferAlignment);
            if (!allocation.IsValid()) { ++outOfRange; }
            else
            {
                // BeginFrame이 회전시킨 실제 슬롯을 오프셋에서 역산한다.
                const uint64_t segment = allocation.offset / bytesPerFrame;
                const uint64_t withinSegment = allocation.offset % bytesPerFrame;
                if (segment >= frameCount || withinSegment + allocation.size > bytesPerFrame)
                {
                    ++outOfRange;
                }
            }

            if (!resources.EndFrame(error)) { outLog += "[2/5] End 실패\n"; return false; }
        }

        if (0 != outOfRange) { passed = false; }
        outLog += "[2/5] 프레임 구간 분리 " + std::string(0 == outOfRange ? "통과" : "실패")
            + " (범위 이탈 " + std::to_string(outOfRange) + "건)\n";
    }

    // ── [3/5] 되감기 ──
    //
    // BeginFrame이 커서를 되감지 않으면 몇 프레임 만에 구간이 차서 할당이
    // 거절되기 시작한다. 같은 크기를 여러 프레임 요청해 사용량이 누적되지
    // 않는지 본다.
    {
        uint64_t firstUsed = 0;
        uint64_t lastUsed = 0;
        for (uint32_t frame = 0; frame < frameCount * 2; ++frame)
        {
            if (!resources.BeginFrame(error)) { outLog += "[3/5] Begin 실패\n"; return false; }
            ring.Allocate(4096, DX12UploadRing::kConstantBufferAlignment);
            lastUsed = ring.GetFrameUsedBytes();
            if (0 == frame) firstUsed = lastUsed;
            if (!resources.EndFrame(error)) { outLog += "[3/5] End 실패\n"; return false; }
        }

        const bool rewound = (firstUsed == lastUsed) && (0 != firstUsed);
        if (!rewound) { passed = false; }
        outLog += "[3/5] 되감기 " + std::string(rewound ? "통과" : "실패")
            + " (첫 프레임 " + std::to_string(firstUsed)
            + "B · " + std::to_string(frameCount * 2) + "번째 " + std::to_string(lastUsed) + "B)\n";
    }

    // ── [4/5] 넘침 거절 ──
    //
    // 구간보다 큰 요청은 무효를 돌려줘야 한다. 조용히 다음 구간을 침범하면
    // 증상이 다음 프레임에 나타나 추적이 어렵다.
    {
        if (!resources.BeginFrame(error)) { outLog += "[4/5] Begin 실패\n"; return false; }

        const uint64_t before = ring.GetStats().overflows;
        const auto tooBig = ring.Allocate(bytesPerFrame + 1, DX12UploadRing::kConstantBufferAlignment);
        const uint64_t after = ring.GetStats().overflows;

        // 거절 뒤에도 링이 멀쩡해야 한다 — 정상 요청은 계속 성공해야 한다.
        const auto normal = ring.Allocate(256, DX12UploadRing::kConstantBufferAlignment);

        if (!resources.EndFrame(error)) { outLog += "[4/5] End 실패\n"; return false; }

        const bool rejected = !tooBig.IsValid() && (after == before + 1) && normal.IsValid();
        if (!rejected) { passed = false; }
        outLog += "[4/5] 넘침 거절 " + std::string(rejected ? "통과" : "실패") + "\n";
    }

    // ── [5/5] 실제 GPU 도달 ──
    //
    // 위 넷은 전부 CPU 쪽 계산이다. 링을 거친 데이터가 정말 GPU 리소스에
    // 닿는지는 복사해서 되읽어야만 알 수 있다.
    {
        constexpr uint32_t kBytes = 1024;
        ComPtr<ID3D12Resource> destination;

        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = kBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        // 버퍼는 초기 상태를 지정해도 무시되고 COMMON으로 만들어진다(검증 레이어가
        // 경고로 알려 준다). 첫 사용 시 암묵 승격으로 COPY_DEST가 되므로 동작은
        // 같지만, 힌트를 사실과 맞춰 두어야 경고가 쌓이지 않는다.
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&destination))))
        {
            outLog += "[5/5] 대상 버퍼 생성 실패\n";
            return false;
        }

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        ComPtr<ID3D12Resource> readback;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&readback))))
        {
            outLog += "[5/5] 리드백 버퍼 생성 실패\n";
            return false;
        }

        if (!resources.BeginFrame(error)) { outLog += "[5/5] Begin 실패\n"; return false; }

        const auto staging = ring.Allocate(kBytes, DX12UploadRing::kConstantBufferAlignment);
        if (!staging.IsValid()) { outLog += "[5/5] 링 할당 실패\n"; return false; }

        auto* bytes = static_cast<uint8_t*>(staging.cpuAddress);
        for (uint32_t i = 0; i < kBytes; ++i)
        {
            bytes[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);
        }

        resources.GetCommandList()->CopyBufferRegion(destination.Get(), 0,
            staging.resource, staging.offset, kBytes);

        D3D12_RESOURCE_BARRIER toSource{};
        toSource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSource.Transition.pResource = destination.Get();
        toSource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;  // 복사 대상으로 암묵 승격된 상태
        toSource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toSource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        resources.GetCommandList()->ResourceBarrier(1, &toSource);

        resources.GetCommandList()->CopyBufferRegion(readback.Get(), 0, destination.Get(), 0, kBytes);

        if (!resources.EndFrame(error)) { outLog += "[5/5] End 실패\n"; return false; }
        resources.WaitForGpu();

        void* mapped = nullptr;
        D3D12_RANGE range{ 0, kBytes };
        if (FAILED(readback->Map(0, &range, &mapped)))
        {
            outLog += "[5/5] 리드백 Map 실패\n";
            return false;
        }

        const auto* readBytes = static_cast<const uint8_t*>(mapped);
        uint32_t mismatches = 0;
        for (uint32_t i = 0; i < kBytes; ++i)
        {
            if (readBytes[i] != static_cast<uint8_t>((i * 7 + 13) & 0xFF)) ++mismatches;
        }
        readback->Unmap(0, nullptr);

        if (0 != mismatches) { passed = false; }
        outLog += "[5/5] GPU 도달 " + std::string(0 == mismatches ? "통과" : "실패")
            + " (" + std::to_string(kBytes) + "바이트 중 불일치 "
            + std::to_string(mismatches) + ")\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    const auto stats = ring.GetStats();
    outLog += "링 통계: 할당 " + std::to_string(stats.allocations)
        + "건 · 누적 " + std::to_string(stats.bytesAllocated)
        + "B · 최대 프레임 사용 " + std::to_string(stats.peakFrameBytes)
        + "B / 구간 " + std::to_string(bytesPerFrame) + "B\n";

    resources.Shutdown();

    outLog += passed ? "업로드 링 검증 통과\n" : "업로드 링 검증 실패\n";
    return passed;
}

bool EnhancedSceneRenderer::RunDescriptorHeapTest(std::string& outLog)
{
    DX12DeviceResources resources;
    std::string error;

    if (!resources.Initialize(64, 64, error))
    {
        outLog += "[1/5] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    DX12DescriptorRing& ring = resources.GetDescriptorRing();
    const uint32_t perFrame = ring.GetDescriptorsPerFrame();
    const uint32_t frameCount = ring.GetFrameCount();
    bool passed = true;

    // ── [1/5] 핸들 연속성 ──
    //
    // 디스크립터 테이블은 연속이어야 한다. 구간 안의 i번째 핸들이 increment 크기
    // 간격으로 정확히 떨어지지 않으면, 테이블 두 번째 원소부터 엉뚱한 리소스를
    // 가리키게 된다 — 화면에는 '텍스처 하나만 틀리게' 나와서 원인을 찾기 어렵다.
    {
        if (!resources.BeginFrame(error)) { outLog += "[1/5] Begin 실패\n"; return false; }

        const auto range = ring.Allocate(4);
        uint32_t broken = 0;
        if (!range.IsValid() || 4 != range.count || 0 == range.incrementSize)
        {
            ++broken;
        }
        else
        {
            for (uint32_t i = 0; i < range.count; ++i)
            {
                const auto handle = range.CpuAt(i);
                if (handle.ptr != range.cpu.ptr + static_cast<SIZE_T>(i) * range.incrementSize)
                {
                    ++broken;
                }
            }

            // 다음 할당은 앞 구간 바로 뒤에 붙어야 한다(겹치지도, 비지도 않게).
            const auto next = ring.Allocate(1);
            if (!next.IsValid() ||
                next.cpu.ptr != range.cpu.ptr + static_cast<SIZE_T>(4) * range.incrementSize ||
                next.gpu.ptr != range.gpu.ptr + static_cast<UINT64>(4) * range.incrementSize)
            {
                ++broken;
            }
        }

        if (!resources.EndFrame(error)) { outLog += "[1/5] End 실패\n"; return false; }

        if (0 != broken) { passed = false; }
        outLog += "[1/5] 핸들 연속성 " + std::string(0 == broken ? "통과" : "실패")
            + " (어긋남 " + std::to_string(broken) + "건)\n";
    }

    // ── [2/5] 프레임 구간 분리 ──
    {
        uint32_t outOfRange = 0;
        for (uint32_t frame = 0; frame < frameCount; ++frame)
        {
            if (!resources.BeginFrame(error)) { outLog += "[2/5] Begin 실패\n"; return false; }

            const auto allocation = ring.Allocate(8);
            if (!allocation.IsValid()) { ++outOfRange; }
            else
            {
                // 기준 힙에서 몇 번째 디스크립터인지 역산해 구간 안인지 본다.
                const SIZE_T base = ring.GetHeap()->GetCPUDescriptorHandleForHeapStart().ptr;
                const uint32_t index = static_cast<uint32_t>(
                    (allocation.cpu.ptr - base) / allocation.incrementSize);
                const uint32_t segment = index / perFrame;
                const uint32_t within = index % perFrame;
                if (segment >= frameCount || within + allocation.count > perFrame)
                {
                    ++outOfRange;
                }
            }

            if (!resources.EndFrame(error)) { outLog += "[2/5] End 실패\n"; return false; }
        }

        if (0 != outOfRange) { passed = false; }
        outLog += "[2/5] 프레임 구간 분리 " + std::string(0 == outOfRange ? "통과" : "실패")
            + " (범위 이탈 " + std::to_string(outOfRange) + "건)\n";
    }

    // ── [3/5] 되감기 ──
    {
        uint32_t firstUsed = 0;
        uint32_t lastUsed = 0;
        for (uint32_t frame = 0; frame < frameCount * 2; ++frame)
        {
            if (!resources.BeginFrame(error)) { outLog += "[3/5] Begin 실패\n"; return false; }
            ring.Allocate(16);
            lastUsed = ring.GetFrameUsed();
            if (0 == frame) firstUsed = lastUsed;
            if (!resources.EndFrame(error)) { outLog += "[3/5] End 실패\n"; return false; }
        }

        const bool rewound = (firstUsed == lastUsed) && (0 != firstUsed);
        if (!rewound) { passed = false; }
        outLog += "[3/5] 되감기 " + std::string(rewound ? "통과" : "실패")
            + " (첫 프레임 " + std::to_string(firstUsed)
            + "개 · " + std::to_string(frameCount * 2) + "번째 " + std::to_string(lastUsed) + "개)\n";
    }

    // ── [4/5] 넘침 거절 ──
    {
        if (!resources.BeginFrame(error)) { outLog += "[4/5] Begin 실패\n"; return false; }

        const uint64_t before = ring.GetStats().overflows;
        const auto tooMany = ring.Allocate(perFrame + 1);
        const uint64_t after = ring.GetStats().overflows;
        const auto normal = ring.Allocate(1);

        if (!resources.EndFrame(error)) { outLog += "[4/5] End 실패\n"; return false; }

        const bool rejected = !tooMany.IsValid() && (after == before + 1) && normal.IsValid();
        if (!rejected) { passed = false; }
        outLog += "[4/5] 넘침 거절 " + std::string(rejected ? "통과" : "실패") + "\n";
    }

    // ── [5/5] 샘플러 중복 제거 ──
    //
    // 샘플러 힙은 상한이 2048로 작다. 같은 설정을 머티리얼마다 새로 만들면
    // 큰 씬에서 상한에 먼저 부딪히고, 그때 증상은 '어느 순간부터 샘플러가
    // 안 만들어진다'라 원인이 멀다.
    {
        DX12SamplerHeap& samplers = resources.GetSamplerHeap();

        D3D12_SAMPLER_DESC linear{};
        linear.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        linear.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linear.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linear.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        linear.MaxLOD = D3D12_FLOAT32_MAX;

        D3D12_SAMPLER_DESC point = linear;
        point.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;

        const auto a = samplers.GetOrCreate(linear);
        const auto again = samplers.GetOrCreate(linear);   // 같은 설정 → 같은 핸들
        const auto b = samplers.GetOrCreate(point);        // 다른 설정 → 다른 핸들

        const bool deduped = (0 != a.ptr) && (a.ptr == again.ptr);
        const bool separated = (0 != b.ptr) && (b.ptr != a.ptr);

        if (!deduped || !separated) { passed = false; }

        const auto samplerStats = samplers.GetStats();
        outLog += "[5/5] 샘플러 중복 제거 "
            + std::string((deduped && separated) ? "통과" : "실패")
            + " (생성 " + std::to_string(samplerStats.creates)
            + " · 히트 " + std::to_string(samplerStats.hits)
            + " · 보관 " + std::to_string(samplers.GetCachedCount()) + ")\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    const auto ringStats = ring.GetStats();
    outLog += "링 통계: 할당 " + std::to_string(ringStats.allocations)
        + "건 · 디스크립터 " + std::to_string(ringStats.descriptors)
        + "개 · 최대 프레임 사용 " + std::to_string(ringStats.peakFrameDescriptors)
        + " / 구간 " + std::to_string(perFrame) + "\n";

    resources.Shutdown();

    outLog += passed ? "디스크립터 힙 검증 통과\n" : "디스크립터 힙 검증 실패\n";
    return passed;
}

bool EnhancedSceneRenderer::RunSharedTextureTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kSize = 64;

    // ── [1/4] DX11과 같은 어댑터에 DX12 디바이스 ──
    //
    // 공유는 같은 물리 어댑터에서만 성립한다. DX11은 기본 어댑터(nullptr)를,
    // DX12는 고성능 우선을 골라 왔으므로 정책이 갈릴 수 있다 — iGPU+dGPU
    // 노트북에서는 실제로 갈린다. LUID로 맞춘다.
    ID3D11Device3* dx11Device = DirectX11::DeviceStates->g_pDevice;
    if (nullptr == dx11Device)
    {
        outLog += "[1/4] DX11 디바이스가 없다(에디터 실행 중에만 의미 있는 검증)\n";
        return false;
    }

    LUID dx11Luid{};
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> dxgiAdapter;
        if (FAILED(dx11Device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
            FAILED(dxgiDevice->GetAdapter(&dxgiAdapter)))
        {
            outLog += "[1/4] DX11 어댑터 조회 실패\n";
            return false;
        }
        DXGI_ADAPTER_DESC adapterDesc{};
        dxgiAdapter->GetDesc(&adapterDesc);
        dx11Luid = adapterDesc.AdapterLuid;
    }

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kSize, kSize, error, dx11Luid))
    {
        outLog += "[1/4] 같은 어댑터에 DX12 디바이스 생성 실패: " + error + "\n";
        return false;
    }
    outLog += "[1/4] 어댑터 일치 확인 — DX11/DX12가 같은 물리 GPU에 올라갔다\n";

    // ── [2/4] 공유 텍스처 생성 + DX12로 그리기 ──
    //
    // 실제로는 렌더 타깃으로 그리지만, 여기서는 경로 검증이 목적이므로 업로드
    // 링으로 알려진 패턴을 채워 넣는다 — GPU가 쓴 내용이 DX11에 보이는지가
    // 확인 대상이지 무엇을 그렸는지는 중요하지 않다.
    ComPtr<ID3D12Resource> sharedTexture;
    {
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kSize;
        desc.Height = kSize;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        // SHARED 플래그가 핵심이다. 이게 없으면 CreateSharedHandle이 실패한다.
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&defaultHeap,
            D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&sharedTexture))))
        {
            outLog += "[2/4] 공유 텍스처 생성 실패\n";
            return false;
        }
    }

    constexpr uint32_t rowPitch = kSize * 4;
    if (!resources.BeginFrame(error))
    {
        outLog += "[2/4] Begin 실패: " + error + "\n";
        return false;
    }

    const auto staging = resources.GetUploadRing().Allocate(
        static_cast<uint64_t>(rowPitch) * kSize, DX12UploadRing::kTexturePlacementAlignment);
    if (!staging.IsValid())
    {
        outLog += "[2/4] 업로드 링 할당 실패\n";
        return false;
    }

    {
        auto* dst = static_cast<uint8_t*>(staging.cpuAddress);
        constexpr uint32_t cellSize = kSize / kCheckerCells;
        for (uint32_t y = 0; y < kSize; ++y)
        {
            for (uint32_t x = 0; x < kSize; ++x)
            {
                const bool isA = (((x / cellSize) + (y / cellSize)) % 2) == 0;
                const uint8_t* color = isA ? kColorA : kColorB;
                memcpy(&dst[y * rowPitch + x * 4], color, 4);
            }
        }
    }

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = staging.resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = staging.offset;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = kSize;
    src.PlacedFootprint.Footprint.Height = kSize;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = rowPitch;

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = sharedTexture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

    resources.GetCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &src, nullptr);

    // DX11이 읽을 상태로 돌려놓는다. 공유 리소스의 상태 전이는 DX12 쪽 책임이다.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = sharedTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    resources.GetCommandList()->ResourceBarrier(1, &barrier);

    if (!resources.EndFrame(error))
    {
        outLog += "[2/4] End 실패: " + error + "\n";
        return false;
    }

    // 브링업 검증이므로 완전 대기로 동기화한다. 실전에서는 공유 펜스
    // (D3D12_FENCE_FLAG_SHARED + ID3D11Fence)로 프레임을 겹쳐야 하고,
    // 그 설계는 이 경로가 뚫린 것을 확인한 뒤 3-6에서 붙인다.
    resources.WaitForGpu();
    outLog += "[2/4] DX12가 공유 텍스처에 체커보드 기록 완료\n";

    // ── [3/4] DX11에서 공유 핸들 열기 ──
    HANDLE sharedHandle = nullptr;
    if (FAILED(resources.GetDevice()->CreateSharedHandle(sharedTexture.Get(), nullptr,
        GENERIC_ALL, nullptr, &sharedHandle)) || nullptr == sharedHandle)
    {
        outLog += "[3/4] 공유 핸들 생성 실패\n";
        return false;
    }

    ComPtr<ID3D11Texture2D> openedTexture;
    ComPtr<ID3D11ShaderResourceView> openedSrv;
    {
        // NT 핸들 공유라 OpenSharedResource1이 필요하다(ID3D11Device1 이상).
        ComPtr<ID3D11Device1> device1;
        if (FAILED(dx11Device->QueryInterface(IID_PPV_ARGS(&device1))))
        {
            CloseHandle(sharedHandle);
            outLog += "[3/4] ID3D11Device1 조회 실패\n";
            return false;
        }

        const HRESULT openResult = device1->OpenSharedResource1(sharedHandle,
            IID_PPV_ARGS(&openedTexture));
        CloseHandle(sharedHandle);   // 열고 나면 핸들은 필요 없다 — 리소스가 참조를 든다

        if (FAILED(openedTexture ? S_OK : openResult) || !openedTexture)
        {
            outLog += "[3/4] DX11에서 공유 리소스 열기 실패\n";
            return false;
        }

        if (FAILED(dx11Device->CreateShaderResourceView(openedTexture.Get(), nullptr, &openedSrv)))
        {
            outLog += "[3/4] SRV 생성 실패\n";
            return false;
        }
    }
    outLog += "[3/4] DX11이 공유 텍스처를 SRV로 열었다 — ImGui::Image에 그대로 넘길 수 있다\n";

    // ── [4/4] DX11로 되읽어 픽셀 대조 ──
    //
    // SRV가 만들어졌다는 것만으로는 부족하다. 실제로 DX12가 쓴 내용이 보이는지는
    // 읽어 봐야 안다 — 어댑터가 갈렸거나 동기화가 없으면 여기서 어긋난다.
    {
        D3D11_TEXTURE2D_DESC stagingDesc{};
        openedTexture->GetDesc(&stagingDesc);
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> stagingTexture;
        if (FAILED(dx11Device->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture)))
        {
            outLog += "[4/4] 스테이징 텍스처 생성 실패\n";
            return false;
        }

        auto* context = DirectX11::DeviceStates->g_pDeviceContext;
        context->CopyResource(stagingTexture.Get(), openedTexture.Get());
        context->Flush();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            outLog += "[4/4] 스테이징 Map 실패\n";
            return false;
        }

        constexpr uint32_t cellSize = kSize / kCheckerCells;
        uint32_t mismatches = 0;
        const auto* rows = static_cast<const uint8_t*>(mapped.pData);
        for (uint32_t y = 0; y < kSize; ++y)
        {
            const auto* row = rows + static_cast<size_t>(y) * mapped.RowPitch;
            for (uint32_t x = 0; x < kSize; ++x)
            {
                const bool isA = (((x / cellSize) + (y / cellSize)) % 2) == 0;
                const uint8_t* expected = isA ? kColorA : kColorB;
                const uint8_t* actual = row + static_cast<size_t>(x) * 4;
                if (actual[0] != expected[0] || actual[1] != expected[1] ||
                    actual[2] != expected[2])
                {
                    ++mismatches;
                }
            }
        }
        context->Unmap(stagingTexture.Get(), 0);

        if (0 != mismatches)
        {
            outLog += "[4/4] 픽셀 대조 실패 — " + std::to_string(kSize * kSize)
                + "픽셀 중 불일치 " + std::to_string(mismatches) + "\n";
            return false;
        }

        outLog += "[4/4] 픽셀 대조 통과 — DX12가 그린 " + std::to_string(kSize * kSize)
            + "픽셀이 DX11에 그대로 보인다\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
        resources.Shutdown();
        return false;
    }

    resources.Shutdown();
    outLog += "공유 텍스처 출력 경로 검증 통과\n";
    return true;
}

bool EnhancedSceneRenderer::RunRenderGraphTest(std::string& outLog)
{
    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(64, 64, error))
    {
        outLog += "[1/5] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    bool passed = true;

    // ── [1/5] 실행 순서 = 선언 순서 ──
    //
    // 그래프가 패스를 재정렬하지 않는 것이 계약이다. 재정렬하면 프레임이 실행마다
    // 달라질 수 있고, 그러면 픽셀 대조(3-6의 정확성 검증 수단)가 흔들린다.
    // 컬링된 것만 빠져야 한다.
    {
        EnhancedRenderGraph graph;

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64;
        desc.allowRenderTarget = true;
        desc.name = "gbuffer";
        const RGHandle gbuffer = graph.CreateTexture(desc);

        desc.name = "lit";
        const RGHandle lit = graph.CreateTexture(desc);

        const RGPassId producer = graph.AddPass("gbuffer",
            { { gbuffer, RGResourceState::RenderTarget } }, nullptr);
        const RGPassId consumer = graph.AddPass("lighting",
            { { gbuffer, RGResourceState::ShaderResource }, { lit, RGResourceState::RenderTarget } },
            nullptr, true);

        if (!graph.Compile(resources.GetDevice(), error))
        {
            outLog += "[1/5] Compile 실패: " + error + "\n";
            return false;
        }

        const auto& order = graph.GetExecuteOrder();
        const bool correct = (2 == order.size())
            && (order[0] == producer.index) && (order[1] == consumer.index);
        if (!correct) { passed = false; }

        outLog += "[1/5] 실행 순서 = 선언 순서 " + std::string(correct ? "통과" : "실패")
            + " (실행 " + std::to_string(order.size()) + "개)\n";
    }

    // ── [2/5] 선언 순서와 데이터 흐름의 불일치 검출 ──
    //
    // 그래프가 만든 리소스를 아무도 쓰기 전에 읽으면 초기화되지 않은 메모리를
    // 읽는 것이다. 증상은 검은 화면이 아니라 '이전 프레임 내용이 보인다'라서
    // 알아채기 어렵다 — 컴파일에서 잡아야 한다.
    //
    // 임포트한 리소스는 검사하지 않는다는 것도 함께 확인한다. 지난 프레임 결과를
    // 읽는 것(히스토리 버퍼)이 정상 사용이라 막으면 안 된다.
    {
        EnhancedRenderGraph graph;

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64; desc.allowRenderTarget = true;
        desc.name = "transient";
        const RGHandle transient = graph.CreateTexture(desc);

        // 쓰기 없이 읽기만 하는 패스 — 잡혀야 한다.
        graph.AddPass("readsUnwritten",
            { { transient, RGResourceState::ShaderResource } }, nullptr, true);

        std::string flowError;
        const bool detected = !graph.Compile(resources.GetDevice(), flowError);

        // 임포트 리소스를 먼저 읽는 것은 정상이어야 한다.
        EnhancedRenderGraph importedGraph;
        const RGHandle imported = importedGraph.ImportTexture(resources.GetRenderTarget(),
            RGResourceState::RenderTarget, "external");
        importedGraph.AddPass("readsImported",
            { { imported, RGResourceState::ShaderResource } }, nullptr, true);

        std::string importedError;
        const bool importedOk = importedGraph.Compile(resources.GetDevice(), importedError);

        const bool correct = detected && importedOk;
        if (!correct) { passed = false; }

        outLog += "[2/5] 흐름 불일치 검출 " + std::string(correct ? "통과" : "실패")
            + (detected ? (" (" + flowError + ")") : " (transient 미검출)")
            + (importedOk ? " · 임포트는 허용" : " · 임포트를 잘못 막음") + "\n";
    }

    // ── [3/5] 배리어 유도 ──
    //
    // 그래프를 두는 이유의 절반이다. 상태가 바뀌는 곳에만 정확히 하나씩 나와야
    // 하고, 같은 상태로 이어지는 곳에는 나오면 안 된다(불필요한 배리어는
    // 파이프라인을 끊는다).
    {
        EnhancedRenderGraph graph;

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64; desc.allowRenderTarget = true;
        desc.name = "color";
        const RGHandle color = graph.CreateTexture(desc);

        // COMMON → RENDER_TARGET (전이 1)
        const RGPassId draw = graph.AddPass("draw",
            { { color, RGResourceState::RenderTarget } }, nullptr);
        // RENDER_TARGET 유지 (전이 0이어야 한다)
        const RGPassId drawMore = graph.AddPass("draw2",
            { { color, RGResourceState::RenderTarget } }, nullptr);
        // RENDER_TARGET → PIXEL_SHADER_RESOURCE (전이 1)
        const RGPassId read = graph.AddPass("post",
            { { color, RGResourceState::ShaderResource } }, nullptr, true);

        if (!graph.Compile(resources.GetDevice(), error))
        {
            outLog += "[3/5] Compile 실패: " + error + "\n";
            return false;
        }

        const uint32_t drawBarriers = graph.GetPassBarrierCount(draw);
        const uint32_t keepBarriers = graph.GetPassBarrierCount(drawMore);
        const uint32_t readBarriers = graph.GetPassBarrierCount(read);

        const bool correct = (1 == drawBarriers) && (0 == keepBarriers) && (1 == readBarriers);
        if (!correct) { passed = false; }

        outLog += "[3/5] 배리어 유도 " + std::string(correct ? "통과" : "실패")
            + " (전이 " + std::to_string(drawBarriers)
            + " · 유지 " + std::to_string(keepBarriers)
            + " · 전이 " + std::to_string(readBarriers) + ")\n";
    }

    // ── [4/5] 미사용 패스 컬링 ──
    //
    // 결과에 기여하지 않는 패스는 걷어낸다. 그 패스만 쓰던 transient도 만들지
    // 않아야 한다 — 만들면 프레임마다 낭비가 반복된다.
    {
        EnhancedRenderGraph graph;

        RGTextureDesc desc{};
        desc.width = 64; desc.height = 64; desc.allowRenderTarget = true;
        desc.name = "used";   const RGHandle used = graph.CreateTexture(desc);
        desc.name = "orphan"; const RGHandle orphan = graph.CreateTexture(desc);

        const RGPassId keep = graph.AddPass("keep",
            { { used, RGResourceState::RenderTarget } }, nullptr, true);
        const RGPassId dead = graph.AddPass("dead",
            { { orphan, RGResourceState::RenderTarget } }, nullptr);

        if (!graph.Compile(resources.GetDevice(), error))
        {
            outLog += "[4/5] Compile 실패: " + error + "\n";
            return false;
        }

        const auto stats = graph.GetStats();
        const bool correct = !graph.IsPassCulled(keep) && graph.IsPassCulled(dead)
            && (1 == stats.passesCulled) && (1 == stats.transientCreated);
        if (!correct) { passed = false; }

        outLog += "[4/5] 미사용 패스 컬링 " + std::string(correct ? "통과" : "실패")
            + " (선언 " + std::to_string(stats.passesDeclared)
            + " · 컬링 " + std::to_string(stats.passesCulled)
            + " · transient 생성 " + std::to_string(stats.transientCreated) + "/2)\n";
    }

    // ── [5/5] 실제 실행 + 픽셀 확인 ──
    //
    // 앞의 넷은 계획이 맞는지만 본다. 그 계획대로 GPU가 돌아 결과가 나오는지는
    // 실행해서 되읽어야 안다 — 배리어가 하나라도 틀리면 검증 레이어가 잡거나
    // 픽셀이 어긋난다.
    {
        EnhancedRenderGraph graph;

        const RGHandle backbuffer = graph.ImportTexture(resources.GetRenderTarget(),
            RGResourceState::RenderTarget, "backbuffer");

        // 클리어만 하는 패스. 임포트 리소스에 쓰므로 뿌리로 잡혀 살아남는다.
        graph.AddPass("clear", { { backbuffer, RGResourceState::RenderTarget } },
            [&resources](const EnhancedRenderGraph::ExecuteContext& context)
            {
                const auto rtv = resources.GetRtvHandle();
                context.commandList->ClearRenderTargetView(rtv,
                    DX12DeviceResources::kClearColor, 0, nullptr);
            });

        // 리드백을 위해 COPY_SOURCE로 전이시키는 패스 — 배리어는 그래프가 만든다.
        graph.AddPass("readback", { { backbuffer, RGResourceState::CopySource } },
            [&resources](const EnhancedRenderGraph::ExecuteContext& context)
            {
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = resources.GetReadbackBuffer();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                dst.PlacedFootprint.Footprint.Width = resources.GetWidth();
                dst.PlacedFootprint.Footprint.Height = resources.GetHeight();
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = resources.GetRowPitch();

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = resources.GetRenderTarget();
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                context.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }, true);

        // 다음 프레임을 위해 RENDER_TARGET으로 되돌린다(임포트 리소스의 상태 계약).
        graph.AddPass("restore", { { backbuffer, RGResourceState::RenderTarget } },
            nullptr, true);

        if (!graph.Compile(resources.GetDevice(), error))
        {
            outLog += "[5/5] Compile 실패: " + error + "\n";
            return false;
        }

        if (!resources.BeginFrame(error)) { outLog += "[5/5] Begin 실패\n"; return false; }
        if (!graph.Execute(resources.GetCommandList(), error))
        {
            outLog += "[5/5] Execute 실패: " + error + "\n";
            return false;
        }
        if (!resources.EndFrame(error)) { outLog += "[5/5] End 실패\n"; return false; }
        resources.WaitForGpu();

        void* mapped = nullptr;
        const size_t readbackBytes = static_cast<size_t>(resources.GetRowPitch()) * resources.GetHeight();
        D3D12_RANGE range{ 0, readbackBytes };
        if (FAILED(resources.GetReadbackBuffer()->Map(0, &range, &mapped)))
        {
            outLog += "[5/5] 리드백 Map 실패\n";
            return false;
        }

        // 클리어 색이 그대로 나와야 한다.
        const auto* pixels = static_cast<const uint8_t*>(mapped);
        const uint8_t expectedR = static_cast<uint8_t>(DX12DeviceResources::kClearColor[0] * 255.f + 0.5f);
        const uint8_t expectedG = static_cast<uint8_t>(DX12DeviceResources::kClearColor[1] * 255.f + 0.5f);
        const uint8_t expectedB = static_cast<uint8_t>(DX12DeviceResources::kClearColor[2] * 255.f + 0.5f);

        uint32_t mismatches = 0;
        for (uint32_t y = 0; y < resources.GetHeight(); ++y)
        {
            const auto* row = pixels + static_cast<size_t>(y) * resources.GetRowPitch();
            for (uint32_t x = 0; x < resources.GetWidth(); ++x)
            {
                const auto* pixel = row + static_cast<size_t>(x) * 4;
                // sRGB 변환 없이 그대로 저장되므로 ±1 오차만 허용한다.
                if (std::abs(pixel[0] - expectedR) > 1 ||
                    std::abs(pixel[1] - expectedG) > 1 ||
                    std::abs(pixel[2] - expectedB) > 1)
                {
                    ++mismatches;
                }
            }
        }
        resources.GetReadbackBuffer()->Unmap(0, nullptr);

        const auto stats = graph.GetStats();
        if (0 != mismatches) { passed = false; }

        outLog += "[5/5] 실행·픽셀 확인 " + std::string(0 == mismatches ? "통과" : "실패")
            + " (불일치 " + std::to_string(mismatches)
            + " · 배리어 " + std::to_string(stats.barriersEmitted)
            + "건을 " + std::to_string(stats.barrierBatches) + "번에 삽입)\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    resources.Shutdown();

    outLog += passed ? "렌더 그래프 검증 통과\n" : "렌더 그래프 검증 실패\n";
    return passed;
}

bool EnhancedSceneRenderer::RunGBufferTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 128;
    constexpr uint32_t kHeight = 128;

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/3] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_gbuffer.cache", error))
    {
        outLog += "[1/3] PSO 매니저 초기화 실패: " + error + "\n";
        return false;
    }

    DX12RootSignatureCache rootSignatures;
    if (!rootSignatures.Initialize(resources.GetDevice(), error))
    {
        outLog += "[1/3] 루트 시그니처 캐시 초기화 실패: " + error + "\n";
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kWidth;
    frameContext.height = kHeight;

    EnhancedGBufferPass gbuffer;
    if (!gbuffer.Initialize(frameContext, error))
    {
        outLog += "[1/3] GBuffer 패스 초기화 실패: " + error + "\n";
        return false;
    }
    outLog += "[1/3] 패스 초기화 완료 — 정점·인덱스 버퍼, MRT 5 PSO, 깊이\n";

    // ── [2/3] 그래프에 선언하고 실행 ──
    EnhancedRenderGraph graph;
    gbuffer.Declare(graph, frameContext);
    const auto outputs = gbuffer.GetOutputs();

    // 리드백을 위해 각 타깃을 COPY_SOURCE로 옮기는 패스를 붙인다.
    // 배리어는 그래프가 만든다 — 여기서 손으로 넣지 않는 것이 요점이다.
    struct ReadbackTarget
    {
        const char*     name;
        RGHandle        handle;
        DXGI_FORMAT     format;
        uint32_t        bytesPerPixel;
        ComPtr<ID3D12Resource> buffer;
        uint32_t        rowPitch;
    };

    std::vector<ReadbackTarget> targets = {
        { "Diffuse",    outputs.diffuse,    DXGI_FORMAT_R16G16B16A16_FLOAT, 8, nullptr, 0 },
        { "MetalRough", outputs.metalRough, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, nullptr, 0 },
        { "Normal",     outputs.normal,     DXGI_FORMAT_R16G16B16A16_FLOAT, 8, nullptr, 0 },
        { "Emissive",   outputs.emissive,   DXGI_FORMAT_R16G16B16A16_FLOAT, 8, nullptr, 0 },
        { "Bitmask",    outputs.bitmask,    DXGI_FORMAT_R32_UINT,           4, nullptr, 0 },
    };

    for (auto& target : targets)
    {
        target.rowPitch = (kWidth * target.bytesPerPixel + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
            & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(target.rowPitch) * kHeight;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&target.buffer))))
        {
            outLog += "[2/3] 리드백 버퍼 생성 실패\n";
            return false;
        }
    }

    std::vector<EnhancedRenderGraph::RGPassUsage> readbackUsages;
    for (const auto& target : targets)
    {
        readbackUsages.push_back({ target.handle, RGResourceState::CopySource });
    }

    graph.AddPass("gbuffer_readback", readbackUsages,
        [&targets](const EnhancedRenderGraph::ExecuteContext& context)
        {
            for (const auto& target : targets)
            {
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = target.buffer.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = target.format;
                dst.PlacedFootprint.Footprint.Width = kWidth;
                dst.PlacedFootprint.Footprint.Height = kHeight;
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = target.rowPitch;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = context.Resolve(target.handle);
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                context.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }
        }, true);

    if (!graph.Compile(resources.GetDevice(), error))
    {
        outLog += "[2/3] 그래프 Compile 실패: " + error + "\n";
        return false;
    }

    if (!resources.BeginFrame(error)) { outLog += "[2/3] Begin 실패\n"; return false; }
    if (!graph.Execute(resources.GetCommandList(), error))
    {
        outLog += "[2/3] 그래프 Execute 실패: " + error + "\n";
        return false;
    }
    if (!resources.EndFrame(error)) { outLog += "[2/3] End 실패\n"; return false; }
    resources.WaitForGpu();

    const auto graphStats = graph.GetStats();
    outLog += "[2/3] 그래프 실행 완료 — 패스 " + std::to_string(graphStats.passesExecuted)
        + " · transient " + std::to_string(graphStats.transientCreated)
        + " · 배리어 " + std::to_string(graphStats.barriersEmitted)
        + "건을 " + std::to_string(graphStats.barrierBatches) + "번에 삽입\n";

    // ── [3/3] 타깃별 픽셀 확인 ──
    //
    // 셰이더가 타깃마다 다른 값을 쓰므로, 각 타깃이 기대값을 갖는지 따로 본다.
    // 하나만 기록되고 나머지가 0으로 남는 경우를 잡는 것이 이 검사의 목적이다.
    const auto halfToFloat = [](uint16_t half) -> float
    {
        const uint32_t sign = (half >> 15) & 0x1;
        const uint32_t exponent = (half >> 10) & 0x1F;
        const uint32_t mantissa = half & 0x3FF;

        uint32_t bits = 0;
        if (0 == exponent)
        {
            bits = sign << 31;   // 0 또는 비정규 — 검증 값 범위에서는 0으로 충분하다
        }
        else if (31 == exponent)
        {
            bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
        }
        else
        {
            bits = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
        }

        float result = 0.f;
        memcpy(&result, &bits, sizeof(result));
        return result;
    };

    // 쿼드가 -0.8~0.8을 덮으므로 화면 중앙은 반드시 그려진 곳이다.
    const uint32_t sampleX = kWidth / 2;
    const uint32_t sampleY = kHeight / 2;

    bool passed = true;
    std::string detail;

    for (size_t i = 0; i < targets.size(); ++i)
    {
        auto& target = targets[i];

        void* mapped = nullptr;
        const size_t bytes = static_cast<size_t>(target.rowPitch) * kHeight;
        D3D12_RANGE range{ 0, bytes };
        if (FAILED(target.buffer->Map(0, &range, &mapped)))
        {
            outLog += "[3/3] " + std::string(target.name) + " Map 실패\n";
            return false;
        }

        const auto* row = static_cast<const uint8_t*>(mapped)
            + static_cast<size_t>(sampleY) * target.rowPitch;

        bool ok = false;
        std::string got;

        if (DXGI_FORMAT_R32_UINT == target.format)
        {
            uint32_t value = 0;
            memcpy(&value, row + static_cast<size_t>(sampleX) * 4, 4);
            ok = (0xABCDu == value);
            got = std::to_string(value);
        }
        else
        {
            uint16_t halves[4]{};
            memcpy(halves, row + static_cast<size_t>(sampleX) * 8, 8);
            const float r = halfToFloat(halves[0]);
            const float g = halfToFloat(halves[1]);
            const float b = halfToFloat(halves[2]);

            char buffer[96]{};
            std::snprintf(buffer, sizeof(buffer), "(%.3f %.3f %.3f)", r, g, b);
            got = buffer;

            constexpr float kEpsilon = 0.01f;
            switch (i)
            {
            case 0: // Diffuse = uv, 중앙이므로 0.5 근처
                ok = std::fabs(r - 0.5f) < 0.05f && std::fabs(g - 0.5f) < 0.05f;
                break;
            case 1: // MetalRough = (0.25, 0.75, 0)
                ok = std::fabs(r - 0.25f) < kEpsilon && std::fabs(g - 0.75f) < kEpsilon;
                break;
            case 2: // Normal = (0,0,-1) 인코딩 → (0.5, 0.5, 0)
                ok = std::fabs(r - 0.5f) < kEpsilon && std::fabs(b - 0.f) < kEpsilon;
                break;
            case 3: // Emissive = (0, 0.5, 1)
                ok = std::fabs(g - 0.5f) < kEpsilon && std::fabs(b - 1.f) < kEpsilon;
                break;
            default:
                break;
            }
        }

        target.buffer->Unmap(0, nullptr);

        if (!ok) passed = false;
        detail += std::string("      ") + target.name + " " + (ok ? "통과" : "실패")
            + " " + got + "\n";
    }

    outLog += "[3/3] 타깃별 픽셀 확인 " + std::string(passed ? "통과" : "실패") + "\n" + detail;

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    gbuffer.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "GBuffer 패스 검증 통과\n" : "GBuffer 패스 검증 실패\n";
    return passed;
}

bool EnhancedSceneRenderer::RunSceneBindingTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 256;

    // ── [1/4] 씬에서 카메라 스냅샷과 드로우 목록을 뽑는다 ──
    //
    // 프록시를 그대로 넘기지 않고 필요한 것만 복사한다. 렌더가 게임 자료구조를
    // 직접 읽으면 3-2에서 걷어낸 부류(렌더가 게임 상태를 만짐)가 되살아난다.
    Camera* sceneCamera = nullptr;
    for (auto& camera : CameraManagement->GetCameras())
    {
        if (camera && RenderPassData::VaildCheck(camera.get()))
        {
            sceneCamera = camera.get();
            break;
        }
    }

    if (nullptr == sceneCamera)
    {
        outLog += "[1/4] 활성 카메라가 없다(에디터 실행 중에만 의미 있는 검증)\n";
        return false;
    }

    const RenderPassData* renderData = RenderPassData::GetData(sceneCamera);
    const FrameCameraSnapshot cameraSnapshot = renderData->GetFrameSnapshot();

    // 커맨드 빌드 스레드가 채워 둔 deferred 큐를 그대로 읽는다. 프록시를 넘기지
    // 않고 메시 포인터와 월드 행렬만 복사한다 — 렌더가 게임 자료구조를 들고
    // 다니면 3-2에서 걷어낸 부류가 되살아난다.

    // 큐 하나를 드로우 목록으로 옮긴다. deferred와 forward가 같은 복사
    // 규칙을 쓰므로 함수로 뽑았다 — 두 곳에 같은 코드를 두면 한쪽만 고치고
    // 다른 쪽을 잊는 부류의 버그가 생긴다.
    const auto copyQueue = [&](const auto& queue, std::vector<EnhancedDrawItem>& out)
    {
        for (auto* proxy : queue)
        {
            if (nullptr == proxy || nullptr == proxy->m_Mesh) continue;

            EnhancedDrawItem item{};
            item.mesh = proxy->m_Mesh.get();
            item.worldMatrix = proxy->m_worldMatrix;

            // 재질도 Material* 자체가 아니라 필요한 것만 복사한다.
            if (auto* material = proxy->m_Material.get())
            {
                item.baseColor = material->m_pBaseColor;
                item.normalMap = material->m_pNormal;
                item.occRoughMetal = material->m_pOccRoughMetal;
                item.emissive = material->m_pEmissive;

                item.baseColorFactor = material->m_materialInfo.m_baseColor;
                item.metallic = material->m_materialInfo.m_metallic;
                item.roughness = material->m_materialInfo.m_roughness;
                item.useNormalMap =
                    (0 != material->m_materialInfo.m_useNormalMap) ? 1u : 0u;
            }

            out.push_back(item);
        }
    };

    std::vector<EnhancedDrawItem> draws;
    std::vector<EnhancedDrawItem> forwardDraws;
    copyQueue(renderData->m_deferredQueue, draws);
    copyQueue(renderData->m_forwardQueue, forwardDraws);

    // ★ deferred 큐만 센다.
    //
    // 이 수는 GBuffer의 텍스처 업로드 단정이 쓰는 값이다. 두 큐를 합쳐 세면
    // 씬이 전부 forward로 갔을 때 "baseColor는 4건인데 업로드가 0" 이라는
    // 오진이 나온다 — GBuffer는 그릴 것이 없었을 뿐이다. 실제로 그렇게 찍혔고,
    // 그건 두 큐 복사를 한 함수로 묶으면서 카운터까지 딸려 들어간 탓이었다.
    uint32_t materialsWithTexture = 0;
    for (const EnhancedDrawItem& item : draws)
    {
        if (nullptr != item.baseColor) ++materialsWithTexture;
    }

    // 광원도 씬에서 뽑아 셰이더가 쓰는 형태로 복사한다. 엔진의 Light는 감쇠
    // 계수와 그림자 행렬까지 들고 있어 그대로 상수 버퍼에 올리기엔 크다.
    std::vector<EnhancedLight> lights;
    if (auto* renderScene = RenderPassData::GetActiveRenderScene())
    {
        auto* lightController = renderScene->m_LightController;
        if (nullptr != lightController)
        {
            for (uint32 i = 0; i < lightController->m_lightCount; ++i)
            {
                const Light& source = lightController->GetLight(i);

                EnhancedLight light{};
                light.position = source.m_position;
                light.position.w = static_cast<float>(source.m_lightType);
                light.direction = source.m_direction;
                light.direction.w = XMConvertToRadians(source.m_spotLightAngle);
                light.color = source.m_color;
                light.color.w = source.m_intencity;
                light.attenuation = Mathf::Vector4{
                    source.m_constantAttenuation, source.m_linearAttenuation,
                    source.m_quadraticAttenuation, source.m_range };

                lights.push_back(light);
            }
        }
    }

    outLog += "[1/4] 씬 입력 확보 — 카메라 " + std::to_string(sceneCamera->m_cameraIndex)
        + " · 드로우 후보 " + std::to_string(draws.size())
        + " · 포워드 " + std::to_string(forwardDraws.size())
        + " · 광원 " + std::to_string(lights.size()) + "\n";

    // ── [2/4] DX12 쪽 준비 ──
    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[2/4] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_scene.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, DirectX11::DeviceStates->g_pDevice,
            DirectX11::DeviceStates->g_pDeviceContext, error))
    {
        outLog += "[2/4] 보조 시스템 초기화 실패: " + error + "\n";
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.textureCache = &textureCache;
    frameContext.width = kWidth;
    frameContext.height = kHeight;
    frameContext.camera = &cameraSnapshot;
    frameContext.draws = &draws;
    frameContext.forwardDraws = &forwardDraws;
    frameContext.lights = &lights;

    EnhancedGBufferPass gbuffer;
    if (!gbuffer.Initialize(frameContext, error))
    {
        outLog += "[2/4] GBuffer 초기화 실패: " + error + "\n";
        return false;
    }

    // Deferred가 GBuffer를 읽으므로 뿌리 표시를 뗀다 — 그래도 살아남아야 하고,
    // 그것이 3-5 컬링이 실전에서 동작한다는 확인이다.
    gbuffer.SetKeepAlive(false);

    EnhancedShadowPass shadow;
    if (!shadow.Initialize(frameContext, error))
    {
        outLog += "[2/4] 그림자 초기화 실패: " + error + "\n";
        return false;
    }

    EnhancedDeferredPass deferred;
    EnhancedSSGIPass     ssgi;
    if (!deferred.Initialize(frameContext, error))
    {
        outLog += "[2/4] Deferred 초기화 실패: " + error + "\n";
        return false;
    }

    if (!ssgi.Initialize(frameContext, error))
    {
        outLog += "[2/4] SSGI 초기화 실패: " + error + "\n";
        return false;
    }

    EnhancedForwardPass forward;
    if (!forward.Initialize(frameContext, error))
    {
        outLog += "[2/4] Forward+ 초기화 실패: " + error + "\n";
        return false;
    }

    DX12GpuProfiler profiler;
    // SSGI가 붙어 패스가 스무 개를 넘는다(Hi-Z 밉 여덟 + 트레이스·리졸브·
    // 필터·합성·히스토리). 질의는 패스당 둘이라 넉넉히 잡는다 — 모자라면
    // 뒤쪽 패스의 시간이 조용히 0으로 나온다.
    if (!profiler.Initialize(resources.GetDevice(), resources.GetCommandQueue(),
        64, DX12DeviceResources::kFrameCount, error))
    {
        outLog += "[2/4] GPU 프로파일러 초기화 실패: " + error + "\n";
        return false;
    }

    // 깊이를 리드백할 버퍼. 커버리지(그려진 픽셀 수)를 세는 데 쓴다.
    const uint32_t depthRowPitch = (kWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    ComPtr<ID3D12Resource> depthReadback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(depthRowPitch) * kHeight;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&depthReadback))))
        {
            outLog += "[2/4] 깊이 리드백 버퍼 생성 실패\n";
            return false;
        }
    }

    // 한 번 그리고 커버리지를 센다. 카메라를 바꿔 두 번 부른다.
    // 라이팅 결과 리드백. 16비트 float 4채널이라 픽셀당 8바이트.
    const uint32_t lightingRowPitch = (kWidth * 8 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    ComPtr<ID3D12Resource> lightingReadback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(lightingRowPitch) * kHeight;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&lightingReadback))))
        {
            outLog += "[2/4] 라이팅 리드백 버퍼 생성 실패\n";
            return false;
        }
    }

    // 그림자 맵 리드백. 깊이 전용 렌더가 실제로 무언가를 기록했는지 세는 데 쓴다.
    const uint32_t shadowRowPitch =
        (EnhancedShadowPass::kShadowMapSize * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    ComPtr<ID3D12Resource> shadowReadback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(shadowRowPitch) * EnhancedShadowPass::kShadowMapSize
            * EnhancedShadowPass::kCascadeCount;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&shadowReadback))))
        {
            outLog += "[2/4] 그림자 리드백 버퍼 생성 실패\n";
            return false;
        }
    }

    std::vector<DX12GpuProfiler::PassTiming> timings;
    EnhancedRenderGraph::Stats lastGraphStats{};

    // 실제 씬 패스를 병렬 경로에 태우기 위한 것. 람다가 참조로 잡는다.
    DX12CommandListPool commandPool;
    // 워커를 패스 수 이상으로 둔다.
    //
    // 4개로 뒀을 때 연속 블록 배분이 Shadow와 GBuffer를 한 워커에 몰았다
    // (패스 6 · 워커 4 → 0,0,1,2,2,3). 그 둘이 이 그래프에서 가장 무거운
    // 패스라 병렬화 효과가 거의 사라졌다. 워커가 패스 수 이상이면 1:1이 된다.
    if (!commandPool.Initialize(resources.GetDevice(), 8,
        DX12DeviceResources::kFrameCount, error))
    {
        outLog += "[2/4] 커맨드 리스트 풀 초기화 실패: " + error + "\n";
        return false;
    }

    bool     useParallelRecording = false;
    uint32_t parallelWorkers = 1;
    double   lastRecordMilliseconds = 0.0;

    // 교차점을 재는 동안에는 되무름을 끈다(0). 켜 두면 임계값 아래 규모에서
    // 병렬 시간이 순차와 같아져, 정작 재려던 '얼마나 지는가'가 가려진다.
    // 되무름이 실제로 도는지는 측정이 끝난 뒤 기본값으로 따로 확인한다.
    uint32_t parallelCostThreshold = 0;

    // 규모 측정에서 재질을 가르지 않은 경우와 가른 경우의 배치 수.
    // 둘이 같으면 재질 키가 죽어 두 모드가 같은 씬이 된 것이다.
    uint32_t uniformBatchCount = 0;
    uint32_t variedBatchCount = 0;

    // 되무름 확인 결과.
    bool     declineObserved = false;
    uint32_t declineCost = 0;
    uint32_t declineWorkers = 0;
    uint32_t declineCoverage = 0;

    // 렌더마다 바꿔 가며 넣는 그림자 설정. 람다가 참조로 잡는다.
    constexpr float kTestShadowBias = 0.0015f;

    bool     shadowEnabled = false;
    float    shadowBias = kTestShadowBias;
    bool     captureShadowMap = false;

    // 켜면 lighting_readback이 라이팅 대신 SSGI 합성 결과를 복사한다.
    // 같은 크기·포맷이라 버퍼를 하나 더 만들 이유가 없다.
    bool     captureSSGIOutput = false;
    uint32_t shadowOccluders = 0;
    std::array<uint32_t, EnhancedShadowPass::kCascadeCount> cascadeOccluders{};

    const auto renderAndCount = [&](const FrameCameraSnapshot& camera,
        uint32_t& outCovered, uint32_t& outDrawCount, std::string& outStepError,
        std::vector<DX12GpuProfiler::PassTiming>& outTimings,
        EnhancedRenderGraph::Stats& outGraphStats,
        double& outAverageLuminance) -> bool
    {
        frameContext.camera = &camera;

        if (!resources.BeginFrame(outStepError)) return false;
        commandPool.BeginFrame(0);
        profiler.BeginFrame(0);

        // 업로드는 그래프 밖에서 — Declare는 선언만, Record는 리소스를 만들지 않는다.
        shadow.SetBias(shadowBias);
        if (!shadow.PrepareFrame(frameContext, outStepError)) return false;
        if (!gbuffer.PrepareFrame(frameContext, outStepError)) return false;
        if (!deferred.PrepareFrame(frameContext, outStepError)) return false;
        if (!ssgi.PrepareFrame(frameContext, outStepError)) return false;
        if (!forward.PrepareFrame(frameContext, outStepError)) return false;
        outDrawCount = gbuffer.GetLastDrawCount();

        EnhancedRenderGraph graph;
        graph.SetProfiler(&profiler);
        graph.SetParallelRecordCostThreshold(parallelCostThreshold);

        // 그림자를 먼저 선언한다. 선언 순서가 실행 순서라, Deferred가 읽기 전에
        // 써 두는 것이 선언으로 표현된다 — 뒤집으면 컴파일이 잡아 준다.
        shadow.Declare(graph, frameContext);

        gbuffer.Declare(graph, frameContext);
        const auto outputs = gbuffer.GetOutputs();

        deferred.SetInputs(outputs);

        EnhancedShadowData shadowData = shadow.GetShadowData();
        shadowData.enabled = shadowData.enabled && shadowEnabled;
        deferred.SetShadow(shadow.GetShadowMap(), shadowData);

        deferred.Declare(graph, frameContext);

        // ── SSGI ──
        //
        // Deferred 뒤에 온다. 간접광의 광원이 직접광 결과이기 때문이다 —
        // 순서가 뒤집히면 아직 아무도 안 쓴 것을 읽게 되고, 그래프가
        // 컴파일에서 잡는다("선언 순서가 데이터 흐름과 어긋난다").
        {
            EnhancedSSGIPass::Inputs ssgiInputs{};
            ssgiInputs.depth = outputs.depth;
            ssgiInputs.normal = outputs.normal;
            ssgiInputs.diffuse = outputs.diffuse;
            ssgiInputs.metalRough = outputs.metalRough;
            ssgiInputs.lighting = deferred.GetOutput();
            ssgi.SetInputs(ssgiInputs);
        }
        ssgi.Declare(graph, frameContext);

        // ── Forward+ ──
        //
        // 불투명 셰이딩이 끝난 뒤에 온다. 포워드 물체는 GBuffer에 기록되지
        // 않고 자기 색을 직접 계산하므로 deferred 결과 위에 얹히는 것이 맞고,
        // 깊이는 GBuffer가 채운 것을 그대로 써서 불투명 기하에 가려진다.
        //
        // 컬링은 그 깊이에서 타일 min/max를 뽑는다 — GBuffer가 이미 채웠으므로
        // 공짜다. 이것이 Forward+를 deferred 뒤에 두는 이유이기도 하다.
        {
            EnhancedForwardPass::Inputs forwardInputs{};
            forwardInputs.depth = outputs.depth;
            forwardInputs.lighting = ssgi.GetOutput().IsValid()
                ? ssgi.GetOutput() : deferred.GetOutput();
            forward.SetInputs(forwardInputs);
        }
        forward.Declare(graph, frameContext);

        // Deferred 출력도 되읽는다. 광원이 실제로 셰이더에 닿는지는 결과를 봐야
        // 안다 — 재질 때 "업로드 0인데 통과"를 겪었으므로 같은 함정을 막는다.
        // SSGI 출력도 사용에 넣는다. 플래그에 따라 어느 쪽을 복사할지가
        // 기록 시점에 갈리는데, 배리어는 선언으로 정해지므로 둘 다 선언해야
        // 어느 쪽을 읽어도 상태가 맞다.
        std::vector<EnhancedRenderGraph::RGPassUsage> readbackUsages{
            { deferred.GetOutput(), RGResourceState::CopySource } };
        if (ssgi.GetOutput().IsValid())
        {
            readbackUsages.push_back({ ssgi.GetOutput(), RGResourceState::CopySource });
        }

        graph.AddPass("lighting_readback", readbackUsages,
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = lightingReadback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = EnhancedDeferredPass::kOutputFormat;
                dst.PlacedFootprint.Footprint.Width = kWidth;
                dst.PlacedFootprint.Footprint.Height = kHeight;
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = lightingRowPitch;

                const bool useSSGI = captureSSGIOutput && ssgi.GetOutput().IsValid();

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = executeContext.Resolve(
                    useSSGI ? ssgi.GetOutput() : deferred.GetOutput());
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }, true);

        graph.AddPass("depth_readback", { { outputs.depth, RGResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = depthReadback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
                dst.PlacedFootprint.Footprint.Width = kWidth;
                dst.PlacedFootprint.Footprint.Height = kHeight;
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = depthRowPitch;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = executeContext.Resolve(outputs.depth);
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }, true);

        // 그림자 맵도 한 번은 되읽는다. 깊이 전용 렌더가 정말로 기록했는지는
        // 결과를 봐야 알고, 안 그러면 '맵은 비었는데 통과'가 가능하다.
        if (captureShadowMap)
        {
            graph.AddPass("shadow_readback",
                { { shadow.GetShadowMap(), RGResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = shadowReadback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
                    dst.PlacedFootprint.Footprint.Width = EnhancedShadowPass::kShadowMapSize;
                    dst.PlacedFootprint.Footprint.Height = EnhancedShadowPass::kShadowMapSize;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch = shadowRowPitch;

                    // 캐스케이드마다 서브리소스가 하나씩이다. 배열을 한 번에
                    // 옮기는 복사는 없으므로 슬라이스별로 부른다.
                    const uint64_t sliceBytes = static_cast<uint64_t>(shadowRowPitch)
                        * EnhancedShadowPass::kShadowMapSize;
                    for (uint32_t slice = 0; slice < EnhancedShadowPass::kCascadeCount; ++slice)
                    {
                        dst.PlacedFootprint.Offset = sliceBytes * slice;

                        D3D12_TEXTURE_COPY_LOCATION src{};
                        src.pResource = executeContext.Resolve(shadow.GetShadowMap());
                        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                        src.SubresourceIndex = slice;

                        executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                    }
                }, true);
        }

        if (!graph.Compile(resources.GetDevice(), outStepError)) return false;

        // 컬링이 GBuffer를 살렸는지 확인한다. 소비자(Deferred)가 읽는데도
        // 걷어냈다면 화면이 비고, 원인이 컬링이라는 것을 알아채기 어렵다.
        outGraphStats = graph.GetStats();

        // ── 기록 시간 측정 ──
        //
        // 병렬화가 줄이는 것은 CPU 기록 시간이다. GPU 시간은 같은 커맨드를
        // 같은 순서로 실행하므로 줄지 않는다 — 그쪽을 근거로 삼으면 '병렬화가
        // 효과 없다'는 잘못된 결론이 나온다.
        const auto recordBegin = std::chrono::steady_clock::now();

        if (useParallelRecording)
        {
            // 업로드(PrepareFrame)가 워커 리스트보다 먼저 실행되어야 한다.
            if (!resources.FlushCommandList(outStepError)) return false;

            if (!graph.ExecuteParallel(commandPool, resources.GetCommandQueue(),
                parallelWorkers, outStepError))
            {
                return false;
            }
            outGraphStats = graph.GetStats();
        }
        else
        {
            if (!graph.Execute(resources.GetCommandList(), outStepError)) return false;
        }

        lastRecordMilliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - recordBegin).count();

        profiler.ResolveFrame(resources.GetCommandList());
        if (!resources.EndFrame(outStepError)) return false;
        resources.WaitForGpu();

        if (!profiler.Collect(outTimings, outStepError)) return false;

        void* mapped = nullptr;
        const size_t bytes = static_cast<size_t>(depthRowPitch) * kHeight;
        D3D12_RANGE range{ 0, bytes };
        if (FAILED(depthReadback->Map(0, &range, &mapped)))
        {
            outStepError = "깊이 리드백 Map 실패";
            return false;
        }

        // 깊이 1.0은 아무것도 안 그려진 곳이다(클리어 값).
        outCovered = 0;
        const auto* pixels = static_cast<const uint8_t*>(mapped);
        for (uint32_t y = 0; y < kHeight; ++y)
        {
            const auto* row = pixels + static_cast<size_t>(y) * depthRowPitch;
            for (uint32_t x = 0; x < kWidth; ++x)
            {
                float depth = 0.f;
                memcpy(&depth, row + static_cast<size_t>(x) * 4, 4);
                if (depth < 0.999f) ++outCovered;
            }
        }
        depthReadback->Unmap(0, nullptr);

        // 그림자 맵에 기록된 텍셀 수. 1.0은 클리어 값 그대로라 '가리는 것 없음'이다.
        if (captureShadowMap)
        {
            shadowOccluders = 0;
            cascadeOccluders.fill(0);
            void* shadowMapped = nullptr;
            const size_t shadowBytes = static_cast<size_t>(shadowRowPitch)
                * EnhancedShadowPass::kShadowMapSize * EnhancedShadowPass::kCascadeCount;
            D3D12_RANGE shadowRange{ 0, shadowBytes };
            if (SUCCEEDED(shadowReadback->Map(0, &shadowRange, &shadowMapped)))
            {
                const auto* shadowPixels = static_cast<const uint8_t*>(shadowMapped);
                const size_t rows = static_cast<size_t>(EnhancedShadowPass::kShadowMapSize)
                    * EnhancedShadowPass::kCascadeCount;

                // 캐스케이드별로 따로 센다. 합계만 보면 한 장만 채워져도
                // 통과하는데, 그건 캐스케이드가 도는 것이 아니다.
                for (size_t y = 0; y < rows; ++y)
                {
                    const auto* row = shadowPixels + y * shadowRowPitch;
                    const uint32_t cascade = static_cast<uint32_t>(
                        y / EnhancedShadowPass::kShadowMapSize);

                    for (uint32_t x = 0; x < EnhancedShadowPass::kShadowMapSize; ++x)
                    {
                        float depth = 0.f;
                        memcpy(&depth, row + static_cast<size_t>(x) * 4, 4);
                        if (depth < 0.999f)
                        {
                            ++shadowOccluders;
                            ++cascadeOccluders[cascade];
                        }
                    }
                }
                shadowReadback->Unmap(0, nullptr);
            }
        }

        // 라이팅 결과의 평균 밝기. 광원이 닿지 않으면 0에 가깝다.
        outAverageLuminance = 0.0;
        void* litMapped = nullptr;
        const size_t litBytes = static_cast<size_t>(lightingRowPitch) * kHeight;
        D3D12_RANGE litRange{ 0, litBytes };
        if (SUCCEEDED(lightingReadback->Map(0, &litRange, &litMapped)))
        {
            const auto halfToFloat = [](uint16_t half) -> float
            {
                const uint32_t sign = (half >> 15) & 0x1;
                const uint32_t exponent = (half >> 10) & 0x1F;
                const uint32_t mantissa = half & 0x3FF;
                uint32_t bits = 0;
                if (0 == exponent) bits = sign << 31;
                else if (31 == exponent) bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
                else bits = (sign << 31) | ((exponent + 112) << 23) | (mantissa << 13);
                float result = 0.f;
                memcpy(&result, &bits, sizeof(result));
                return result;
            };

            const auto* litPixels = static_cast<const uint8_t*>(litMapped);
            double sum = 0.0;
            for (uint32_t y = 0; y < kHeight; ++y)
            {
                const auto* row = litPixels + static_cast<size_t>(y) * lightingRowPitch;
                for (uint32_t x = 0; x < kWidth; ++x)
                {
                    uint16_t halves[4]{};
                    memcpy(halves, row + static_cast<size_t>(x) * 8, 8);
                    sum += (halfToFloat(halves[0]) + halfToFloat(halves[1])
                        + halfToFloat(halves[2])) / 3.0;
                }
            }
            outAverageLuminance = sum / (kWidth * kHeight);
            lightingReadback->Unmap(0, nullptr);
        }

        return true;
    };

    // ── [3/4] 씬 카메라로 렌더 ──
    double luminanceLit = 0.0;
    double luminanceMoved = 0.0;
    uint32_t coveredA = 0;
    uint32_t drawCountA = 0;

    // 첫 렌더가 기준선이다 — 그림자를 켜고, 그림자 맵도 한 번 되읽는다.
    shadowEnabled = true;
    captureShadowMap = true;
    if (!renderAndCount(cameraSnapshot, coveredA, drawCountA, error, timings, lastGraphStats, luminanceLit))
    {
        outLog += "[3/4] 렌더 실패: " + error + "\n";
        return false;
    }

    captureShadowMap = false;

    // ── SSGI 전후 그림을 남긴다 ──
    //
    // 스윕 숫자로는 두께·거리의 옳은 값을 정할 수 없었다(합성 씬은 깊이와
    // 노멀이 서로 모순이라 총 히트 비율이 아무것에도 반응하지 않는다).
    // 최종 판정은 결국 실제 씬의 그림이다. 라이팅만(끔)과 SSGI 합성(켬)을
    // 나란히 저장해 눈으로 비교할 수 있게 한다.
    {
        const auto savePng = [&](const char* path) -> bool
        {
            void* mapped = nullptr;
            const size_t bytes = static_cast<size_t>(lightingRowPitch) * kHeight;
            D3D12_RANGE range{ 0, bytes };
            if (FAILED(lightingReadback->Map(0, &range, &mapped))) return false;

            // R16G16B16A16_FLOAT → R8G8B8A8. 노출을 낮춰 담는다.
            //
            // ★ 처음에는 감마만 입혔더니 끔/켬 PNG가 바이트까지 동일했다.
            //   라이팅이 대부분 1.0을 넘어 클램프됐고, 포화된 픽셀에는 GI를
            //   더해도 같은 흰색이 된다 — 차이를 재려는 그림이 차이를 가리고
            //   있었다. 0.35배로 낮춰 밝은 영역에도 여유를 남긴다.
            constexpr float kExposure = 0.35f;
            const auto halfToF = [](uint16_t half) -> float
            {
                const uint32_t exponent = (half >> 10) & 0x1Fu;
                const uint32_t mantissa = half & 0x3FFu;
                if (0 == exponent) return 0.f;
                const uint32_t bits = ((exponent + 112u) << 23) | (mantissa << 13);
                float value = 0.f;
                memcpy(&value, &bits, sizeof(value));
                return value;
            };

            std::vector<uint8_t> rgba(static_cast<size_t>(kWidth) * kHeight * 4);
            const auto* source = static_cast<const uint8_t*>(mapped);

            for (uint32_t y = 0; y < kHeight; ++y)
            {
                const auto* row = reinterpret_cast<const uint16_t*>(source
                    + static_cast<size_t>(y) * lightingRowPitch);

                for (uint32_t x = 0; x < kWidth; ++x)
                {
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        const float linear = (std::min)(1.f,
                            (std::max)(0.f, halfToF(row[x * 4 + channel]) * kExposure));
                        rgba[(static_cast<size_t>(y) * kWidth + x) * 4 + channel] =
                            static_cast<uint8_t>(powf(linear, 1.f / 2.2f) * 255.f + 0.5f);
                    }
                    rgba[(static_cast<size_t>(y) * kWidth + x) * 4 + 3] = 255;
                }
            }

            lightingReadback->Unmap(0, nullptr);

            DirectX::Image image{};
            image.width = kWidth;
            image.height = kHeight;
            image.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            image.rowPitch = static_cast<size_t>(kWidth) * 4;
            image.slicePitch = image.rowPitch * kHeight;
            image.pixels = rgba.data();

            const std::string narrow(path);
            const std::wstring wide(narrow.begin(), narrow.end());
            return SUCCEEDED(DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
                DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), wide.c_str()));
        };

        // 기준선 렌더의 리드백이 아직 라이팅(끔)을 들고 있다.
        const bool savedOff = savePng("dx12_ssgi_off.png");

        // SSGI 합성을 상수 변형별로 저장한다. intensity와 추적 거리의 옳은
        // 값은 숫자로 정할 수 없었으므로(합성 스윕의 한계) 그림을 나란히
        // 놓고 고른다.
        captureSSGIOutput = true;
        uint32_t coveredTemp = 0;
        uint32_t drawTemp = 0;
        double luminanceTemp = 0.0;
        std::vector<DX12GpuProfiler::PassTiming> timingsTemp;
        EnhancedRenderGraph::Stats statsTemp{};
        uint32_t savedCount = 0;

        struct PictureCase { float intensity; float distance; const char* path; };
        const PictureCase pictureCases[] = {
            { 0.5f, 8.f,  "dx12_ssgi_i05_d8.png" },
            { 1.0f, 8.f,  "dx12_ssgi_on.png" },
            { 2.0f, 8.f,  "dx12_ssgi_i20_d8.png" },
            { 1.0f, 2.f,  "dx12_ssgi_i10_d2.png" },
            { 1.0f, 32.f, "dx12_ssgi_i10_d32.png" },
        };

        const EnhancedSSGIPass::Tuning savedTuning = ssgi.GetTuning();

        for (const PictureCase& pictureCase : pictureCases)
        {
            EnhancedSSGIPass::Tuning tuning = savedTuning;
            tuning.intensity = pictureCase.intensity;
            tuning.traceDistance = pictureCase.distance;
            ssgi.SetTuning(tuning);

            if (renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
                timingsTemp, statsTemp, luminanceTemp)
                && savePng(pictureCase.path))
            {
                ++savedCount;
            }
        }

        ssgi.SetTuning(savedTuning);
        captureSSGIOutput = false;

        outLog += std::string("      SSGI 그림 — 끔 ") + (savedOff ? "저장" : "실패")
            + " · 변형 " + std::to_string(savedCount) + "/5 저장\n";
    }

    // 기준선 렌더의 그림자 상태를 여기서 붙잡는다.
    //
    // 패스가 들고 있는 상태를 나중에 읽으면 그 사이의 렌더가 덮어쓴다. 실제로
    // 뒤에 오는 '광원 0개' 렌더가 방향광을 못 찾아 m_hasDirectionalLight를
    // false로 만들었고, 그 값으로 판정하는 바람에 그림자 단정이 통째로
    // 건너뛰어지고도 통과가 나왔다 — 재질 때와 같은 부류의 조용한 통과다.
    const bool     baselineHasDirectional = shadow.HasDirectionalLight();
    const uint32_t baselineShadowCasters = shadow.GetLastDrawCount();
    const uint32_t baselineShadowCulled = shadow.GetLastCulledCount();
    const uint32_t baselineShadowBatches = shadow.GetLastBatchCount();
    const uint32_t baselineShadowOccluders = shadowOccluders;
    const auto     baselineCascadeOccluders = cascadeOccluders;
    const EnhancedShadowData baselineShadowData = shadow.GetShadowData();

    const auto meshStats = meshCache.GetStats();
    const auto textureStats = textureCache.GetStats();
    outLog += "      재질 — 텍스처 업로드 " + std::to_string(textureStats.uploads)
        + "(" + std::to_string(textureStats.bytesUploaded / 1024) + "KB)"
        + " · 히트 " + std::to_string(textureStats.hits)
        + " · 실패 " + std::to_string(textureStats.failures)
        + " · baseColor 있는 드로우 " + std::to_string(materialsWithTexture) + "\n";
    outLog += "      키잉 — 드로우 " + std::to_string(gbuffer.GetLastDrawCount())
        + " · 메시 " + std::to_string(gbuffer.GetLastMeshCount())
        + " · 재질 " + std::to_string(gbuffer.GetLastMaterialCount())
        + " · 배치 " + std::to_string(gbuffer.GetLastBatchCount()) + "\n";
    outLog += "[3/4] 씬 카메라 렌더 — 드로우 " + std::to_string(drawCountA)
        + " · 메시 업로드 " + std::to_string(meshStats.uploads)
        + "(" + std::to_string(meshStats.bytesUploaded / 1024) + "KB)"
        + " · 커버리지 " + std::to_string(coveredA) + "/" + std::to_string(kWidth * kHeight) + "\n";

    // 그래프가 GBuffer를 살렸는지. Deferred가 읽으므로 뿌리 표시 없이 살아남아야 한다.
    // Forward+가 실제 씬에서 무엇을 했는지. 포워드 큐가 비면 셰이딩은
    // 선언되지 않는 것이 정상이라, 그 사실을 수로 남겨야 "안 도는 것"과
    // "그릴 것이 없는 것"이 구분된다.
    outLog += "      Forward+ — 포워드 드로우 " + std::to_string(forwardDraws.size())
        + " · 셰이딩 " + std::string(forward.GetOutput().IsValid()
            ? "선언됨" : "생략(포워드 큐 비어 있음)") + "\n";

    outLog += "      그래프 — 선언 " + std::to_string(lastGraphStats.passesDeclared)
        + " · 컬링 " + std::to_string(lastGraphStats.passesCulled)
        + " · 실행 " + std::to_string(lastGraphStats.passesExecuted)
        + " · 배리어 " + std::to_string(lastGraphStats.barriersEmitted)
        + "건을 " + std::to_string(lastGraphStats.barrierBatches) + "번에\n";

    // 패스별 GPU 시간. 3-6의 성능 판정이 여기서 시작된다 — DX11 대비 비교는
    // 같은 씬을 양쪽으로 그릴 수 있게 되는 시점(재질 연결 후)에 붙인다.
    double totalMs = 0.0;
    for (const auto& timing : timings)
    {
        char line[128]{};
        std::snprintf(line, sizeof(line), "      GPU %-18s %.4f ms\n",
            timing.name.c_str(), timing.milliseconds);
        outLog += line;
        totalMs += timing.milliseconds;
    }
    {
        char line[96]{};
        std::snprintf(line, sizeof(line), "      GPU %-18s %.4f ms\n", "(합계)", totalMs);
        outLog += line;
    }

    // ── [4/4] 카메라를 옮겨 다시 렌더 ──
    //
    // 이것이 이 검증의 핵심이다. 상수 버퍼가 실제로 셰이더에 닿지 않으면
    // 두 결과가 같다 — '그려지긴 하는데 카메라를 무시한다'를 잡는다.
    FrameCameraSnapshot movedCamera = cameraSnapshot;
    movedCamera.view = XMMatrixMultiply(cameraSnapshot.view,
        XMMatrixTranslation(0.f, 0.f, 500.f));

    uint32_t coveredB = 0;
    uint32_t drawCountB = 0;
    std::vector<DX12GpuProfiler::PassTiming> timingsB;
    EnhancedRenderGraph::Stats statsB{};
    if (!renderAndCount(movedCamera, coveredB, drawCountB, error, timingsB, statsB, luminanceMoved))
    {
        outLog += "[4/4] 이동 카메라 렌더 실패: " + error + "\n";
        return false;
    }

    bool passed = true;
    std::string verdict;

    // ── 광원이 실제로 셰이더에 닿는가 ──
    //
    // 광원을 비운 채로 한 번 더 그려 밝기를 비교한다. 상수가 안 닿으면 두 결과가
    // 같고, 그러면 '광원 목록은 넘겼는데 라이팅은 안 된다'를 못 잡는다.
    // 카메라 상수 때 쓴 것과 같은 논리다.
    double luminanceUnlit = 0.0;
    if (!lights.empty())
    {
        const std::vector<EnhancedLight> noLights;
        frameContext.lights = &noLights;

        uint32_t coveredDark = 0;
        uint32_t drawCountDark = 0;
        std::vector<DX12GpuProfiler::PassTiming> timingsDark;
        EnhancedRenderGraph::Stats statsDark{};
        if (!renderAndCount(cameraSnapshot, coveredDark, drawCountDark, error,
            timingsDark, statsDark, luminanceUnlit))
        {
            outLog += "[4/4] 광원 0 렌더 실패: " + error + "\n";
            return false;
        }
        frameContext.lights = &lights;
    }

    char luminanceLine[160]{};
    std::snprintf(luminanceLine, sizeof(luminanceLine),
        "      라이팅 — 광원 %zu개 밝기 %.5f · 광원 0개 밝기 %.5f\n",
        lights.size(), luminanceLit, luminanceUnlit);
    outLog += luminanceLine;

    // ── 그림자가 실제로 셰이더에 닿는가 ──
    //
    // 그림자를 끈 것과 비교만 하면 씬 배치에 답이 좌우된다 — 가리는 것이 없는
    // 씬에서는 켜나 끄나 같고, 그러면 '그림자 경로가 통째로 죽었다'와 구분되지
    // 않는다. 그래서 편향을 음수로 밀어 모든 표면이 자기 그림자에 걸리는 렌더를
    // 하나 더 한다. 그림자 맵을 읽고 비교 샘플러가 도는 이상 반드시 어두워지므로,
    // 배치와 무관하게 ③④를 판정할 수 있다.
    double luminanceNoShadow = 0.0;
    double luminanceForced = 0.0;
    const bool shadowTestable = baselineHasDirectional && 0 != drawCountA;
    if (shadowTestable)
    {
        uint32_t coveredTemp = 0;
        uint32_t drawTemp = 0;
        std::vector<DX12GpuProfiler::PassTiming> timingsTemp;
        EnhancedRenderGraph::Stats statsTemp{};

        shadowEnabled = false;
        if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
            timingsTemp, statsTemp, luminanceNoShadow))
        {
            outLog += "[4/4] 그림자 끔 렌더 실패: " + error + "\n";
            return false;
        }

        shadowEnabled = true;
        shadowBias = -1.f;
        if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
            timingsTemp, statsTemp, luminanceForced))
        {
            outLog += "[4/4] 그림자 강제 렌더 실패: " + error + "\n";
            return false;
        }
        shadowBias = kTestShadowBias;
    }

    // ── 캐스터 컬링이 실제로 자르는가 ──
    //
    // 기준선에서 컬링 0이 나오는 것은 정상이다(작은 씬에서는 모든 오브젝트가
    // 모든 캐스케이드에 걸린다). 문제는 그 0이 '자를 것이 없었다'와 '판정이 늘
    // 참이다'를 구분하지 못한다는 것이다. 그래서 잘릴 수밖에 없는 것을 하나
    // 넣어 본다 — 광원 방향에 수직으로 멀리 옮긴 드로우는 어느 캐스케이드에도
    // 그림자를 드리울 수 없으므로 셋 다에서 걸러져야 한다.
    uint32_t culledWithFarCaster = 0;
    if (shadowTestable)
    {
        Mathf::Vector3 lightDir{ baselineShadowData.lightDirection.x,
            baselineShadowData.lightDirection.y, baselineShadowData.lightDirection.z };

        const Mathf::Vector3 axis = (std::fabs(lightDir.x) < 0.9f)
            ? Mathf::Vector3{ 1.f, 0.f, 0.f } : Mathf::Vector3{ 0.f, 1.f, 0.f };
        Mathf::Vector3 perpendicular = lightDir.Cross(axis);
        perpendicular.Normalize();

        // 마지막 캐스케이드가 덮는 거리보다 훨씬 멀리. 경계 근처에 두면
        // 판정이 맞는지 애매해진다.
        const Mathf::Vector3 offset = perpendicular * (baselineShadowData.splitDepths.z * 100.f);

        std::vector<EnhancedDrawItem> farDraws = draws;
        EnhancedDrawItem farCaster = draws.front();
        farCaster.worldMatrix = XMMatrixMultiply(farCaster.worldMatrix,
            XMMatrixTranslation(offset.x, offset.y, offset.z));
        farDraws.push_back(farCaster);

        frameContext.draws = &farDraws;

        uint32_t coveredTemp = 0;
        uint32_t drawTemp = 0;
        double luminanceTemp = 0.0;
        std::vector<DX12GpuProfiler::PassTiming> timingsTemp;
        EnhancedRenderGraph::Stats statsTemp{};

        shadowEnabled = true;
        if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
            timingsTemp, statsTemp, luminanceTemp))
        {
            outLog += "[4/4] 캐스터 컬링 렌더 실패: " + error + "\n";
            return false;
        }
        culledWithFarCaster = shadow.GetLastCulledCount();

        frameContext.draws = &draws;
    }

    // ── 드로우별 재질 키잉이 사는가 ──
    //
    // 같은 메시를 재질만 바꿔 두 번 그린다. 전에는 재질을 메시로 키잉해서
    // 두 번째가 첫 번째의 텍스처로 그려졌는데, 실제 씬에서는 메시마다 재질이
    // 달라 그 상태로도 통과했다 — 그래서 일부러 겹치는 경우를 만든다.
    // 씬의 재질에 기대지 않는다. 이 씬의 baseColor가 전부 비어 있으면
    // '다른 재질'을 만들 수 없어 단정이 조용히 통과한다 — 실제로 그랬다.
    // 그래서 검증용 텍스처 둘을 직접 만들어 포인터를 갈라 놓는다.
    uint32_t keyedMaterialCount = 0;
    uint32_t keyedBatchCount = 0;
    if (!draws.empty())
    {
        auto* keyTextureA = Texture::Create(4, 4, "MatKeyA",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
        auto* keyTextureB = Texture::Create(4, 4, "MatKeyB",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);

        if (nullptr != keyTextureA && nullptr != keyTextureB)
        {
            keyTextureA->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);
            keyTextureB->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);

            std::vector<EnhancedDrawItem> sameMeshDraws;

            EnhancedDrawItem first = draws.front();
            first.baseColor = keyTextureA;
            sameMeshDraws.push_back(first);

            EnhancedDrawItem variant = draws.front();   // 메시는 같다
            variant.baseColor = keyTextureB;            // 재질만 다르다
            sameMeshDraws.push_back(variant);

            frameContext.draws = &sameMeshDraws;

            // PrepareFrame을 직접 부르지 않고 렌더를 한 번 돌린다.
            //
            // PrepareFrame은 업로드를 위해 커맨드 리스트에 기록하는데, 프레임
            // 밖에서 부르면 닫힌 리스트를 만진다("This API cannot be called on
            // a closed command list" 4건이 실제로 나왔다). 프레임 경계를 여는
            // 경로가 이미 있으니 그것을 쓴다.
            uint32_t coveredTemp = 0;
            uint32_t drawTemp = 0;
            double luminanceTemp = 0.0;
            std::vector<DX12GpuProfiler::PassTiming> timingsTemp;
            EnhancedRenderGraph::Stats statsTemp{};

            if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
                timingsTemp, statsTemp, luminanceTemp))
            {
                outLog += "[4/4] 재질 키잉 렌더 실패: " + error + "\n";
                return false;
            }
            keyedMaterialCount = gbuffer.GetLastMaterialCount();
            keyedBatchCount = gbuffer.GetLastBatchCount();

            frameContext.draws = &draws;
        }

        Memory::SafeDelete(keyTextureA);
        Memory::SafeDelete(keyTextureB);
    }

    // ── 정반사 항이 사는가 ──
    //
    // 같은 씬을 거칠기 0과 1로 그려 밝기를 비교한다. 확산 감쇠만 있던 예전
    // 셰이더에서도 값은 달라지지만, 그때는 거칠기가 밝기를 '깎기만' 했다.
    // GGX가 들어오면 거친 쪽이 더 어두운 관계가 유지되면서 차이가 커진다.
    double luminanceSmooth = 0.0;
    double luminanceRough = 0.0;
    {
        std::vector<EnhancedDrawItem> smoothDraws = draws;
        std::vector<EnhancedDrawItem> roughDraws = draws;
        for (auto& item : smoothDraws) { item.roughness = 0.05f; item.metallic = 0.f; }
        for (auto& item : roughDraws) { item.roughness = 1.0f; item.metallic = 0.f; }

        uint32_t coveredTemp = 0;
        uint32_t drawTemp = 0;
        std::vector<DX12GpuProfiler::PassTiming> timingsTemp;
        EnhancedRenderGraph::Stats statsTemp{};

        frameContext.draws = &smoothDraws;
        if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
            timingsTemp, statsTemp, luminanceSmooth))
        {
            outLog += "[4/4] 매끈한 재질 렌더 실패: " + error + "\n";
            return false;
        }

        frameContext.draws = &roughDraws;
        if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
            timingsTemp, statsTemp, luminanceRough))
        {
            outLog += "[4/4] 거친 재질 렌더 실패: " + error + "\n";
            return false;
        }

        frameContext.draws = &draws;
    }

    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "      재질 경로 — 같은 메시 두 재질 키 %u개 · 거칠기 0.05 밝기 %.5f"
            " · 거칠기 1.0 밝기 %.5f\n",
            keyedMaterialCount, luminanceSmooth, luminanceRough);
        outLog += line;
    }

    // ── 실제 씬 패스를 병렬 경로에 태운다 ──
    //
    // 여기까지의 병렬 검증은 클리어만 하는 인공 패스였다. 실제 패스는 업로드
    // 링·디스크립터 링·PSO를 모두 쓰므로, 그 조합에서도 결과가 같은지는 따로
    // 봐야 한다.
    //
    // 재는 것은 CPU 기록 시간이다. 병렬화가 줄이는 것이 그것이고, GPU 시간은
    // 같은 커맨드를 같은 순서로 실행하므로 줄지 않는다.
    double sequentialRecordMs = 0.0;
    double parallelRecordMs = 0.0;
    double parallelLuminance = 0.0;
    uint32_t parallelCovered = 0;
    uint32_t parallelWorkersUsed = 0;

    if (0 != drawCountA)
    {
        constexpr uint32_t kMeasureRuns = 20;

        uint32_t coveredTemp = 0;
        uint32_t drawTemp = 0;
        double luminanceTemp = 0.0;
        std::vector<DX12GpuProfiler::PassTiming> timingsTemp;
        EnhancedRenderGraph::Stats statsTemp{};

        // 한 번은 버린다. 첫 실행에는 PSO 조회·힙 준비 같은 일회성 비용이 섞인다.
        useParallelRecording = false;
        if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
            timingsTemp, statsTemp, luminanceTemp))
        {
            outLog += "[4/4] 순차 기록 예열 실패: " + error + "\n";
            return false;
        }

        double sequentialTotal = 0.0;
        for (uint32_t run = 0; run < kMeasureRuns; ++run)
        {
            if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
                timingsTemp, statsTemp, luminanceTemp))
            {
                outLog += "[4/4] 순차 기록 실패: " + error + "\n";
                return false;
            }
            sequentialTotal += lastRecordMilliseconds;
        }
        sequentialRecordMs = sequentialTotal / kMeasureRuns;

        useParallelRecording = true;
        parallelWorkers = 6;
        if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
            timingsTemp, statsTemp, luminanceTemp))
        {
            outLog += "[4/4] 병렬 기록 예열 실패: " + error + "\n";
            return false;
        }

        double parallelTotal = 0.0;
        for (uint32_t run = 0; run < kMeasureRuns; ++run)
        {
            if (!renderAndCount(cameraSnapshot, parallelCovered, drawTemp, error,
                timingsTemp, statsTemp, parallelLuminance))
            {
                outLog += "[4/4] 병렬 기록 실패: " + error + "\n";
                return false;
            }
            parallelTotal += lastRecordMilliseconds;
        }
        parallelRecordMs = parallelTotal / kMeasureRuns;
        parallelWorkersUsed = statsTemp.recordWorkers;

        useParallelRecording = false;
        parallelWorkers = 1;
    }

    {
        char line[288]{};
        std::snprintf(line, sizeof(line),
            "      병렬 기록 — 순차 %.4f ms · 병렬 %.4f ms(워커 %u) · %.2f배"
            " · 커버리지 %u vs %u · 밝기 %.5f vs %.5f\n",
            sequentialRecordMs, parallelRecordMs, parallelWorkersUsed,
            (parallelRecordMs > 0.0) ? (sequentialRecordMs / parallelRecordMs) : 0.0,
            coveredA, parallelCovered, luminanceLit, parallelLuminance);
        outLog += line;
    }

    // ── 규모를 키워 다시 잰다 ──
    //
    // 병렬화가 이기려면 기록 비용이 동기화 비용을 넘어야 한다. 패스 6·드로우
    // 11에서는 기록이 너무 싸서 무엇을 해도 진다. 드로우를 늘려 그 경계가
    // 어디인지 본다 — '병렬화가 값을 하는가'가 아니라 '언제부터 하는가'가
    // 답해야 할 질문이다.
    if (0 != drawCountA)
    {
        // ★ 규모를 크게 잡는 이유는 Release 실측 때문이다.
        //
        // 처음에는 {4, 16, 64}였다. Debug에서는 그 안에 교차점이 있는 것처럼
        // 보였는데(기록량 1419에서 1.37배), Release로 재니 2827에서도 0.72배로
        // 병렬이 졌다. 순차 기록이 Debug 5~6 ms에서 Release 0.36 ms로 15배
        // 빨라지면서 워커를 깨우는 비용이 상대적으로 훨씬 커진 것이다.
        //
        // 즉 Debug에서 본 교차점은 Debug의 것이었다. 실제 경계를 보려면
        // 그보다 한참 큰 규모까지 훑어야 한다.
        constexpr uint32_t kScales[] = { 4, 64, 256, 1024 };
        constexpr uint32_t kMeasureRuns = 10;

        // ── 재질을 갈라 놓을 텍스처 ──
        //
        // 지금까지의 규모 확대는 같은 메시 11종을 복제해서 재질이 전부 같았다.
        // 그러면 인스턴싱이 최대로 먹어 드로우 11264가 배치 11개로 묶이는데,
        // 그것은 병렬 기록에 최악의 조건이다 — 나눌 것이 배치 11개뿐이다.
        //
        // 실제 씬은 재질이 다양해 배치가 수백 개다. 그 조건에서도 병렬이
        // 지는지 봐야 '병렬 기록은 값을 못 한다'를 말할 수 있다. 지금까지는
        // '이 조건에서는 값을 못 했다'까지만 확인한 것이었다.
        //
        // 4x4 텍스처를 여러 개 만들어 복제마다 돌려 쓴다. 픽셀 내용은 상관없다
        // — 재질 키가 포인터라 객체가 다르기만 하면 배치가 갈린다.
        constexpr uint32_t kMaterialVariants = 64;
        std::vector<Texture*> variantTextures;
        variantTextures.reserve(kMaterialVariants);
        for (uint32_t index = 0; index < kMaterialVariants; ++index)
        {
            auto* texture = Texture::Create(4, 4, "ScaleMat" + std::to_string(index),
                DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
            if (nullptr == texture) break;

            texture->CreateSRV(DXGI_FORMAT_R8G8B8A8_UNORM);
            variantTextures.push_back(texture);
        }

        // 재질을 가르지 않은 경우와 가른 경우를 나란히 잰다. 하나만 재면
        // 배치 수가 결과를 얼마나 좌우하는지 알 수 없다.
        struct MaterialMode
        {
            const char* label;
            bool        varyMaterial;
        };
        constexpr MaterialMode kMaterialModes[] = {
            { "재질 1종", false },
            { "재질 다종", true },
        };

        for (const MaterialMode& mode : kMaterialModes)
        {
        if (mode.varyMaterial && variantTextures.empty()) continue;

        for (uint32_t scale : kScales)
        {
            std::vector<EnhancedDrawItem> scaled;
            scaled.reserve(draws.size() * scale);
            for (uint32_t copy = 0; copy < scale; ++copy)
            {
                for (const auto& item : draws)
                {
                    EnhancedDrawItem clone = item;
                    // 화면 밖으로 흩는다. 픽셀 비용이 아니라 기록 비용을 재는
                    // 것이므로, 겹쳐 그려 픽셀을 태우면 무엇을 재는지 흐려진다.
                    clone.worldMatrix = XMMatrixMultiply(item.worldMatrix,
                        XMMatrixTranslation(static_cast<float>(copy) * 12.f, 0.f, 0.f));

                    if (mode.varyMaterial)
                    {
                        clone.baseColor = variantTextures[copy % variantTextures.size()];
                    }

                    scaled.push_back(clone);
                }
            }

            frameContext.draws = &scaled;

            uint32_t coveredTemp = 0;
            uint32_t drawTemp = 0;
            double luminanceTemp = 0.0;
            std::vector<DX12GpuProfiler::PassTiming> timingsTemp;
            EnhancedRenderGraph::Stats statsTemp{};

            const auto measure = [&](bool parallelMode, double& outMilliseconds) -> bool
            {
                useParallelRecording = parallelMode;
                parallelWorkers = parallelMode ? 6u : 1u;

                if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
                    timingsTemp, statsTemp, luminanceTemp))
                {
                    return false;
                }

                // 평균이 아니라 중앙값을 쓴다.
                //
                // 같은 빌드로 세 번 재니 순차 시간이 4.14 · 5.66 · 5.66 ms로
                // ±18% 흔들렸다. OS 스케줄링이나 다른 프로세스가 한 번 끼면
                // 그 실행만 크게 늘어나는데, 평균은 그것을 그대로 받는다.
                // 중앙값은 이상치 하나에 움직이지 않는다.
                std::vector<double> samples;
                samples.reserve(kMeasureRuns);
                for (uint32_t run = 0; run < kMeasureRuns; ++run)
                {
                    if (!renderAndCount(cameraSnapshot, coveredTemp, drawTemp, error,
                        timingsTemp, statsTemp, luminanceTemp))
                    {
                        return false;
                    }
                    samples.push_back(lastRecordMilliseconds);
                }
                std::sort(samples.begin(), samples.end());
                outMilliseconds = samples[samples.size() / 2];
                return true;
            };

            double scaledSequential = 0.0;
            double scaledParallel = 0.0;
            if (!measure(false, scaledSequential) || !measure(true, scaledParallel))
            {
                outLog += "[4/4] 규모 측정 실패: " + error + "\n";
                return false;
            }

            char line[288]{};
            std::snprintf(line, sizeof(line),
                "        [%s] 드로우 %zu(배치 %u) — 순차 %.4f ms · 병렬 %.4f ms · %.2f배"
                " · 기록 단위 %u(워커 %u) · 기록량 %u%s\n",
                mode.label,
                scaled.size(), gbuffer.GetLastBatchCount(), scaledSequential, scaledParallel,
                (scaledParallel > 0.0) ? (scaledSequential / scaledParallel) : 0.0,
                statsTemp.recordUnits, statsTemp.recordWorkers,
                statsTemp.totalRecordCost,
                statsTemp.parallelDeclined ? "(순차로 되무름)" : "");
            outLog += line;

            // 재질을 갈랐는데 배치가 안 갈렸다면 이 측정은 아무것도 재지
            // 않은 것이다 — 두 모드가 같은 씬이 되므로 비교가 성립하지 않는다.
            // 실제로 재질 키가 죽어 있으면 이렇게 된다.
            if (mode.varyMaterial)
            {
                variedBatchCount = gbuffer.GetLastBatchCount();
            }
            else
            {
                uniformBatchCount = gbuffer.GetLastBatchCount();
            }

            // 병렬 경로의 패스별 GPU 시간. 마지막 규모에서만 찍는다 —
            // 이 값이 다음 최적화의 근거가 된다. 무엇이 무거운지 모르면
            // 어디를 쪼갤지 정할 수 없다.
            if (mode.varyMaterial && scale == kScales[std::size(kScales) - 1])
            {
                for (const auto& timing : timingsTemp)
                {
                    char timingLine[160]{};
                    std::snprintf(timingLine, sizeof(timingLine),
                        "          GPU(병렬) %-22s %.4f ms\n",
                        timing.name.c_str(), timing.milliseconds);
                    outLog += timingLine;
                }
            }
        }
        }

        for (auto* texture : variantTextures) Memory::SafeDelete(texture);
        variantTextures.clear();

        frameContext.draws = &draws;

        // ── 되무름이 실제로 도는가 ──
        //
        // 임계값을 기본값으로 되돌리고 원래 씬(작은 규모)을 병렬로 요청한다.
        // 되물러야 맞다. 이것을 확인하지 않으면 임계값이 조용히 죽어 있어도
        // 모른다 — 그러면 작은 씬에서 계속 손해를 보면서 '병렬이니 빠르겠지'로
        // 넘어간다.
        parallelCostThreshold = EnhancedRenderGraph::kParallelRecordCostThreshold;
        useParallelRecording = true;
        parallelWorkers = 6;

        uint32_t declineCovered = 0;
        uint32_t declineDraws = 0;
        double   declineLuminance = 0.0;
        std::vector<DX12GpuProfiler::PassTiming> declineTimings;
        EnhancedRenderGraph::Stats declineStats{};

        if (renderAndCount(cameraSnapshot, declineCovered, declineDraws, error,
            declineTimings, declineStats, declineLuminance))
        {
            declineObserved = declineStats.parallelDeclined;
            declineCost = declineStats.totalRecordCost;
            declineWorkers = declineStats.recordWorkers;
            declineCoverage = declineCovered;
        }
        else
        {
            outLog += "[4/4] 되무름 확인 렌더 실패: " + error + "\n";
            return false;
        }

        parallelCostThreshold = 0;
        useParallelRecording = false;
        parallelWorkers = 1;
    }

    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "      되무름 — 기록량 %u(임계 %u) · %s · 워커 %u · 커버리지 %u\n",
            declineCost, EnhancedRenderGraph::kParallelRecordCostThreshold,
            declineObserved ? "순차로 되무름" : "병렬 유지",
            declineWorkers, declineCoverage);
        outLog += line;
    }

    char shadowLine[320]{};
    std::snprintf(shadowLine, sizeof(shadowLine),
        "      그림자 — 방향광 %s · 캐스터 %u(배치 %u · 컬링 %u) · 분할 %.1f/%.1f/%.1f"
        " · 캐스케이드 텍셀 %u/%u/%u · 먼 캐스터 컬링 %u"
        " · 끔 %.5f · 켬 %.5f · 강제 %.5f\n",
        baselineHasDirectional ? "있음" : "없음", baselineShadowCasters,
        baselineShadowBatches, baselineShadowCulled,
        baselineShadowData.splitDepths.x, baselineShadowData.splitDepths.y,
        baselineShadowData.splitDepths.z,
        baselineCascadeOccluders[0], baselineCascadeOccluders[1], baselineCascadeOccluders[2],
        culledWithFarCaster,
        luminanceNoShadow, luminanceLit, luminanceForced);
    outLog += shadowLine;

    // ★ 그릴 것이 없으면 이 단정은 판정할 수 없다.
    //
    // 처음에는 draws를 보지 않고 밝기만 비교했는데, 씬의 재질을 전부 투명으로
    // 바꿔 보니(전부 forward 큐로 감) "광원이 셰이더에 닿지 않는다"고 나왔다.
    // 광원은 멀쩡했고 Deferred가 칠할 기하가 하나도 없었을 뿐이다.
    // 원인을 잘못 지목하는 진단은 없느니만 못하다 — 그 방향으로 몇 시간을
    // 쓰게 만든다.
    if (draws.empty())
    {
        outLog += "      ※ 불투명 드로우가 없어 라이팅 단정은 판정하지 않았다"
                  "(씬의 재질이 전부 forward 큐로 갔다)\n";
    }
    else if (!lights.empty() && luminanceLit <= luminanceUnlit + 1e-5)
    {
        passed = false;
        verdict = "광원을 " + std::to_string(lights.size())
            + "개 넘겼는데 밝기가 광원 0개일 때와 같다 — 광원이 셰이더에 닿지 않는다";
    }
    // 병렬 경로가 순차와 같은 그림을 내는가.
    //
    // 실제 패스는 업로드 링·디스크립터 링·PSO를 다 쓴다. 인공 패스로 확인한
    // 동일성이 여기서도 성립하는지는 따로 봐야 한다 — 링에서 잘라 간 구간이
    // 어긋나면 커버리지가 아니라 밝기 쪽에서 먼저 드러난다.
    else if (0 != drawCountA && coveredA != parallelCovered)
    {
        passed = false;
        verdict = "병렬 기록의 커버리지가 순차와 다르다("
            + std::to_string(coveredA) + " vs " + std::to_string(parallelCovered) + ")";
    }
    else if (0 != drawCountA && std::fabs(luminanceLit - parallelLuminance) > 1e-4)
    {
        passed = false;
        verdict = "병렬 기록의 밝기가 순차와 다르다 — 링에서 잘라 간 구간이 어긋났을 수 있다";
    }
    else if (0 != drawCountA && parallelWorkersUsed <= 1)
    {
        passed = false;
        verdict = "병렬로 요청했는데 워커가 하나다 — 비교가 성립하지 않는다";
    }
    // 인스턴싱이 도는가.
    //
    // 같은 메시에 서로 다른 재질 둘을 넣었으므로 배치가 둘이어야 한다.
    // 하나면 재질이 다른 것까지 묶은 것이고, 셋 이상이면 묶이지 않은 것이다.
    else if (!draws.empty() && 2 != keyedBatchCount)
    {
        passed = false;
        verdict = "같은 메시에 서로 다른 재질 둘을 넣었는데 배치가 "
            + std::to_string(keyedBatchCount) + "개다 — 인스턴싱 병합이 어긋났다";
    }
    // 드로우별 재질 키잉. 같은 메시에 재질 둘을 넣었으므로 키가 둘이어야 한다.
    else if (!draws.empty() && 2 != keyedMaterialCount)
    {
        passed = false;
        verdict = "같은 메시에 서로 다른 재질 둘을 넣었는데 재질 키가 "
            + std::to_string(keyedMaterialCount)
            + "개다 — 재질이 메시로 키잉되어 뒤엣것이 무시된다";
    }
    // 정반사 항. 거칠기가 밝기를 바꾸지 못하면 BRDF가 결과에 닿지 않는 것이다.
    else if (0 != drawCountA && std::fabs(luminanceSmooth - luminanceRough) <= 1e-5)
    {
        passed = false;
        verdict = "거칠기를 0.05와 1.0으로 바꿔 그렸는데 밝기가 같다"
            " — 정반사 항이 결과에 닿지 않는다";
    }
    // 깊이 전용 렌더가 무언가 기록했는가(①②).
    else if (shadowTestable && 0 == baselineShadowOccluders)
    {
        passed = false;
        verdict = "그림자 캐스터가 " + std::to_string(baselineShadowCasters)
            + "건인데 그림자 맵이 클리어 값 그대로다 — 깊이 전용 렌더나 라이트 행렬이 어긋났다";
    }
    // 캐스케이드가 실제로 셋 다 도는가.
    //
    // 합계만 보면 한 장만 채워져도 통과한다. 그건 배열을 만들어 놓고 슬라이스
    // 0에만 그리는 상태와 구분되지 않는다 — 캐스케이드를 넣은 의미가 사라진다.
    else if (shadowTestable && (0 == baselineCascadeOccluders[0] ||
        0 == baselineCascadeOccluders[1] || 0 == baselineCascadeOccluders[2]))
    {
        passed = false;
        verdict = "캐스케이드 기록 텍셀이 "
            + std::to_string(baselineCascadeOccluders[0]) + "/"
            + std::to_string(baselineCascadeOccluders[1]) + "/"
            + std::to_string(baselineCascadeOccluders[2])
            + " — 빈 캐스케이드가 있다(슬라이스별 DSV나 캐스터 컬링 판정을 볼 것)";
    }
    else if (0 != variedBatchCount && variedBatchCount <= uniformBatchCount)
    {
        passed = false;
        verdict = "재질을 갈랐는데 배치가 " + std::to_string(variedBatchCount)
            + "로 재질 1종의 " + std::to_string(uniformBatchCount)
            + "보다 크지 않다 — 두 모드가 같은 씬이라 병렬 비교가 성립하지 않는다";
    }
    else if (0 != declineCost && !declineObserved)
    {
        // 기록량이 임계값 아래인데 병렬로 갔다면 되무름이 죽은 것이다.
        passed = false;
        verdict = "기록량 " + std::to_string(declineCost) + "(임계 "
            + std::to_string(EnhancedRenderGraph::kParallelRecordCostThreshold)
            + ")인데 병렬로 갔다 — 되무름이 동작하지 않는다";
    }
    else if (declineObserved && declineCoverage != coveredA)
    {
        // 되물렀는데 그림이 달라지면 순차 경로가 병렬 경로와 다른 일을 한 것이다.
        passed = false;
        verdict = "되무른 뒤 커버리지가 " + std::to_string(declineCoverage)
            + "로 기준 " + std::to_string(coveredA) + "과 다르다";
    }
    else if (baselineShadowBatches > baselineShadowCasters)
    {
        // 배치가 캐스터보다 많으면 인스턴스가 안 묶인 정도가 아니라 빈 드로우를
        // 내고 있다는 뜻이다. 둘이 같은 것은 정상이다 — 메시가 전부 다른 씬이면
        // 묶일 것이 없다.
        passed = false;
        verdict = "그림자 배치가 " + std::to_string(baselineShadowBatches)
            + "개인데 캐스터는 " + std::to_string(baselineShadowCasters)
            + "개다 — 인스턴스 없는 드로우를 내고 있다";
    }
    // 캐스터 컬링이 자를 수 있는가. 잘릴 수밖에 없는 것을 하나 넣었으므로
    // 캐스케이드 셋에서 각각 한 번씩 걸러져야 한다.
    else if (shadowTestable && culledWithFarCaster < EnhancedShadowPass::kCascadeCount)
    {
        passed = false;
        verdict = "그림자를 드리울 수 없는 캐스터를 넣었는데 컬링이 "
            + std::to_string(culledWithFarCaster) + "건뿐이다(기대 "
            + std::to_string(EnhancedShadowPass::kCascadeCount)
            + ") — 캐스터 컬링 판정이 늘 참이다";
    }
    // 분할이 단조 증가하는가. 뒤집히면 셰이더의 캐스케이드 선택이 조용히 틀린다.
    else if (shadowTestable &&
        !(baselineShadowData.splitDepths.x < baselineShadowData.splitDepths.y &&
          baselineShadowData.splitDepths.y < baselineShadowData.splitDepths.z))
    {
        passed = false;
        verdict = "캐스케이드 분할이 단조 증가하지 않는다";
    }
    // 그림자 맵을 SRV로 읽고 비교 샘플러가 도는가(③④).
    else if (shadowTestable && luminanceForced >= luminanceNoShadow - 1e-5)
    {
        passed = false;
        verdict = "편향을 음수로 밀어 전부 가려지게 했는데 밝기가 그대로다"
            " — 그림자 맵이 셰이더에 닿지 않는다";
    }
    // 그림자가 밝게 만들면 부호가 뒤집힌 것이다.
    else if (shadowTestable && luminanceLit > luminanceNoShadow + 1e-5)
    {
        passed = false;
        verdict = "그림자를 켰더니 오히려 밝아졌다 — 비교 방향이나 UV가 뒤집혔다";
    }
    // 재질에 baseColor가 있는데 텍스처가 하나도 안 올라갔다면 재질 경로가
    // 통째로 건너뛰어진 것이다. 이 단정이 없어서 실제로 그 상태로 통과한 적이
    // 있다(업로드 0건인데 검증 통과) — 확인하지 못한 것과 확인했고 문제없는
    // 것은 다르다.
    // ★ 아래 체인은 앞 체인과 별개다. 앞에서 이미 실패했으면 들어가지 않는다.
    //
    // 예전에는 그냥 이어 붙어 있어서, 앞에서 잡은 실패의 사유가 이 체인의 마지막
    // else("카메라 이동으로 커버리지가 바뀌었다")에 덮여 사라졌다. 실패로는
    // 끝나지만 왜 실패했는지가 성공 메시지로 바뀌어 있었다 — 실제로 재질 키잉
    // 단정을 넣고 나서 그 모양으로 한 번 속았다.
    const auto finalTextureStats = textureCache.GetStats();
    if (!passed)
    {
        // 사유는 앞에서 정했다. 여기서 덮지 않는다.
    }
    else if (passed && 0 != materialsWithTexture && 0 == finalTextureStats.uploads)
    {
        passed = false;
        verdict = "재질에 baseColor가 " + std::to_string(materialsWithTexture)
            + "건 있는데 텍스처 업로드가 0이다 — 재질 경로가 건너뛰어졌다";
    }
    // 컬링 확인. GBuffer가 걷어내졌다면 이후 단정이 전부 무의미하다.
    else if (passed && 0 != lastGraphStats.passesCulled)
    {
        passed = false;
        verdict = "Deferred가 읽는데도 패스가 "
            + std::to_string(lastGraphStats.passesCulled) + "개 걷어내졌다";
    }
    else if (0 == drawCountA)
    {
        // 씬이 비어 있으면 연결 자체를 확인할 수 없다. 통과로 처리하면
        // '아무것도 안 그렸는데 통과'가 되므로 실패로 알린다.
        //
        // 포워드 큐가 차 있는 경우를 따로 적는다. 씬에 물체가 멀쩡히 있는데도
        // "메시를 배치하라"고 하면 엉뚱한 곳을 보게 된다 — 이 검증이 보는 것은
        // deferred 경로뿐이라는 사실이 메시지에 들어 있어야 한다.
        passed = false;
        verdict = forwardDraws.empty()
            ? "드로우가 0건이다 — 씬에 메시를 배치한 뒤 다시 실행할 것"
            : ("deferred 드로우가 0건이다(포워드는 "
                + std::to_string(forwardDraws.size())
                + "건) — 이 검증은 deferred 경로를 보므로 불투명 재질이 필요하다");
    }
    else if (0 == coveredA)
    {
        passed = false;
        verdict = "드로우는 있는데 깊이에 아무것도 기록되지 않았다";
    }
    else if (coveredA == coveredB)
    {
        passed = false;
        verdict = "카메라를 옮겨도 커버리지가 같다 — 상수 버퍼가 셰이더에 닿지 않는다";
    }
    else
    {
        verdict = "카메라 이동으로 커버리지가 " + std::to_string(coveredA)
            + " → " + std::to_string(coveredB) + "로 바뀌었다";
    }

    outLog += "[4/4] " + std::string(passed ? "통과" : "실패") + " — " + verdict + "\n";

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    textureCache.Shutdown();
    profiler.Shutdown();
    ssgi.Shutdown();
    deferred.Shutdown();
    shadow.Shutdown();
    gbuffer.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    commandPool.Shutdown();
    resources.Shutdown();

    outLog += passed ? "씬 연결 검증 통과\n" : "씬 연결 검증 실패\n";
    return passed;
}

bool EnhancedSceneRenderer::RunScreenResizeTest(std::string& outLog)
{
    bool passed = true;

    // ── [1/3] DX11 텍스처가 정책대로 따라가는가 ──
    //
    // 실제 창을 흔들지 않고 크기 통지만 직접 건다. 창을 흔들면 스왑체인
    // 재생성까지 얽혀 무엇을 재고 있는지 흐려진다.
    const uint32_t startWidth = (std::max)(1u, ScreenResizeBus::Get().GetWidth());
    const uint32_t startHeight = (std::max)(1u, ScreenResizeBus::Get().GetHeight());

    {
        auto* following = Texture::CreateScreenSized("ResizeTest.Following",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
        auto* half = Texture::CreateScreenSized("ResizeTest.Half",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 2, 2);
        auto* fixed = Texture::Create(2048, 2048, "ResizeTest.Fixed",
            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);

        if (nullptr == following || nullptr == half || nullptr == fixed)
        {
            outLog += "[1/3] 검증용 텍스처 생성 실패\n";
            return false;
        }

        // 지금 크기와 다른 값으로 흔든다. 같은 값이면 바뀐 것이 없어도 통과한다.
        const uint32_t newWidth = (1280u == startWidth) ? 1600u : 1280u;
        const uint32_t newHeight = (720u == startHeight) ? 900u : 720u;

        following->ApplyScreenSize(newWidth, newHeight);
        half->ApplyScreenSize(newWidth, newHeight);
        fixed->ApplyScreenSize(newWidth, newHeight);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[1/3] DX11 %ux%u -> %ux%u · 추종 %.0fx%.0f · 1/2 %.0fx%.0f · 고정 %.0fx%.0f\n",
            startWidth, startHeight, newWidth, newHeight,
            following->GetWidth(), following->GetHeight(),
            half->GetWidth(), half->GetHeight(),
            fixed->GetWidth(), fixed->GetHeight());
        outLog += line;

        if (following->GetWidth() != static_cast<float>(newWidth) ||
            following->GetHeight() != static_cast<float>(newHeight))
        {
            passed = false;
            outLog += "      실패 — 따라가겠다고 선언한 텍스처가 새 크기를 받지 않았다\n";
        }
        if (half->GetWidth() != static_cast<float>(newWidth / 2) ||
            half->GetHeight() != static_cast<float>(newHeight / 2))
        {
            passed = false;
            outLog += "      실패 — 1/2 해상도 버퍼가 나눈 크기로 따라가지 않았다\n";
        }
        if (2048.f != fixed->GetWidth() || 2048.f != fixed->GetHeight())
        {
            passed = false;
            outLog += "      실패 — 선언하지 않은 텍스처까지 크기가 바뀌었다"
                "(그림자 맵·LUT가 창을 따라가는 상태다)\n";
        }

        Memory::SafeDelete(following);
        Memory::SafeDelete(half);
        Memory::SafeDelete(fixed);
    }

    // ── [2/3] DX12 디바이스가 리사이즈를 받는가 ──
    DX12DeviceResources resources;
    std::string error;
    constexpr uint32_t kInitialWidth = 256;
    constexpr uint32_t kInitialHeight = 256;
    if (!resources.Initialize(kInitialWidth, kInitialHeight, error))
    {
        outLog += "[2/3] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    constexpr uint32_t kResizedWidth = 384;
    constexpr uint32_t kResizedHeight = 192;
    if (!resources.Resize(kResizedWidth, kResizedHeight, error))
    {
        outLog += "[2/3] 리사이즈 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/3] DX12 %ux%u -> %ux%u · 보고 %ux%u · rowPitch %u\n",
            kInitialWidth, kInitialHeight, kResizedWidth, kResizedHeight,
            resources.GetWidth(), resources.GetHeight(), resources.GetRowPitch());
        outLog += line;
    }

    if (resources.GetWidth() != kResizedWidth || resources.GetHeight() != kResizedHeight)
    {
        passed = false;
        outLog += "      실패 — 디바이스가 새 크기를 반영하지 않았다\n";
    }

    // 리소스가 실제로 다시 만들어졌는지는 설명(desc)으로 본다. 보고 값만 보면
    // 멤버 변수만 바뀌고 타깃은 그대로인 상태와 구분되지 않는다.
    if (nullptr != resources.GetRenderTarget())
    {
        const auto desc = resources.GetRenderTarget()->GetDesc();
        if (desc.Width != kResizedWidth || desc.Height != kResizedHeight)
        {
            passed = false;
            outLog += "      실패 — 렌더 타깃 리소스가 옛 크기 그대로다\n";
        }
    }
    else
    {
        passed = false;
        outLog += "      실패 — 리사이즈 뒤 렌더 타깃이 비었다\n";
    }

    // ── [3/3] 리사이즈한 타깃에 실제로 그리고 되읽을 수 있는가 ──
    //
    // 크기만 맞고 뷰가 옛 리소스를 가리키면 여기서 드러난다.
    if (!resources.BeginFrame(error))
    {
        outLog += "[3/3] BeginFrame 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    auto* commandList = resources.GetCommandList();
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = resources.GetRtvHandle();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    commandList->ClearRenderTargetView(rtv, DX12DeviceResources::kClearColor, 0, nullptr);

    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resources.GetRenderTarget();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = resources.GetReadbackBuffer();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = kResizedWidth;
        dst.PlacedFootprint.Footprint.Height = kResizedHeight;
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = resources.GetRowPitch();

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = resources.GetRenderTarget();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commandList->ResourceBarrier(1, &barrier);
    }

    if (!resources.EndFrame(error))
    {
        outLog += "[3/3] EndFrame 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    resources.WaitForGpu();

    uint32_t clearedPixels = 0;
    {
        void* mapped = nullptr;
        const size_t bytes = static_cast<size_t>(resources.GetRowPitch()) * kResizedHeight;
        D3D12_RANGE range{ 0, bytes };
        if (SUCCEEDED(resources.GetReadbackBuffer()->Map(0, &range, &mapped)))
        {
            const auto expected = static_cast<int>(
                DX12DeviceResources::kClearColor[0] * 255.f + 0.5f);
            const auto* pixels = static_cast<const uint8_t*>(mapped);
            for (uint32_t y = 0; y < kResizedHeight; ++y)
            {
                const auto* row = pixels + static_cast<size_t>(y) * resources.GetRowPitch();
                for (uint32_t x = 0; x < kResizedWidth; ++x)
                {
                    const int red = row[static_cast<size_t>(x) * 4];
                    if (std::abs(red - expected) <= 2) ++clearedPixels;
                }
            }
            resources.GetReadbackBuffer()->Unmap(0, nullptr);
        }
    }

    const uint32_t totalPixels = kResizedWidth * kResizedHeight;
    {
        char line[160]{};
        std::snprintf(line, sizeof(line), "[3/3] 리사이즈 후 렌더 — 클리어 픽셀 %u/%u\n",
            clearedPixels, totalPixels);
        outLog += line;
    }

    if (clearedPixels != totalPixels)
    {
        passed = false;
        outLog += "      실패 — 리사이즈한 타깃에 그린 결과가 새 크기 전체를 덮지 않는다"
            "(뷰가 옛 리소스를 가리킬 때 나오는 모양이다)\n";
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    resources.Shutdown();

    outLog += passed ? "크기 추종 검증 통과\n" : "크기 추종 검증 실패\n";
    return passed;
}

bool EnhancedSceneRenderer::RunParallelRecordTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    bool passed = true;

    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 256;

    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/4] 디바이스 초기화 실패: " + error + "\n";
        return false;
    }

    // ── [1/4] 업로드 링을 여러 스레드가 동시에 잘라도 겹치지 않는가 ──
    //
    // 경합은 매번 나지 않는다. 그래서 결과 픽셀만 보면 우연히 통과할 수 있고,
    // 그 우연은 나중에 '가끔 상수가 다른 드로우 것으로 보인다'로 돌아온다.
    // 할당 구간을 직접 모아 겹침을 본다.
    {
        constexpr uint32_t kThreads = 8;
        constexpr uint32_t kPerThread = 256;

        std::vector<std::vector<std::pair<uint64_t, uint64_t>>> ranges(kThreads);
        std::vector<std::thread> threads;

        for (uint32_t t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&, t]()
            {
                ranges[t].reserve(kPerThread);
                for (uint32_t i = 0; i < kPerThread; ++i)
                {
                    // 크기를 섞는다. 같은 크기만 쓰면 정렬 계산이 항상 같아
                    // 경합 창이 좁아진다.
                    const uint64_t size = 16ull + (i % 7) * 48ull;
                    const auto allocation = resources.GetUploadRing().Allocate(
                        size, DX12UploadRing::kConstantBufferAlignment);
                    if (allocation.IsValid())
                    {
                        ranges[t].emplace_back(allocation.offset, allocation.offset + size);
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();

        std::vector<std::pair<uint64_t, uint64_t>> all;
        for (auto& list : ranges) all.insert(all.end(), list.begin(), list.end());
        std::sort(all.begin(), all.end());

        uint32_t overlaps = 0;
        for (size_t i = 1; i < all.size(); ++i)
        {
            if (all[i].first < all[i - 1].second) ++overlaps;
        }

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[1/4] 업로드 링 — 스레드 %u · 할당 %zu건 · 겹침 %u건\n",
            kThreads, all.size(), overlaps);
        outLog += line;

        if (0 != overlaps)
        {
            passed = false;
            outLog += "      실패 — 두 스레드가 같은 구간을 받았다\n";
        }
        if (all.size() < kThreads)
        {
            passed = false;
            outLog += "      실패 — 할당이 거의 이뤄지지 않았다(구간이 너무 작다)\n";
        }
    }

    // ── [2/4] 디스크립터 링도 같은가 ──
    {
        constexpr uint32_t kThreads = 8;
        constexpr uint32_t kPerThread = 128;

        std::vector<std::vector<std::pair<uint64_t, uint64_t>>> ranges(kThreads);
        std::vector<std::thread> threads;

        for (uint32_t t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&, t]()
            {
                ranges[t].reserve(kPerThread);
                for (uint32_t i = 0; i < kPerThread; ++i)
                {
                    const uint32_t count = 1u + (i % 4);
                    const auto allocation = resources.GetDescriptorRing().Allocate(count);
                    if (allocation.IsValid())
                    {
                        ranges[t].emplace_back(allocation.gpu.ptr,
                            allocation.gpu.ptr + static_cast<uint64_t>(count)
                                * allocation.incrementSize);
                    }
                }
            });
        }
        for (auto& thread : threads) thread.join();

        std::vector<std::pair<uint64_t, uint64_t>> all;
        for (auto& list : ranges) all.insert(all.end(), list.begin(), list.end());
        std::sort(all.begin(), all.end());

        uint32_t overlaps = 0;
        for (size_t i = 1; i < all.size(); ++i)
        {
            if (all[i].first < all[i - 1].second) ++overlaps;
        }

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 디스크립터 링 — 스레드 %u · 할당 %zu건 · 겹침 %u건\n",
            kThreads, all.size(), overlaps);
        outLog += line;

        if (0 != overlaps)
        {
            passed = false;
            outLog += "      실패 — 두 스레드가 같은 디스크립터 구간을 받았다\n";
        }
    }

    // ── [3/4] 순차와 병렬의 결과가 같은가 ──
    //
    // 같은 타깃에 색을 순서대로 덮는 패스를 여러 개 둔다. 순서가 지켜지면
    // 마지막 패스의 색만 남는다. 순서가 깨지거나 리스트가 두 번 실행되면
    // 다른 색이 나오고, 그 차이는 픽셀 대조로 잡힌다.
    constexpr uint32_t kPassCount = 6;

    const uint32_t rowPitch = (kWidth * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    ComPtr<ID3D12Resource> readback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<uint64_t>(rowPitch) * kHeight;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&readback))))
        {
            outLog += "[3/4] 리드백 버퍼 생성 실패\n";
            resources.Shutdown();
            return false;
        }
    }

    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 1;
        if (FAILED(resources.GetDevice()->CreateDescriptorHeap(&heapDesc,
            IID_PPV_ARGS(&rtvHeap))))
        {
            outLog += "[3/4] RTV 힙 생성 실패\n";
            resources.Shutdown();
            return false;
        }
    }

    DX12CommandListPool pool;
    if (!pool.Initialize(resources.GetDevice(), 4, DX12DeviceResources::kFrameCount, error))
    {
        outLog += "[3/4] 커맨드 리스트 풀 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // 순차/병렬을 같은 코드로 돌린다. 그래야 차이가 '병렬이라서'로 좁혀진다.
    const auto renderOnce = [&](uint32_t workers, std::vector<uint8_t>& outPixels,
        EnhancedRenderGraph::Stats& outStats, std::string& outStepError) -> bool
    {
        if (!resources.BeginFrame(outStepError)) return false;
        pool.BeginFrame(0);

        EnhancedRenderGraph graph;

        RGTextureDesc targetDesc{};
        targetDesc.width = kWidth;
        targetDesc.height = kHeight;
        targetDesc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        targetDesc.allowRenderTarget = true;
        targetDesc.name = "Parallel.Target";

        // 클리어 값을 리소스에 미리 알린다. 다른 값으로 클리어하면 검증 레이어가
        // "느려진다"고 경고하고, 그 경고를 검증에서 무시하기 시작하면 다른 곳의
        // 진짜 실수도 같이 묻힌다.
        targetDesc.clearColor[0] = 0.25f;
        targetDesc.clearColor[1] = 0.5f;
        targetDesc.clearColor[2] = 0.75f;
        targetDesc.clearColor[3] = 1.f;
        const RGHandle target = graph.CreateTexture(targetDesc);

        // 패스마다 자기 가로 띠만 지운다.
        //
        // 색으로 순서를 확인하려면 패스마다 다른 색을 써야 하는데 그러면 위
        // 경고가 난다. 대신 띠를 나눠 '전 구간이 빠짐없이 덮이는가'를 본다 —
        // 리스트 하나가 통째로 빠지면 그 띠가 미정의 값으로 남는다.
        //
        // 순서 자체는 순차와 병렬의 픽셀이 완전히 같은지로 확인한다([3/4]).
        for (uint32_t i = 0; i < kPassCount; ++i)
        {
            graph.AddPass("clear" + std::to_string(i),
                { { target, RGResourceState::RenderTarget } },
                [&, i](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
                    resources.GetDevice()->CreateRenderTargetView(
                        executeContext.Resolve(target), nullptr, rtv);

                    const LONG bandTop = static_cast<LONG>(kHeight * i / kPassCount);
                    const LONG bandBottom = static_cast<LONG>(kHeight * (i + 1) / kPassCount);
                    const D3D12_RECT band{ 0, bandTop, static_cast<LONG>(kWidth), bandBottom };

                    const float color[4] = { 0.25f, 0.5f, 0.75f, 1.f };
                    executeContext.commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
                    executeContext.commandList->ClearRenderTargetView(rtv, color, 1, &band);
                }, true);
        }

        graph.AddPass("readback", { { target, RGResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = readback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                dst.PlacedFootprint.Footprint.Width = kWidth;
                dst.PlacedFootprint.Footprint.Height = kHeight;
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = executeContext.Resolve(target);
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }, true);

        if (!graph.Compile(resources.GetDevice(), outStepError)) return false;

        // 업로드(여기서는 없지만 실제 패스에서는 있다)가 워커 리스트보다 먼저
        // 가야 하므로 중간 제출로 경계를 만든다.
        if (!resources.FlushCommandList(outStepError)) return false;

        if (!graph.ExecuteParallel(pool, resources.GetCommandQueue(), workers, outStepError))
        {
            return false;
        }
        outStats = graph.GetStats();

        if (!resources.EndFrame(outStepError)) return false;
        resources.WaitForGpu();

        outPixels.assign(static_cast<size_t>(rowPitch) * kHeight, 0);
        void* mapped = nullptr;
        D3D12_RANGE range{ 0, outPixels.size() };
        if (FAILED(readback->Map(0, &range, &mapped)))
        {
            outStepError = "리드백 Map 실패";
            return false;
        }
        memcpy(outPixels.data(), mapped, outPixels.size());
        readback->Unmap(0, nullptr);
        return true;
    };

    std::vector<uint8_t> sequential;
    std::vector<uint8_t> parallel;
    EnhancedRenderGraph::Stats sequentialStats{};
    EnhancedRenderGraph::Stats parallelStats{};

    if (!renderOnce(1, sequential, sequentialStats, error))
    {
        outLog += "[3/4] 순차 실행 실패: " + error + "\n";
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }
    if (!renderOnce(4, parallel, parallelStats, error))
    {
        outLog += "[3/4] 병렬 실행 실패: " + error + "\n";
        pool.Shutdown();
        resources.Shutdown();
        return false;
    }

    size_t differing = 0;
    for (size_t i = 0; i < sequential.size() && i < parallel.size(); ++i)
    {
        if (sequential[i] != parallel[i]) ++differing;
    }

    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 순차(워커 %u·리스트 %u) vs 병렬(워커 %u·리스트 %u) — 다른 바이트 %zu\n",
            sequentialStats.recordWorkers, sequentialStats.submittedLists,
            parallelStats.recordWorkers, parallelStats.submittedLists, differing);
        outLog += line;
    }

    if (0 != differing)
    {
        passed = false;
        outLog += "      실패 — 병렬 결과가 순차와 다르다\n";
    }
    if (parallelStats.recordWorkers <= 1)
    {
        passed = false;
        outLog += "      실패 — 병렬로 요청했는데 워커가 하나다(비교가 성립하지 않는다)\n";
    }

    // ── [4/4] 모든 패스의 띠가 빠짐없이 덮였는가 ──
    //
    // 리스트 하나가 통째로 빠지면 그 띠가 미정의 값으로 남는다.
    //
    // 제출 순서 자체는 [3/4]가 본다 — 순서가 달라지면 순차와 픽셀이 갈린다.
    // 리스트 중복 제출은 구조로 막았다(연속 블록 배분이라 워커당 한 번이고,
    // 제출 리스트 수 == 워커 수로 확인한다).
    {
        const auto expected = static_cast<int>(0.25f * 255.f + 0.5f);

        uint32_t wrongPixels = 0;
        for (uint32_t y = 0; y < kHeight; ++y)
        {
            const uint8_t* row = parallel.data() + static_cast<size_t>(y) * rowPitch;
            for (uint32_t x = 0; x < kWidth; ++x)
            {
                if (std::abs(static_cast<int>(row[static_cast<size_t>(x) * 4]) - expected) > 1)
                {
                    ++wrongPixels;
                }
            }
        }

        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[4/4] 띠 덮임 — 기대와 다른 픽셀 %u/%u · 제출 리스트 %u(워커 %u)\n",
            wrongPixels, kWidth * kHeight,
            parallelStats.submittedLists, parallelStats.recordWorkers);
        outLog += line;

        if (0 != wrongPixels)
        {
            passed = false;
            outLog += "      실패 — 덮이지 않은 구간이 있다(리스트가 빠졌다)\n";
        }
        if (parallelStats.submittedLists != parallelStats.recordWorkers)
        {
            passed = false;
            outLog += "      실패 — 제출 리스트 수가 워커 수와 다르다\n";
        }
    }

    std::string messages;
    const uint32_t problems = resources.DrainDebugMessages(messages);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + messages;
    }

    pool.Shutdown();
    resources.Shutdown();

    outLog += passed ? "병렬 기록 검증 통과\n" : "병렬 기록 검증 실패\n";
    return passed;
}

#endif
