// SSGI 자가 검증 (E5에서 EnhancedSSGIPass.cpp 말미에서 분리 — 위와 같은 역결합).
#include "Render/Passes/Lighting/EnhancedSSGIPass.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "../DX12TestTextureRegistration.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "RHI/RHIEncoder.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Render/Passes/Lighting/EnhancedSSGIShaders.h"
#include "RHI/RHIShaderCompiler.h"
#include <algorithm>
#include <sstream>
#include <wrl/client.h>

namespace
{
    // EnhancedSSGIPass.cpp의 파일 지역 헬퍼와 같은 래퍼 — 추출하며 함께 왔다.
    // 유니티 병합 대비 이름을 테스트 쪽으로 고유하게 둔다.
    bool CompileSsgiTestShader(const char* file, const RHIShaderDefine* defines,
        RHIShaderBlob& outBlob, std::string& outError)
    {
        return RHIShaderCompiler::CompileFile(file, "CSMain", "cs_5_0", defines,
            outBlob, outError);
    }
}


// ── 자가 검증 ──
//
// 셰이더는 런타임 컴파일이라 C++ 빌드로는 HLSL 오류가 안 잡힌다. 실제로
// 선언하지 않은 샘플러를 쓰는 코드가 빌드를 통과했다 — 부르는 곳이 없으면
// 컴파일 자체가 안 돈다. 그래서 부르는 자리를 만든다.
bool EnhancedSceneRenderer::RunSSGITest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    constexpr uint32_t kWidth = 256;
    constexpr uint32_t kHeight = 256;

    outLog += "── SSGI 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kWidth, kHeight, error))
    {
        outLog += "[1/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_ssgi.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/3] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kWidth;
    frameContext.height = kHeight;

    // ★ 카메라를 넣어야 재투영이 성립한다.
    //
    // 처음에는 width/height만 넣었다. 그러자 리졸브가 이전 프레임 행렬을
    // 못 얻어 히스토리를 통째로 버렸고, 여덟 프레임을 돌려도 누적이 1에
    // 머물렀다(평균 1.00 · 최소 1 · 최대 1). 셰이더도 그래프도 멀쩡한데
    // 검증 쪽 입력이 빠져 있었던 것이다.
    //
    // 카메라를 고정해 둔다 — 정지 상태에서 누적이 쌓이는지가 질문이므로
    // 움직이면 답이 흐려진다.
    FrameCameraSnapshot camera{};
    camera.view = XMMatrixLookAtLH(
        XMVectorSet(0.f, 1.f, -3.f, 1.f),
        XMVectorSet(0.f, 0.f, 0.f, 1.f),
        XMVectorSet(0.f, 1.f, 0.f, 0.f));
    camera.projection = XMMatrixPerspectiveFovLH(
        XM_PIDIV4, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1f, 100.f);
    camera.inverseView = XMMatrixInverse(nullptr, camera.view);
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.f;

    frameContext.camera = &camera;

    // ── [1/3] 셰이더 컴파일과 PSO 생성 ──
    EnhancedSSGIPass ssgi;
    if (!ssgi.Initialize(frameContext, error))
    {
        outLog += "[1/3] SSGI 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/3] 셰이더 컴파일·PSO 생성 통과(Hi-Z·트레이스·리졸브·필터·합성)\n";

    // ── [2/3] 밉 수 산정 ──
    if (!ssgi.PrepareFrame(frameContext, error))
    {
        outLog += "[2/3] PrepareFrame 실패: " + error + "\n";
        ssgi.Shutdown();
        resources.Shutdown();
        return false;
    }

    // 256을 1/2로 줄이면 128이고, 1이 될 때까지 반이면 128→64→…→1로
    // 여덟 단계다. 상한(kMaxHiZMips)에 걸린다.
    const uint32_t expectedMips = EnhancedSSGIPass::kMaxHiZMips;
    const uint32_t giWidth = kWidth / EnhancedSSGIPass::kResolutionDivisor;

    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/3] GI 해상도 %ux%u · Hi-Z 밉 %u(기대 %u)\n",
            giWidth, kHeight / EnhancedSSGIPass::kResolutionDivisor,
            expectedMips, expectedMips);
        outLog += line;
    }

    // ── [3/3] 실제 렌더 ──
    //
    // 깊이를 그래프 밖에서 만들어 임포트한다. 여기서 확인하려는 것은
    // Hi-Z 체인과 트레이스가 도는가이지 GBuffer가 아니다.
    bool passed = true;
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kWidth;
        depthDesc.Height = kHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_R32_FLOAT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        ComPtr<ID3D12Resource> depth;
        DX12TestTextureRegistration depthRegistration;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&depth))))
        {
            outLog += "[3/3] 깊이 텍스처 생성 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        // 노멀도 만든다.
        //
        // 필터가 노멀 가중을 쓰므로 없으면 그 항을 잴 수 없다. 대체물(깊이
        // 텍스처)을 꽂으면 깊이값을 노멀로 해석하게 되고, 그러면
        // filterNormalPower를 스윕해도 무엇을 재는지 알 수 없다.
        D3D12_RESOURCE_DESC normalDesc = depthDesc;
        normalDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

        ComPtr<ID3D12Resource> testNormal;
        DX12TestTextureRegistration normalRegistration;
        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &normalDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&testNormal))))
        {
            outLog += "[3/3] 테스트 노멀 생성 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        depthRegistration.Register(resources, depth.Get());
        normalRegistration.Register(resources, testNormal.Get());
        if (!depthRegistration.IsValid() || !normalRegistration.IsValid())
        {
            outLog += "[3/3] 입력 텍스처 핸들 등록 실패\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        // ★ 깊이를 실제로 채운다.
        //
        // 만들기만 하고 두면 초기화되지 않은 메모리를 깊이로 읽는다. 그
        // 상태로는 히트 비율 같은 숫자가 뜻을 잃고, 무엇을 재는지 모르는 채
        // 상수를 조이게 된다. 바닥 평면과 구 하나를 절차적으로 그린다.
        {
            RHIShaderBlob depthBlob;
            if (!CompileSsgiTestShader(SsgiShaders::kTestDepthFile, nullptr, depthBlob, error))
            {
                outLog += "[3/3] 테스트 깊이 셰이더 컴파일 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            const RHIPipelineLayoutParam depthParams[] = {
                RHILayout::Cbv(0),
                RHILayout::UavTable(2, 0),   // 깊이 + 노멀
            };

            RHIPipelineLayoutDesc depthRootDesc{};
            depthRootDesc.params = depthParams;

            const auto depthRoot = rootSignatures.GetOrCreate(depthRootDesc, error);
            if (!depthRoot.IsValid())
            {
                outLog += "[3/3] 테스트 깊이 루트 시그니처 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            RHIComputePipelineDesc depthPsoDesc{};
            depthPsoDesc.csBytecode = depthBlob.Data();
            depthPsoDesc.csSize = depthBlob.Size();
            depthPsoDesc.layout = depthRoot;

            const RHIPipelineHandle depthPso = psoManager.GetOrCreateCompute(depthPsoDesc, error);
            if (!depthPso.IsValid())
            {
                outLog += "[3/3] 테스트 깊이 PSO 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            if (!resources.BeginFrame(error))
            {
                outLog += "[3/3] 깊이 채우기 BeginFrame 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }

            struct { uint32_t w, h; float nearP, farP; } depthCb{ kWidth, kHeight, 0.1f, 100.f };

            const auto cb = resources.AllocateUpload(
                RHIUploadRequest{ sizeof(depthCb), RHIUploadUsage::ConstantBuffer, 1 });
            const RHIBindingDesc depthUavs[] = {
                RHIBindingDesc::Uav2D(depthRegistration.Handle(),
                    RHIFormat::R32Float),
                RHIBindingDesc::Uav2D(normalRegistration.Handle(),
                    RHIFormat::RGBA16Float),
            };
            const RHIBindingTable uavTable = resources.CreateBindings(depthUavs);
            if (cb.IsValid() && uavTable.IsValid())
            {
                memcpy(cb.cpuAddress, &depthCb, sizeof(depthCb));

                auto* cmd = resources.GetCommandList();
                resources.BindDescriptorHeaps(cmd);
                // ' + chr(0x2605) + ' 이 검사는 인코더를 안 타고 원시 경로를 쓴다 — 그것이 검사의
                //   목적이라 그대로 두고, 핸들만 스스로 푼다.
                const DX12PipelineEntry depthEntry = resources.Resolve(depthPso);
                cmd->SetComputeRootSignature(depthEntry.signature);
                cmd->SetPipelineState(depthEntry.pipeline);
                cmd->SetComputeRootConstantBufferView(0,
                    resources.Resolve(cb.buffer)->GetGPUVirtualAddress() + cb.offset);
                cmd->SetComputeRootDescriptorTable(1, DX12ToGpuHandle(uavTable.backend));
                cmd->Dispatch((kWidth + 7) / 8, (kHeight + 7) / 8, 1);

                // SSGI가 SRV로 읽으므로 전이한다.
                // ★ 여기가 NON_PIXEL 로 전이하면서 그래프에는 ShaderResource
                //   (=ALL)라고 말하고 있었다. 그래프의 첫 usage 도
                //   ShaderResource 라 전이가 안 나와서 드러나지 않던 불일치다 —
                //   배리어가 한 번이라도 나왔으면 before 가 실제와 어긋난다.
                //   중립 어휘로 옮기면서 선언을 참으로 만든다(V3-c).
                const RHITransition both[] = {
                    { depthRegistration.Handle(),
                      RHIResourceState::UnorderedAccess, RHIResourceState::ShaderResource },
                    { normalRegistration.Handle(),
                      RHIResourceState::UnorderedAccess, RHIResourceState::ShaderResource } };
                resources.TransitionResources(both);
            }

            if (!resources.EndFrame(error))
            {
                outLog += "[3/3] 깊이 채우기 EndFrame 실패: " + error + "\n";
                ssgi.Shutdown();
                resources.Shutdown();
                return false;
            }
            resources.WaitForGpu();
        }

        // ★ 결과를 읽는 패스를 붙인다.
        //
        // 없으면 트레이스가 컬링돼 Dispatch가 한 번도 안 돈다. 실제로 첫
        // 실행이 '선언 9 · 실행 0'이었다 — 셰이더 컴파일만 확인하고 넘어갈
        // 뻔했다. 컬링이 도는 것은 옳지만, 도는지 보려면 읽는 쪽이 있어야 한다.
        // ★ 리드백 크기는 읽는 리소스에 맞춘다.
        //
        // 처음에는 GI 해상도(1/2)로 잡았다. 합성이 붙으면서 출력이 전 해상도가
        // 됐는데 리드백은 그대로였고, CopyTextureRegion이 "목적지 경계를\n// 넘는다"로 실패했다. 그 실패가 커맨드 리스트를 무효로 만들어
        // EndFrame의 Close가 E_INVALIDARG를 냈다 — 증상이 나온 자리와
        // 원인이 있는 자리가 달랐다.
        // ★ 리드백 대상은 리졸브 결과다.
        //
        // 합성 결과가 아니라 리졸브를 읽는 이유: a 채널에 누적 프레임 수가
        // 들어 있다. 그것이 '시간축이 샘플 수를 대신한다'는 전제가 실제로
        // 도는지 보여 주는 유일한 숫자다. 합성 결과에는 그 정보가 없다.
        const uint32_t giW = kWidth / EnhancedSSGIPass::kResolutionDivisor;
        const uint32_t giH = kHeight / EnhancedSSGIPass::kResolutionDivisor;
        RHIReadback readback{};
        std::string readbackError;
        if (!resources.CreateReadback(giW, giH, EnhancedSSGIPass::kGIFormat, 1,
            readback, readbackError))
        {
            outLog += "[3/3] 리드백 버퍼 생성 실패\n";
            passed = false;
        }

        std::vector<double> sweepHits;

        // ── 여러 프레임 돌린다 ──
        //
        // 한 프레임만 돌면 누적이 늘 1이고, 그러면 '시간축이 샘플 수를
        // 대신한다'는 이 설계의 전제를 확인할 수 없다. 카메라를 고정한 채
        // 여러 프레임 돌려 누적이 실제로 쌓이는지 본다.
        //
        // 정지 상태에서 쌓이지 않으면 재투영이 어긋난 것이고, 그때는
        // depthTolerance가 너무 빡빡하다는 뜻이다.
        constexpr uint32_t kTestFrames = 8;
        EnhancedRenderGraph::Stats stats{};

        for (uint32_t frameIndex = 0; frameIndex < kTestFrames && passed; ++frameIndex)
        {
        if (!resources.BeginFrame(error))
        {
            outLog += "[3/3] BeginFrame 실패: " + error + "\n";
            ssgi.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!ssgi.PrepareFrame(frameContext, error))
        {
            outLog += "[3/3] PrepareFrame 실패: " + error + "\n";
            passed = false;
            break;
        }

        EnhancedRenderGraph graph(resources);

        EnhancedSSGIPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depthRegistration.Handle(),
            RHIResourceState::ShaderResource, "SSGI.TestDepth");
        inputs.normal = graph.ImportTexture(normalRegistration.Handle(),
            RHIResourceState::ShaderResource, "SSGI.TestNormal");
        ssgi.SetInputs(inputs);

        ssgi.Declare(graph, frameContext);

        const auto output = ssgi.GetOutput();
        if (!output.IsValid())
        {
            outLog += "[3/3] 출력 핸들이 비었다 — Declare가 패스를 선언하지 않았다\n";
            passed = false;
        }

        if (passed && output.IsValid())
        {
            graph.AddPass("SSGI.Readback",
                { { ssgi.GetResolvedResult(), RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback( readback,
                        executeContext.ResolveHandle(ssgi.GetResolvedResult()));
                }, true);
        }

        if (passed && !graph.Compile(error))
        {
            outLog += "[3/3] 그래프 Compile 실패: " + error + "\n";
            passed = false;
        }

        if (passed && !graph.Execute(error))
        {
            outLog += "[3/3] 그래프 Execute 실패: " + error + "\n";
            passed = false;
        }

        stats = graph.GetStats();

        if (!resources.EndFrame(error))
        {
            outLog += "[3/3] EndFrame 실패: " + error + "\n";
            passed = false;
        }
        else
        {
            resources.WaitForGpu();
        }
        }   // 프레임 루프 끝

        {
            char line[224]{};
            std::snprintf(line, sizeof(line),
                "[3/3] 그래프 — 선언 %u · 실행 %u · 컬링 %u · 배리어 %u(%u번에)\n",
                stats.passesDeclared, stats.passesExecuted, stats.passesCulled,
                stats.barriersEmitted, stats.barrierBatches);
            outLog += line;
        }

        // ── 누적이 실제로 쌓이는가 ──
        //
        // 리졸브 결과의 a 채널에 누적 프레임 수가 들어 있다. 카메라를
        // 고정하고 여덟 프레임 돌렸으므로, 재투영이 맞으면 8까지 올라가야
        // 한다. 1에 머물면 매 프레임 히스토리를 버리는 것이고, 그러면
        // 프레임당 슬라이스 둘만 쓰는 셈이라 품질만 나빠진다.
        {
            RHIReadbackImage captured{};
            std::string mapError;
            if (resources.MapReadback(readback, captured, mapError))
            {
                double accumSum = 0.0;
                float  accumMin = 1e9f;
                float  accumMax = 0.f;
                size_t counted = 0;

                // a 채널이 누적 수다. 디코드는 캡처가 한다(R2c-b2).
                for (uint32_t y = 0; y < giH; ++y)
                    for (uint32_t x = 0; x < giW; ++x)
                    {
                        const float value = captured.At(x, y, 3);
                        if (value <= 0.f) continue;   // 하늘은 0이다

                        accumSum += value;
                        accumMin = (std::min)(accumMin, value);
                        accumMax = (std::max)(accumMax, value);
                        ++counted;
                    }

                if (0 != counted)
                {
                    const double average = accumSum / static_cast<double>(counted);

                    char line[256]{};
                    std::snprintf(line, sizeof(line),
                        "[3/3] 누적 — %u프레임 뒤 평균 %.2f · 최소 %.0f · 최대 %.0f"
                        " (픽셀 %zu)\n",
                        kTestFrames, average, accumMin, accumMax, counted);
                    outLog += line;

                    // ★ 정지 상태에서 누적이 안 쌓이면 전제가 무너진 것이다.
                    //   여덟 프레임을 돌았으니 평균이 절반은 넘어야 한다.
                    if (average < kTestFrames * 0.5)
                    {
                        outLog += "누적이 쌓이지 않는다 — 재투영이 매 프레임"
                            " 히스토리를 버리고 있다(depthTolerance를 볼 것)\n";
                        passed = false;
                    }
                }
                else
                {
                    outLog += "[3/3] 누적을 잴 픽셀이 없다 — 전부 하늘로 판정됐다\n";
                    passed = false;
                }
            }
        }

        // ── 상수 스윕 ──
        //
        // 값을 정하기 전에 그 값이 결과를 실제로 바꾸는지부터 본다.
        // 바꿔도 숫자가 그대로면 그 상수는 지금 아무 일도 안 하는 것이고,
        // 그때는 값을 고르는 것이 아니라 배선을 봐야 한다 — 누적에서
        // 이미 그렇게 세 번 틀렸다.
        //
        // 히트 비율은 트레이스 출력의 a 채널이다. 추적 거리와 두께가
        // 그것을 좌우한다.
        {
            struct SweepCase { float distance; float thickness; };
            const SweepCase cases[] = {
                { 2.f,  0.5f }, { 8.f,  0.5f }, { 32.f, 0.5f },
                // 두께 검사가 뷰 공간으로 바뀌었으므로 뷰 단위 눈금으로 본다.
                // 0.05는 얇은 판, 0.5는 사람 몸통, 5는 벽 두께쯤 된다.
                { 8.f,  0.05f }, { 8.f,  5.0f },
            };

            outLog += "[3/3] 추적 스윕 — 거리/두께 → 히트 비율\n";

            for (const SweepCase& sweepCase : cases)
            {
                EnhancedSSGIPass::Tuning tuning = ssgi.GetTuning();
                tuning.traceDistance = sweepCase.distance;
                tuning.traceThickness = sweepCase.thickness;
                ssgi.SetTuning(tuning);

                if (!resources.BeginFrame(error)) break;
                if (!ssgi.PrepareFrame(frameContext, error)) break;

                EnhancedRenderGraph sweepGraph(resources);

                // 깊이는 그래프마다 새로 임포트한다. 핸들은 그래프에 매인
                // 것이라 앞 그래프의 것을 넘겨 쓰면 다른 리소스를 가리킨다.
                EnhancedSSGIPass::Inputs sweepInputs{};
                sweepInputs.depth = sweepGraph.ImportTexture(depthRegistration.Handle(),
                    RHIResourceState::ShaderResource, "SSGI.SweepDepth");
                sweepInputs.normal = sweepGraph.ImportTexture(normalRegistration.Handle(),
                    RHIResourceState::ShaderResource, "SSGI.SweepNormal");
                ssgi.SetInputs(sweepInputs);

                ssgi.Declare(sweepGraph, frameContext);

                sweepGraph.AddPass("SSGI.SweepReadback",
                    { { ssgi.GetTraceResult(), RHIResourceState::CopySource } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {
                        executeContext.encoder->CopyToReadback( readback,
                            executeContext.ResolveHandle(ssgi.GetTraceResult()));
                    }, true);

                if (!sweepGraph.Compile(error)) break;
                if (!sweepGraph.Execute(error)) break;
                if (!resources.EndFrame(error)) break;
                resources.WaitForGpu();

                RHIReadbackImage captured{};
                std::string mapError;
                if (!resources.MapReadback(readback, captured, mapError)) break;

                double hitSum = 0.0;
                size_t counted = 0;

                for (uint32_t y = 0; y < giH; ++y)
                    for (uint32_t x = 0; x < giW; ++x)
                    {
                        hitSum += captured.At(x, y, 3);
                        ++counted;
                    }

                char line[192]{};
                std::snprintf(line, sizeof(line),
                    "        거리 %5.1f · 두께 %4.2f → 히트 %.4f\n",
                    sweepCase.distance, sweepCase.thickness,
                    (0 != counted) ? (hitSum / static_cast<double>(counted)) : 0.0);
                outLog += line;

                sweepHits.push_back((0 != counted)
                    ? (hitSum / static_cast<double>(counted)) : 0.0);
            }

            // ★ 값을 바꿔도 결과가 그대로면 그 상수는 지금 안 쓰이는 것이다.
            if (sweepHits.size() >= 3)
            {
                const double spread = *std::max_element(sweepHits.begin(), sweepHits.end())
                    - *std::min_element(sweepHits.begin(), sweepHits.end());
                if (spread < 1e-4)
                {
                    outLog += "추적 상수를 바꿔도 히트 비율이 그대로다"
                        " — 그 값이 셰이더에 닿지 않는다\n";
                    passed = false;
                }
            }
        }

        // ── 필터 상수 스윕 ──
        //
        // 필터가 하는 일은 노이즈를 줄이는 것이다. 그러니 지표는 분산이다 —
        // 리졸브 결과와 필터 결과의 표준편차를 나란히 보면 실제로 일하는지
        // 알 수 있다. 값을 바꿔도 감소율이 그대로면 그 상수는 안 닿는 것이다.
        //
        // 깊이 시그마: 작을수록 깊이 차이에 민감해져 덜 섞는다.
        // 노멀 지수: 클수록 노멀이 다른 이웃을 강하게 배제한다.
        {
            struct FilterCase { float depthSigma; float normalPower; };
            const FilterCase cases[] = {
                { 0.0001f, 16.f }, { 0.01f, 16.f }, { 1.0f, 16.f },
                { 0.01f,    1.f }, { 0.01f, 64.f },
            };

            outLog += "[3/3] 필터 스윕 — 시그마/지수 → 이웃 차이(리졸브 → 필터)\n";

            std::vector<double> reductions;

            for (const FilterCase& filterCase : cases)
            {
                EnhancedSSGIPass::Tuning tuning = ssgi.GetTuning();
                tuning.filterDepthSigma = filterCase.depthSigma;
                tuning.filterNormalPower = filterCase.normalPower;
                ssgi.SetTuning(tuning);

                double sigmaBefore = 0.0;
                double sigmaAfter = 0.0;

                // 리졸브와 필터를 각각 읽는다. 한 프레임에 둘 다 읽으려면
                // 리드백 버퍼가 둘이어야 하는데, 두 번 도는 편이 단순하다.
                for (uint32_t stage = 0; stage < 2; ++stage)
                {
                    if (!resources.BeginFrame(error)) break;
                    if (!ssgi.PrepareFrame(frameContext, error)) break;

                    EnhancedRenderGraph filterGraph(resources);

                    EnhancedSSGIPass::Inputs filterInputs{};
                    filterInputs.depth = filterGraph.ImportTexture(depthRegistration.Handle(),
                        RHIResourceState::ShaderResource, "SSGI.FilterDepth");
                    filterInputs.normal = filterGraph.ImportTexture(normalRegistration.Handle(),
                        RHIResourceState::ShaderResource, "SSGI.FilterNormal");
                    ssgi.SetInputs(filterInputs);

                    ssgi.Declare(filterGraph, frameContext);

                    const RGHandle target = (0 == stage)
                        ? ssgi.GetResolvedResult() : ssgi.GetOutput();
                    const RGHandle readSource = (0 == stage)
                        ? ssgi.GetResolvedResult() : ssgi.GetFilteredResult();
                    (void)target;

                    filterGraph.AddPass("SSGI.FilterReadback",
                        { { readSource, RHIResourceState::CopySource } },
                        [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                        {
                            executeContext.encoder->CopyToReadback( readback,
                                executeContext.ResolveHandle(readSource));
                        }, true);

                    if (!filterGraph.Compile(error)) break;
                    if (!filterGraph.Execute(error)) break;
                    if (!resources.EndFrame(error)) break;
                    resources.WaitForGpu();

                    RHIReadbackImage captured{};
                    std::string mapError;
                    if (!resources.MapReadback(readback, captured, mapError)) break;

                    // ★ 표준편차가 아니라 '이웃 간 차이'를 잰다.
                    //
                    // 처음에는 전체 표준편차를 썼다. 필터 전후로 0.079422 →
                    // 0.079416, 감소 0.0%가 나왔다. 필터가 죽은 것처럼 보였지만
                    // 지표가 틀린 것이었다 — 표준편차는 공간 구조(밝은 곳과
                    // 어두운 곳의 차이)를 포함하고, 필터는 그것을 지우면 안 된다.
                    //
                    // 노이즈는 이웃 픽셀 사이에서 튀고 구조는 완만하다. 그래서
                    // 오른쪽·아래 이웃과의 차이 평균을 본다. 필터가 일하면
                    // 이 값이 줄고, 구조를 지우는지는 별개로 봐야 한다.
                    double diffSum = 0.0;
                    size_t counted = 0;

                    const auto lumaAt = [&](uint32_t x, uint32_t y) -> double
                    {
                        return 0.2126 * captured.At(x, y, 0)
                            + 0.7152 * captured.At(x, y, 1)
                            + 0.0722 * captured.At(x, y, 2);
                    };

                    for (uint32_t y = 0; y + 1 < giH; ++y)
                    {
                        for (uint32_t x = 0; x + 1 < giW; ++x)
                        {
                            const double here = lumaAt(x, y);
                            diffSum += std::abs(lumaAt(x + 1, y) - here);
                            diffSum += std::abs(lumaAt(x, y + 1) - here);
                            counted += 2;
                        }
                    }

                    if (0 != counted)
                    {
                        const double roughness = diffSum / static_cast<double>(counted);
                        if (0 == stage) sigmaBefore = roughness;
                        else            sigmaAfter = roughness;
                    }
                }

                const double reduction = (sigmaBefore > 1e-9)
                    ? (1.0 - sigmaAfter / sigmaBefore) : 0.0;
                reductions.push_back(reduction);

                char line[224]{};
                std::snprintf(line, sizeof(line),
                    "        시그마 %7.4f · 지수 %5.1f → %.6f → %.6f (감소 %.1f%%)\n",
                    filterCase.depthSigma, filterCase.normalPower,
                    sigmaBefore, sigmaAfter, reduction * 100.0);
                outLog += line;
            }

            // ★ 값을 바꿔도 감소율이 그대로면 그 상수는 안 닿는 것이다.
            if (reductions.size() >= 3)
            {
                const double spread =
                    *std::max_element(reductions.begin(), reductions.end())
                    - *std::min_element(reductions.begin(), reductions.end());

                char line[192]{};
                std::snprintf(line, sizeof(line),
                    "        감소율 폭 %.1f%%p — %s\n",
                    spread * 100.0,
                    (spread < 0.01) ? "상수가 결과를 거의 안 바꾼다"
                                    : "상수가 결과를 바꾼다");
                outLog += line;
            }
        }

        // Hi-Z 밉마다 하나 + 트레이스 + 리졸브 + 필터 + 합성
        // + 히스토리 저장 + 리드백.
        const uint32_t expectedPasses = expectedMips + 6;
        if (stats.passesDeclared != expectedPasses)
        {
            outLog += "선언된 패스가 " + std::to_string(stats.passesDeclared)
                + "개인데 " + std::to_string(expectedPasses) + "개여야 한다\n";
            passed = false;
        }

        // ★ 리드백이 결과를 읽으므로 전부 살아나야 한다.
        //
        // 이 단정이 없던 첫 실행이 '선언 9 · 실행 0'이었다 — 아무도 결과를
        // 안 읽어 전부 컬링됐고, 셰이더 컴파일만 확인한 채 '통과'가 나왔다.
        // 컬링이 도는 것은 옳지만 그 상태로는 Hi-Z 체인과 행진이 실제로
        // 도는지 알 수 없다. 그래서 리드백을 붙이고 실행 수를 단정한다.
        if (stats.passesExecuted != expectedPasses)
        {
            outLog += "실행된 패스가 " + std::to_string(stats.passesExecuted)
                + "개다 — Hi-Z 체인의 의존이 끊겼다\n";
            passed = false;
        }

        // 밉이 앞 밉을 SRV로 읽으므로 UAV→SRV 전이가 밉 수만큼은 나와야 한다.
        // 0이면 배리어 유도가 죽은 것이고, 그러면 GPU가 덜 쓴 것을 읽는다.
        if (stats.barriersEmitted < expectedMips)
        {
            outLog += "배리어가 " + std::to_string(stats.barriersEmitted)
                + "건뿐이다 — UAV→SRV 전이가 빠졌다\n";
            passed = false;
        }
        normalRegistration.Reset();
        depthRegistration.Reset();
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    ssgi.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "SSGI 검증 통과\n" : "SSGI 검증 실패\n";
    return passed;
}
