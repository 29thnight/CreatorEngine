#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedForwardPass.h"
#include "DX12DeviceResources.h"
#include "DX12GpuProfiler.h"
#include "DX12MeshCache.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedSceneRenderer.h"
#include "../../Mesh.h"

#include <algorithm>
#include <cstdio>
#include <vector>

// Forward+ 광원 수 스케일링 실측 (PHASE 3-6, 5단계).
//
// ── 재려는 것 ──
//
// "Forward+가 언제부터 이기는가". 광원이 적으면 컬링 디스패치가 그대로
// 손해다 — 목록을 만드는 값을 못 뽑는다. 그 경계가 어디인지 짐작으로
// 적으면 나중에 '왜 이 값인가'에 답할 수 없으므로 실측으로 적는다.
//
// ── 왜 128x128 자가 검증에서 재지 않는가 ──
//
// 셰이딩 비용은 픽셀 수 × 픽셀당 광원 수다. 128x128에서는 픽셀이 16384개
// 뿐이라 광원을 아무리 늘려도 두 경로 다 수십 마이크로초에 머물고, 그건
// 타임스탬프 분해능과 프레임 잡음에 묻힌다. 거기서 나온 수는 경계에 대해
// 아무것도 말해 주지 않는다 — 실제로 쓰는 해상도에서 재야 뜻이 있다.
//
// ── 측정 규율 ──
//
//   · 두 경로가 같은 기하·같은 광원·같은 해상도를 쓴다. PSO만 다르다.
//   · 워밍업 프레임을 버린다(첫 프레임은 PSO·힙 준비가 섞인다).
//   · 여러 프레임의 중앙값을 쓴다. 평균은 한 번의 튐에 끌려간다.
//   · 컬링 시간과 셰이딩 시간을 따로 낸다 — 합계만 보면 어느 쪽이
//     경계를 정하는지 알 수 없다.
namespace
{
    constexpr uint32_t kScaleWidth = 1280;
    constexpr uint32_t kScaleHeight = 720;
    // 깊이 평면을 판보다 아주 조금 뒤에 둔다(뷰 z 0.2528 대 0.25). 같은 자리면
    // LESS 깊이 테스트가 판을 떨어뜨려 아무것도 안 그려지고, 그러면 셰이딩
    // 시간이 0에 가까워져 측정이 통째로 거짓이 된다.
    constexpr float    kScaleSurfaceDepth = 0.605f;
    constexpr float    kScaleSurfaceZ = 0.25f;

    // 워밍업 + 측정. 측정 프레임은 홀수로 둬서 중앙값이 실제 표본이 되게 한다.
    constexpr uint32_t kWarmupFrames = 3;
    constexpr uint32_t kMeasureFrames = 9;

    std::unique_ptr<Mesh> MakeScaleQuad()
    {
        // 90° FOV · 16:9. 화면을 정확히 덮도록 x 반폭을 종횡비만큼 넓힌다.
        const float aspect = static_cast<float>(kScaleWidth)
            / static_cast<float>(kScaleHeight);
        const float halfY = kScaleSurfaceZ;
        const float halfX = halfY * aspect;
        const float z = kScaleSurfaceZ;

        std::vector<Vertex> vertices(4);
        const Mathf::Vector3 corners[4] = {
            { -halfX, -halfY, z }, { -halfX, halfY, z },
            {  halfX,  halfY, z }, {  halfX, -halfY, z },
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
        return std::make_unique<Mesh>("Fwd.ScaleQuad",
            std::move(vertices), std::move(indices));
    }

    // 광원을 화면 전체에 고르게 뿌린다. 총 개수가 제곱수가 아니어도
    // 화면을 덮도록 격자 한 변을 올림으로 잡고 앞에서부터 잘라 쓴다.
    //
    // 반경은 광원 수와 무관하게 고정한다. 반경을 같이 줄이면 '광원이 늘어서
    // 느려진 것'과 '반경이 줄어 빨라진 것'이 섞여 무엇을 쟀는지 알 수 없다.
    std::vector<EnhancedLight> MakeScaleLights(uint32_t count)
    {
        const float aspect = static_cast<float>(kScaleWidth)
            / static_cast<float>(kScaleHeight);
        const float halfY = kScaleSurfaceZ;
        const float halfX = halfY * aspect;

        uint32_t side = 1;
        while (side * side < count) ++side;

        std::vector<EnhancedLight> lights;
        lights.reserve(count);

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t x = i % side;
            const uint32_t y = i / side;

            EnhancedLight light{};
            light.position = Mathf::Vector4(
                -halfX + (static_cast<float>(x) + 0.5f) * (halfX * 2.f / side),
                -halfY + (static_cast<float>(y) + 0.5f) * (halfY * 2.f / side),
                kScaleSurfaceZ - 0.05f,
                1.f);
            light.color = Mathf::Color4(1.f, 1.f, 1.f, 0.2f);
            light.attenuation = Mathf::Vector4(1.f, 0.f, 0.f, 0.06f);
            lights.push_back(light);
        }
        return lights;
    }

    double Median(std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[values.size() / 2];
    }
}

bool EnhancedSceneRenderer::RunForwardPlusScaleTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── Forward+ 광원 수 스케일링 (PHASE 3-6) ──\n";

    std::string error;

    DX12DeviceResources resources;
    if (!resources.Initialize(kScaleWidth, kScaleHeight, error))
    {
        outLog += "DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12GpuProfiler profiler;
    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_fwd.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !meshCache.Initialize(&resources, error) ||
        !profiler.Initialize(resources.GetDevice(), resources.GetCommandQueue(),
            16, DX12DeviceResources::kFrameCount, error))
    {
        outLog += "초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    FrameCameraSnapshot camera{};
    camera.view = XMMatrixIdentity();
    camera.projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.f, 0.1f, 100.f);
    camera.inverseView = XMMatrixIdentity();
    camera.inverseProjection = XMMatrixInverse(nullptr, camera.projection);

    const std::unique_ptr<Mesh> quad = MakeScaleQuad();
    std::vector<EnhancedDrawItem> draws(1);
    draws[0].mesh = quad.get();
    draws[0].worldMatrix = XMMatrixIdentity();

    std::vector<EnhancedLight> lights;

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.width = kScaleWidth;
    frameContext.height = kScaleHeight;
    frameContext.camera = &camera;
    frameContext.forwardDraws = &draws;
    frameContext.lights = &lights;

    EnhancedForwardPass forward;
    if (!forward.Initialize(frameContext, error))
    {
        outLog += "Forward+ 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // ── 깊이 평면 ──
    //
    // 셰이딩이 깊이 테스트를 하므로 D32_FLOAT + DSV로 만든다(실제 프레임과
    // 같은 방식). 예전의 업로드 복사는 DSV를 만들 수 없어 못 쓴다.
    ComPtr<ID3D12Resource>       depth;
    ComPtr<ID3D12DescriptorHeap> depthDsvHeap;
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kScaleWidth;
        depthDesc.Height = kScaleHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = ToDXGI(EnhancedForwardPass::kDepthFormat);
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE clearValue{};
        clearValue.Format = ToDXGI(EnhancedForwardPass::kDepthFormat);
        clearValue.DepthStencil.Depth = kScaleSurfaceDepth;

        if (FAILED(resources.GetDevice()->CreateCommittedResource(&heap,
            D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            &clearValue, IID_PPV_ARGS(&depth))))
        {
            outLog += "깊이 생성 실패\n";
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
            outLog += "깊이 DSV 힙 생성 실패\n";
            forward.Shutdown();
            resources.Shutdown();
            return false;
        }
    }

    bool passed = true;
    uint32_t frameIndex = 0;

    // 한 프레임을 그리고 패스별 GPU 시간을 돌려준다. 두 경로가 이 함수
    // 하나를 쓴다 — 측정 절차가 갈리면 그 차이가 결과에 섞인다.
    const auto renderFrame = [&](bool reference, double& outCullMs, double& outShadeMs)
    {
        outCullMs = 0.0;
        outShadeMs = 0.0;

        forward.SetUseReferencePath(reference);

        if (!resources.BeginFrame(error)) return false;
        profiler.BeginFrame(frameIndex % DX12DeviceResources::kFrameCount);
        ++frameIndex;

        // 프레임마다 다시 채운다. 셰이딩이 자기 기하의 깊이를 쓰므로,
        // 안 지우면 다음 프레임의 컬링이 이전 프레임 기하의 깊이를 본다.
        {
            const auto dsv = depthDsvHeap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = ToDXGI(EnhancedForwardPass::kDepthFormat);
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            resources.GetDevice()->CreateDepthStencilView(depth.Get(), &dsvDesc, dsv);
            resources.GetCommandList()->ClearDepthStencilView(
                dsv, D3D12_CLEAR_FLAG_DEPTH, kScaleSurfaceDepth, 0, 0, nullptr);
        }

        if (!forward.PrepareFrame(frameContext, error)) return false;

        EnhancedRenderGraph graph(resources);
        graph.SetProfiler(&profiler);

        EnhancedForwardPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RGResourceState::DepthWrite, "Fwd.ScaleDepth");
        forward.SetInputs(inputs);

        forward.Declare(graph, frameContext);

        if (!graph.Compile(resources.GetDevice(), error)) return false;
        if (!graph.Execute(resources.GetCommandList(), error)) return false;

        profiler.ResolveFrame(resources.GetCommandList());

        if (!resources.EndFrame(error)) return false;
        resources.WaitForGpu();

        std::vector<DX12GpuProfiler::PassTiming> timings;
        std::string collectError;
        if (!profiler.Collect(timings, collectError)) return false;

        for (const auto& timing : timings)
        {
            if (timing.name.find("Cull") != std::string::npos)  outCullMs = timing.milliseconds;
            if (timing.name.find("Shade") != std::string::npos) outShadeMs = timing.milliseconds;
        }
        return true;
    };

    struct ScalePoint
    {
        uint32_t lightCount{ 0 };
        double   cullMs{ 0.0 };
        double   plusShadeMs{ 0.0 };
        double   referenceMs{ 0.0 };
        uint32_t overflowTiles{ 0 };   // 타일당 상한을 넘겨 광원이 잘린 타일 수
        uint32_t peakRawCount{ 0 };    // 자르기 전 최대 타일 광원 수
    };

    // ★ 넘침을 세는 리드백.
    //
    // 이것이 없으면 이 측정은 거짓말을 할 수 있다. 광원이 빽빽해지면 타일당
    // 상한(kMaxLightsPerTile)을 넘고, 넘친 광원은 조용히 버려진다. 버린 만큼
    // 셰이딩이 싸지므로 Forward+는 실제보다 빨라 보인다 — '빨라졌다'가 아니라
    // '덜 그렸다'인데 시간만 보면 구분되지 않는다.
    const uint32_t tileTotal = (kScaleWidth + EnhancedForwardPass::kTileSize - 1)
        / EnhancedForwardPass::kTileSize
        * ((kScaleHeight + EnhancedForwardPass::kTileSize - 1)
            / EnhancedForwardPass::kTileSize);

    RHIReadback tileReadback{};
    {
        std::string readbackError;
        if (!resources.CreateBufferReadback(
            static_cast<uint64_t>(tileTotal) * 2ull * sizeof(uint32_t),
            tileReadback, readbackError))
        {
            outLog += "타일 리드백 생성 실패: " + readbackError + "\n";
            passed = false;
        }
    }

    // 현재 광원 배치의 넘침을 잰다. 프레임을 하나 더 돌려 카운트만 가져온다.
    const auto measureOverflow = [&](uint32_t& outOverflowTiles, uint32_t& outPeak)
    {
        outOverflowTiles = 0;
        outPeak = 0;
        if (!tileReadback.IsValid()) return true;

        if (!resources.BeginFrame(error)) return false;

        EnhancedRenderGraph graph(resources);
        EnhancedForwardPass::Inputs inputs{};
        inputs.depth = graph.ImportTexture(depth.Get(),
            RGResourceState::DepthWrite, "Fwd.ScaleDepth");
        forward.SetInputs(inputs);
        forward.Declare(graph, frameContext);

        // 깊이는 여전히 선언하지 않는다 — ShaderResource로 걸면 그래프의
        // 마지막 상태가 셰이딩의 DepthWrite가 아니라 이 패스의 PSR이 되고,
        // 다음 프레임의 ClearDepthStencilView가 운다(예전 실측). 타일 카운트는
        // 이제 그래프 안 리소스라(R4-2b) usage 하나로 전이가 해결된다.
        // "그것은 그래프 밖 리소스라 걸 사용이 애초에 없다"고 적혀 있던
        // 자리인데, 그 전제가 반증됐다.
        graph.AddPass("Fwd.ScaleTileReadback",
            { { forward.GetTileCountHandle(), RGResourceState::CopySource } },
            [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
            {
                resources.CopyBufferToReadback(executeContext.commandList,
                    tileReadback, resources.Resolve(forward.GetTileCountBuffer()));
            }, true);

        if (!graph.Compile(resources.GetDevice(), error)) return false;
        if (!graph.Execute(resources.GetCommandList(), error)) return false;
        if (!resources.EndFrame(error)) return false;
        resources.WaitForGpu();

        RHIReadbackImage captured{};
        if (!resources.MapReadback(tileReadback, captured, error)) return false;

        // 뒤 절반이 자르기 전의 진짜 개수다(앞 절반은 상한으로 자른 것).
        const uint32_t* counts = captured.Elements<uint32_t>();
        if (nullptr == counts) return false;

        const uint32_t* raw = counts + tileTotal;
        for (uint32_t i = 0; i < tileTotal; ++i)
        {
            if (raw[i] > EnhancedForwardPass::kMaxLightsPerTile) ++outOverflowTiles;
            outPeak = std::max(outPeak, raw[i]);
        }
        return true;
    };

    const uint32_t kLightCounts[] = { 1, 4, 16, 64, 256, 1024 };
    std::vector<ScalePoint> points;

    for (const uint32_t lightCount : kLightCounts)
    {
        if (!passed) break;

        lights = MakeScaleLights(lightCount);

        std::vector<double> cullSamples;
        std::vector<double> plusSamples;
        std::vector<double> refSamples;

        for (uint32_t i = 0; i < kWarmupFrames + kMeasureFrames; ++i)
        {
            double cullMs = 0.0;
            double shadeMs = 0.0;

            if (!renderFrame(false, cullMs, shadeMs))
            {
                outLog += "Forward+ 프레임 실패: " + error + "\n";
                passed = false;
                break;
            }
            if (i >= kWarmupFrames)
            {
                cullSamples.push_back(cullMs);
                plusSamples.push_back(shadeMs);
            }

            double refCullMs = 0.0;
            double refShadeMs = 0.0;
            if (!renderFrame(true, refCullMs, refShadeMs))
            {
                outLog += "참조 프레임 실패: " + error + "\n";
                passed = false;
                break;
            }
            if (i >= kWarmupFrames) refSamples.push_back(refShadeMs);
        }

        if (!passed) break;

        ScalePoint point{};
        point.lightCount = lightCount;
        point.cullMs = Median(cullSamples);
        point.plusShadeMs = Median(plusSamples);
        point.referenceMs = Median(refSamples);

        if (!measureOverflow(point.overflowTiles, point.peakRawCount))
        {
            outLog += "넘침 측정 실패: " + error + "\n";
            passed = false;
            break;
        }
        points.push_back(point);

        char line[288]{};
        std::snprintf(line, sizeof(line),
            "  광원 %5u — 컬링 %6.3f + 셰이딩 %6.3f = %6.3f ms · 참조 %6.3f ms"
            " · %s · 넘친 타일 %u(최대 %u)\n",
            point.lightCount, point.cullMs, point.plusShadeMs,
            point.cullMs + point.plusShadeMs, point.referenceMs,
            (point.cullMs + point.plusShadeMs < point.referenceMs) ? "Forward+ 우세"
                                                                  : "참조 우세",
            point.overflowTiles, point.peakRawCount);
        outLog += line;
    }

    // ── 경계 ──
    //
    // 이기는 첫 지점을 적는다. 없으면 없다고 적는다 — 없는 경계를 지어내는
    // 것보다 '이 배치에서는 안 이긴다'가 훨씬 쓸모 있는 정보다.
    if (passed)
    {
        // 넘친 지점은 경계 판정에서 뺀다. 거기서 Forward+가 빠른 것은 광원을
        // 버렸기 때문일 수 있고, 그건 '빨라졌다'가 아니라 '덜 그렸다'다.
        uint32_t crossover = 0;
        for (const ScalePoint& point : points)
        {
            if (0 != point.overflowTiles) continue;
            if (point.cullMs + point.plusShadeMs < point.referenceMs)
            {
                crossover = point.lightCount;
                break;
            }
        }

        uint32_t firstOverflow = 0;
        for (const ScalePoint& point : points)
        {
            if (0 != point.overflowTiles) { firstOverflow = point.lightCount; break; }
        }
        if (0 != firstOverflow)
        {
            outLog += "※ 광원 " + std::to_string(firstOverflow)
                + "개부터 타일당 상한("
                + std::to_string(EnhancedForwardPass::kMaxLightsPerTile)
                + ")을 넘겨 광원이 잘렸다 — 그 지점의 시간은 '빨라진 것'이 아니라\n"
                  "  '덜 그린 것'이라 경계 판정에서 뺐다\n";
        }

        if (0 != crossover)
        {
            outLog += "경계: 광원 " + std::to_string(crossover)
                + "개부터 Forward+가 전 광원 루프보다 싸다\n";
        }
        else
        {
            outLog += "경계 없음 — 이 배치의 광원 수 범위에서는 Forward+가 "
                      "이기지 못했다\n";
        }

        // 측정이 뜻을 가지려면 참조 경로가 광원 수에 따라 실제로 느려져야 한다.
        // 전 구간이 평평하면 셰이딩이 광원을 안 돌고 있다는 뜻이고, 그러면
        // 위의 경계 판정도 전부 무의미하다.
        if (points.size() >= 2)
        {
            const double first = points.front().referenceMs;
            const double last = points.back().referenceMs;
            if (last < first * 2.0)
            {
                outLog += "참조 경로가 광원 수에 반응하지 않는다 — 셰이딩이 "
                          "광원을 안 돌거나 측정이 잡음에 묻혔다\n";
                passed = false;
            }
        }
    }

    std::string validation;
    const uint32_t problems = resources.DrainDebugMessages(validation);
    if (0 != problems)
    {
        passed = false;
        outLog += "검증 레이어 문제 " + std::to_string(problems) + "건\n" + validation;
    }

    forward.Shutdown();
    profiler.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    outLog += passed ? "Forward+ 스케일링 측정 완료\n" : "Forward+ 스케일링 측정 실패\n";
    return passed;
}

#endif
