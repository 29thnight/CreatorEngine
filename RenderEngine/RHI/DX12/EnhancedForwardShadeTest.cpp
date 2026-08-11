#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedForwardPass.h"
#include "DX12DeviceResources.h"
#include "DX12MeshCache.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../../Mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// Forward+ 셰이딩 검증 (PHASE 3-6, 3·4·5단계).
//
// ── 왜 컬링 검증과 따로인가 ──
//
// 컬링 검증은 '타일 목록이 맞는가'를 리드백 숫자로 본다. 셰이딩 검증은
// '그 목록을 쓴 그림이 전 광원 루프와 같은가'를 픽셀로 본다. 필요한 배치가
// 달라서 한 함수에 우겨넣으면 어느 단정이 무엇을 지키는지 흐려진다.
//
// ── 배치를 이렇게 고른 이유 ──
//
// 광원 하나짜리 배치로 두 경로를 대조하면 통과가 공짜다. 광원이 안 닿는
// 곳은 참조 경로도 0이라, 컬링이 광원을 통째로 떨어뜨려도 결과가 같다.
//
// 그래서 화면을 덮는 판 하나 위에 작은 점광을 격자로 뿌린다. 광원마다
// 닿는 타일이 두세 개뿐이라 컬링이 실제로 대부분을 떨어뜨리고, 그 판단이
// 한 칸이라도 틀리면 참조 경로와 픽셀이 갈린다.
//
//   판    뷰 z = 0.25, 반폭 0.25 → 90° FOV 화면을 정확히 채운다
//   광원  8x8 격자, 뷰 z = 0.20(판 앞 0.05), 반경 0.06
//
// ★ 실측이 타일당 정확히 9개(평균 9.00 · 최대 9)로 나왔고, 이 수가 기하로
//   설명된다는 것이 이 배치가 의도대로 섰다는 증거다:
//
//     광원은 판보다 카메라에 0.05 가까워서 화면에 1.25배로 퍼진다.
//     격자 간격 0.0625 → 화면 0.0625/0.20 = 0.3125 NDC = 20px
//     광원 반경 0.06   → 화면 0.06/0.20  = 0.30   NDC = 19.2px
//     타일 중심에서 ±(8 + 19.2) = ±27.2px 안의 광원이 통과 → 축마다 3개
//     → 3 x 3 = 9
//
//   처음에는 '광원끼리 안 겹친다'고 적었는데 틀렸다. 판 위 좌표로 계산해서
//   투영 확대를 빼먹은 탓이다 — 화면에서는 반경 19.2px가 간격 20px과 거의
//   같아 이웃까지 넉넉히 닿는다. 64개 중 9개면 컬링이 86%를 떨어뜨린 것이라
//   대조가 지킬 것이 남아 있고, 그것이 이 배치에 바라던 바다.
//
// 깊이 텍스처는 컬링 검증과 같은 0.6 평면을 쓴다 — 그 깊이가 뷰 z 0.25로
// 판과 같은 자리다. 여기가 어긋나면 컬링이 판 앞뒤의 엉뚱한 타일을 본다.
namespace
{
    constexpr uint32_t kShadeWidth = 128;
    constexpr uint32_t kShadeHeight = 128;
    // ★ 깊이 평면을 판보다 아주 조금 뒤에 둔다.
    //
    // 셰이딩이 깊이 테스트를 하게 되면서 필요해진 조정이다. 둘이 같은 자리면
    // LESS 테스트가 판을 떨어뜨려 아무것도 안 그려지고, 그 실패는
    // '화면이 검다'로만 드러난다.
    //
    // 처음에는 판을 앞으로 당겼는데(0.25 → 0.245) 그러면 광원 배치까지 같이
    // 움직여 '타일당 정확히 9개'라는 기하 예측이 깨진다(실제로 10.56이 나왔다).
    // 예측 가능한 배치가 이 검증의 값어치이므로, 판을 두고 깊이 평면을
    // 뒤로 옮기는 쪽으로 고쳤다:
    //
    //   판 뷰 z 0.25   → 깊이 0.6006
    //   깊이 평면 0.605 → 뷰 z 0.2528  (판보다 뒤 → LESS 통과)
    //   광원 z 범위 0.14~0.26이 0.2528을 포함 → 컬링의 깊이 검사도 통과
    constexpr float    kSurfaceDepth = 0.605f;
    constexpr float    kSurfaceViewZ = 0.25f;
    constexpr float    kSurfaceHalf = 0.25f;
    constexpr uint32_t kTileTotal =
        (kShadeWidth / EnhancedForwardPass::kTileSize)
        * (kShadeHeight / EnhancedForwardPass::kTileSize);

    // 화면을 덮는 판. 카메라를 향하도록 노멀은 -Z다.
    std::unique_ptr<Mesh> MakeSurfaceQuad()
    {
        const float h = kSurfaceHalf;
        const float z = kSurfaceViewZ;

        std::vector<Vertex> vertices(4);
        const Mathf::Vector3 corners[4] = {
            { -h, -h, z }, { -h,  h, z }, {  h,  h, z }, {  h, -h, z },
        };
        const Mathf::Vector2 uvs[4] = { {0,1}, {0,0}, {1,0}, {1,1} };

        for (uint32_t i = 0; i < 4; ++i)
        {
            vertices[i].position = corners[i];
            vertices[i].normal = { 0.f, 0.f, -1.f };
            vertices[i].uv0 = uvs[i];
            vertices[i].tangent = { 1.f, 0.f, 0.f };
            vertices[i].bitangent = { 0.f, 1.f, 0.f };
        }

        std::vector<uint32> indices = { 0, 1, 2, 0, 2, 3 };
        return std::make_unique<Mesh>("Fwd.SurfaceQuad",
            std::move(vertices), std::move(indices));
    }

    std::vector<EnhancedLight> MakeLightGrid(uint32_t side)
    {
        std::vector<EnhancedLight> lights;
        lights.reserve(static_cast<size_t>(side) * side);

        const float span = kSurfaceHalf * 2.f;
        const float step = span / static_cast<float>(side);

        for (uint32_t y = 0; y < side; ++y)
        {
            for (uint32_t x = 0; x < side; ++x)
            {
                EnhancedLight light{};
                light.position = Mathf::Vector4(
                    -kSurfaceHalf + (static_cast<float>(x) + 0.5f) * step,
                    -kSurfaceHalf + (static_cast<float>(y) + 0.5f) * step,
                    kSurfaceViewZ - 0.05f,
                    1.f);                                     // w=1 점광
                light.color = Mathf::Color4(1.f, 1.f, 1.f, 1.f);
                light.attenuation = Mathf::Vector4(1.f, 0.f, 0.f, 0.06f);
                lights.push_back(light);
            }
        }
        return lights;
    }

}

bool EnhancedSceneRenderer::RunForwardPlusShadeTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── Forward+ 셰이딩 검증 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kShadeWidth, kShadeHeight, error))
    {
        outLog += "[1/4] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    if (!psoManager.Initialize(&resources, L"dx12_fwd.cache", error) ||
        !rootSignatures.Initialize(&resources, error) ||
        !meshCache.Initialize(&resources, error))
    {
        outLog += "[1/4] 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 0.1f, 100.f);
    camera.inverseView = XMMatrixIdentity();
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);

    const std::unique_ptr<Mesh> quad = MakeSurfaceQuad();
    std::vector<EnhancedLight> lights = MakeLightGrid(8);

    std::vector<EnhancedDrawItem> draws(1);
    draws[0].mesh = quad.get();
    draws[0].worldMatrix = XMMatrixIdentity();

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.width = kShadeWidth;
    frameContext.height = kShadeHeight;
    frameContext.camera = &camera;
    frameContext.forwardDraws = &draws;
    frameContext.lights = &lights;

    EnhancedForwardPass forward;
    if (!forward.Initialize(frameContext, error))
    {
        outLog += "[1/4] Forward+ 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }
    outLog += "[1/4] 셰이딩 VS/PS·참조 경로 PSO 생성 통과\n";

    // ── 깊이 평면 ──
    //
    // GBuffer가 채운 깊이를 흉내 낸다. 예전에는 업로드 링에서 R32_FLOAT
    // 텍스처로 복사해 넣었는데, 셰이딩이 깊이 테스트를 하게 되면서 DSV가
    // 필요해져 실제 프레임과 같은 방식(D32_FLOAT + 클리어)으로 바꿨다.
    // 코드도 짧아졌다 — 상수 하나로 채우는 일에 업로드 복사는 과했다.
    ComPtr<ID3D12Resource>       depth;
    ComPtr<ID3D12DescriptorHeap> depthDsvHeap;
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kShadeWidth;
        depthDesc.Height = kShadeHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = ToDXGI(EnhancedForwardPass::kDepthFormat);
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = ToDXGI(EnhancedForwardPass::kDepthFormat);
        clearValue.DepthStencil.Depth = kSurfaceDepth;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue, IID_PPV_ARGS(&depth))))
        {
            outLog += "[2/4] 깊이 생성 실패\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        if (FAILED(resources.GetDevice()->CreateDescriptorHeap(
            &dsvHeapDesc, IID_PPV_ARGS(&depthDsvHeap))))
        {
            outLog += "[2/4] 깊이 DSV 힙 생성 실패\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    // 프레임마다 다시 채운다. 셰이딩이 자기 기하의 깊이를 쓰므로, 안 지우면
    // 두 번째 경로가 첫 번째가 남긴 깊이에서 출발해 대조가 뜻을 잃는다.
    const auto fillDepth = [&]()
    {
        const auto dsv = depthDsvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = ToDXGI(EnhancedForwardPass::kDepthFormat);
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        resources.GetDevice()->CreateDepthStencilView(depth.Get(), &dsvDesc, dsv);
        resources.GetCommandList()->ClearDepthStencilView(
            dsv, D3D12_CLEAR_FLAG_DEPTH, kSurfaceDepth, 0, 0, nullptr);
    };

    // ── 두 경로를 한 번씩 그려 픽셀을 모은다 ──
    constexpr uint32_t kPixelCount = kShadeWidth * kShadeHeight;

    std::vector<float> pixels[2];
    std::vector<uint32_t> tileCounts;
    bool passed = true;

    for (uint32_t pass = 0; pass < 2 && passed; ++pass)
    {
        const bool reference = (1 == pass);
        forward.SetUseReferencePath(reference);

        if (!resources.BeginFrame(error))
        {
            outLog += "[2/4] BeginFrame 실패: " + error + "\n";
            passed = false;
            break;
        }

        fillDepth();

        if (passed && !forward.PrepareFrame(frameContext, error))
        {
            outLog += "[2/4] PrepareFrame 실패: " + error + "\n";
            passed = false;
        }

        // ★ 그래프는 제출 이후까지 살아 있어야 한다. 블록 안에서 선언하면
        //   블록을 나가며 transient를 놓아 제출 전에 리소스가 사라진다 —
        //   dx12.compare에서 크래시로 겪은 그 실수다.
        EnhancedRenderGraph graph(resources);

        // 그림은 텍스처 리드백, 타일 카운트는 버퍼 리드백이다(R2c-b2).
        // 두 시그니처가 갈리는 이유가 여기 그대로 보인다 — 하나는 행 간격을
        // 맞춰야 하고 다른 하나는 맞출 행이 없다.
        RHIReadback readback{};
        RHIReadback tileReadback{};
        {
            std::string readbackError;
            if (!resources.CreateReadback(kShadeWidth, kShadeHeight,
                EnhancedForwardPass::kOutputFormat, 1, readback, readbackError))
            {
                outLog += "[2/4] 리드백 생성 실패: " + readbackError + "\n";
                passed = false;
            }

            if (!resources.CreateBufferReadback(kTileTotal * sizeof(uint32_t),
                tileReadback, readbackError))
            {
                outLog += "[2/4] 타일 리드백 생성 실패: " + readbackError + "\n";
                passed = false;
            }
        }

        if (passed)
        {
            EnhancedForwardPass::Inputs inputs{};
            inputs.depth = graph.ImportTexture(depth.Get(),
                RHIResourceState::DepthWrite, "Fwd.ShadeDepth");
            forward.SetInputs(inputs);

            forward.Declare(graph, frameContext);

            const RGHandle output = forward.GetOutput();
            if (!output.IsValid())
            {
                outLog += "[2/4] 셰이딩 출력이 선언되지 않았다\n";
                passed = false;
            }
            else
            {
                // 타일 카운트의 전이는 usage 선언이 만든다(R4-2b) — 예전에는
                // 손 배리어가 before를 '늘 UAV'로 단정했고, 셰이딩이 SRV로
                // 바꾼 뒤라 검증 레이어가 잡았다.
                graph.AddPass("Fwd.ShadeReadback",
                    { { output, RHIResourceState::CopySource },
                      { forward.GetTileCountHandle(), RHIResourceState::CopySource } },
                    [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                    {
                        executeContext.encoder->CopyToReadback( readback,
                            executeContext.ResolveHandle(output));

                        // 타일 카운트도 같이 가져온다. 이것이 없으면 대조가
                        // 공짜로 통과할 수 있다 — 컬링이 전 광원을 모든 타일에
                        // 넣어도 결과는 참조 경로와 정확히 같기 때문이다.
                        executeContext.encoder->CopyBufferToReadback(
                            tileReadback, forward.GetTileCountBuffer());
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
            }
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

        if (passed)
        {
            RHIReadbackImage captured{};
            std::string readbackError;
            if (!resources.MapReadback(readback, captured, readbackError))
            {
                outLog += "[2/4] 리드백 Map 실패: " + readbackError + "\n";
                passed = false;
            }
            else
            {
                pixels[pass].resize(kPixelCount);
                for (uint32_t y = 0; y < kShadeHeight; ++y)
                    for (uint32_t x = 0; x < kShadeWidth; ++x)
                    {
                        // R 채널만 본다. 광원이 흰색이라 RGB가 같다.
                        pixels[pass][y * kShadeWidth + x] = captured.At(x, y, 0);
                    }
            }
        }

        if (passed && 0 == pass)
        {
            RHIReadbackImage tileCaptured{};
            std::string readbackError;
            if (resources.MapReadback(tileReadback, tileCaptured, readbackError))
            {
                const uint32_t* counts = tileCaptured.Elements<uint32_t>();
                if (nullptr != counts) tileCounts.assign(counts, counts + kTileTotal);
            }
        }
    }

    // ── 단정 ──
    if (passed)
    {
        uint32_t litPixels = 0;
        float    maxValue = 0.f;
        for (const float value : pixels[0])
        {
            if (value > 0.f) ++litPixels;
            maxValue = std::max(maxValue, value);
        }

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[2/4] Forward+ 그림 — 밝은 픽셀 %u/%u · 최대 %.3f\n",
            litPixels, kPixelCount, maxValue);
        outLog += line;

        // 아무것도 안 그려졌으면 대조는 무의미하다. 두 경로가 다 0이어도
        // '같다'가 나오므로, 그림이 있는지를 먼저 단정한다.
        if (0 == litPixels)
        {
            outLog += "셰이딩이 아무것도 그리지 않았다 — 대조가 무의미하다\n";
            passed = false;
        }
        else if (litPixels == kPixelCount)
        {
            outLog += "화면 전체가 밝다 — 감쇠·반경이 죽었다\n";
            passed = false;
        }
    }

    if (passed)
    {
        uint32_t mismatched = 0;
        uint32_t exact = 0;
        float    worstDelta = 0.f;
        uint32_t worstIndex = 0;

        for (uint32_t i = 0; i < kPixelCount; ++i)
        {
            const float a = pixels[0][i];
            const float b = pixels[1][i];
            if (a == b) { ++exact; continue; }

            const float delta = std::fabs(a - b);
            if (delta > worstDelta) { worstDelta = delta; worstIndex = i; }
            // half 정밀도의 마지막 비트는 합산 순서와 무관하게 흔들릴 수 있다.
            if (delta > 1e-3f) ++mismatched;
        }

        char line[256]{};
        std::snprintf(line, sizeof(line),
            "[3/4] 참조 대조 — 완전 일치 %u/%u · 불일치 %u · 최대 차 %.5f (%u,%u)\n",
            exact, kPixelCount, mismatched, worstDelta,
            worstIndex % kShadeWidth, worstIndex / kShadeWidth);
        outLog += line;

        if (0 != mismatched)
        {
            outLog += "타일 목록으로 그린 그림이 전 광원 루프와 다르다 — "
                      "컬링이 닿는 광원을 떨어뜨렸다\n";
            passed = false;
        }
    }

    // ★ 대조가 '뜻이 있었는가'를 여기서 단정한다.
    //
    // 컬링이 전 광원을 모든 타일에 넣어도 위의 대조는 완전 일치로 통과한다 —
    // 두 경로가 같은 광원을 같은 순서로 돌기 때문이다. 즉 위 단정만으로는
    // 컬링이 죽은 상태와 정상 상태를 구분하지 못한다. 타일당 평균 광원 수가
    // 전체보다 확실히 적어야 대조가 무언가를 지킨 것이다.
    if (passed)
    {
        if (tileCounts.empty())
        {
            outLog += "타일 카운트를 못 읽었다 — 대조가 뜻이 있었는지 알 수 없다\n";
            passed = false;
        }
        else
        {
            uint32_t total = 0;
            uint32_t peak = 0;
            for (const uint32_t count : tileCounts)
            {
                total += count;
                peak = std::max(peak, count);
            }
            const float average = static_cast<float>(total)
                / static_cast<float>(tileCounts.size());
            const uint32_t lightCount = static_cast<uint32_t>(lights.size());

            char line[256]{};
            std::snprintf(line, sizeof(line),
                "[4/4] 컬링 실효 — 광원 %u개 중 타일당 평균 %.2f · 최대 %u\n",
                lightCount, average, peak);
            outLog += line;

            // 절반을 기준으로 둔다. 이 배치는 광원마다 두세 타일에만 닿게
            // 짰으므로 평균이 한 자리여야 정상이고, 절반을 넘으면 컬링이
            // 사실상 아무것도 안 자른 것이다.
            if (average > static_cast<float>(lightCount) * 0.5f)
            {
                outLog += "타일당 광원이 전체의 절반을 넘는다 — 컬링이 자르지 "
                          "않았고, 위의 픽셀 대조는 공짜로 통과한 것이다\n";
                passed = false;
            }
        }
    }

    // ── 광원 수 스케일링 ──
    //
    // Forward+가 이기는 경계를 찾는다. 광원이 적으면 컬링 디스패치 비용이
    // 그대로 손해다 — 그 경계를 짐작이 아니라 실측으로 적는다.
    if (passed)
    {
        outLog += "   ※ 광원 수 스케일링(5단계)은 dx12.forwardscale에서 1280x720으로\n"
                  "     잰다 — 128x128에서는 두 경로 다 측정 잡음에 묻힌다\n";
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    forward.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "Forward+ 셰이딩 검증 통과\n" : "Forward+ 셰이딩 검증 실패\n";
    return passed;
}

#endif
