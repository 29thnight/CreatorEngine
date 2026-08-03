#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSceneRenderer.h"
#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "DX12TextureCache.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedGBufferPass.h"
#include "../../Material.h"
#include "../../SceneRenderer.h"
#include "../../Texture.h"
#include "../../RenderPassData.h"
#include "../../MeshRendererProxy.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.

    /// 커버리지 비트맵을 만든다. 그려진 픽셀이면 true.
    ///
    /// bitmask 타깃(R32_UINT)은 그려진 곳에만 0이 아닌 값이 들어간다.
    std::vector<bool> PixelCompareCoverageFromBitmask(const std::vector<uint8_t>& bytes,
        uint32_t width, uint32_t height, uint32_t rowPitch)
    {
        std::vector<bool> coverage(static_cast<size_t>(width) * height, false);

        for (uint32_t y = 0; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const uint32_t*>(bytes.data()
                + static_cast<size_t>(y) * rowPitch);

            for (uint32_t x = 0; x < width; ++x)
            {
                coverage[static_cast<size_t>(y) * width + x] = (0 != row[x]);
            }
        }

        return coverage;
    }
}

// ★★ 미완성이다. 이 함수는 현재 크래시한다. ★★
//
// 진행된 것:
//   - DX11 GBuffer 캡처가 동작한다(1920x1080 · 그려진 픽셀 983038/2073600).
//     캡처는 렌더 스레드가 프레임 끝에서 수행하고, 드로우 목록도 큐가
//     지워지기 직전에 함께 뜬다.
//
// 막힌 것:
//   - 캡처한 드로우로 DX12 GBuffer를 그리면 DX12DeviceResources::EndFrame에서
//     ACCESS_VIOLATION이 난다. 스택 상단이 전부 드라이버 내부(NVDEV_Thunk →
//     D3D12GetInterface)라 호출부만 보고는 원인을 알 수 없다.
//
// 배제한 것(둘 다 시도했고 크래시가 그대로였다):
//   - 해상도. 1920x1080에서 512x288로 줄여도 같은 자리에서 죽는다.
//   - 텍스처 업로드. 재질을 비워 DX12TextureCache가 DX11 컨텍스트를 만지지
//     않게 해도 같다. (그래도 그 코드는 남겨 둔다 — 게임 스레드에서 DX11
//     컨텍스트를 만지는 것은 그 자체로 위험하고, 커버리지에 필요도 없다.)
//
// 다음에 볼 것:
//   - dx12.scene은 같은 구조로 잘 돈다. 둘의 차이를 좁히는 것이 가장 빠른
//     길이다 — 그쪽은 자체 드로우 목록을 쓰고 Shadow·Deferred도 함께 선언한다.
//   - 캡처 스냅샷의 Mesh* 수명. 렌더 스레드가 뜬 뒤 프레임이 지나는 동안
//     유효한지 확인하지 못했다.
//   - D3D12 디버그 레이어를 켜고 무엇이 잘못됐는지 직접 물을 것.
//
// 통과하지 않는 것을 통과처럼 두지 않으려고 이 주석을 남긴다.
bool EnhancedSceneRenderer::RunPixelCompareTest(std::string& outLog)
{
    using Microsoft::WRL::ComPtr;

    outLog += "── DX11 대조 (PHASE 3-6, 미완성) ──\n";

    // ── [1/3] DX11이 그린 것을 가져온다 ──

    auto* dx11Renderer = SceneRenderer::GetActive();
    if (nullptr == dx11Renderer)
    {
        outLog += "[1/3] DX11 렌더러가 없다(에디터가 떠 있어야 한다)\n";
        return false;
    }

    std::vector<uint8_t> dx11Bytes;
    uint32_t dx11Width = 0;
    uint32_t dx11Height = 0;
    uint32_t dx11RowPitch = 0;
    DXGI_FORMAT dx11Format = DXGI_FORMAT_UNKNOWN;
    std::string error;

    // ── 캡처를 렌더 스레드에 맡기는 이유 ──
    //
    // 처음에는 여기서 직접 CopyResource·Map을 불렀다. 그 자리에서 크래시했다 —
    // DX11 즉시 컨텍스트는 스레드 안전하지 않은데 이 함수는 게임 스레드에서
    // 돌고 렌더는 CommandExecuteThread에서 돈다. 두 스레드가 같은 컨텍스트를
    // 만지면 그 자리에서 깨진다.
    //
    // 그래서 요청만 걸고 렌더 스레드가 프레임 끝에서 복사하게 했다. 아직
    // 준비되지 않았으면 실패가 아니라 '다시 부르라'로 답한다.
    if (!dx11Renderer->ConsumeGBufferCapture(dx11Bytes, dx11Width, dx11Height,
        dx11RowPitch, dx11Format))
    {
        dx11Renderer->RequestGBufferCapture();
        outLog += "[1/3] DX11 GBuffer 캡처를 요청했다 — 몇 프레임 뒤 다시 부를 것\n";
        return false;
    }

    if (DXGI_FORMAT_R32_UINT != dx11Format)
    {
        // 형식이 바뀌면 커버리지 해석이 틀어진다. 조용히 다른 것을 읽느니
        // 여기서 멈춘다 — 잘못된 대조 결과는 대조가 없는 것보다 나쁘다.
        char line[160]{};
        std::snprintf(line, sizeof(line),
            "[1/3] bitmask 형식이 R32_UINT가 아니다(%d) — 커버리지 해석을 손봐야 한다\n",
            static_cast<int>(dx11Format));
        outLog += line;
        return false;
    }

    // ── 대조 격자를 줄이는 이유 ──
    //
    // 화면 해상도 그대로(1920x1080) DX12를 그리면 EndFrame에서 죽는다.
    // 스택이 드라이버 내부라 원인을 아직 좁히지 못했고, 별건으로 남겼다.
    //
    // 다만 그것을 기다릴 이유가 없다. 대조가 묻는 것은 '어느 픽셀이
    // 그려졌는가'이고, 그 답은 격자를 줄여도 유지된다 — 기하가 어긋나면
    // 축소한 격자에서도 어긋난다. 경계 픽셀의 정밀도만 잃는다.
    //
    // 종횡비는 유지한다. 여기서 비율이 틀어지면 대조가 잡으려는 바로 그
    // 종류의 오차(투영 어긋남)를 대조 자신이 만들어 낸다.
    constexpr uint32_t kMaxCompareWidth = 512;

    const uint32_t compareWidth = (std::min)(kMaxCompareWidth, dx11Width);
    const uint32_t compareHeight = (std::max)(1u,
        (dx11Height * compareWidth) / (std::max)(1u, dx11Width));

    const auto dx11Full = PixelCompareCoverageFromBitmask(
        dx11Bytes, dx11Width, dx11Height, dx11RowPitch);

    // 최근접이 아니라 '그 칸에 그려진 픽셀이 하나라도 있으면 그려진 것'으로
    // 줄인다. 최근접은 얇은 물체를 통째로 떨어뜨려, DX11에만 있는 것으로
    // 잘못 세게 만든다.
    std::vector<bool> dx11Coverage(static_cast<size_t>(compareWidth) * compareHeight, false);
    for (uint32_t y = 0; y < dx11Height; ++y)
    {
        const uint32_t cellY = (std::min)(compareHeight - 1, y * compareHeight / dx11Height);
        for (uint32_t x = 0; x < dx11Width; ++x)
        {
            if (!dx11Full[static_cast<size_t>(y) * dx11Width + x]) continue;

            const uint32_t cellX = (std::min)(compareWidth - 1, x * compareWidth / dx11Width);
            dx11Coverage[static_cast<size_t>(cellY) * compareWidth + cellX] = true;
        }
    }

    size_t dx11Covered = 0;
    for (const bool covered : dx11Coverage) { if (covered) ++dx11Covered; }

    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[1/3] DX11 GBuffer — %ux%u · 그려진 픽셀 %zu/%zu\n",
            dx11Width, dx11Height, dx11Covered, dx11Coverage.size());
        outLog += line;
    }

    if (0 == dx11Covered)
    {
        // 아무것도 안 그려졌으면 대조할 것이 없다. 이 상태로 '커버리지가
        // 같다'를 통과시키면 DX12도 비어 있을 때 통과해 버린다.
        outLog += "[1/3] DX11이 아무것도 그리지 않았다 — 씬을 열고 다시 부를 것\n";
        return false;
    }

    // ── [2/3] 같은 입력으로 DX12를 그린다 ──
    //
    // 입력이 같다는 것이 이 대조의 전제다. DX11 GBufferPass도, 여기도 같은
    // 카메라의 RenderPassData에서 스냅샷과 deferredQueue를 읽는다. 그래서
    // 맞춰야 할 것은 해상도뿐이다.

    Camera* sceneCamera = nullptr;
    for (auto& camera : CameraManagement->GetCameras())
    {
        if (camera && RenderPassData::VaildCheck(camera.get()))
        {
            sceneCamera = camera.get();
            break;
        }
    }

    if (nullptr == sceneCamera)
    {
        outLog += "[2/3] 활성 카메라가 없다\n";
        return false;
    }

    const RenderPassData* renderData = RenderPassData::GetData(sceneCamera);
    const FrameCameraSnapshot cameraSnapshot = renderData->GetFrameSnapshot();

    // 드로우는 렌더 큐에서 직접 읽지 않는다.
    //
    // 큐(m_deferredQueue)는 커맨드 리스트를 만든 직후 비워지므로, 게임
    // 스레드가 여기서 읽으면 대개 비어 있다 — 실제로 '그릴 것이 없다'가
    // 나왔다. 캡처와 짝을 이뤄 렌더 스레드가 떠 둔 스냅샷을 쓴다.
    //
    // 부수 효과로 정확성도 올라간다. 스냅샷은 DX11이 그 픽셀을 그릴 때 쓴
    // 바로 그 목록이므로, 지금 큐를 읽는 것보다 대조의 전제에 더 맞다.
    std::vector<EnhancedDrawItem> draws;
    for (const auto& captured : dx11Renderer->GetCaptureDraws())
    {
        EnhancedDrawItem item{};
        item.mesh = captured.mesh;
        item.worldMatrix = captured.worldMatrix;

        // ★ 재질은 일부러 비운다.
        //
        // 커버리지는 재질과 무관하다 — 어느 픽셀이 그려지는지는 기하와 변환이
        // 정한다. 그런데 텍스처를 물리면 DX12TextureCache가 DX11 텍스처를
        // 읽으려고 DX11 즉시 컨텍스트를 만지고, 이 함수는 게임 스레드에서
        // 돌므로 렌더 스레드와 경합한다. 캡처를 렌더 스레드로 옮긴 것과 같은
        // 이유로 여기도 만지면 안 된다.
        //
        // 대조에 필요 없는 것을 위해 위험을 지지 않는다. 색을 대조하게 되면
        // 그때는 텍스처 업로드도 렌더 스레드로 보내야 한다.
        draws.push_back(item);
    }

    if (draws.empty())
    {
        outLog += "[2/3] 캡처된 드로우가 없다\n";
        return false;
    }

    DX12DeviceResources resources;
    if (!resources.Initialize(compareWidth, compareHeight, error))
    {
        outLog += "[2/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    DX12PSOManager psoManager;
    DX12RootSignatureCache rootSignatures;
    DX12MeshCache meshCache;
    DX12TextureCache textureCache;

    if (!psoManager.Initialize(resources.GetDevice(), L"dx12_compare.cache", error) ||
        !rootSignatures.Initialize(resources.GetDevice(), error) ||
        !meshCache.Initialize(&resources, error) ||
        !textureCache.Initialize(&resources, DirectX11::DeviceStates->g_pDevice,
            DirectX11::DeviceStates->g_pDeviceContext, error))
    {
        outLog += "[2/3] DX12 캐시 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    std::vector<EnhancedLight> lights;

    EnhancedFrameContext frameContext{};
    frameContext.resources = &resources;
    frameContext.psoManager = &psoManager;
    frameContext.rootSignatures = &rootSignatures;
    frameContext.meshCache = &meshCache;
    frameContext.textureCache = &textureCache;
    frameContext.width = compareWidth;
    frameContext.height = compareHeight;
    frameContext.camera = &cameraSnapshot;
    frameContext.draws = &draws;
    frameContext.lights = &lights;

    EnhancedGBufferPass gbuffer;
    if (!gbuffer.Initialize(frameContext, error))
    {
        outLog += "[2/3] GBuffer 초기화 실패: " + error + "\n";
        resources.Shutdown();
        return false;
    }

    // 깊이를 리드백할 버퍼.
    const uint32_t depthRowPitch = ((compareWidth * 4u) + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);

    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = static_cast<uint64_t>(depthRowPitch) * compareHeight;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> depthReadback;
    if (FAILED(resources.GetDevice()->CreateCommittedResource(&readbackHeap,
        D3D12_HEAP_FLAG_NONE, &readbackDesc, D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr, IID_PPV_ARGS(&depthReadback))))
    {
        outLog += "[2/3] 리드백 버퍼 생성 실패\n";
        gbuffer.Shutdown();
        resources.Shutdown();
        return false;
    }

    bool renderOk = true;
    {
        if (!resources.BeginFrame(error)) { renderOk = false; }

        if (renderOk && !gbuffer.PrepareFrame(frameContext, error)) { renderOk = false; }

        if (renderOk)
        {
            EnhancedRenderGraph graph;
            gbuffer.Declare(graph, frameContext);
            const auto outputs = gbuffer.GetOutputs();

            graph.AddPass("depth_readback", { { outputs.depth, RGResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = depthReadback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_FLOAT;
                    dst.PlacedFootprint.Footprint.Width = compareWidth;
                    dst.PlacedFootprint.Footprint.Height = compareHeight;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch = depthRowPitch;

                    D3D12_TEXTURE_COPY_LOCATION src{};
                    src.pResource = executeContext.Resolve(outputs.depth);
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                    executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }, true);

            if (!graph.Compile(resources.GetDevice(), error)) { renderOk = false; }
            if (renderOk && !graph.Execute(resources.GetCommandList(), error)) { renderOk = false; }
        }

        if (renderOk && !resources.EndFrame(error)) { renderOk = false; }
        if (renderOk) resources.WaitForGpu();
    }

    if (!renderOk)
    {
        outLog += "[2/3] DX12 렌더 실패: " + error + "\n";
        gbuffer.Shutdown();
        resources.Shutdown();
        return false;
    }

    std::vector<bool> dx12Coverage(static_cast<size_t>(compareWidth) * compareHeight, false);
    {
        void* mapped = nullptr;
        const size_t bytes = static_cast<size_t>(depthRowPitch) * compareHeight;
        D3D12_RANGE range{ 0, bytes };
        if (FAILED(depthReadback->Map(0, &range, &mapped)))
        {
            outLog += "[2/3] 깊이 리드백 Map 실패\n";
            gbuffer.Shutdown();
            resources.Shutdown();
            return false;
        }

        // 깊이 1.0은 아무것도 안 그려진 곳이다(클리어 값).
        const auto* pixels = static_cast<const uint8_t*>(mapped);
        for (uint32_t y = 0; y < compareHeight; ++y)
        {
            const auto* row = reinterpret_cast<const float*>(pixels
                + static_cast<size_t>(y) * depthRowPitch);

            for (uint32_t x = 0; x < compareWidth; ++x)
            {
                dx12Coverage[static_cast<size_t>(y) * compareWidth + x] = (row[x] < 1.0f);
            }
        }

        depthReadback->Unmap(0, nullptr);
    }

    size_t dx12Covered = 0;
    for (const bool covered : dx12Coverage) { if (covered) ++dx12Covered; }

    {
        char line[192]{};
        std::snprintf(line, sizeof(line),
            "[2/3] DX12 GBuffer — %ux%u · 그려진 픽셀 %zu/%zu · 드로우 %u(배치 %u)\n",
            compareWidth, compareHeight, dx12Covered, dx12Coverage.size(),
            gbuffer.GetLastDrawCount(), gbuffer.GetLastBatchCount());
        outLog += line;
    }

    // ── [3/3] 대조 ──
    //
    // 픽셀 단위로 센다. 총합만 비교하면 '같은 수만큼 다른 곳을 그린' 경우를
    // 놓친다 — 카메라 행렬이 뒤집혔을 때 정확히 그런 결과가 나온다.

    size_t onlyDX11 = 0;
    size_t onlyDX12 = 0;
    size_t both = 0;

    for (size_t index = 0; index < dx11Coverage.size(); ++index)
    {
        const bool a = dx11Coverage[index];
        const bool b = dx12Coverage[index];

        if (a && b) ++both;
        else if (a)  ++onlyDX11;
        else if (b)  ++onlyDX12;
    }

    const size_t unionCount = both + onlyDX11 + onlyDX12;
    const double agreement = (0 != unionCount)
        ? (static_cast<double>(both) / static_cast<double>(unionCount)) : 0.0;

    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "[3/3] 일치 %zu · DX11만 %zu · DX12만 %zu · 겹침 비율 %.4f\n",
            both, onlyDX11, onlyDX12, agreement);
        outLog += line;
    }

    gbuffer.Shutdown();
    textureCache.Shutdown();
    meshCache.Shutdown();
    rootSignatures.Shutdown();
    psoManager.Shutdown();
    resources.Shutdown();

    // ── 판정 기준을 어디에 둘 것인가 ──
    //
    // 완전 일치를 요구하지 않는다. 두 경로는 래스터 규칙이 같아도 깊이 정밀도와
    // 정점 변환의 부동소수 순서가 달라 경계 픽셀이 흔들린다. 그것을 실패로
    // 삼으면 판정이 매번 흔들려 아무도 보지 않게 된다.
    //
    // 반대로 기준을 너무 낮추면 '대충 비슷하면 통과'가 되어 카메라가 조금
    // 어긋난 것을 놓친다. 지금은 0.95를 시작점으로 두고, 실제로 어느 정도
    // 나오는지 본 뒤 조인다 — 처음부터 맞는 숫자를 아는 척하지 않는다.
    constexpr double kMinAgreement = 0.95;

    const bool passed = (agreement >= kMinAgreement);

    if (!passed)
    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "겹침 비율 %.4f가 기준 %.2f 미만이다 — 기하·변환·컬링 중 하나가 어긋난다\n",
            agreement, kMinAgreement);
        outLog += line;
    }

    outLog += passed ? "DX11 대조 통과\n" : "DX11 대조 실패\n";
    return passed;
}

#endif
