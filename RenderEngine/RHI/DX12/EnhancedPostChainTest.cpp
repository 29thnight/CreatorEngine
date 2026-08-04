#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedPostChainPass.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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

    constexpr const char* kPostSceneShader = R"(
RWTexture2D<float4> gColor : register(u0);

cbuffer SceneParams : register(b0)
{
    uint2 gSize;
    uint2 gPad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gSize)) return;

    // 배경: 중간 회색. 블룸 임계 아래라 번지지 않는다.
    float3 color = 0.25f;

    // 가운데 사각: 블룸 임계(1.0)는 넘되 톤맵이 포화하지는 않는 밝기.
    //
    // ★ 처음에 8.0을 썼다가 단정이 헛돌았다. ACES는 8.0에서 정확히 1.0으로
    //   포화해서, '톤맵을 거친 1.0'과 '그냥 잘린 1.0'이 구분되지 않는다.
    //   3.0이면 0.954가 나와 둘이 갈린다.
    //   ACES(1)=0.804 · ACES(2)=0.915 · ACES(3)=0.954 · ACES(8)=1.000
    const int2 center = int2(gSize) / 2;
    const int2 delta = abs(int2(id.xy) - center);
    if (delta.x < 8 && delta.y < 8)
    {
        color = 3.0f;
    }

    // 오른쪽 아래 구역: 완만한 대각선 경계. FXAA가 무는 계단이 여기 생긴다.
    //
    // ★ 처음에 한 픽셀 간격 세로 줄무늬를 썼다가 FXAA 감소가 0.0%로 나왔다.
    //   FXAA는 경계를 '따라' 흐린다 — 세로 경계면 세로로 흐리므로 세로
    //   줄무늬는 아무리 흐려도 그대로다. 그게 FXAA의 정상 동작이고,
    //   틀린 것은 검사 배치였다.
    //
    //   기울기 1/4이면 네 픽셀마다 한 칸씩 올라가는 계단이 생긴다.
    //   이것이 FXAA가 실제로 부드럽게 만드는 모양이다.
    //   ★ 구석이 아니라 중앙 왼쪽에 둔다. 처음에 오른쪽 아래 구석에 뒀더니
    //     비네트가 그 구역을 거의 검게 만들어(감쇠 0.094) 대비가 0.069까지
    //     떨어졌다. FXAA의 대비 임계가 0.050이라 겨우 넘는 상태였고,
    //     측정된 감소도 0.7%뿐이었다. 통과는 했지만 무엇이 조금만 바뀌어도
    //     흔들리는 지표라, 비네트가 약한 곳(감쇠 0.89)으로 옮겨 여유를 뒀다.
    //     검사끼리 간섭하면 그 지표는 더 이상 그 단계를 재지 않는다.
    if (id.x >= 40 && id.x < 104 && id.y >= 104 && id.y < 156)
    {
        const float edge = 104.0f + (float(id.x) - 40.0f) * 0.25f;
        color = (float(id.y) < edge + 20.0f) ? 0.9f : 0.05f;
    }

    gColor[id.xy] = float4(color, 1.0f);
}
)";

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
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_post.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error))
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
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = kPostWidth;
        desc.Height = kPostHeight;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = EnhancedPostChainPass::kHDRFormat;
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
    }

    ID3D12PipelineState* scenePSO = nullptr;
    ID3D12RootSignature* sceneRoot = nullptr;
    {
        D3D12_DESCRIPTOR_RANGE uavRange{};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
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
        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> errors;
        if (!root.IsValid() ||
            FAILED(D3DCompile(kPostSceneShader, strlen(kPostSceneShader), nullptr,
                nullptr, nullptr, "CSMain", "cs_5_0", 0, 0, &blob, &errors)))
        {
            outLog += "[2/4] 씬 셰이더 준비 실패";
            if (errors) outLog += std::string(": ")
                + static_cast<const char*>(errors->GetBufferPointer());
            outLog += "\n";
            post.Shutdown();
            resources.Shutdown();
            return false;
        }
        sceneRoot = root.signature;

        DX12ComputePipelineDesc desc{};
        desc.csBytecode = blob->GetBufferPointer();
        desc.csSize = blob->GetBufferSize();
        desc.rootSignature = root.signature;
        desc.rootSignatureId = root.id;

        scenePSO = psoManager.GetOrCreateCompute(desc, error);
        if (nullptr == scenePSO)
        {
            outLog += "[2/4] 씬 PSO 생성 실패: " + error + "\n";
            post.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    // 최종(LDR) · FXAA 전(LDR) 둘 다 가져온다. FXAA가 죽어 있어도
    // 최종 그림만 봐서는 알 수 없다.
    constexpr uint64_t kLdrRowPitch =
        ((static_cast<uint64_t>(kPostWidth) * 4ull) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull)
        & ~static_cast<uint64_t>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1ull);

    ComPtr<ID3D12Resource> finalReadback;
    ComPtr<ID3D12Resource> preAAReadback;
    {
        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = kLdrRowPitch * kPostHeight;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&finalReadback))) ||
            FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
                D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&preAAReadback))))
        {
            outLog += "[2/4] 리드백 생성 실패\n";
            post.Shutdown();
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
        EnhancedRenderGraph graph;

        const RGHandle hdrHandle = graph.ImportTexture(hdr.Get(),
            RGResourceState::UnorderedAccess, "PostChain.TestHDR");

        graph.AddPass("PostChain.TestScene",
            { { hdrHandle, RGResourceState::UnorderedAccess } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                auto* commandList = executeContext.commandList;
                auto* device = resources.GetDevice();

                PostSceneParams params{};
                params.sizeX = kPostWidth;
                params.sizeY = kPostHeight;

                const auto cb = resources.GetUploadRing().Allocate(
                    sizeof(PostSceneParams), DX12UploadRing::kConstantBufferAlignment);
                if (!cb.IsValid()) return;
                memcpy(cb.cpuAddress, &params, sizeof(params));

                const auto uavTable = resources.GetDescriptorRing().Allocate(1);
                if (!uavTable.IsValid()) return;

                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uavDesc.Format = EnhancedPostChainPass::kHDRFormat;
                device->CreateUnorderedAccessView(hdr.Get(), nullptr,
                    &uavDesc, uavTable.CpuAt(0));

                ID3D12DescriptorHeap* heaps[] = { resources.GetDescriptorRing().GetHeap() };
                commandList->SetDescriptorHeaps(1, heaps);

                commandList->SetComputeRootSignature(sceneRoot);
                commandList->SetPipelineState(scenePSO);
                commandList->SetComputeRootConstantBufferView(0, cb.gpuAddress);
                commandList->SetComputeRootDescriptorTable(1, uavTable.gpu);
                commandList->Dispatch((kPostWidth + 7) / 8, (kPostHeight + 7) / 8, 1);
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
                { { finalHandle, RGResourceState::CopySource },
                  { preAAHandle, RGResourceState::CopySource } },
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
                        dst.PlacedFootprint.Footprint.Format =
                            EnhancedPostChainPass::kLDRFormat;
                        dst.PlacedFootprint.Footprint.Width = kPostWidth;
                        dst.PlacedFootprint.Footprint.Height = kPostHeight;
                        dst.PlacedFootprint.Footprint.Depth = 1;
                        dst.PlacedFootprint.Footprint.RowPitch =
                            static_cast<UINT>(kLdrRowPitch);

                        executeContext.commandList->CopyTextureRegion(
                            &dst, 0, 0, 0, &src, nullptr);
                    };

                    copy(finalHandle, finalReadback.Get());
                    copy(preAAHandle, preAAReadback.Get());
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
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[2/4] 그래프 — 선언 %u · 컬링 %u · 실행 %u · 블룸 단수 %u\n",
            graphStats.passesDeclared, graphStats.passesCulled,
            graphStats.passesExecuted, post.GetBloomMipCount());
        outLog += line;

        // 씬 + 임계 + 다운(n-1) + 업(n-1) + Uber + FXAA + 리드백
        const uint32_t expected = 1 + 1 + (post.GetBloomMipCount() - 1) * 2 + 1 + 1 + 1;
        if (graphStats.passesExecuted < expected)
        {
            outLog += "패스가 걷어내졌다 — 결과를 읽는 사슬이 끊겼다\n";
            passed = false;
        }
    }

    // ── 단정 ──
    std::vector<float> finalPixels;   // R 채널만
    std::vector<float> preAAPixels;

    if (passed)
    {
        const auto read = [&](ID3D12Resource* buffer, std::vector<float>& out) -> bool
        {
            void* mapped = nullptr;
            D3D12_RANGE range{ 0, static_cast<SIZE_T>(kLdrRowPitch * kPostHeight) };
            if (FAILED(buffer->Map(0, &range, &mapped))) return false;

            out.resize(static_cast<size_t>(kPostWidth) * kPostHeight);
            for (uint32_t y = 0; y < kPostHeight; ++y)
            {
                const auto* row = static_cast<const uint8_t*>(mapped)
                    + static_cast<size_t>(y) * kLdrRowPitch;
                for (uint32_t x = 0; x < kPostWidth; ++x)
                {
                    out[static_cast<size_t>(y) * kPostWidth + x] = row[x * 4] / 255.f;
                }
            }
            buffer->Unmap(0, nullptr);
            return true;
        };

        if (!read(finalReadback.Get(), finalPixels) ||
            !read(preAAReadback.Get(), preAAPixels))
        {
            outLog += "[3/4] 리드백 Map 실패\n";
            passed = false;
        }
    }

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
    resources.Shutdown();

    outLog += passed ? "포스트 체인 검증 통과\n" : "포스트 체인 검증 실패\n";
    return passed;
}

#endif
