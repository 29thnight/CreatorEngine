#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSAOPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")

// SSAO 자가 검증 (PHASE 3-6).
//
// ── 알려진 배치로 단정한다 ──
//
// 깊이·노멀을 절차적으로 만든다. 화면 왼쪽 절반은 카메라를 마주 보는
// 평평한 벽, 오른쪽 절반은 그보다 앞으로 튀어나온 벽이다. 둘 사이에
// 수직 계단이 하나 생긴다.
//
//   · 평평한 곳  — 가릴 것이 없으므로 AO는 1에 가까워야 한다
//   · 계단 안쪽  — 튀어나온 벽이 하늘의 절반을 가리므로 AO가 확실히 낮다
//
// 이 배치를 고른 이유는 두 방향의 실패가 모두 위험하기 때문이다.
// 평평한 곳이 어두우면 자기 자신을 가림막으로 세는 것이고(전체가 탁해진다),
// 계단이 밝으면 AO가 아무 일도 안 하는 것이다. 둘 다 그림만 봐서는
// '좀 이상한데' 이상으로 특정되지 않는다.
//
// ★ 실행 여부를 먼저 단정한다. SSGI 첫 검증에서 '선언 9 · 실행 0'인데
//   통과가 나온 적이 있다 — 아무도 결과를 읽지 않으면 그래프가 패스를
//   통째로 걷어내고, 그러면 셰이더 컴파일만 확인한 셈이 된다.
namespace
{
    constexpr uint32_t kTestWidth = 256;
    constexpr uint32_t kTestHeight = 256;

    // 검사용 깊이·노멀 생성. 왼쪽 절반은 뷰 z = 1.0, 오른쪽 절반은 0.6인
    // 벽 두 장이고 둘 다 카메라를 마주 본다(노멀 (0,0,-1)).
    constexpr const char* kSceneShader = R"(
RWTexture2D<float>  gDepth  : register(u0);
RWTexture2D<float4> gNormal : register(u1);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    float gNearZ;
    float gFarZ;
    float gLeftViewZ;
    float gRightViewZ;
    uint2 gPad;
};

float DepthFromViewZ(float viewZ)
{
    // XMMatrixPerspectiveFovLH의 z 변환. 검증이 패스와 같은 규약을 쓰는지가
    // 여기서 갈린다 — 다르면 AO가 엉뚱한 거리를 본다.
    const float a = gFarZ / (gFarZ - gNearZ);
    return a * (1.0f - gNearZ / viewZ);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    const bool right = (id.x >= gSize.x / 2);
    const float viewZ = right ? gRightViewZ : gLeftViewZ;

    gDepth[id.xy] = DepthFromViewZ(viewZ);
    gNormal[id.xy] = float4(0.5f, 0.5f, 0.0f, 1.0f);   // (0,0,-1) 인코딩
}
)";

    struct SceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        float    nearZ{ 0.f };
        float    farZ{ 0.f };
        float    leftViewZ{ 0.f };
        float    rightViewZ{ 0.f };
        uint32_t pad[2]{};
    };

    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게
    // 둔다. 그냥 HalfToFloat으로 뒀다가 Forward+ 검증의 같은 이름과 부딪혔다.
    float SsaoHalfToFloat(uint16_t bits)
    {
        const uint32_t sign = static_cast<uint32_t>(bits & 0x8000u) << 16;
        uint32_t exponent = (bits >> 10) & 0x1Fu;
        uint32_t mantissa = bits & 0x3FFu;

        if (0 == exponent)
        {
            if (0 == mantissa)
            {
                float out; memcpy(&out, &sign, 4); return out;
            }
            exponent = 1;
            while (0 == (mantissa & 0x400u)) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3FFu;
            const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
            float out; memcpy(&out, &bitsOut, 4); return out;
        }
        if (31 == exponent)
        {
            const uint32_t bitsOut = sign | 0x7F800000u | (mantissa << 13);
            float out; memcpy(&out, &bitsOut, 4); return out;
        }

        const uint32_t bitsOut = sign | ((exponent + 112u) << 23) | (mantissa << 13);
        float out; memcpy(&out, &bitsOut, 4); return out;
    }
}

bool EnhancedSceneRenderer::RunSSAOTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── SSAO 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kTestWidth, kTestHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_ssao.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    constexpr float kNearZ = 0.1f;
    constexpr float kFarZ = 100.f;
    constexpr float kLeftViewZ = 1.0f;
    constexpr float kRightViewZ = 0.6f;

    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, kNearZ, kFarZ);
    camera.inverseView = XMMatrixIdentity();
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);

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
        outLog += "[1/4] SSAO 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] AO·필터 셰이더 컴파일·PSO 생성 통과\n";

    // ── 입력 텍스처 ──
    ComPtr<ID3D12Resource> depth;
    ComPtr<ID3D12Resource> normal;
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kTestWidth;
        desc.Height = kTestHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[2/4] 깊이 생성 실패\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }

        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, IID_PPV_ARGS(&normal))))
        {
            outLog += "[2/4] 노멀 생성 실패\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    // 씬 생성 PSO
    ID3D12PipelineState* scenePSO = nullptr;
    ID3D12RootSignature* sceneRoot = nullptr;
    {
        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 2;
        uavRange.BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = 0;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &uavRange;

        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = _countof(params);
        rootDesc.pParameters = params;

        const auto root = rootSignatures.GetOrCreate(rootDesc, error);
        if (!root.IsValid())
        {
            outLog += "[2/4] 씬 루트 시그니처 실패: " + error + "\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }
        sceneRoot = root.signature;

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> errors;
        if (FAILED(D3DCompile(kSceneShader, strlen(kSceneShader), nullptr, nullptr,
            nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &errors)))
        {
            outLog += "[2/4] 씬 셰이더 컴파일 실패: ";
            if (errors) outLog += static_cast<const char*>(errors->GetBufferPointer());
            outLog += "\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }

        DX12ComputePipelineDesc desc{};
        desc.csBytecode = blob->GetBufferPointer();
        desc.csSize = blob->GetBufferSize();
        desc.rootSignature = root.signature;
        desc.rootSignatureId = root.id;

        scenePSO = psoManager.GetOrCreateCompute(desc, error);
        if (nullptr == scenePSO)
        {
            outLog += "[2/4] 씬 PSO 생성 실패: " + error + "\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    const uint32_t halfWidth =
        (kTestWidth + EnhancedSSAOPass::kResolutionDivisor - 1)
        / EnhancedSSAOPass::kResolutionDivisor;
    const uint32_t halfHeight =
        (kTestHeight + EnhancedSSAOPass::kResolutionDivisor - 1)
        / EnhancedSSAOPass::kResolutionDivisor;

    const uint64_t rowPitch =
        ((static_cast<uint64_t>(halfWidth) * 4ull) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull)
        & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull);

    // 필터 전후를 둘 다 가져온다. 같으면 필터가 죽은 것이고, 그것은
    // 최종 그림만 봐서는 알 수 없다.
    ComPtr<ID3D12Resource> rawReadback;
    ComPtr<ID3D12Resource> filteredReadback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = rowPitch * halfHeight;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&rawReadback))) ||
            FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&filteredReadback))))
        {
            outLog += "[2/4] 리드백 생성 실패\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    EnhancedRenderGraph::Stats graphStats{};

    {
        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] BeginFrame 실패: " + error + "\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!ssao.PrepareFrame(frameContext, error))
        {
            outLog += "[2/4] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다. 블록 안에 두면 나가면서
        //   transient를 놓아 제출 전에 리소스가 사라진다(dx12.compare에서
        //   크래시로 겪은 그 실수다).
        EnhancedRenderGraph graph;

        EnhancedSSAOPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RGResourceState::UnorderedAccess, "SSAO.TestDepth");
        inputs.normal = graph.ImportTexture(normal.Get(),
            RGResourceState::UnorderedAccess, "SSAO.TestNormal");

        // 씬을 먼저 그린다. 그래프가 '아직 아무도 안 쓴 것을 읽는다'를
        // 컴파일에서 잡으므로, 이 패스가 없으면 SSAO 선언 자체가 거절된다.
        graph.AddPass("SSAO.TestScene",
            { { inputs.depth, RGResourceState::UnorderedAccess },
              { inputs.normal, RGResourceState::UnorderedAccess } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                auto* commandList = executeContext.commandList;
                auto* device = resources.GetDevice();

                SceneParams params{};
                params.sizeX = kTestWidth;
                params.sizeY = kTestHeight;
                params.nearZ = kNearZ;
                params.farZ = kFarZ;
                params.leftViewZ = kLeftViewZ;
                params.rightViewZ = kRightViewZ;

                const auto cb = resources.GetUploadRing().Allocate(
                    sizeof(SceneParams), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, &params, sizeof(params));

                const auto uavTable = resources.GetDescriptorRing().Allocate(2);
                if (!uavTable.IsValid()) return;

                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
                device->CreateUnorderedAccessView(depth.Get(), nullptr,
                    &uavDesc, uavTable.CpuAt(0));

                uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                device->CreateUnorderedAccessView(normal.Get(), nullptr,
                    &uavDesc, uavTable.CpuAt(1));

                ID3D12DescriptorHeap* heaps[] = { resources.GetDescriptorRing().GetHeap() };
                commandList->SetDescriptorHeaps(1, heaps);

                commandList->SetComputeRootSignature(sceneRoot);
                commandList->SetPipelineState(scenePSO);
                commandList->SetComputeRootConstantBufferView(0, cb.gpuAddress);
                commandList->SetComputeRootDescriptorTable(1, uavTable.gpu);
                commandList->Dispatch((kTestWidth + 7) / 8, (kTestHeight + 7) / 8, 1);
            });

        ssao.SetInputs(inputs);
        ssao.Declare(graph, frameContext);

        const RGHandle rawHandle = ssao.GetRawOutput();
        const RGHandle outHandle = ssao.GetOutput();

        if (!rawHandle.IsValid() || !outHandle.IsValid())
        {
            outLog += "[2/4] SSAO 출력이 선언되지 않았다\n";
            passed = false;
        }
        else
        {
            // ★ 결과를 읽는 패스를 반드시 둔다.
            //
            // 이것이 없으면 그래프가 SSAO를 통째로 걷어내고(아무도 결과를
            // 안 쓰므로) 검증은 셰이더 컴파일만 확인한 셈이 된다. SSGI에서
            // '선언 9 · 실행 0'으로 통과가 났던 그 자리다.
            graph.AddPass("SSAO.Readback",
                { { rawHandle, RGResourceState::CopySource },
                  { outHandle, RGResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    const auto copy = [&](RGHandle handle, ID3D12Resource* target)
                    {
                        D3D12_TEXTURE_COPY_LOCATION src{};
                        src.pResource = executeContext.Resolve(handle);
                        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                        D3D12_TEXTURE_COPY_LOCATION dst{};
                        dst.pResource = target;
                        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                        dst.PlacedFootprint.Footprint.Format = EnhancedSSAOPass::kAOFormat;
                        dst.PlacedFootprint.Footprint.Width = halfWidth;
                        dst.PlacedFootprint.Footprint.Height = halfHeight;
                        dst.PlacedFootprint.Footprint.Depth = 1;
                        dst.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

                        executeContext.commandList->CopyTextureRegion(
                            &dst, 0, 0, 0, &src, nullptr);
                    };

                    copy(rawHandle, rawReadback.Get());
                    copy(outHandle, filteredReadback.Get());
                }, true);

            if (!graph.Compile(resources.GetDevice(), error))
            {
                outLog += "[2/4] Compile 실패: " + error + "\n";
                passed = false;
            }
            if (passed && !graph.Execute(resources.GetCommandList(), error))
            {
                outLog += "[2/4] Execute 실패: " + error + "\n";
                passed = false;
            }
            graphStats = graph.GetStats();
        }

        if (!resources.EndFrame(error))
        {
            outLog += "[2/4] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }
    }

    if (passed)
    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 그래프 — 선언 %u · 컬링 %u · 실행 %u · 반해상도 %ux%u\n",
            graphStats.passesDeclared, graphStats.passesCulled,
            graphStats.passesExecuted, halfWidth, halfHeight);
        outLog += line;

        // 실행 수가 선언 수보다 적으면 무언가 걷어내진 것이다. 여기서
        // 걷어내질 것은 없다 — 씬·AO·필터·리드백 넷이 사슬로 이어져 있다.
        if (graphStats.passesExecuted < 4)
        {
            outLog += "패스가 걷어내졌다 — 결과를 읽는 사슬이 끊겼다\n";
            passed = false;
        }
    }

    // ── 단정 ──
    std::vector<float> rawAO;
    std::vector<float> filteredAO;

    if (passed)
    {
        const auto read = [&](ID3D12Resource* buffer, std::vector<float>& out) -> bool
        {
            void* mapped = nullptr;
            D3D12_RANGE range{ 0, static_cast<SIZE_T>(rowPitch * halfHeight) };
            if (FAILED(buffer->Map(0, &range, &mapped))) return false;

            out.resize(static_cast<size_t>(halfWidth) * halfHeight);
            for (uint32_t y = 0; y < halfHeight; ++y)
            {
                const auto* row = reinterpret_cast<const uint16_t*>(
                    static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * rowPitch);
                for (uint32_t x = 0; x < halfWidth; ++x)
                {
                    out[static_cast<size_t>(y) * halfWidth + x] = SsaoHalfToFloat(row[x * 2]);
                }
            }
            buffer->Unmap(0, nullptr);
            return true;
        };

        if (!read(rawReadback.Get(), rawAO) || !read(filteredReadback.Get(), filteredAO))
        {
            outLog += "[3/4] 리드백 Map 실패\n";
            passed = false;
        }
    }

    if (passed)
    {
        // 표본 자리. 계단은 화면 가운데(x = halfWidth/2)에 있다.
        //   평평한 곳 — 계단에서 충분히 먼 왼쪽 1/8 지점
        //   계단 안쪽 — 계단 바로 왼쪽(뒤로 물러난 벽 쪽이 가려진다)
        const uint32_t sampleY = halfHeight / 2;
        const uint32_t flatX = halfWidth / 8;
        const uint32_t edgeX = halfWidth / 2 - 2;

        const auto at = [&](const std::vector<float>& v, uint32_t x)
        {
            return v[static_cast<size_t>(sampleY) * halfWidth + x];
        };

        const float flatAO = at(filteredAO, flatX);
        const float edgeAO = at(filteredAO, edgeX);

        // 필터가 실제로 무언가 했는지. 이웃 차이의 평균이 줄었는지로 본다 —
        // 표준편차는 그림의 구조(계단)까지 포함해서 필터 효과를 가린다.
        const auto neighbourDiff = [&](const std::vector<float>& v)
        {
            double sum = 0.0;
            uint32_t count = 0;
            for (uint32_t y = 0; y < halfHeight; ++y)
            {
                for (uint32_t x = 1; x < halfWidth; ++x)
                {
                    sum += std::fabs(at(v, x) - at(v, x - 1));
                    ++count;
                }
                (void)y;
                break;   // 한 줄이면 충분하다 — 배치가 x축으로만 변한다
            }
            return (0 == count) ? 0.0 : sum / count;
        };

        const double rawDiff = neighbourDiff(rawAO);
        const double filteredDiff = neighbourDiff(filteredAO);

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[3/4] AO — 평평한 곳 %.3f · 계단 안쪽 %.3f\n",
            flatAO, edgeAO);
        outLog += line;

        std::snprintf(line, sizeof(line),
            "[4/4] 필터 — 이웃 차이 %.5f → %.5f (%.1f%% 감소)\n",
            rawDiff, filteredDiff,
            (rawDiff > 1e-9) ? (100.0 * (1.0 - filteredDiff / rawDiff)) : 0.0);
        outLog += line;

        // 평평한 곳이 어두우면 자기 자신을 가림막으로 세고 있다.
        if (flatAO < 0.9f)
        {
            outLog += "평평한 곳이 어둡다 — 자기 자신을 가림막으로 세고 있다\n";
            passed = false;
        }

        // 계단이 밝으면 AO가 아무 일도 안 한 것이다.
        if (edgeAO > flatAO - 0.05f)
        {
            outLog += "계단 안쪽이 평평한 곳과 같다 — AO가 가림을 못 찾는다\n";
            passed = false;
        }

        // 필터가 아무것도 안 했으면 통과시키면 안 된다. 그 상태로 두면
        // 잡음이 그대로 화면에 남는데, 결과만 봐서는 필터 탓인지 AO 탓인지
        // 구분되지 않는다.
        if (rawDiff > 1e-9 && filteredDiff >= rawDiff)
        {
            outLog += "필터가 이웃 차이를 줄이지 못했다 — 필터가 죽었다\n";
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

    ssao.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "SSAO 검증 통과\n" : "SSAO 검증 실패\n";
    return passed;
}

#endif
