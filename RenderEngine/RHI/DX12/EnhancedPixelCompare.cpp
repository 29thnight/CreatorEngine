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
#include <map>
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
// ── 남은 차이 8.75%: 대상은 특정했고 원인은 못 찾았다 ──
//
// 관찰:
//   DX11만 그린 픽셀 — x 0~1919(화면 전체 폭) · y 528~683 · 95.2%가 덩어리 내부
//   세로 범위 — DX11 y 528~1079 · DX12 y 556~1079
//
// ★ 범인은 드로우 9번이다. 드로우별 화면 y 범위를 재 보니:
//     0:419~847  1:367~899  2~6:370~879  7,8:490~776  10:470~796
//     9:-706~2347   ← 화면을 한참 넘어서는 거대한 평면(바닥)
//   나머지 열 개는 전부 y 367~899 안에 있어 y 528 위쪽을 채울 수 없다.
//   차이 구간을 그릴 수 있는 것은 9번뿐이다.
//
//   값(bitmask)으로 추론하다 두 번 틀린 뒤 직접 센 결과다. 추론보다 세는 것이
//   빨랐다.
//
// 그래서 질문이 좁혀졌다: 왜 DX12는 그 거대한 평면의 먼 쪽을 덜 그리는가.
//
// 배제한 것(각각 확인함):
//   1. 깊이 판정 방식 — DX12도 깊이 대신 bitmask를 읽게 바꿨으나 결과가 한
//      픽셀도 안 바뀌었다. 셰이더가 그리는 픽셀에 둘을 함께 쓰므로 같은 답이다.
//   2. 다른 렌더 큐 — terrain·foliage·forward 모두 0이다.
//   3. 깊이 포맷 — 양쪽 다 D32_FLOAT(RenderPassData.cpp:124, kDepthFormat).
//   4. 깊이 비교 함수 — 양쪽 다 LESS(RenderModules.cpp DepthPreset::Default,
//      DX12PSOManager.cpp:192).
//   5. 투영 행렬의 출처 — DX11의 BindFrameCameraBuffers도 이 검증도 같은
//      프레임 밀봉 스냅샷(GetFrameSnapshot)에서 값을 가져온다.
//   6. bitmask 잔상 — GBufferPass::Execute가 매 프레임 모든 RTV를 지운다.
//   7. BitMaskPass — bitmask를 SRV로 읽기만 한다.
//   8. 오브젝트 누락 — 9번은 DX12도 그린다(y 556부터). 안 그리는 것이 아니라
//      먼 쪽만 덜 그린다.
//
// ★ 잘림은 삼각형 단위다(확인함).
//   열마다 DX12 커버리지의 위쪽 끝을 재 보니 y 556~684로 128픽셀에 걸쳐
//   변동하고 계단이 94곳이다. far plane 클리핑은 화면 공간 수평선을 따라
//   일어나므로 모든 열에서 같은 y여야 한다 — 직선이 아니므로 클리핑이 아니다.
//   삼각형이 통째로 빠지고 있다.
//
// ★ 컬링도 아니다(확인함). 오히려 반대 방향이다.
//   DX11은 CD3D11_DEFAULT() 래스터라이저라 CullMode가 BACK이고,
//   DX12는 D3D12_CULL_MODE_NONE이다(EnhancedGBufferPass.cpp:423).
//   DX12가 컬링을 덜 하므로 더 많이 그려야 하는데 덜 그린다.
//   다만 이 차이는 반대편 관찰을 설명한다 — DX12만 그린 40픽셀이 그것이다.
//   (양쪽을 맞출지는 별도 판단이다. 컬링을 끈 데는 이유가 있었다.)
//
// ── z-fighting 가설: 기각됐다 ──
//
// 결정적 실험: 문제의 드로우를 단독으로 그린다. 겹칠 상대가 없는데도 같은
// 자리가 비면 겹침이 원인이 아니다.
//
// 실험을 한 번 잘못 짰다가 고쳤다. 처음에는 대상 선택을 여기서만 다른 식
// (월드 반지름)으로 계산해 엉뚱한 드로우를 집었고, 단독 렌더가 실제로
// 반영되는지도 찍지 않았다. 대상을 바꿔도 결과가 픽셀 하나까지 같게 나오는
// 것을 보고서야 실험을 의심했다. 고친 것 둘:
//   - 대상 선택을 '드로우별 화면 y 범위' 진단과 같은 식으로 통일했다.
//     지금은 두 진단이 같은 드로우(4번)를 지목한다.
//   - 단독 렌더에 실제 드로우 수와 배치 수를 함께 찍는다. 1·1로 나온다.
//
// 결과: 그 평면을 단독으로 그려도 y 634~1079다(픽셀 839654).
//   겹칠 상대가 전혀 없는데 같은 한계에 걸린다. 겹침이 원인이 아니다.
//   오히려 단독일 때 더 적게 그린다 — 전체 렌더의 556~634는 다른 드로우들이
//   채우는 몫이었다.
//
// 남은 사실: 이 평면은 DX12에서 y 634까지만 그려진다. DX11은 528까지 그린다.
//   삼각형 단위로 빠지는 것은 확인됐고(경계가 들쭉날쭉), 겹침도 클리핑도
//   컬링도 아니다.
//
// 다음에 볼 것:
//   - 단독 렌더의 경계 모양을 따로 잴 것. 앞서 '들쭉날쭉'을 잰 것은 여러
//     드로우가 섞인 전체 렌더였다. 이 평면 하나만의 경계가 직선인지 계단인지
//     보면 클리핑과 삼각형 누락을 다시 가를 수 있다.
//   - 그 메시의 삼각형 수와 정점 분포. 평면이 격자로 잘려 있다면 어느 줄부터
//     빠지는지가 far 거리와 대응하는지 볼 것.
//   - DX11이 이 드로우를 인스턴싱 경로(m_instancePSO)로 그리는지. 정점
//     스트라이드나 입력 레이아웃이 다르면 같은 버퍼를 다르게 읽는다.
//
//   - 판정 기준 0.95는 여전히 근거가 없다. 원인을 알고 나서 정할 값이다.
//
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

    // 한 번 그리고 커버리지를 돌려준다.
    //
    // 두 번 쓴다 — 전체 드로우로 한 번, z-fighting 가설을 확인하려고 문제의
    // 드로우만 단독으로 한 번. 겹칠 상대가 없는데도 같은 자리가 빠지면
    // 겹침이 원인이 아니다.
    const auto renderCoverage = [&](const std::vector<EnhancedDrawItem>& useDraws,
        std::vector<bool>& outCoverage) -> bool
    {
        frameContext.draws = &useDraws;
        outCoverage.assign(static_cast<size_t>(compareWidth) * compareHeight, false);

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
                    useDraws.size(), meshStats.uploads,
                    static_cast<unsigned long long>(meshStats.bytesUploaded),
                    static_cast<const void*>(useDraws.front().mesh));
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
                outCoverage[static_cast<size_t>(y) * compareWidth + x] = (0 != row[x]);
            }
        }

        depthReadback->Unmap(0, nullptr);
    }
        return true;
    };

    std::vector<bool> dx12Coverage;
    if (!renderCoverage(draws, dx12Coverage))
    {
        outLog += "[2/3] DX12 렌더 실패: " + error + "\n";
        gbuffer.Shutdown();
        resources.Shutdown();
        return false;
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

    // ── 어느 오브젝트가 빠졌는가 ──
    //
    // DX11 bitmask는 그린 것이 무엇인지 값으로 남긴다. 차이 나는 띠에만
    // 나타나는 값이 있으면 그것이 범인을 지목한다. 전체에 고루 있는 값이면
    // 오브젝트가 빠진 것이 아니라 다른 이유다.
    {
        std::map<uint32_t, size_t> valuesInGap;     // 차이 나는 곳
        std::map<uint32_t, size_t> valuesInMatch;   // 양쪽 다 그린 곳

        for (uint32_t y = 0; y < dx11Height; ++y)
        {
            const auto* row = reinterpret_cast<const uint32_t*>(dx11Bytes.data()
                + static_cast<size_t>(y) * dx11RowPitch);

            for (uint32_t x = 0; x < dx11Width; ++x)
            {
                if (0 == row[x]) continue;

                const size_t index = static_cast<size_t>(y) * compareWidth + x;
                if (index >= dx12Coverage.size()) continue;

                if (dx12Coverage[index]) ++valuesInMatch[row[x]];
                else                     ++valuesInGap[row[x]];
            }
        }

        std::string summary = "        bitmask 값 — 차이 구간: ";
        for (const auto& entry : valuesInGap)
        {
            summary += std::to_string(entry.first) + "(" + std::to_string(entry.second) + ") ";
        }
        summary += "· 일치 구간: ";
        for (const auto& entry : valuesInMatch)
        {
            summary += std::to_string(entry.first) + "(" + std::to_string(entry.second) + ") ";
        }
        outLog += summary + "\n";
    }

    // ── z-fighting 가설의 결정적 실험 ──
    //
    // 가설: 그 평면이 다른 면과 같은 깊이에 겹쳐 있고, 두 경로의 부동소수
    // 계산 차이로 겹친 면의 승자가 갈린다.
    //
    // 그렇다면 그 평면을 단독으로 그릴 때는 겹칠 상대가 없으므로 전부
    // 그려져야 한다. 단독으로 그려도 같은 자리가 빠지면 겹침이 원인이 아니다.
    //
    // 대상은 화면에서 가장 넓게 걸치는 드로우로 고른다 — 앞선 측정에서
    // 그것(9번)만이 차이 구간을 그릴 수 있었다.
    {
        size_t widestIndex = 0;
        float widestSpan = -1.f;

        const Mathf::xMatrix viewProjection =
            XMMatrixMultiply(cameraSnapshot.view, cameraSnapshot.projection);

        for (size_t i = 0; i < draws.size(); ++i)
        {
            if (nullptr == draws[i].mesh) continue;

            // ★ 뒤쪽 '드로우별 화면 y 범위' 진단과 같은 식을 쓴다.
            //
            // 앞서 여기만 다른 식을 써서 엉뚱한 드로우를 집었다. 같은 질문에
            // 두 코드가 다른 답을 내면 둘 중 하나는 틀린 것이고, 어느 쪽인지
            // 알 수 없다. 식을 하나로 맞춘다.
            const auto sphere = draws[i].mesh->GetBoundingSphere();
            const float scale = (std::max)({
                XMVectorGetX(XMVector3Length(draws[i].worldMatrix.r[0])),
                XMVectorGetX(XMVector3Length(draws[i].worldMatrix.r[1])),
                XMVectorGetX(XMVector3Length(draws[i].worldMatrix.r[2])) });

            const Mathf::xVector center = XMVector3Transform(
                XMVectorSet(sphere.Center.x, sphere.Center.y, sphere.Center.z, 1.f),
                draws[i].worldMatrix);
            const Mathf::xVector clip = XMVector4Transform(center, viewProjection);
            const float w = XMVectorGetW(clip);
            if (w <= 0.f) continue;

            const float ndcY = XMVectorGetY(clip) / w;
            const float screenY = (1.f - ndcY) * 0.5f * static_cast<float>(compareHeight);
            const float screenR = (sphere.Radius * scale) / w
                * 0.5f * static_cast<float>(compareHeight);

            // 차이 구간의 위쪽(DX12가 안 그린 y 528~556)을 덮을 수 있는
            // 드로우여야 한다. 그중 화면에서 가장 넓게 걸치는 것을 고른다.
            const float top = screenY - screenR;
            const float bottom = screenY + screenR;
            if (bottom < 528.f || top > 556.f) continue;

            const float span = bottom - top;
            if (span > widestSpan) { widestSpan = span; widestIndex = i; }
        }

        std::vector<EnhancedDrawItem> soloDraw{ draws[widestIndex] };
        std::vector<bool> soloCoverage;

        if (renderCoverage(soloDraw, soloCoverage))
        {
            uint32_t soloTop = UINT32_MAX, soloBottom = 0;
            size_t soloCovered = 0;
            for (uint32_t y = 0; y < compareHeight; ++y)
            {
                for (uint32_t x = 0; x < compareWidth; ++x)
                {
                    if (!soloCoverage[static_cast<size_t>(y) * compareWidth + x]) continue;
                    soloTop = (std::min)(soloTop, y);
                    soloBottom = (std::max)(soloBottom, y);
                    ++soloCovered;
                }
            }

            char line[288]{};
            std::snprintf(line, sizeof(line),
                "        단독 렌더(드로우 %zu · 실제 드로우 %u · 배치 %u) — y %u~%u"
                " · 픽셀 %zu → %s\n",
                widestIndex, gbuffer.GetLastDrawCount(), gbuffer.GetLastBatchCount(),
                soloTop, soloBottom, soloCovered,
                (soloTop <= 530u) ? "겹침이 원인(z-fighting 쪽)"
                                  : "단독으로도 같은 자리가 빈다(겹침 아님)");
            outLog += line;
        }

        // 전체 드로우로 되돌린다. 뒤의 단정이 이 목록을 본다.
        frameContext.draws = &draws;
    }

    // ── 잘린 경계의 모양 ──
    //
    // 이것이 '삼각형 단위로 잘리는가'에 직접 답한다.
    //
    // 열마다 DX12 커버리지의 위쪽 끝(가장 작은 y)을 구한다. far plane
    // 클리핑이면 그 끝이 모든 열에서 같아 직선이 된다 — 클리핑은 화면 공간의
    // 수평선을 따라 일어나기 때문이다. 삼각형이 통째로 빠지는 것이면 빠진
    // 삼각형의 폭만큼 계단이 생겨 열마다 값이 튄다.
    {
        std::vector<uint32_t> topPerColumn(compareWidth, UINT32_MAX);
        for (uint32_t x = 0; x < compareWidth; ++x)
        {
            for (uint32_t y = 0; y < compareHeight; ++y)
            {
                if (dx12Coverage[static_cast<size_t>(y) * compareWidth + x])
                {
                    topPerColumn[x] = y;
                    break;
                }
            }
        }

        uint32_t minTop = UINT32_MAX, maxTop = 0;
        size_t   columns = 0;
        double   sum = 0.0;
        size_t   steps = 0;      // 이웃 열과 2 이상 차이 나는 곳
        uint32_t previous = UINT32_MAX;

        for (uint32_t x = 0; x < compareWidth; ++x)
        {
            const uint32_t top = topPerColumn[x];
            if (UINT32_MAX == top) { previous = UINT32_MAX; continue; }

            minTop = (std::min)(minTop, top);
            maxTop = (std::max)(maxTop, top);
            sum += top;
            ++columns;

            if (UINT32_MAX != previous)
            {
                const uint32_t delta = (top > previous) ? (top - previous) : (previous - top);
                if (delta >= 2) ++steps;
            }
            previous = top;
        }

        if (0 != columns)
        {
            char line[256]{};
            std::snprintf(line, sizeof(line),
                "        DX12 상단 경계 — 열 %zu · y %u~%u(평균 %.1f) · 계단 %zu곳"
                " → %s\n",
                columns, minTop, maxTop, sum / static_cast<double>(columns), steps,
                (steps <= columns / 100) ? "직선(클리핑 쪽)" : "들쭉날쭉(삼각형 단위 쪽)");
            outLog += line;
        }
    }

    // ── 어느 드로우가 그 띠를 그리는가 ──
    //
    // 값으로 추론하는 것을 그만두고 직접 센다. DX11 커버리지에서 차이 나는
    // 영역의 y 범위를 잡고, 드로우마다 그 범위에 걸치는지 화면 공간 경계로
    // 확인한다. 11개뿐이라 전부 훑어도 싸다.
    {
        uint32_t gapMinY = UINT32_MAX, gapMaxY = 0;
        for (uint32_t y = 0; y < compareHeight; ++y)
        {
            for (uint32_t x = 0; x < compareWidth; ++x)
            {
                const size_t index = static_cast<size_t>(y) * compareWidth + x;
                if (dx11Coverage[index] && !dx12Coverage[index])
                {
                    gapMinY = (std::min)(gapMinY, y);
                    gapMaxY = (std::max)(gapMaxY, y);
                }
            }
        }

        const Mathf::xMatrix viewProjection =
            XMMatrixMultiply(cameraSnapshot.view, cameraSnapshot.projection);

        std::string report = "        드로우별 화면 y 범위 — ";
        for (size_t i = 0; i < draws.size(); ++i)
        {
            const auto* mesh = draws[i].mesh;
            if (nullptr == mesh) continue;

            // 경계 구를 화면으로 옮긴다. 정확한 실루엣이 아니라 어느 드로우가
            // 그 띠에 닿는지만 보면 되므로 이것으로 충분하다.
            const auto sphere = mesh->GetBoundingSphere();
            const Mathf::xVector center = XMVector3Transform(
                XMVectorSet(sphere.Center.x, sphere.Center.y, sphere.Center.z, 1.f),
                draws[i].worldMatrix);

            const float scale = (std::max)({
                XMVectorGetX(XMVector3Length(draws[i].worldMatrix.r[0])),
                XMVectorGetX(XMVector3Length(draws[i].worldMatrix.r[1])),
                XMVectorGetX(XMVector3Length(draws[i].worldMatrix.r[2])) });
            const float radius = sphere.Radius * scale;

            const Mathf::xVector clip = XMVector4Transform(center, viewProjection);
            const float w = XMVectorGetW(clip);
            if (w <= 0.f) { report += std::to_string(i) + ":뒤 "; continue; }

            const float ndcY = XMVectorGetY(clip) / w;
            const float screenY = (1.f - ndcY) * 0.5f * static_cast<float>(compareHeight);
            const float screenR = radius / w * 0.5f * static_cast<float>(compareHeight);

            const int top = static_cast<int>(screenY - screenR);
            const int bottom = static_cast<int>(screenY + screenR);
            const bool touchesGap = (bottom >= static_cast<int>(gapMinY))
                && (top <= static_cast<int>(gapMaxY));

            report += std::to_string(i) + ":" + std::to_string(top) + "~"
                + std::to_string(bottom) + (touchesGap ? "* " : " ");
        }

        outLog += report + "(*=차이 구간 y " + std::to_string(gapMinY) + "~"
            + std::to_string(gapMaxY) + "에 걸침)\n";
    }

    // 두 커버리지의 세로 범위. DX12가 위나 아래에서 잘렸는지 본다.
    {
        uint32_t a0 = UINT32_MAX, a1 = 0, b0 = UINT32_MAX, b1 = 0;
        for (uint32_t y = 0; y < compareHeight; ++y)
        {
            for (uint32_t x = 0; x < compareWidth; ++x)
            {
                const size_t index = static_cast<size_t>(y) * compareWidth + x;
                if (dx11Coverage[index]) { a0 = (std::min)(a0, y); a1 = (std::max)(a1, y); }
                if (dx12Coverage[index]) { b0 = (std::min)(b0, y); b1 = (std::max)(b1, y); }
            }
        }

        char line[160]{};
        std::snprintf(line, sizeof(line),
            "        세로 범위 — DX11 y %u~%u · DX12 y %u~%u\n", a0, a1, b0, b1);
        outLog += line;
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
