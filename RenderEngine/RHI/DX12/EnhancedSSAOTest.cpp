#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSAOPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../RHIEncoder.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <vector>
#include "DX12ShaderCompiler.h"

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
    constexpr const char* kSceneShaderFile = "SelfTest/SsaoScene.hlsl";

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
    if (!psoManager.Initialize(&resources, L"dx12_ssao.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
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
    // 표에 빌려준다 — desc가 핸들을 받으므로(V2-b). 소유는 위 ComPtr이 든다.
    RHITextureHandle depthHandle;
    RHITextureHandle normalHandle;
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
        depthHandle = resources.RegisterExternalTexture(depth.Get());

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
        normalHandle = resources.RegisterExternalTexture(normal.Get());
    }

    // 씬 생성 PSO
    RHIPipelineHandle scenePSO;
        {
        const RHIPipelineLayoutParam params[] = {
            RHILayout::Cbv(0),
            RHILayout::UavTable(2, 0),
        };

        RHIPipelineLayoutDesc rootDesc{};
        rootDesc.params = params;

        const auto root = rootSignatures.GetOrCreate(rootDesc, error);
        if (!root.IsValid())
        {
            outLog += "[2/4] 씬 루트 시그니처 실패: " + error + "\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }

        RHIShaderBlob blob;
        ComPtr<ID3DBlob> errors;
        if (!DX12ShaderCompiler::CompileFile(kSceneShaderFile, "CSMain", "cs_5_0", blob, error))
        {
            outLog += "[2/4] 씬 셰이더 컴파일 실패: " + error + "\n";
            ssao.Shutdown();
            resources.Shutdown();
            return false;
        }

        RHIComputePipelineDesc desc{};
        desc.csBytecode = blob.Data();
        desc.csSize = blob.Size();
        desc.layout = root;

        scenePSO = psoManager.GetOrCreateCompute(desc, error);
        if (!scenePSO.IsValid())
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

    // 필터 전후를 둘 다 가져온다. 같으면 필터가 죽은 것이고, 그것은
    // 최종 그림만 봐서는 알 수 없다.
    //
    // 장 둘을 한 리드백에 담는다(R2c-b2). 예전에는 리드백 버퍼를 둘 만들고
    // 행 간격을 손으로 정렬했다 — 그 산술이 파일마다 있었다.
    RHIReadback readback{};
    {
        std::string error;
        if (!resources.CreateReadback(halfWidth, halfHeight,
            EnhancedSSAOPass::kAOFormat, 2, readback, error))
        {
            outLog += "[2/4] 리드백 생성 실패: " + error + "\n";
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
        EnhancedRenderGraph graph(resources);

        EnhancedSSAOPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RHIResourceState::UnorderedAccess, "SSAO.TestDepth");
        inputs.normal = graph.ImportTexture(normal.Get(),
            RHIResourceState::UnorderedAccess, "SSAO.TestNormal");

        // 씬을 먼저 그린다. 그래프가 '아직 아무도 안 쓴 것을 읽는다'를
        // 컴파일에서 잡으므로, 이 패스가 없으면 SSAO 선언 자체가 거절된다.
        graph.AddPass("SSAO.TestScene",
            { { inputs.depth, RHIResourceState::UnorderedAccess },
              { inputs.normal, RHIResourceState::UnorderedAccess } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                RHIEncoder& encoder = *executeContext.encoder;

                SceneParams params{};
                params.sizeX = kTestWidth;
                params.sizeY = kTestHeight;
                params.nearZ = kNearZ;
                params.farZ = kFarZ;
                params.leftViewZ = kLeftViewZ;
                params.rightViewZ = kRightViewZ;

                const auto cb = resources.UploadConstants(
                    &params, sizeof(SceneParams));
                if (!cb.IsValid()) return;
                // 링에서 직접 자르고 뷰를 손으로 만들던 것을 CreateBindings로
                // 바꿨다(R2a). 힙 바인딩은 인코더가 스스로 한다(R4-1c).
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(depthHandle, RHIFormat::R32Float),
                    RHIBindingDesc::Uav2D(normalHandle, RHIFormat::RGBA16Float),
                };
                const RHIBindingTable uavTable = resources.CreateBindings(uavs);
                if (!uavTable.IsValid()) return;

                encoder.SetPipeline(RHIBindPoint::Compute, scenePSO);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                encoder.SetBindings(RHIBindPoint::Compute, 1, uavTable);
                encoder.Dispatch((kTestWidth + 7) / 8, (kTestHeight + 7) / 8, 1);
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
                { { rawHandle, RHIResourceState::CopySource },
                  { outHandle, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback( readback,
                        executeContext.ResolveHandle(rawHandle), 0);
                    executeContext.encoder->CopyToReadback( readback,
                        executeContext.ResolveHandle(outHandle), 1);
                }, true);

            if (!graph.Compile(error))
            {
                outLog += "[2/4] Compile 실패: " + error + "\n";
                passed = false;
            }
            if (passed && !graph.Execute(error))
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
    //
    // Map·행 간격 산술·half 디코드를 손으로 하던 것을 걷었다(R2c-b2).
    // RHIReadbackImage가 포맷을 알고 있으므로 At(x, y, channel, slice)면 된다.
    RHIReadbackImage captured{};

    if (passed)
    {
        std::string error;
        if (!resources.MapReadback(readback, captured, error))
        {
            outLog += "[3/4] 리드백 Map 실패: " + error + "\n";
            passed = false;
        }
    }

    const auto rawAt = [&](uint32_t x, uint32_t y) { return captured.At(x, y, 0, 0); };
    const auto filteredAt = [&](uint32_t x, uint32_t y) { return captured.At(x, y, 0, 1); };

    if (passed)
    {
        // 표본 자리. 계단은 화면 가운데(x = halfWidth/2)에 있다.
        //   평평한 곳 — 계단에서 충분히 먼 왼쪽 1/8 지점
        //   계단 안쪽 — 계단 바로 왼쪽(뒤로 물러난 벽 쪽이 가려진다)
        const uint32_t sampleY = halfHeight / 2;
        const uint32_t flatX = halfWidth / 8;
        const uint32_t edgeX = halfWidth / 2 - 2;

        const float flatAO = filteredAt(flatX, sampleY);
        const float edgeAO = filteredAt(edgeX, sampleY);

        // 필터가 실제로 무언가 했는지. 이웃 차이의 평균이 줄었는지로 본다 —
        // 표준편차는 그림의 구조(계단)까지 포함해서 필터 효과를 가린다.
        //
        // 한 줄이면 충분하다 — 배치가 x축으로만 변한다.
        const auto neighbourDiff = [&](auto&& sample)
        {
            double sum = 0.0;
            uint32_t count = 0;
            for (uint32_t x = 1; x < halfWidth; ++x)
            {
                sum += std::fabs(sample(x, sampleY) - sample(x - 1, sampleY));
                ++count;
            }
            return (0 == count) ? 0.0 : sum / count;
        };

        const double rawDiff = neighbourDiff(rawAt);
        const double filteredDiff = neighbourDiff(filteredAt);

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
