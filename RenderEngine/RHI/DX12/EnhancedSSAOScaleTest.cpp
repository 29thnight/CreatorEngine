#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSSAOPass.h"
#include "DX12DeviceResources.h"
#include "DX12GpuProfiler.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "RHIEncoder.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cstdio>
#include <vector>
#include "DX12ShaderCompiler.h"

#pragma comment(lib, "d3dcompiler.lib")

// SSAO 신구 시간 비교 (PHASE 3-6, 3단계).
//
// ── 무엇과 무엇을 재는가 ──
//
// 실제 DX11 SSAO 패스와 직접 재지 않는다. 그러면 API 차이·드라이버 차이·
// 프레임 구조 차이가 전부 수에 섞여, 빨라진 것이 알고리즘 덕인지 DX12 덕인지
// 구분할 수 없다.
//
// 대신 같은 디바이스·같은 입력·같은 하네스 위에 옛 알고리즘(반구 커널 64개,
// 표본마다 클립 투영 + 깊이 역투영)을 그대로 올려 두고 그것과 잰다.
// 갈리는 것은 알고리즘 하나뿐이다 — Forward+에서 쓴 것과 같은 방법이다.
//
// 참조 경로는 전 해상도로 돈다. 반해상도는 새 방식이 가져가는 이득의
// 일부라, 기준선에까지 적용하면 그 이득이 수에서 사라진다.
//
// ── 측정 규율 ──
//
//   · 워밍업 프레임을 버린다(첫 프레임은 PSO·힙 준비가 섞인다)
//   · 여러 프레임의 중앙값을 쓴다(평균은 한 번의 튐에 끌려간다)
//   · 해상도를 늘려 가며 잰다 — 한 점에서만 재면 '이 해상도에서만 그렇다'와
//     구분되지 않는다
namespace
{
    constexpr uint32_t kSsaoWarmupFrames = 3;
    constexpr uint32_t kSsaoMeasureFrames = 9;

    // 결과를 살려 두려고 옮기는 텍셀 수. 내용은 안 본다.
    constexpr uint32_t kSsaoKeepAliveTexels = 8;

    // 검사용 깊이·노멀. 자가 검증과 같은 계단 배치를 쓴다 — 배치가 다르면
    // 표본이 유효한 비율이 달라져 두 검증의 수를 나란히 놓을 수 없다.
    constexpr const char* kScaleSceneShaderFile = "SelfTest/SsaoScaleScene.hlsl";

    struct ScaleSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        float    nearZ{ 0.f };
        float    farZ{ 0.f };
        float    leftViewZ{ 0.f };
        float    rightViewZ{ 0.f };
        uint32_t pad[2]{};
    };

    double SsaoMedian(std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }
}

bool EnhancedSceneRenderer::RunSSAOScaleTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── SSAO 신구 시간 비교 (PHASE 3-6) ──\n";

    struct Resolution { uint32_t width; uint32_t height; };
    const Resolution kResolutions[] = {
        {  640,  360 },
        { 1280,  720 },
        { 1920, 1080 },
    };

    bool passed = true;

    struct Point
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        double   newMs{ 0.0 };
        double   referenceMs{ 0.0 };
    };
    std::vector<Point> points;

    for (const Resolution& resolution : kResolutions)
    {
        if (!passed) break;

        std::string error;

        DX12DeviceResources resources;
        if (!resources.Initialize(resolution.width, resolution.height, error))
        {
            outLog += "DX12 초기화 실패: " + error + "\n";
            return false;
        }

        DX12PSOManager psoManager;
        DX12RootSignatureCache rootSignatures;
        DX12GpuProfiler profiler;
        if (!psoManager.Initialize(&resources, L"dx12_ssao.cache", error) ||
            !rootSignatures.Initialize(&resources, error) ||
            !profiler.Initialize(resources.GetDevice(), resources.GetCommandQueue(),
                16, DX12DeviceResources::kFrameCount, error))
        {
            outLog += "초기화 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        constexpr float kNearZ = 0.1f;
        constexpr float kFarZ = 100.f;

        FrameCameraSnapshot camera{};
        camera.view = XMMatrixIdentity();
        camera.projection = XMMatrixPerspectiveFovLH(XM_PIDIV2,
            static_cast<float>(resolution.width) / static_cast<float>(resolution.height),
            kNearZ, kFarZ);
        camera.inverseView = XMMatrixIdentity();
        camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);

        EnhancedFrameContext frameContext{};
        frameContext.resources = &resources;
        frameContext.psoManager = &psoManager;
        frameContext.rootSignatures = &rootSignatures;
        frameContext.width = resolution.width;
        frameContext.height = resolution.height;
        frameContext.camera = &camera;

        EnhancedSSAOPass ssao;
        if (!ssao.Initialize(frameContext, error))
        {
            outLog += "SSAO 초기화 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        // ── 입력 ──
        ComPtr<ID3D12Resource> depth;
        ComPtr<ID3D12Resource> normal;
        RHITextureHandle depthHandle;
        RHITextureHandle normalHandle;
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = resolution.width;
            desc.Height = resolution.height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_R32_FLOAT;
            desc.SampleDesc.Count = 1;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr, IID_PPV_ARGS(&depth))))
            {
                outLog += "깊이 생성 실패\n";
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
                outLog += "노멀 생성 실패\n";
                ssao.Shutdown();
                resources.Shutdown();
                return false;
            }
        normalHandle = resources.RegisterExternalTexture(normal.Get());
        }

        RHIPipelineHandle scenePSO;
                {
            const RHIPipelineLayoutParam params[] = {
                RHILayout::Cbv(0),
                RHILayout::UavTable(2, 0),
            };

            RHIPipelineLayoutDesc rootDesc{};
            rootDesc.params = params;

            const auto root = rootSignatures.GetOrCreate(rootDesc, error);
            RHIShaderBlob blob;
            ComPtr<ID3DBlob> errors;
            if (!root.IsValid() ||
                !DX12ShaderCompiler::CompileFile(kScaleSceneShaderFile, "CSMain", "cs_5_0",
                    blob, error))
            {
                outLog += "씬 셰이더 준비 실패: " + error + "\n";
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
                outLog += "씬 PSO 생성 실패: " + error + "\n";
                ssao.Shutdown();
                resources.Shutdown();
                return false;
            }
        }

        // 결과를 살려 두기 위한 목적지. 내용은 안 본다 — 소비자가 있다는
        // 사실만으로 그래프가 SSAO를 걷어내지 않는다.
        RHIReadback keepAlive{};
        {
            std::string readbackError;
            if (!resources.CreateReadback(kSsaoKeepAliveTexels, 1,
                EnhancedSSAOPass::kAOFormat, 1, keepAlive, readbackError))
            {
                outLog += "유지용 버퍼 생성 실패: " + readbackError + "\n";
                ssao.Shutdown();
                resources.Shutdown();
                return false;
            }
        }

        uint32_t frameIndex = 0;
        bool sceneFilled = false;

        // 한 프레임을 그리고 AO 패스의 GPU 시간을 돌려준다. 두 경로가 이
        // 함수 하나를 쓴다 — 측정 절차가 갈리면 그 차이가 결과에 섞인다.
        const auto renderFrame = [&](bool reference, double& outMs) -> bool
        {
            outMs = 0.0;
            ssao.SetUseReferencePath(reference);
            ssao.SetFrameIndex(frameIndex);

            if (!resources.BeginFrame(error)) return false;
            profiler.BeginFrame(frameIndex % DX12DeviceResources::kFrameCount);
            ++frameIndex;

            if (!ssao.PrepareFrame(frameContext, error)) return false;

            EnhancedRenderGraph graph(resources);
            graph.SetProfiler(&profiler);

            // ★ 임포트 상태는 지난 프레임이 남긴 것이어야 한다.
            //
            // 그래프는 임포트한 리소스를 끝에서 되돌려 놓지 않는다. 첫 프레임은
            // 씬 채우기가 UAV로 쓰고 SSAO가 SRV로 읽으므로 끝 상태가 SRV인데,
            // 다음 프레임에 UAV라고 선언하면 배리어의 before가 어긋난다.
            // 처음에 늘 UnorderedAccess로 적었다가 검증 레이어 46건을 봤다.
            const RHIResourceState importState = sceneFilled
                ? RHIResourceState::ShaderResource : RHIResourceState::UnorderedAccess;

            EnhancedSSAOPass::Inputs inputs{};
            inputs.depth = graph.ImportTexture(depth.Get(), importState, "SSAO.ScaleDepth");
            inputs.normal = graph.ImportTexture(normal.Get(), importState, "SSAO.ScaleNormal");

            // 씬은 첫 프레임에만 채운다. 매 프레임 다시 채우면 그 시간이
            // 프레임에 섞이는데, 재려는 것은 AO 패스 하나다.
            if (!sceneFilled)
            {
                graph.AddPass("SSAO.ScaleScene",
                    { { inputs.depth, RHIResourceState::UnorderedAccess },
                      { inputs.normal, RHIResourceState::UnorderedAccess } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {

                        ScaleSceneParams params{};
                        params.sizeX = resolution.width;
                        params.sizeY = resolution.height;
                        params.nearZ = kNearZ;
                        params.farZ = kFarZ;
                        params.leftViewZ = 1.0f;
                        params.rightViewZ = 0.6f;

                        const auto cb = resources.UploadConstants(
                            &params, sizeof(ScaleSceneParams));
                        if (!cb.IsValid()) return;
                        // 링에서 직접 자르고 뷰를 손으로 만들던 것을
                        // CreateBindings로 바꿨다(R2a). 힙 바인딩은 인코더가
                        // 스스로 한다(R4-1c).
                        const RHIBindingDesc uavs[] = {
                            RHIBindingDesc::Uav2D(depthHandle, RHIFormat::R32Float),
                            RHIBindingDesc::Uav2D(normalHandle,
                                RHIFormat::RGBA16Float),
                        };
                        const RHIBindingTable uavTable = resources.CreateBindings(uavs);
                        if (!uavTable.IsValid()) return;

                        RHIEncoder& encoder = *executeContext.encoder;
                        encoder.SetPipeline(RHIBindPoint::Compute, scenePSO);
                        encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                        encoder.SetBindings(RHIBindPoint::Compute, 1, uavTable);
                        encoder.Dispatch((resolution.width + 7) / 8,
                            (resolution.height + 7) / 8, 1);
                    });
            }

            ssao.SetInputs(inputs);
            ssao.Declare(graph, frameContext);

            // ★ 결과를 읽는 패스를 반드시 둔다.
            //
            // 처음에 빼먹었더니 세 해상도 모두 0.000 ms가 나왔다. 아무도
            // 결과를 안 쓰면 그래프가 SSAO를 통째로 걷어내고, 그러면 시간이
            // 0인 것이 '빠른 것'처럼 보인다. 자가 검증에는 같은 이유로
            // 넣어 뒀으면서 여기에는 안 넣었다.
            const RGHandle aoHandle = ssao.GetOutput();
            if (!aoHandle.IsValid()) return false;

            graph.AddPass("SSAO.ScaleKeepAlive",
                { { aoHandle, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    // 한 줄만 가져온다. 살려 두는 것이 목적이라 전부 옮길
                    // 이유가 없고, 복사 시간이 측정에 섞이지도 않는다
                    // (재는 것은 SSAO.Compute 하나다).
                    executeContext.encoder->CopyPartialToReadback(
                        keepAlive, executeContext.ResolveHandle(aoHandle));
                }, true);

            if (!graph.Compile(error)) return false;
            if (!graph.Execute(error)) return false;

            profiler.ResolveFrame(resources.GetCommandList());

            if (!resources.EndFrame(error)) return false;
            resources.WaitForGpu();
            sceneFilled = true;

            std::vector<DX12GpuProfiler::PassTiming> timings;
            std::string collectError;
            if (!profiler.Collect(timings, collectError)) return false;

            for (const auto& timing : timings)
            {
                if (timing.name.find("SSAO.Compute") != std::string::npos)
                {
                    outMs = timing.milliseconds;
                }
            }
            return true;
        };

        std::vector<double> newSamples;
        std::vector<double> refSamples;

        for (uint32_t i = 0; i < kSsaoWarmupFrames + kSsaoMeasureFrames; ++i)
        {
            double newMs = 0.0;
            double refMs = 0.0;

            if (!renderFrame(false, newMs) || !renderFrame(true, refMs))
            {
                outLog += "프레임 실패: " + error + "\n";
                passed = false;
                break;
            }
            if (i >= kSsaoWarmupFrames)
            {
                newSamples.push_back(newMs);
                refSamples.push_back(refMs);
            }
        }

        std::string validation;
        const uint32_t problems = resources.DrainDebugMessages(validation);
        if (0 != problems)
        {
            passed = false;
            outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
        }

        if (passed)
        {
            Point point{};
            point.width = resolution.width;
            point.height = resolution.height;
            point.newMs = SsaoMedian(newSamples);
            point.referenceMs = SsaoMedian(refSamples);
            points.push_back(point);

            char line[224]{};
            std::snprintf(line, sizeof(line),
                "  %4ux%-4u — 신규(반해상도) %6.3f ms · 참조(커널 64, 전 해상도) %7.3f ms"
                " · %5.1f배\n",
                point.width, point.height, point.newMs, point.referenceMs,
                (point.newMs > 1e-6) ? (point.referenceMs / point.newMs) : 0.0);
            outLog += line;
        }

        ssao.Shutdown();
        profiler.Shutdown();
        rootSignatures.Shutdown();
        psoManager.Shutdown();
        resources.Shutdown();
    }

    // ── 측정이 뜻이 있었는지 ──
    //
    // 참조 경로가 해상도에 따라 실제로 느려져야 한다. 전 구간이 평평하면
    // 셰이더가 표본을 안 돌거나 측정이 잡음에 묻힌 것이고, 그러면 배수도
    // 전부 무의미하다.
    if (passed)
    {
        // ★ 0부터 잡는다.
        //
        // 처음에는 '해상도에 반응하는가'만 봤는데, 세 해상도가 전부 0.000일 때
        // last < first * 2.0이 0 < 0이라 거짓이 되어 그대로 통과했다.
        // 아무것도 안 돈 것이 '빠른 것'으로 읽히는 것이 이 검증에서 가장
        // 위험한 실패라, 그것을 맨 앞에 둔다.
        for (const Point& point : points)
        {
            if (point.newMs < 1e-6 || point.referenceMs < 1e-6)
            {
                outLog += "측정값이 0이다 — 패스가 걷어내졌거나 타임스탬프가 "
                          "안 붙었다\n";
                passed = false;
                break;
            }
        }
    }

    if (passed && points.size() >= 2)
    {
        const double first = points.front().referenceMs;
        const double last = points.back().referenceMs;
        if (last < first * 2.0)
        {
            outLog += "참조 경로가 해상도에 반응하지 않는다 — 표본을 안 돌거나 "
                      "측정이 잡음에 묻혔다\n";
            passed = false;
        }
    }

    outLog += passed ? "SSAO 시간 비교 완료\n" : "SSAO 시간 비교 실패\n";
    return passed;
}

#endif
