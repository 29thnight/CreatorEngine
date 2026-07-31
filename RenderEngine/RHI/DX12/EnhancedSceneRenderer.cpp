#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSceneRenderer.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"

#include <DirectXTex.h>
#include <d3dcompiler.h>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>

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

    ComPtr<ID3D12RootSignature> rootSignature;
    {
        D3D12_ROOT_SIGNATURE_DESC desc{};
        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &errors)))
        {
            outLog += "[2/4] 루트 시그니처 직렬화 실패\n";
            return false;
        }
        if (FAILED(resources.GetDevice()->CreateRootSignature(0,
            serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature))))
        {
            outLog += "[2/4] 루트 시그니처 생성 실패\n";
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
    triangleDesc.rootSignature = rootSignature.Get();
    triangleDesc.rootSignatureId = 1;

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

    ComPtr<ID3D12DescriptorHeap> srvHeap;
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(resources.GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap))))
        {
            outLog += "[2/4] SRV 힙 생성 실패\n";
            return false;
        }
    }

    ComPtr<ID3D12RootSignature> quadRootSignature;
    {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;

        D3D12_ROOT_PARAMETER param{};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges = 1;
        param.DescriptorTable.pDescriptorRanges = &range;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        // 샘플러는 정적으로 루트에 박는다 — 샘플러 힙 없이 완료 기준(텍스처 블릿)을
        // 증명할 수 있고, 실전의 샘플러 힙 설계는 PSOManager(3-4) 몫이다.
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = 1;
        desc.pParameters = &param;
        desc.NumStaticSamplers = 1;
        desc.pStaticSamplers = &sampler;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &errors)))
        {
            outLog += "[2/4] 쿼드 루트 시그니처 직렬화 실패";
            if (errors) { outLog += ": "; outLog += static_cast<const char*>(errors->GetBufferPointer()); }
            outLog += "\n";
            return false;
        }
        if (FAILED(resources.GetDevice()->CreateRootSignature(0,
            serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&quadRootSignature))))
        {
            outLog += "[2/4] 쿼드 루트 시그니처 생성 실패\n";
            return false;
        }
    }

    DX12GraphicsPipelineDesc quadDesc{};
    quadDesc.vsBytecode = quadVsBlob->GetBufferPointer();
    quadDesc.vsSize = quadVsBlob->GetBufferSize();
    quadDesc.psBytecode = quadPsBlob->GetBufferPointer();
    quadDesc.psSize = quadPsBlob->GetBufferSize();
    quadDesc.rootSignature = quadRootSignature.Get();
    quadDesc.rootSignatureId = 2;

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
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> uploadBuffer;
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

        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = static_cast<uint64_t>(rowPitch) * kTexSize;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.SampleDesc.Count = 1;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&uploadHeap,
            D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&uploadBuffer))))
        {
            outLog += "[2/4] 업로드 버퍼 생성 실패\n";
            return false;
        }

        void* mapped = nullptr;
        if (FAILED(uploadBuffer->Map(0, nullptr, &mapped)))
        {
            outLog += "[2/4] 업로드 버퍼 Map 실패\n";
            return false;
        }
        auto* dst = static_cast<uint8_t*>(mapped);
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
        uploadBuffer->Unmap(0, nullptr);

        // 업로드는 전용 사이클로 제출 — 렌더 프레임과 섞지 않아 실패 지점이 분리된다.
        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] 업로드 Begin 실패: " + error + "\n";
            return false;
        }

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = uploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
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

        resources.GetDevice()->CreateShaderResourceView(texture.Get(), nullptr,
            srvHeap->GetCPUDescriptorHandleForHeapStart());
    }
    outLog += "[2/4] 체커보드 텍스처 업로드·SRV 생성 완료\n";

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

        commandList->SetGraphicsRootSignature(rootSignature.Get());
        commandList->SetPipelineState(pso);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->DrawInstanced(3, 1, 0, 0);

        // 텍스처 쿼드 — 디스크립터 힙 바인딩은 루트 테이블 설정보다 먼저(검증 레이어 규칙).
        ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetGraphicsRootSignature(quadRootSignature.Get());
        commandList->SetPipelineState(quadPso);
        commandList->SetGraphicsRootDescriptorTable(0, srvHeap->GetGPUDescriptorHandleForHeapStart());
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

    ComPtr<ID3D12RootSignature> rootSignature;
    {
        D3D12_ROOT_SIGNATURE_DESC desc{};
        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized, &errors)) ||
            FAILED(resources.GetDevice()->CreateRootSignature(0,
                serialized->GetBufferPointer(), serialized->GetBufferSize(),
                IID_PPV_ARGS(&rootSignature))))
        {
            outLog += "루트 시그니처 준비 실패\n";
            return false;
        }
    }

    // 상태만 다른 변형 3종 — 해시가 상태를 실제로 구분하는지 확인한다.
    // (셰이더가 같아도 다른 PSO여야 한다)
    DX12GraphicsPipelineDesc base{};
    base.vsBytecode = vsBlob->GetBufferPointer();
    base.vsSize = vsBlob->GetBufferSize();
    base.psBytecode = psBlob->GetBufferPointer();
    base.psSize = psBlob->GetBufferSize();
    base.rootSignature = rootSignature.Get();
    base.rootSignatureId = 1;

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

        ComPtr<ID3D12RootSignature> computeRoot;
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

            ComPtr<ID3DBlob> serialized;
            ComPtr<ID3DBlob> errors;
            if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                &serialized, &errors)) ||
                FAILED(resources.GetDevice()->CreateRootSignature(0,
                    serialized->GetBufferPointer(), serialized->GetBufferSize(),
                    IID_PPV_ARGS(&computeRoot))))
            {
                outLog += "컴퓨트 루트 시그니처 준비 실패\n";
                return false;
            }
        }

        DX12ComputePipelineDesc computeDesc{};
        computeDesc.csBytecode = csBlob->GetBufferPointer();
        computeDesc.csSize = csBlob->GetBufferSize();
        computeDesc.rootSignature = computeRoot.Get();
        computeDesc.rootSignatureId = 10;

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

#endif
