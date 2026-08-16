#ifndef DYNAMICCPP_EXPORTS
#include "../../../../Render/Passes/PostProcess/EnhancedPostChainPass.h"
#include "../../DX12DeviceResources.h"
#include "../../DX12PSOManager.h"
#include "../../DX12RootSignatureCache.h"
#include "../DX12TestTextureRegistration.h"
#include "../../../../Render/Graph/EnhancedRenderGraph.h"
#include "../../../../Render/Scene/EnhancedSceneRenderer.h"
#include "../../../RHIEncoder.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include "../../../RHIShaderCompiler.h"

#pragma comment(lib, "d3dcompiler.lib")

// 포스트 체인 자가 검증 (PHASE 3-6).
//
// ── 알려진 배치로 단정한다 ──
//
// HDR 입력을 절차적으로 만든다:
//
//   · 화면 대부분  — 중간 회색(0.25). 블룸 임계(1.0) 아래라 안 번진다
//   · 가운데 사각  — 아주 밝은 흰색(8.0). 임계를 크게 넘어 번진다
//   · 오른쪽 절반의 세로 줄무늬 — 한 픽셀 간격 흑백. FXAA가 물 곳이다
//
// 각 단계가 자기 몫을 했는지 따로 본다:
//
//   블룸   — 밝은 사각 바깥(회색 영역)이 원래보다 밝아졌는가
//   톤맵   — 8.0이 1.0 이하로 눌렸는가(안 눌리면 그냥 잘린 것과 구분 안 됨)
//   비네트 — 구석이 중앙보다 어두운가
//   FXAA   — 줄무늬 영역의 이웃 차이가 줄었는가
//
// ★ 한 단계만 단정하면 나머지가 죽어도 통과한다. 넷을 따로 재는 것이
//   이 검증의 요점이다 — 합친 패스(Uber)는 특히 그렇다. 넷이 한 셰이더에
//   들어 있어서 하나가 빠져도 '어딘가 조금 다르다'로만 보인다.
namespace
{
    constexpr uint32_t kPostWidth = 256;
    constexpr uint32_t kPostHeight = 256;

    constexpr const char* kPostSceneShaderFile = "SelfTest/PostChainScene.hlsl";

    struct PostSceneParams
    {
        uint32_t sizeX{ 0 };
        uint32_t sizeY{ 0 };
        uint32_t pad[2]{};
    };
}

bool EnhancedSceneRenderer::RunPostChainTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── 포스트 체인 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kPostWidth, kPostHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    if (!psoManager.Initialize(&resources, L"dx12_post.cache", error) ||
        !rootSignatures.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.width = kPostWidth;
    frameContext.height = kPostHeight;

    EnhancedPostChainPass post;
    if (!post.Initialize(frameContext, error))
    {
        outLog += "[1/4] 포스트 체인 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 셰이더 컴파일·PSO 생성 통과(임계·다운·업·Uber·FXAA)\n";

    // ── 입력 HDR ──
    ComPtr<ID3D12Resource> hdr;
    DX12TestTextureRegistration hdrHandleTable;
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kPostWidth;
        desc.Height = kPostHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = ToDXGI(EnhancedPostChainPass::kHDRFormat);
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, IID_PPV_ARGS(&hdr))))
        {
            outLog += "[2/4] HDR 생성 실패\n";
            post.Shutdown();
            resources.Shutdown();
            return false;
        }
        hdrHandleTable.Register(resources, hdr.Get());
    }

    if (!hdrHandleTable.IsValid())
    {
        outLog += "[2/5] HDR 핸들 등록 실패\n";
        resources.Shutdown();
        return false;
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
            !RHIShaderCompiler::CompileFile(kPostSceneShaderFile, "CSMain", "cs_5_0", blob, error))
        {
            outLog += "[2/4] 씬 셰이더 준비 실패: " + error + "\n";
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
            outLog += "[2/4] 씬 PSO 생성 실패: " + error + "\n";
            post.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    // 최종(LDR) · FXAA 전(LDR) 둘 다 가져온다. FXAA가 죽어 있어도
    // 최종 그림만 봐서는 알 수 없다.
    // 장 둘을 한 리드백에 담는다(R2c-b2). 예전에는 버퍼를 둘 만들고 행 간격을
    // 손으로 정렬했다.
    constexpr uint32_t kPostSliceFinal = 0;
    constexpr uint32_t kPostSlicePreAA = 1;

    RHIReadback readback{};
    {
        std::string readbackError;
        if (!resources.CreateReadback(kPostWidth, kPostHeight,
            EnhancedPostChainPass::kLDRFormat, 2, readback, readbackError))
        {
            outLog += "[2/4] 리드백 생성 실패: " + readbackError + "\n";
            post.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    bool sceneFilled = false;
    EnhancedRenderGraph::Stats graphStats{};

    // ★ 톤매퍼 둘을 각각 한 번씩 그린다.
    //
    // 하나만 그리면 '스위치가 먹었는가'를 알 수 없다. 두 결과가 같으면
    // 플래그가 셰이더에 안 닿은 것인데, 그건 어느 한쪽 그림만 봐서는
    // 절대 드러나지 않는다.
    struct Frame
    {
        EnhancedPostChainPass::ToneMapper mapper;
        const char*                       name;
        std::vector<float>                r;
        std::vector<float>                g;
        std::vector<float>                b;
        std::vector<float>                preAA;
    };
    Frame frames[2] = {
        { EnhancedPostChainPass::ToneMapper::ACES, "ACES", {}, {}, {}, {} },
        { EnhancedPostChainPass::ToneMapper::AgX,  "AgX",  {}, {}, {}, {} },
    };

    for (Frame& frame : frames)
    {
        if (!passed) break;

        EnhancedPostChainPass::Tuning tuning = post.GetTuning();
        tuning.toneMapper = frame.mapper;
        post.SetTuning(tuning);

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] BeginFrame 실패: " + error + "\n";
            post.Shutdown();
            resources.Shutdown();
            return false;
        }

        if (!post.PrepareFrame(frameContext, error))
        {
            outLog += "[2/4] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다(dx12.compare 크래시).
        EnhancedRenderGraph graph(resources);

        // ★ 임포트 상태는 지난 프레임이 남긴 것이어야 한다.
        //
        // 그래프는 임포트한 리소스를 끝에서 되돌려 놓지 않는다. 첫 프레임은
        // 씬 채우기가 UAV로 쓰고 블룸·Uber가 SRV로 읽으므로 끝 상태가 SRV인데,
        // 두 번째 프레임에 UAV라고 선언하면 배리어의 before가 어긋난다.
        // SSAO 스케일 검증에서 같은 것에 물렸고, 여기서 또 물렸다.
        const RHIResourceState importState = sceneFilled
            ? RHIResourceState::ShaderResource : RHIResourceState::UnorderedAccess;

        const RGHandle hdrHandle = graph.ImportTexture(hdrHandleTable.Handle(),
            importState, "PostChain.TestHDR");

        // 씬은 첫 프레임에만 채운다. 내용이 같으므로 다시 채울 이유가 없고,
        // 두 톤매퍼가 정확히 같은 입력을 보는 것이 대조의 전제다.
        if (!sceneFilled)
        graph.AddPass("PostChain.TestScene",
            { { hdrHandle, RHIResourceState::UnorderedAccess } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {

                PostSceneParams params{};
                params.sizeX = kPostWidth;
                params.sizeY = kPostHeight;

                const auto cb = resources.UploadConstants(
                    &params, sizeof(PostSceneParams));
                if (!cb.IsValid()) return;
                // 링에서 직접 자르고 뷰를 손으로 만들던 것을 CreateBindings로
                // 바꿨다(R2a). 힙 바인딩은 인코더가 스스로 한다(R4-1c).
                const RHIBindingDesc uavs[] = {
                    RHIBindingDesc::Uav2D(hdrHandleTable.Handle(), EnhancedPostChainPass::kHDRFormat),
                };
                const RHIBindingTable uavTable = resources.CreateBindings(uavs);
                if (!uavTable.IsValid()) return;

                RHIEncoder& encoder = *executeContext.encoder;
                encoder.SetPipeline(RHIBindPoint::Compute, scenePSO);
                encoder.SetConstantBuffer(RHIBindPoint::Compute, 0, cb);
                encoder.SetBindings(RHIBindPoint::Compute, 1, uavTable);
                encoder.Dispatch((kPostWidth + 7) / 8, (kPostHeight + 7) / 8, 1);
            });

        EnhancedPostChainPass::Inputs inputs{};
        inputs.color = hdrHandle;
        post.SetInputs(inputs);
        post.Declare(graph, frameContext);

        const RGHandle finalHandle = post.GetOutput();
        const RGHandle preAAHandle = post.GetPreAAOutput();

        if (!finalHandle.IsValid() || !preAAHandle.IsValid())
        {
            outLog += "[2/4] 출력이 선언되지 않았다\n";
            passed = false;
        }
        else
        {
            // ★ 결과를 읽는 패스를 반드시 둔다 — 아무도 안 읽으면 그래프가
            //   체인을 통째로 걷어낸다.
            graph.AddPass("PostChain.Readback",
                { { finalHandle, RHIResourceState::CopySource },
                  { preAAHandle, RHIResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    executeContext.encoder->CopyToReadback( readback,
                        executeContext.ResolveHandle(finalHandle), kPostSliceFinal);
                    executeContext.encoder->CopyToReadback( readback,
                        executeContext.ResolveHandle(preAAHandle), kPostSlicePreAA);
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
            sceneFilled = true;
        }

        if (passed)
        {
            // Map·행 간격·정규화를 손으로 하던 것을 걷었다(R2c-b2).
            // 네 번 Map하던 것이 한 번이 된다 — 장 둘이 한 버퍼에 있으므로.
            RHIReadbackImage captured{};
            std::string readbackError;
            if (!resources.MapReadback(readback, captured, readbackError))
            {
                outLog += "[2/4] 리드백 Map 실패: " + readbackError + "\n";
                passed = false;
            }
            else
            {
                const auto fill = [&](uint32_t slice, uint32_t channel,
                    std::vector<float>& out)
                {
                    out.resize(static_cast<size_t>(kPostWidth) * kPostHeight);
                    for (uint32_t y = 0; y < kPostHeight; ++y)
                        for (uint32_t x = 0; x < kPostWidth; ++x)
                            out[static_cast<size_t>(y) * kPostWidth + x] =
                                captured.At(x, y, channel, slice);
                };

                fill(kPostSliceFinal, 0, frame.r);
                fill(kPostSliceFinal, 1, frame.g);
                fill(kPostSliceFinal, 2, frame.b);
                fill(kPostSlicePreAA, 0, frame.preAA);
            }
        }
    }

    if (passed)
    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 그래프 — 선언 %u · 컬링 %u · 실행 %u · 블룸 단수 %u\n",
            graphStats.passesDeclared, graphStats.passesCulled,
            graphStats.passesExecuted, post.GetBloomMipCount());
        outLog += line;

        // 임계 + 다운(n-1) + 업(n-1) + Uber + FXAA + 리드백.
        // 씬은 첫 프레임에만 있으므로 세지 않는다 — graphStats는 마지막
        // 프레임의 것이다.
        const uint32_t expected = 1 + (post.GetBloomMipCount() - 1) * 2 + 1 + 1 + 1;
        if (graphStats.passesExecuted < expected)
        {
            outLog += "패스가 걷어내졌다 — 결과를 읽는 사슬이 끊겼다\n";
            passed = false;
        }
    }

    // ── 단정 ──
    //
    // 체인 자체의 단정은 ACES 결과로 본다. 톤매퍼는 곡선만 다르고 블룸·
    // 비네트·FXAA는 같은 코드를 지나므로, 둘 다 재는 것은 같은 것을 두 번
    // 재는 셈이다. 톤매퍼끼리의 대조는 그 뒤에 따로 한다.
    const std::vector<float>& finalPixels = frames[0].r;
    const std::vector<float>& preAAPixels = frames[0].preAA;

    if (passed)
    {
        const auto at = [&](const std::vector<float>& v, uint32_t x, uint32_t y)
        {
            return v[static_cast<size_t>(y) * kPostWidth + x];
        };

        // 표본 자리
        const uint32_t cx = kPostWidth / 2;
        const uint32_t cy = kPostHeight / 2;

        const float bright = at(finalPixels, cx, cy);                  // 밝은 사각 안
        const float nearGlow = at(finalPixels, cx + 20, cy);           // 사각 바깥 — 번짐
        const float farBackground = at(finalPixels, cx, cy - 90);      // 멀리 — 번짐 없음
        const float corner = at(finalPixels, 4, 4);                    // 구석 — 비네트

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 밝은 곳 %.3f · 번짐 %.3f · 배경 %.3f · 구석 %.3f\n",
            bright, nearGlow, farBackground, corner);
        outLog += line;

        // ① 톤맵 — HDR 8.0이 1.0 이하로 눌렸는가.
        //
        // 그냥 saturate로 잘린 것과 구분하려면 '1.0보다 작다'를 봐야 한다.
        // ACES는 8.0을 1.0 근처까지 올리지만 정확히 1은 아니다.
        if (bright > 0.999f)
        {
            outLog += "밝은 곳이 1.0이다 — 톤맵이 아니라 그냥 잘린 것이다\n";
            passed = false;
        }

        // ② 블룸 — 사각 바깥이 멀리보다 밝은가.
        if (nearGlow <= farBackground + 0.01f)
        {
            outLog += "밝은 사각 주변이 배경과 같다 — 블룸이 번지지 않았다\n";
            passed = false;
        }

        // ③ 비네트 — 구석이 중앙 근처 배경보다 어두운가.
        if (corner >= farBackground - 0.01f)
        {
            outLog += "구석이 중앙만큼 밝다 — 비네트가 걸리지 않았다\n";
            passed = false;
        }

        // ④ FXAA — 줄무늬 구역의 이웃 차이가 줄었는가.
        //
        // 최종 그림만 보면 FXAA가 죽어도 알 수 없다. 전후를 나란히 잰다.
        // ★ 계단이 실제로 있는 곳만 잰다.
        //
        // 처음에는 구역 전체를 평균했는데, 대각선 한 줄만 계단이고 나머지는
        // 평평해서(FXAA가 대비 임계로 그냥 통과시킨다) 평균이 희석됐다.
        // 감소가 0.4%로 나와 '죽음(0.0%)'과 구분은 되지만 여유가 없었다.
        //
        // 열마다 세로 이웃 차이의 최댓값을 취한다. 그 최댓값이 나오는 자리가
        // 곧 계단이 걸친 행이라, 평평한 부분이 섞이지 않는다.
        const auto stripeDiff = [&](const std::vector<float>& v)
        {
            double sum = 0.0;
            uint32_t count = 0;
            for (uint32_t x = 42; x < 102; ++x)
            {
                double peak = 0.0;
                for (uint32_t y = 106; y < 154; ++y)
                {
                    peak = std::max(peak,
                        static_cast<double>(std::fabs(at(v, x, y) - at(v, x, y - 1))));
                }
                sum += peak;
                ++count;
            }
            return (0 == count) ? 0.0 : sum / count;
        };

        const double before = stripeDiff(preAAPixels);
        const double after = stripeDiff(finalPixels);

        std::snprintf(line, sizeof(line),
            "[4/4] FXAA — 계단 이웃 차이 %.5f → %.5f (%.1f%% 감소)\n",
            before, after,
            (before > 1e-9) ? (100.0 * (1.0 - after / before)) : 0.0);
        outLog += line;

        if (before <= 1e-9)
        {
            outLog += "계단이 없다 — 검사 배치가 깨졌다\n";
            passed = false;
        }
        else if (after >= before)
        {
            outLog += "FXAA가 이웃 차이를 줄이지 못했다 — FXAA가 죽었다\n";
            passed = false;
        }
    }

    // ── 톤매퍼 대조 ──
    if (passed)
    {
        // 빨강 패치의 한가운데. 씬 셰이더가 (160~200, 40~80)에 뒀다.
        const uint32_t px = 180;
        const uint32_t py = 60;
        const size_t index = static_cast<size_t>(py) * kPostWidth + px;

        // 채도 = (max - min) / max. 흰색으로 수렴할수록 0에 가깝다.
        const auto saturationOf = [&](const Frame& frame)
        {
            const float r = frame.r[index];
            const float g = frame.g[index];
            const float b = frame.b[index];
            const float hi = std::max(r, std::max(g, b));
            const float lo = std::min(r, std::min(g, b));
            return (hi > 1e-4f) ? ((hi - lo) / hi) : 0.f;
        };

        const float acesSat = saturationOf(frames[0]);
        const float agxSat = saturationOf(frames[1]);

        char line[288]{};
        std::snprintf(line, sizeof(line),
            "[5/5] 톤매퍼 — 밝은 빨강(4,0,0)에서 채도 ACES %.3f · AgX %.3f\n"
            "      ACES (%.3f %.3f %.3f) · AgX (%.3f %.3f %.3f)\n",
            acesSat, agxSat,
            frames[0].r[index], frames[0].g[index], frames[0].b[index],
            frames[1].r[index], frames[1].g[index], frames[1].b[index]);
        outLog += line;

        // ① 두 결과가 실제로 달라야 한다. 같으면 플래그가 셰이더에 안 닿은
        //    것인데, 어느 한쪽 그림만 봐서는 절대 드러나지 않는다.
        uint32_t differing = 0;
        for (size_t i = 0; i < frames[0].r.size(); ++i)
        {
            if (std::fabs(frames[0].r[i] - frames[1].r[i]) > 1.5f / 255.f) ++differing;
        }
        std::snprintf(line, sizeof(line),
            "      두 톤매퍼가 다른 픽셀 %u/%u\n",
            differing, kPostWidth * kPostHeight);
        outLog += line;

        if (0 == differing)
        {
            outLog += "두 톤매퍼 결과가 같다 — 톤매퍼 플래그가 셰이더에 닿지 않는다\n";
            passed = false;
        }

        // ② AgX가 밝은 채도를 ACES보다 낮춰야 한다. 이것이 AgX를 쓰는
        //    이유 자체다 — 밝아질수록 흰색으로 수렴하는 것.
        if (agxSat >= acesSat)
        {
            outLog += "AgX가 ACES보다 채도를 낮추지 못했다 — 채널 혼합 행렬이 "
                      "빠졌거나 순서가 뒤집혔다\n";
            passed = false;
        }

        // ③ AgX도 포화하면 안 된다. 1.0이면 그냥 잘린 것이라 곡선이
        //    돌았는지 알 수 없다.
        if (frames[1].r[index] > 0.999f)
        {
            outLog += "AgX 결과가 1.0이다 — 곡선이 아니라 그냥 잘린 것이다\n";
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

    post.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    hdrHandleTable.Reset();
    resources.Shutdown();

    outLog += passed ? "포스트 체인 검증 통과\n" : "포스트 체인 검증 실패\n";
    return passed;
}

#endif
