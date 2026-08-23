#include "Render/Passes/PostProcess/EnhancedPostChainPass.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12GpuProfiler.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "../DX12TestTextureRegistration.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "RHI/RHIEncoder.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "RHI/RHIShaderCompiler.h"

#pragma comment(lib, "d3dcompiler.lib")

// 포스트 체인 신구 시간 비교 (PHASE 3-6, 2단계).
//
// ── 무엇과 무엇을 재는가 ──
//
// 합친 패스(Uber) 하나와, 그것을 옛 방식대로 넷으로 나눈 것을 잰다.
//
// 실제 DX11 체인과 직접 재지 않는다. 그러면 API 차이·드라이버 차이·프레임
// 구조 차이가 전부 수에 섞여, 빨라진 것이 '합쳤기 때문'인지 'DX12이기
// 때문'인지 구분할 수 없다.
//
// ★ 참조 경로는 같은 셰이더를 쓴다. 플래그만 하나씩 켜서 네 번 돌린다 —
//   갈리는 것이 화면 왕복 횟수 하나뿐이라야 나온 수가 무엇을 뜻하는지
//   분명해진다. 셰이더를 따로 쓰면 그 안의 차이까지 수에 섞인다.
//
// 블룸 체인과 FXAA는 양쪽이 똑같이 지나가므로 비교에서 뺀다. 재는 것은
// '픽셀 국소 연산 넷을 합친 것이 값을 하는가' 하나다.
namespace
{
    constexpr uint32_t kPostWarmupFrames = 3;
    constexpr uint32_t kPostMeasureFrames = 9;

    // 결과를 살려 두려고 옮기는 텍셀 수. 내용은 안 본다.
    constexpr uint32_t kPostKeepAliveTexels = 8;

    // 검사 입력. 자가 검증과 같은 배치를 쓴다 — 배치가 다르면 분기가
    // 달리 타서 두 검증의 수를 나란히 놓을 수 없다.
    constexpr const char* kPostScaleSceneShaderFile = "SelfTest/PostChainScaleScene.hlsl";

    struct PostScaleSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        uint32_t pad[2]{};
    };

    double PostMedian(std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }
}

bool EnhancedSceneRenderer::RunPostChainScaleTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 포스트 체인 신구 시간 비교 (PHASE 3-6) ──\n";

    struct Resolution { uint32_t width; uint32_t height; };
    const Resolution kResolutions[] = {
        {  640,  360 },
        { 1280,  720 },
        { 1920, 1080 },
    };

    struct Point
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        double   uberMs{ 0.0 };
        double   separateMs{ 0.0 };
    };
    std::vector<Point> points;

    bool passed = true;

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
        if (!psoManager.Initialize(&resources, L"dx12_post.cache", error) ||
            !rootSignatures.Initialize(&resources, error) ||
            !profiler.Initialize(resources.GetDevice(), resources.GetCommandQueue(),
                48, DX12DeviceResources::kFrameCount, error))
        {
            outLog += "초기화 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        EnhancedFrameContext frameContext{};
        frameContext.resources = &resources;
        frameContext.psoManager = &psoManager;
        frameContext.rootSignatures = &rootSignatures;
        frameContext.width = resolution.width;
        frameContext.height = resolution.height;

        EnhancedPostChainPass post;
        if (!post.Initialize(frameContext, error))
        {
            outLog += "포스트 체인 초기화 실패: " + error + "\n";
            resources.Shutdown();
            return false;
        }

        ComPtr<ID3D12Resource> hdr;
        DX12TestTextureRegistration hdrHandleTable;
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;

            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = resolution.width;
            desc.Height = resolution.height;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = ToDXGI(EnhancedPostChainPass::kHDRFormat);
            desc.SampleDesc.Count = 1;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr, IID_PPV_ARGS(&hdr))))
            {
                outLog += "HDR 생성 실패\n";
                post.Shutdown();
                resources.Shutdown();
                return false;
            }
        hdrHandleTable.Register(resources, hdr.Get());
        }

        if (!hdrHandleTable.IsValid())
        {
            outLog += "HDR 핸들 등록 실패\n";
            resources.Shutdown();
            passed = false;
            break;
        }

        RHIPipelineHandle scenePSO;
                {
            const RHIPipelineLayoutParam params[] = {
                RHILayout::Cbv(0),
                RHILayout::UavTable(1, 0),
            };

            RHIPipelineLayoutDesc rootDesc{};
            rootDesc.params = params;

            const auto root = rootSignatures.GetOrCreate(rootDesc, error);
            RHIShaderBlob blob;
            ComPtr<ID3DBlob> errors;
            if (!root.IsValid() ||
            !RHIShaderCompiler::CompileFile(kPostScaleSceneShaderFile, "CSMain", "cs_5_0",
                    blob, error))
            {
                outLog += "씬 셰이더 준비 실패: " + error + "\n";
                post.Shutdown();
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
                post.Shutdown();
                resources.Shutdown();
                return false;
            }
        }

        // 결과를 살려 두는 목적지. 없으면 그래프가 체인을 통째로 걷어내고
        // '0 ms'가 '빠른 것'으로 읽힌다 — SSAO 스케일에서 그렇게 물렸다.
        RHIReadback keepAlive{};
        {
            std::string readbackError;
            if (!resources.CreateReadback(kPostKeepAliveTexels, 1,
                EnhancedPostChainPass::kLDRFormat, 1, keepAlive, readbackError))
            {
                outLog += "유지용 버퍼 생성 실패: " + readbackError + "\n";
                post.Shutdown();
                resources.Shutdown();
                return false;
            }
        }

        uint32_t frameIndex = 0;
        bool sceneFilled = false;

        // 한 프레임을 그리고 '픽셀 국소 연산' 부분의 GPU 시간을 돌려준다.
        // 두 경로가 이 함수 하나를 쓴다 — 측정 절차가 갈리면 그 차이가
        // 결과에 섞인다.
        const auto renderFrame = [&](bool separate, double& outMs) -> bool
        {
            outMs = 0.0;
            post.SetUseSeparatePasses(separate);

            if (!resources.BeginFrame(error)) return false;
            profiler.BeginFrame(frameIndex % DX12DeviceResources::kFrameCount);
            ++frameIndex;

            if (!post.PrepareFrame(frameContext, error)) return false;

            EnhancedRenderGraph graph(resources);
            graph.SetProfiler(&profiler);

            // ★ 임포트 상태는 지난 프레임이 남긴 것이어야 한다. 그래프는
            //   임포트 리소스를 끝에서 되돌리지 않는다(SSAO·포스트 자가
            //   검증에서 각각 한 번씩 물린 자리다).
            const RHIResourceState importState = sceneFilled
                ? RHIResourceState::ShaderResource : RHIResourceState::UnorderedAccess;

            const RGHandle hdrHandle = graph.ImportTexture(hdrHandleTable.Handle(),
                importState, "PostChain.ScaleHDR");

            if (!sceneFilled)
            {
                graph.AddPass("PostChain.ScaleScene",
                    { { hdrHandle, RHIResourceState::UnorderedAccess } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {

                        PostScaleSceneParams params{};
                        params.sizeX = resolution.width;
                        params.sizeY = resolution.height;

                        const auto cb = resources.UploadConstants(
                            &params, sizeof(PostScaleSceneParams));
                        if (!cb.IsValid()) return;
                        // 링에서 직접 자르고 뷰를 손으로 만들던 것을
                        // CreateBindings로 바꿨다(R2a). 힙 바인딩은 인코더가
                        // 스스로 한다(R4-1c).
                        const RHIBindingDesc uavs[] = {
                            RHIBindingDesc::Uav2D(hdrHandleTable.Handle(),
                                EnhancedPostChainPass::kHDRFormat),
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

            EnhancedPostChainPass::Inputs inputs{};
            inputs.color = hdrHandle;
            post.SetInputs(inputs);
            post.Declare(graph, frameContext);

            const RGHandle output = post.GetOutput();
            if (!output.IsValid()) return false;

            graph.AddPass("PostChain.ScaleKeepAlive",
                { { output, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyPartialToReadback(
                        keepAlive, executeContext.ResolveHandle(output));
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

            // 합친 쪽은 Uber 하나, 나눈 쪽은 Ref로 시작하는 넷의 합이다.
            for (const auto& timing : timings)
            {
                if (timing.name.find("PostChain.Uber") != std::string::npos ||
                    timing.name.find("PostChain.Ref") != std::string::npos)
                {
                    outMs += timing.milliseconds;
                }
            }
            return true;
        };

        std::vector<double> uberSamples;
        std::vector<double> separateSamples;

        for (uint32_t i = 0; i < kPostWarmupFrames + kPostMeasureFrames; ++i)
        {
            double uberMs = 0.0;
            double separateMs = 0.0;

            if (!renderFrame(false, uberMs) || !renderFrame(true, separateMs))
            {
                outLog += "프레임 실패: " + error + "\n";
                passed = false;
                break;
            }
            if (i >= kPostWarmupFrames)
            {
                uberSamples.push_back(uberMs);
                separateSamples.push_back(separateMs);
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
            point.uberMs = PostMedian(uberSamples);
            point.separateMs = PostMedian(separateSamples);
            points.push_back(point);

            char line[224]{};
            std::snprintf(line, sizeof(line),
                "  %4ux%-4u — 합친 것 %6.3f ms · 나눈 것(4패스) %6.3f ms · %4.2f배\n",
                point.width, point.height, point.uberMs, point.separateMs,
                (point.uberMs > 1e-6) ? (point.separateMs / point.uberMs) : 0.0);
            outLog += line;
        }

        post.Shutdown();
        profiler.Shutdown();
        rootSignatures.Shutdown();
        psoManager.Shutdown();
        hdrHandleTable.Reset();
        resources.Shutdown();
    }

    // ── 측정이 뜻이 있었는지 ──
    //
    // 0을 맨 앞에서 잡는다. 아무것도 안 돈 것이 '빠른 것'으로 읽히는 것이
    // 이 부류의 검증에서 가장 위험한 실패다(SSAO 스케일에서 실제로 겪었고,
    // 그때는 '해상도에 반응하는가'만 봐서 0 대 0이 그대로 통과했다).
    if (passed)
    {
        for (const Point& point : points)
        {
            if (point.uberMs < 1e-6 || point.separateMs < 1e-6)
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
        const double first = points.front().separateMs;
        const double last = points.back().separateMs;
        if (last < first * 2.0)
        {
            outLog += "참조 경로가 해상도에 반응하지 않는다 — 측정이 잡음에 묻혔다\n";
            passed = false;
        }
    }

    outLog += passed ? "포스트 체인 시간 비교 완료\n" : "포스트 체인 시간 비교 실패\n";
    return passed;
}
