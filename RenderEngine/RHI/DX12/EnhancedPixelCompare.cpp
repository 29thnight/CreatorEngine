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

    /// 즉시 로그.
    ///
    /// outLog는 함수가 끝나야 기록된다. 크래시하면 통째로 사라져, 어디까지
    /// 갔는지 알 수 없다 — 실제로 그 상태로 원인을 추측하고 있었다.
    /// 단계마다 바로 남기면 마지막 줄이 크래시 지점을 가리킨다.
    void PixelCompareTrace(const std::string& message)
    {
        Debug->LogWarning("[dx12.compare] " + message);
    }

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

// DX11 대조. 크래시는 고쳤고, 남은 8.75% 차이는 원인을 좁혔지만 규명하지 못했다.
//
// ── 크래시의 원인은 그래프 수명이었다 ──
//
// EnhancedRenderGraph를 if 블록 안에 선언했다. 블록을 벗어나며 그래프가
// 소멸하고 transient 리소스(GBuffer 타깃·깊이)가 해제되는데, 그것들을
// 참조하는 커맨드는 아직 제출 전이었다. EndFrame의 ExecuteCommandLists가
// 이미 없는 리소스를 읽고 그 자리에서 죽었다.
//
// 스택 상단이 전부 드라이버 내부라 한참 헤맸다. 해상도도(512x288도 같음),
// 텍스처 업로드도(재질을 비워도 같음), 드로우 데이터도 아니었다 — 메시
// 업로드는 dx12.scene과 정확히 같은 값(11개 1439448바이트)이 나왔다.
// 답을 준 것은 단계별 즉시 로그와 크래시 줄 번호였다. outLog는 함수가
// 끝나야 기록되므로 크래시하면 통째로 사라진다는 것을 먼저 고쳤어야 했다.
//
// 규칙: 그래프의 수명은 그 커맨드가 GPU에 제출될 때까지다.
//
// ── 현재 결과 ──
//
//   DX11 983038 · DX12 897044 · 일치 897004
//   DX11만 86034 · DX12만 40 · 겹침 비율 0.9124
//
// DX12만 그린 픽셀이 40개뿐이다. 위치는 정확하다는 뜻이고, 이식에서 틀리기
// 쉬운 것(월드 변환·뷰 투영)은 맞다고 볼 근거가 된다.
//
// ── 남은 차이: 좁혔지만 규명하지 못했다 ──
//
// DX11만 그린 픽셀의 분포를 찍었다: x 0~1919(화면 전체 폭) · y 528~683의
// 가로 띠이고 95.2%가 덩어리 내부다. 윤곽이 한 픽셀씩 어긋난 것이 아니라
// 넓은 면이 통째로 빠졌다.
//
// 처음에는 '먼 곳의 깊이가 1.0에 붙어 클리어 값과 구분되지 않는다'로 봤다.
// 그래서 DX12 쪽도 깊이 대신 bitmask를 읽게 바꿨는데 결과가 한 픽셀도
// 바뀌지 않았다 — DX12 셰이더는 그리는 픽셀에 둘을 함께 쓰므로 두 신호가
// 같은 답을 준다. 가설이 틀렸고, DX12가 실제로 그 영역을 안 그린다.
//
// (bitmask로 바꾼 것은 되돌리지 않았다. 양쪽이 같은 신호를 세는 편이
//  대조로서 옳다 — 우연히 같았을 뿐 원래 다른 것을 묻고 있었다.)
//
// 다음에 볼 것:
//   - DX12가 그린 영역의 y 범위. 683에서 잘렸다면 far plane 쪽 문제다.
//   - DX11 GBuffer에 deferredQueue 외에 무엇이 더 그려지는가.
//     GBufferPass::TerrainRenderCommandList가 따로 있고, BitMaskPass도
//     별도로 존재한다 — 캡처한 bitmask가 GBuffer만의 결과가 아닐 수 있다.
//   - 기준 0.95는 아직 근거가 없다. 차이의 원인을 알고 나서 정할 값이다.
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

    // ── 원해상도로 대조한다 ──
    //
    // 한때 512로 줄여서 쟀다. 화면 해상도로 그리면 크래시한다고 봤기
    // 때문인데, 원인은 해상도가 아니라 그래프 수명이었다(위 주석 참고).
    //
    // 줄인 채로 두지 않는 이유는 그 측정이 공정하지 않았기 때문이다.
    // DX11 쪽은 1920x1080을 '칸에 하나라도 그려졌으면 그려진 것'으로 줄이므로
    // 커버리지가 부풀고, DX12는 줄인 해상도에서 직접 그리므로 부풀지 않는다.
    // 그 상태로 재니 겹침 0.9084에 'DX12만 그린 칸 0'이 나왔다 — DX12가
    // 덜 그린 것처럼 보이지만 실제로는 비교 방법이 만든 차이였다.
    //
    // 대조가 잡으려는 것은 두 경로의 차이인데, 대조 자신이 차이를 만들면
    // 무엇을 보고 있는지 알 수 없게 된다.
    constexpr uint32_t kMaxCompareWidth = 4096;

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

    PixelCompareTrace("DX12 초기화 진입");
    DX12DeviceResources resources;
    if (!resources.Initialize(compareWidth, compareHeight, error))
    {
        outLog += "[2/3] DX12 초기화 실패: " + error + "\n";
        return false;
    }

    PixelCompareTrace("DX12 디바이스 준비됨");

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

    PixelCompareTrace("캐시 준비됨");

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
        // ★ 그래프를 EndFrame 바깥 스코프에 둔다. 이것이 크래시의 원인이었다.
        //
        // 처음에는 아래 if (renderOk) 블록 안에 선언했다. 블록을 벗어나면서
        // 그래프가 소멸하고, 그래프가 소유한 transient 리소스(GBuffer 타깃과
        // 깊이)가 함께 해제된다. 그런데 그것들을 참조하는 커맨드는 아직
        // 제출되지 않았다 — EndFrame의 ExecuteCommandLists에서 이미 없는
        // 리소스를 읽고 그 자리에서 죽었다.
        //
        // 스택 상단이 전부 드라이버 내부라 한참 헤맸다. 해상도도, 텍스처
        // 업로드도, 드로우 데이터도 아니었다(데이터는 dx12.scene과 정확히
        // 같은 값이 나왔다). 답을 준 것은 크래시 줄 번호였다 —
        // DX12DeviceResources.cpp:309가 ExecuteCommandLists라는 것을 확인한
        // 순간 '제출 전에 무엇이 사라졌나'로 질문이 바뀌었다.
        //
        // 규칙: 그래프의 수명은 그 커맨드가 GPU에 제출될 때까지다.
        EnhancedRenderGraph graph;

        PixelCompareTrace("BeginFrame");
        if (!resources.BeginFrame(error)) { renderOk = false; }

        PixelCompareTrace("PrepareFrame(메시 업로드)");
        if (renderOk && !gbuffer.PrepareFrame(frameContext, error)) { renderOk = false; }

        if (renderOk)
        {
            gbuffer.Declare(graph, frameContext);
            const auto outputs = gbuffer.GetOutputs();

            graph.AddPass("bitmask_readback", { { outputs.bitmask, RGResourceState::CopySource } },
                [&](const EnhancedRenderGraph::ExecuteContext& executeContext)
                {
                    D3D12_TEXTURE_COPY_LOCATION dst{};
                    dst.pResource = depthReadback.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_UINT;
                    dst.PlacedFootprint.Footprint.Width = compareWidth;
                    dst.PlacedFootprint.Footprint.Height = compareHeight;
                    dst.PlacedFootprint.Footprint.Depth = 1;
                    dst.PlacedFootprint.Footprint.RowPitch = depthRowPitch;

                    D3D12_TEXTURE_COPY_LOCATION src{};
                    src.pResource = executeContext.Resolve(outputs.bitmask);
                    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                    executeContext.commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }, true);

                {
                // 무엇을 그리려 하는지 남긴다. GPU가 터지는 종류의 문제는
                // 데이터가 이상해서 나는 경우가 많은데, 커맨드만 봐서는
                // 알 수 없다.
                const auto meshStats = meshCache.GetStats();
                char trace[224]{};
                std::snprintf(trace, sizeof(trace),
                    "드로우 %zu · 메시 업로드 %u(%llu바이트) · 첫 메시 %p",
                    draws.size(), meshStats.uploads,
                    static_cast<unsigned long long>(meshStats.bytesUploaded),
                    static_cast<const void*>(draws.front().mesh));
                PixelCompareTrace(trace);
            }

            PixelCompareTrace("그래프 Compile");
            if (!graph.Compile(resources.GetDevice(), error)) { renderOk = false; }
            PixelCompareTrace("그래프 Execute");
            if (renderOk && !graph.Execute(resources.GetCommandList(), error)) { renderOk = false; }
        }

        PixelCompareTrace("EndFrame");
        if (renderOk && !resources.EndFrame(error)) { renderOk = false; }
        PixelCompareTrace("WaitForGpu");
        if (renderOk) resources.WaitForGpu();
        PixelCompareTrace("렌더 완료");
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

        // ★ 깊이가 아니라 bitmask로 센다.
        //
        // 처음에는 깊이를 읽어 '1.0이 아니면 그려진 것'으로 셌다. 그러자
        // DX11만 그린 픽셀이 86034개 나왔는데, 위치를 찍어 보니 화면 전체
        // 폭에 걸친 가로 띠(y 528~683)였고 95.2%가 덩어리 내부였다.
        // 윤곽 오차가 아니라 바닥 평면의 먼 쪽이 통째로 빠진 것이다.
        //
        // 원인은 렌더가 아니라 판정이었다. 먼 곳의 깊이는 1.0에 붙어 클리어
        // 값과 구분되지 않는다. DX11 쪽은 bitmask로 세니 깊이와 무관하게
        // '그렸다'가 되고, 두 신호가 애초에 같은 것을 묻고 있지 않았다.
        //
        // 양쪽 다 bitmask로 센다. 대조에서 가장 중요한 것은 두 쪽이 같은
        // 질문에 답하는 것이다.
        const auto* pixels = static_cast<const uint8_t*>(mapped);
        for (uint32_t y = 0; y < compareHeight; ++y)
        {
            const auto* row = reinterpret_cast<const uint32_t*>(pixels
                + static_cast<size_t>(y) * depthRowPitch);

            for (uint32_t x = 0; x < compareWidth; ++x)
            {
                dx12Coverage[static_cast<size_t>(y) * compareWidth + x] = (0 != row[x]);
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

    // DX11에만 있는 픽셀이 어디에 있는지 본다.
    //
    // 총합만으로는 '물체 하나가 통째로 빠졌다'와 '윤곽이 한 픽셀씩 다르다'를
    // 구분할 수 없다. 둘은 원인이 전혀 다르다 — 앞은 드로우 목록이나 컬링,
    // 뒤는 래스터 규칙이다.
    uint32_t missMinX = UINT32_MAX, missMaxX = 0;
    uint32_t missMinY = UINT32_MAX, missMaxY = 0;
    size_t missInterior = 0;   // 상하좌우가 모두 DX11만인 픽셀 = 덩어리 내부

    for (uint32_t y = 0; y < compareHeight; ++y)
    {
        for (uint32_t x = 0; x < compareWidth; ++x)
        {
            const size_t index = static_cast<size_t>(y) * compareWidth + x;
            if (!dx11Coverage[index] || dx12Coverage[index]) continue;

            missMinX = (std::min)(missMinX, x); missMaxX = (std::max)(missMaxX, x);
            missMinY = (std::min)(missMinY, y); missMaxY = (std::max)(missMaxY, y);

            // 가장자리는 건너뛴다.
            if (0 == x || 0 == y || x + 1 >= compareWidth || y + 1 >= compareHeight) continue;

            const auto onlyA = [&](size_t i) { return dx11Coverage[i] && !dx12Coverage[i]; };
            if (onlyA(index - 1) && onlyA(index + 1)
                && onlyA(index - compareWidth) && onlyA(index + compareWidth))
            {
                ++missInterior;
            }
        }
    }

    if (0 != onlyDX11)
    {
        char line[224]{};
        std::snprintf(line, sizeof(line),
            "        DX11만 — 범위 x %u~%u · y %u~%u · 덩어리 내부 %zu(%.1f%%)\n",
            missMinX, missMaxX, missMinY, missMaxY, missInterior,
            100.0 * static_cast<double>(missInterior) / static_cast<double>(onlyDX11));
        outLog += line;
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
