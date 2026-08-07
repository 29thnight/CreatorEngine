#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ID3D11ShaderResourceView;
class Camera;
class RenderScene;
class Scene;

/// 패스 하나의 GPU 시간. DX12GpuProfiler::PassTiming을 에디터로 옮기는 값
/// 타입이다 — 그 헤더는 d3d12.h를 끌고 오므로 UI 계층에 노출하지 않는다.
struct EnhancedLivePassTiming
{
    std::string name;
    double      milliseconds{ 0.0 };
};

/// 파이프라인 설정 창이 조작하는 패스 파라미터의 미러.
///
/// 패스의 Tuning 구조체를 그대로 노출하지 않는 이유는 두 가지다. 그 헤더는
/// d3d12.h를 끌고 오므로 에디터 계층에 새면 안 되고, 패스 인스턴스는
/// LivePipeline이 소유해 게임 스레드가 매 프레임 만진다 — 창은 CE 렌더
/// 스레드에서 그려지므로 직접 쓰면 경합이다.
///
/// 그래서 창은 이 값 타입으로 읽고 쓰고, 실제 SetTuning은 게임 스레드가
/// TickLive에서 수행한다. 필드가 패스 Tuning과 중복되는 비용은 있지만,
/// 그 대가로 스레드 경계가 한 곳(뮤텍스)에 모인다.
///
/// 여기 없는 패스는 조정 파라미터 자체가 없거나(GBuffer·Deferred·Shadow·
/// SkyBox·Forward·Grid·Gizmo·Decal 계열) 아직 라이브 그래프에 배선되지 않은
/// 것이다(UI — Tuning은 있으나 LivePipeline이 들지 않는다).
///
/// 데칼이 여기 없는 이유는 조정할 것이 없어서다 — 그릴 데칼은 씬의 프록시가
/// 정하고, 하나도 없으면 패스가 아무것도 선언하지 않아 비용이 0이다.
/// 그래서 켬/끔 스위치도 두지 않는다.
struct EnhancedLiveTuning
{
    struct Ssao
    {
        float radius{ 0.5f };
        float thickness{ 0.25f };
        float intensity{ 1.f };
        float filterDepthSigma{ 0.05f };
    } ssao;

    struct Ssgi
    {
        float traceDistance{ 8.f };
        float traceThickness{ 0.5f };
        float accumDepthTolerance{ 0.01f };
        float filterDepthSigma{ 0.01f };
        float filterNormalPower{ 16.f };
        float compositeDepthSigma{ 0.01f };
        float intensity{ 1.f };
    } ssgi;

    /// 볼류메트릭 포그. 기본이 꺼짐인 이유는 두 가지다 — 켤 때 메모리가
    /// +127MB 늘고(프록셀 격자가 뷰당 42MB, 실측), 켜면 모든 씬의 그림이
    /// 달라지므로 저작이 고르는 것이 맞다. 자원은 처음 켜질 때 잡는다.
    struct Fog
    {
        bool  enabled{ false };

        // 아래는 DX11 VolumetricFogPassSetting 기본값 그대로다.
        float anisotropy{ 0.109f };
        float density{ 0.101f };
        float strength{ 2.f };
        float thicknessFactor{ 0.01f };
        float blendingWithSceneColorFactor{ 0.851f };
        float previousFrameBlendFactor{ 0.95f };
        float customNearPlane{ 0.5f };
        float customFarPlane{ 1000.f };
    } fog;

    /// 서브서피스 스캐터링. 기본이 꺼짐인 이유는 이 패스가 재질 마스크 없이
    /// 화면 전체를 번지게 하기 때문이다 — DX11은 기본이 켜짐이었지만 그것은
    /// 모든 씬의 그림에 블러를 한 겹 얹는다는 뜻이고, 저작이 고르는 것이 맞다
    /// (포그와 같은 판단). 파라미터 기본값은 DX11 그대로다.
    struct Sss
    {
        bool  enabled{ false };
        float strength{ 1.35f };
        float width{ 0.013f };
    } sss;

    /// 스크린 스페이스 반사. DX11도 기본이 꺼짐이다(isOn = false) —
    /// 여기서 그 기본값을 그대로 따른다. 파라미터도 DX11 그대로다.
    struct Ssr
    {
        bool  enabled{ false };
        float stepSize{ 0.114f };
        float maxThickness{ 0.00416f };
        int   maxRayCount{ 20 };
    } ssr;

    struct PostChain
    {
        bool  bloomEnabled{ true };
        float bloomThreshold{ 1.f };
        float bloomKnee{ 0.5f };
        float bloomIntensity{ 0.05f };

        bool  toneMapEnabled{ true };
        /// EnhancedPostChainPass::ToneMapper와 같은 값(0=ACES, 1=AgX).
        int   toneMapper{ 1 };
        float exposure{ 0.7f };

        bool  vignetteEnabled{ true };
        float vignetteRadius{ 0.75f };
        float vignetteSoftness{ 0.5f };
        /// 감광 상한(0=무효과, 1=완전 감광). 기본은 코너 30% 감광.
        float vignetteIntensity{ 0.3f };

        bool  gradingEnabled{ true };
        float saturation{ 1.f };
        float contrast{ 1.f };

        bool  fxaaEnabled{ true };
        float fxaaBias{ 0.688f };
        float fxaaBiasMin{ 0.021f };
        float fxaaSpanMax{ 8.f };
    } postChain;
};

/// 렌더 디버그 창이 읽는 상시 러너의 한 시점 스냅샷.
///
/// 포인터가 아니라 값으로 옮기는 이유: 창은 내부 상태의 수명이나 갱신
/// 시점을 모른다. 파이프라인은 리사이즈마다 통째로 다시 서고 패스 타이밍
/// 벡터는 프레임마다 교체되므로, 내부를 가리키게 하면 창이 언제든 사라진
/// 것을 읽게 된다.
struct EnhancedLiveDebugSnapshot
{
    bool     enabled{ false };
    bool     pipelineReady{ false };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint64_t framesRendered{ 0 };
    uint64_t framesIdle{ 0 };
    uint64_t framesInFlight{ 0 };
    uint32_t drawCount{ 0 };
    uint32_t batchCount{ 0 };

    /// 이번 프레임에 그린 데칼 수와 실제 발행한 드로우 수. 데칼은 씬에
    /// 프록시가 없으면 패스가 통째로 빠지므로, 0이 고장인지 '데칼이 없는
    /// 씬'인지는 이 값만으로는 갈리지 않는다 — 갈리는 것은 둘의 관계다.
    /// 데칼이 있는데 배치가 0이면 텍스처 운반이 실패한 것이고, 둘이 늘
    /// 같으면 인스턴싱이 죽은 것이다.
    uint32_t decalCount{ 0 };
    uint32_t decalBatchCount{ 0 };
    double   cpuMs{ 0.0 };
    double   gpuMs{ 0.0 };
    size_t   graveyardCount{ 0 };
    std::string lastError;

    /// 마지막으로 수집에 성공한 프레임의 패스별 GPU 시간. 선언 순서 그대로다.
    std::vector<EnhancedLivePassTiming> passTimings;

    /// 지금까지 처음 관측한 D3D12 검증 레이어 메시지(Debug 빌드에서만 쌓인다).
    std::vector<std::string> validationMessages;
};

// CreatorEngine의 단독 DX12 씬 렌더러. Run* 진단 표면과 메인 런타임을 함께
// 제공하며, 기존 SceneRenderer(DX11)는 메인 배선에서 제거된 dead code다.
class EnhancedSceneRenderer
{
public:
    // 브링업 자가 검증. frameCount 프레임을 그려 얼로케이터 회전(3개 × 2회전 이상
    // 권장)과 펜스 동기화를 검증하고, 마지막 프레임을 PNG로 저장한다.
    //
    // 통과 조건(전부 만족해야 true):
    //   1) 디바이스·큐·PSO 생성 성공   2) frameCount 프레임 제출·완료
    //   3) 리드백 픽셀이 기대 내용(배경 클리어 + 삼각형)   4) 검증 레이어 메시지 0건
    // 진행 로그는 outLog로 — 실패 지점이 어디인지가 곧 진단이다.
    bool RunSelfTest(const std::string& outputPngPath, uint32_t frameCount, std::string& outLog);

    // PSO 캐시 자가 검증(PHASE 3-4). 같은 프로세스에서 매니저를 두 번 세워
    // 캐시 파일이 실제로 컴파일을 없애는지 확인한다.
    //
    // 통과 조건:
    //   1회차  컴파일 N건(캐시 파일이 없거나 무효면 정상)
    //   2회차  컴파일 0건 · 라이브러리 히트 N건 — 이것이 완료 기준
    //   메모리 캐시  같은 desc 재요청이 컴파일 없이 히트
    //   비동기       Pending으로 시작해 폴백 없이 결국 Ready
    bool RunPsoCacheTest(const std::string& cacheFilePath, std::string& outLog);

    /// 업로드 링 자가 검증(PHASE 3-3).
    ///
    /// 링의 계약은 눈에 보이지 않는다 — 잘못돼도 "다음 프레임에 가끔 이상한 것이
    /// 보인다"로만 드러난다. 그래서 규약을 직접 단정한다:
    ///   ① 요청한 정렬이 지켜지는가
    ///   ② 프레임 구간이 서로 겹치지 않는가(다른 프레임 데이터를 덮지 않는가)
    ///   ③ BeginFrame이 같은 구간을 되감는가(무한정 자라지 않는가)
    ///   ④ 구간을 넘기면 조용히 침범하지 않고 거절하는가
    ///   ⑤ 링을 거친 데이터가 실제로 GPU 텍스처에 도달하는가(리드백 대조)
    bool RunUploadRingTest(std::string& outLog);

    /// 디스크립터 링·샘플러 힙 자가 검증(PHASE 3-4).
    ///
    /// 링 계약은 업로드 링과 같은 부류라 같은 것을 단정한다(핸들 연속·구간 분리·
    /// 되감기·넘침 거절). 샘플러는 성격이 달라 중복 제거를 본다.
    ///
    /// GPU 도달은 여기서 따로 재지 않는다 — RunSelfTest의 픽셀 검증이 이미 링에서
    /// 자른 SRV와 샘플러 힙을 거쳐 체커보드 색을 대조하므로, 그것이 곧 종단 증명이다.
    bool RunDescriptorHeapTest(std::string& outLog);

    /// 병존 출력 경로 실증 (PHASE 3-3 미결 항목 / 3-9 / 8-2 공동 결정).
    ///
    /// 에디터의 씬 뷰·게임 뷰는 스왑체인에 직접 그리지 않는다 — 오프스크린
    /// 텍스처의 SRV를 ImGui::Image로 표시한다. 그래서 DX12에 스왑체인이 필요
    /// 없고, 필요한 것은 "DX12가 그린 텍스처를 DX11이 SRV로 여는 것"뿐이다.
    ///
    /// ★ 이 경로는 병존 기간 전용이고 소멸 시점이 정해져 있다.
    ///
    /// 지금 공유가 필요한 이유는 창과 ImGui가 DX11에 묶여 있기 때문이다 —
    /// ImGui가 DX11 백엔드로 백버퍼에 그리므로, DX12가 그린 것을 화면에 올리려면
    /// DX11이 그 텍스처를 볼 수 있어야 한다.
    ///
    /// 교체(3-9)가 끝나면 DX12가 스왑체인을 갖고 셸도 PHASE 8에서 바뀌므로
    /// 공유할 이유가 없어진다. GetNativeHandle()과 같은 부류의 전환기 장치이고,
    /// 3-9에서 이 함수와 공유 경로를 함께 제거하는 것이 계약이다.
    ///
    /// 여기서 그 경로를 끝까지 뚫고 픽셀로 확인한다:
    ///   ① DX11과 같은 어댑터에 DX12 디바이스를 만든다(LUID 일치 — 공유의 전제)
    ///   ② 공유 가능한 텍스처를 만들어 DX12로 그린다
    ///   ③ 공유 핸들을 DX11에서 열어 SRV를 만든다
    ///   ④ DX11로 되읽어 DX12가 그린 픽셀과 대조한다
    bool RunSharedTextureTest(std::string& outLog);

    /// 렌더 그래프 자가 검증 (PHASE 3-5).
    ///
    /// 그래프가 하는 일은 전부 눈에 보이지 않는다 — 순서를 정하고, 배리어를 만들고,
    /// 안 쓰는 패스를 걷어낸다. 틀려도 화면에는 '가끔 이상하게 보인다'로만 나타나서
    /// 그때는 원인이 그래프인지 패스인지도 구분되지 않는다. 계약을 직접 단정한다.
    bool RunRenderGraphTest(std::string& outLog);

    /// GBuffer 패스 검증 (PHASE 3-6, 첫 패스).
    ///
    /// 이 슬라이스에서 새로 뚫는 것을 각각 확인한다: 입력 조립(정점·인덱스 버퍼),
    /// MRT 5개 동시 출력, 깊이, 그래프를 통한 transient 선언과 자동 배리어.
    ///
    /// 다섯 타깃에 서로 다른 값을 쓰게 해 둔 이유가 여기 있다 — 한 타깃만
    /// 기록되고 나머지가 비어 있어도 '그려지긴 한다'로 보이는 것을 막는다.
    bool RunGBufferTest(std::string& outLog);

    /// 씬 연결 검증 (PHASE 3-6, 씬 연결 슬라이스).
    ///
    /// 활성 씬의 카메라 스냅샷과 렌더 프록시를 DX12 GBuffer에 물려 실제로
    /// 그린다. 에디터가 떠 있어야 의미가 있다.
    ///
    /// 단정의 핵심은 '카메라 상수가 정말 셰이더에 닿는가'다. 같은 씬을 두 카메라로
    /// 그려 커버리지가 달라지는지 본다 — 상수가 안 닿으면 두 결과가 같다.
    bool RunSceneBindingTest(std::string& outLog);

    /// LEGACY DEAD DIAGNOSTIC. SceneRenderer 단독 운용 제거 뒤 호출 지점이
    /// 없으며 콘솔에서도 차단한다. 과거 이관 근거 보존용이다.
    bool RunPixelCompareTest(std::string& outLog);

    /// 크기 추종 검증 (해상도 슬라이스).
    ///
    /// 창을 리사이즈해도 렌더 타깃이 따라오지 않던 문제를 고치면서 넣었다.
    /// 원인이 눈에 안 보이는 부류였다 — Texture::Resize2DViews가 새 크기를
    /// 받고도 쓰지 않아서, 리사이즈는 '같은 크기로 다시 만들기'였고 뷰포트만
    /// 새 크기를 따라가 화면이 구석에 몰려 보였다. 코드를 봐야 알 수 있고
    /// 화면만 봐서는 원인을 특정할 수 없는 종류라, 수치로 단정한다.
    ///
    ///   ① DX11: 따라가겠다고 선언한 텍스처는 크기가 바뀐다
    ///   ② DX11: 선언하지 않은 텍스처는 그대로다(그림자 맵·LUT가 여기 속한다)
    ///   ③ DX11: 1/N 해상도 버퍼는 나눈 크기로 따라간다
    ///   ④ DX12: 버스를 구독한 디바이스가 새 크기로 타깃을 다시 만든다
    ///   ⑤ DX12: 리사이즈 뒤에도 그 타깃에 그리고 되읽을 수 있다
    ///
    /// ②가 없으면 '전부 따라가게' 고친 것과 구분되지 않는다 — 그건 그림자
    /// 해상도가 창에 따라 출렁이는 다른 버그다.
    bool RunScreenResizeTest(std::string& outLog);

    /// 커맨드 기록 병렬화 검증 (PHASE 3-6).
    ///
    /// 병렬화는 '빨라졌는가'보다 '같은 그림이 나오는가'가 먼저다. 기록 순서나
    /// 링 할당이 어긋나면 결과가 가끔 달라지는데, 그 '가끔'은 성능 수치에
    /// 드러나지 않는다.
    ///
    ///   ① 업로드 링을 여러 스레드가 동시에 잘라도 구간이 겹치지 않는가
    ///   ② 디스크립터 링도 같은가
    ///   ③ 같은 그래프를 순차/병렬로 실행한 결과 픽셀이 완전히 같은가
    ///   ④ 제출 순서가 선언 순서를 지키는가(마지막 패스의 색이 남는가)
    ///
    /// ①②가 없으면 ③이 우연히 통과할 수 있다 — 경합은 매번 나지 않는다.
    bool RunParallelRecordTest(std::string& outLog);

    /// SSGI 파이프라인 검증 (PHASE 3-6, 신규 SSGI).
    ///
    /// 셰이더는 런타임에 컴파일되므로 C++ 빌드가 통과해도 HLSL 오류는
    /// 드러나지 않는다. 실제로 선언하지 않은 샘플러를 쓰는 코드가 빌드를
    /// 통과했다 — 부르는 곳이 없으면 컴파일 자체가 안 돌기 때문이다.
    ///
    /// 그래서 패스를 부르는 자리를 먼저 만든다. Hi-Z 밉 체인과 트레이스가
    /// 실제로 도는지, 밉이 기대한 수만큼 생기는지, 결과가 0이 아닌지를 본다.
    bool RunSSGITest(std::string& outLog);

    /// Forward+ 컬링 검증 (PHASE 3-6).
    ///
    /// 알려진 배치로 단정한다: 광원 하나를 화면 중앙 표면 위에 두면 중앙
    /// 타일 카운트는 1, 구석은 0이어야 한다. 중앙이 0이면 광원이 사라지고,
    /// 구석이 1이면 컬링이 안 도는 것이다 — 둘 다 조용히 지나가는 부류다.
    bool RunForwardPlusTest(std::string& outLog);

    /// Forward+ 셰이딩 검증 (PHASE 3-6).
    ///
    /// 타일 목록으로 그린 그림과 전 광원 루프로 그린 그림을 픽셀로 대조한다.
    /// 컬링 검증(RunForwardPlusTest)이 목록의 숫자를 보는 데 반해 여기서는
    /// 그 목록을 쓴 결과를 본다 — 목록이 맞아도 셰이더가 잘못 읽으면
    /// 숫자 검증은 통과하고 화면만 틀린다.
    ///
    /// 배치를 광원 격자로 짠 이유가 여기 있다. 광원 하나짜리로 대조하면
    /// 광원이 안 닿는 곳은 참조 경로도 0이라, 컬링이 통째로 틀려도 두 결과가
    /// 같아 통과한다 — 공짜 통과를 막는 것이 배치의 목적이다.
    bool RunForwardPlusShadeTest(std::string& outLog);

    /// Forward+ 광원 수 스케일링 실측 (PHASE 3-6).
    ///
    /// "Forward+가 언제부터 이기는가"를 잰다. 광원이 적으면 컬링 디스패치가
    /// 그대로 손해이므로, 그 경계를 짐작이 아니라 수로 적는다.
    ///
    /// 128x128 자가 검증에서 재지 않는 이유는 셰이딩 비용이 픽셀 수에
    /// 비례하기 때문이다 — 픽셀이 적으면 광원을 아무리 늘려도 두 경로 다
    /// 타임스탬프 분해능에 묻혀, 거기서 나온 수는 경계를 말해 주지 않는다.
    bool RunForwardPlusScaleTest(std::string& outLog);

    /// SSAO 검증 (PHASE 3-6, 신규 SSAO).
    ///
    /// 알려진 배치로 단정한다: 화면 절반은 평평한 벽, 절반은 그보다 앞으로
    /// 튀어나온 벽이라 가운데에 수직 계단이 하나 생긴다. 평평한 곳은 AO가
    /// 1에 가깝고 계단 안쪽은 확실히 낮아야 한다.
    ///
    /// 두 방향의 실패가 모두 위험해서 이 배치를 골랐다. 평평한 곳이
    /// 어두우면 자기 자신을 가림막으로 세는 것이고(화면 전체가 탁해진다),
    /// 계단이 밝으면 AO가 아무 일도 안 하는 것이다 — 둘 다 그림만 봐서는
    /// '좀 이상한데' 이상으로 특정되지 않는다.
    ///
    /// 필터 전후를 둘 다 리드백해 이웃 차이가 실제로 줄었는지도 본다.
    /// 필터가 죽어 있어도 최종 그림만 봐서는 알 수 없기 때문이다.
    bool RunSSAOTest(std::string& outLog);

    /// SSAO 신구 시간 비교 (PHASE 3-6).
    ///
    /// 실제 DX11 패스와 직접 재지 않는다. 그러면 API·드라이버·프레임 구조
    /// 차이가 전부 수에 섞여, 빨라진 것이 알고리즘 덕인지 DX12 덕인지
    /// 구분할 수 없다. 같은 디바이스·같은 입력 위에 옛 알고리즘(반구 커널
    /// 64개)을 그대로 올려 두고 그것과 잰다 — 갈리는 것은 알고리즘 하나뿐이다.
    bool RunSSAOScaleTest(std::string& outLog);

    /// 포스트 체인 검증 (PHASE 3-6, 신규 포스트 체인).
    ///
    /// 네 단계를 따로 단정한다: 톤맵(HDR 8.0이 1.0 미만으로 눌렸는가) ·
    /// 블룸(밝은 사각 주변이 배경보다 밝은가) · 비네트(구석이 어두운가) ·
    /// FXAA(줄무늬의 이웃 차이가 줄었는가).
    ///
    /// 한 단계만 보면 나머지가 죽어도 통과한다. 넷을 따로 재는 것이 요점이고,
    /// 합친 패스(Uber)는 특히 그렇다 — 넷이 한 셰이더에 들어 있어서 하나가
    /// 빠져도 '어딘가 조금 다르다'로만 보인다.
    bool RunPostChainTest(std::string& outLog);

    /// 포스트 체인 신구 시간 비교 (PHASE 3-6).
    ///
    /// 합친 패스(Uber) 하나와, 그것을 옛 방식대로 넷으로 나눈 것을 잰다.
    /// 참조 경로는 같은 셰이더에 플래그만 하나씩 켜서 네 번 돌린다 —
    /// 갈리는 것이 화면 왕복 횟수 하나뿐이라야 나온 수가 무엇을 뜻하는지
    /// 분명해진다.
    bool RunPostChainScaleTest(std::string& outLog);

    /// UI 패스 검증 (PHASE 3-6, 신규 UI).
    ///
    /// 넷을 따로 단정한다: 배칭(같은 텍스처가 묶이는가) · 순서(나중에 그린
    /// 것이 위에 오는가) · 블렌딩(반투명이 아래를 비추는가) · 좌표(화면
    /// 픽셀 자리에 그려지는가).
    ///
    /// 순서가 핵심이다. 배칭을 넣으면서 순서가 뒤집히는 것이 가장 흔한
    /// 실수인데(텍스처로 전역 정렬하면 바로 그렇게 된다), 그 증상은
    /// '가끔 UI가 이상하게 겹친다'로만 나타나 원인을 짚기 어렵다.
    ///
    /// 배치 수도 함께 본다 — 한 건도 안 묶여도 화면은 똑같이 나온다.
    bool RunUITest(std::string& outLog);

    /// 그리드 패스 검증 (PHASE 3-6, Gizmo 계열 첫 슬라이스).
    ///
    /// 라인·셀 내부·밀도를 따로 단정하고, 측면 카메라에서 지평선 위가
    /// 비는지로 카메라 상수가 실제로 닿는지 본다 — 하향 카메라는 화면
    /// 전체가 바닥이라 '그려지긴 하는데 카메라를 무시한다'를 못 잡는다.
    bool RunGridTest(std::string& outLog);

    /// 기즈모 라인 패스 검증 (PHASE 3-6, Gizmo 계열 2차 슬라이스).
    ///
    /// 도형 정점 수(DX11 수식과의 구성 동일성)·픽셀 자리·드로우 병합·카메라
    /// 반응을 따로 단정한다. 드로우 병합이 핵심이다 — DX11은 도형마다 Map과
    /// 드로우가 나갔고(캡슐 하나 = 12회), 병합해도 그림은 똑같이 나온다.
    bool RunGizmoLineTest(std::string& outLog);

    /// 기즈모 아이콘(빌보드) 패스 검증 (PHASE 3-6, Gizmo 계열 3차 슬라이스).
    ///
    /// 픽셀(알파 상한 0.5)·빌보드 회전·배칭·시야를 따로 단정한다. 측면
    /// 카메라가 핵심이다 — GS를 VS로 옮기며 확장 수식이 틀리면 정면에서는
    /// 멀쩡해 보이고 옆에서만 엣지온으로 사라진다.
    bool RunGizmoIconTest(std::string& outLog);

    /// 와이어프레임 패스 검증 (PHASE 3-6, Gizmo 계열 4차 슬라이스).
    ///
    /// 합성 쿼드로 변·내부·인스턴싱 병합·메시 캐시를 따로 단정한다.
    /// 내부 표본이 핵심이다 — fillMode가 SOLID로 남으면 변 표본은 전부
    /// 통과하면서 내부까지 칠해진다.
    bool RunWireFrameTest(std::string& outLog);

    /// Gizmo 계열 씬 연결 검증 (PHASE 3-6, Gizmo 계열 5차 슬라이스).
    ///
    /// 활성 씬의 아이콘 대상·선택 기즈모·콜라이더를 밀봉 복사하고, 그리드 →
    /// 와이어프레임 → 아이콘 → 라인을 한 그래프에서 같은 타깃으로 이어
    /// 그린다. transient가 둘(그리드의 색·깊이)뿐인 것이 체인의 증명이다 —
    /// Inputs를 채워 잇는 것은 이 검증이 처음이다. 에디터 실행 중에만 의미 있다.
    bool RunGizmoSceneTest(std::string& outLog);

    /// 그림자 품질 검증 (PHASE 3-6 — 경사 비례 편향 · 캐스케이드 경계 블렌딩).
    ///
    /// 둘 다 A/B로 잰다: 스치는 빛의 맨바닥에서 경사 항 끔/켬의 여드름 수,
    /// 분할 경계를 가로지르는 줄무늬에서 블렌딩 끔/켬의 차이 픽셀 수.
    /// '끈 쪽이 실제로 실패'까지 확인한다 — 켠 쪽만 보면 조건이 약해도 통과한다.
    bool RunShadowQualityTest(std::string& outLog);

    /// 스카이박스 패스 검증 (PHASE 3-6).
    ///
    /// 면마다 원색을 깐 합성 큐브맵으로 방향(축 뒤집힘이 원색 차이로
    /// 드러난다)·전면 커버(z=w x 0.99999 quirk — 깨지면 화면이 통째로
    /// 빈다)·카메라 반응을 단정한다. IBL 생성 체인은 별도 슬라이스다.
    bool RunSkyBoxTest(std::string& outLog);

    /// IBL 생성 체인 검증 (PHASE 3-6).
    ///
    /// 반구가 갈리는 equirect(위 빨강·아래 초록)로 rect→cube의 방향,
    /// 조도의 반구 우세, 프리필터의 거칠기 수렴, BRDF LUT의 해석적
    /// 극한(매끈 모서리 = (1,0))을 단정한다. 절대값 단정은 피한다 —
    /// 원본의 톤 정책(로그 평균·NoL 이중 가중)에 묶여 있기 때문이다.
    bool RunIBLTest(std::string& outLog);

    /// LEGACY DEAD DIAGNOSTIC: 엔진 스카이박스 텍스처의 DX12 운반 검증.
    bool RunSkySceneTest(std::string& outLog);

    /// IBL 앰비언트 소비 검증 (PHASE 3-6).
    ///
    /// 광원 0개 씬에서 출력 = 앰비언트뿐으로 만들어 셋을 단정한다:
    /// 끔 = 검정(기존 동작 보존), 반구 IBL의 법선 방향성(바닥 빨강·천장
    /// 초록 — 뒤집히면 색이 맞바뀐다), 매끈 금속의 스플릿섬 정반사.
    bool RunIBLShadeTest(std::string& outLog);

    /// GBuffer 스키닝 검증 (PHASE 3-6).
    ///
    /// 층마다 가중치를 다르게 준 합성 띠로 넷을 단정한다: 항등 팔레트 =
    /// 바인드 포즈, 본 이동이 그 본의 정점만 옮김, 가중 혼합의 선형성,
    /// 그리고 같은 프레임의 비스킨드 메시가 흔들리지 않을 것(스키닝을
    /// 붙이며 비스킨드를 망가뜨리는 실패를 대조군으로 잡는다).
    bool RunSkinningTest(std::string& outLog);

    /// SSS 패스 검증 (PHASE 3-6, 미구현 패스 이식 1차).
    ///
    /// 점 둘과 깊이 계단으로 넷을 단정한다: 번짐, 두 축 분리(가로 단계는
    /// 세로로 안 퍼져야 한다), 표면 추종(계단 너머로 안 새야 한다 — 이것이
    /// 단순 블러와 SSS를 가른다), 에너지 보존.
    bool RunSSSTest(std::string& outLog);

    /// 데칼 패스 검증(PHASE 3-6, 미구현 패스 이식 2차).
    bool RunDecalTest(std::string& outLog);

    /// SSR 패스 검증(PHASE 3-6, 미구현 패스 이식 3차).
    bool RunSSRTest(std::string& outLog);

    /// 볼류메트릭 포그 패스 검증(PHASE 3-6, 미구현 패스 이식 4차).
    bool RunVolumetricFogTest(std::string& outLog);

    // ── 메인 런타임 렌더러 (PHASE 3-9 승격) ──
    //
    // 위의 Run* 검증들과 달리 이쪽은 상태를 가진다: 켜 두면 매 프레임 활성
    // 씬을 DX12로 그려 공유 텍스처에 담고, 에디터의 씬 뷰·게임 뷰가 그것을
    // DX11 SRV로 표시한다(RunSharedTextureTest가 증명한 경로). 인스턴스
    // 멤버가 아니라 정적인 이유: 이 클래스의 인스턴스는 콘솔 명령마다 스택에
    // 만들어지는 검증용이고, 상시 상태는 프로세스에 하나뿐이어야 한다.
    // 상태는 구현 파일(EnhancedSceneRendererLive.cpp) 내부에 숨겼고,
    // 교체(3-9) 때 이 API가 본체 인스턴스로 흡수된다.
    //
    // SceneRenderer(DX11)는 메인 런타임에서 생성하지 않는다. 이 클래스가
    // RenderScene·에디터 카메라·프록시 입력·Sky/IBL을 직접 소유하고 유일한
    // 씬 렌더러로 동작한다. DX11은 에디터 ImGui 셸과 기존 Texture 자산을
    // DX12로 올리는 브리지에만 남는다.
    //
    // 스레드·수명 규약: TickLive/GetLiveDisplaySrv는 게임 스레드의 프레임
    // 경계에서만 부른다. 렌더 스레드(CE)가 이전 프레임의 ImGui 드로우
    // 데이터로 SRV를 읽는 중일 수 있으므로, DisableLive는 DX11에 보이는
    // 객체를 즉시 해제하지 않고 묘지에 보관한다 — 최종 해제는 렌더 스레드
    // join 이후의 ShutdownLive(Dx11Main::Finalize)에서만 한다.

    /// SceneRenderer가 예전에 소유하던 런타임 상태를 만든다.
    static bool InitializeRuntime(std::string& outError);

    static Camera* GetEditorCamera();
    static RenderScene* GetRenderScene();
    static void SetActiveScene(Scene* scene);

    /// equirect HDR를 교체한다. 다음 프레임 시작에서 큐브맵과 IBL을 재생성한다.
    static bool SetSkyBoxPath(const std::string& path, std::string& outError);

    /// 켠다. Enhanced-only 런타임에서는 초기화가 이 상태를 유지한다.
    static void EnableLive();

    /// 호환 API. 단독 운용 중에는 끌 수 없다.
    static void DisableLive();

    static bool IsLiveEnabled();

    /// PIX programmatic capture 종료 직전에 상시 러너가 제출한 GPU 작업을 모두
    /// 완료시킨다. 캡처 외의 정상 프레임 경로에서는 호출하지 않는다.
    static void WaitForLiveGpu();

    /// 프레임 경계(게임 스레드)에서 매 프레임 부른다. 엔트리 계층이 씬
    /// 전환 상태와 이번 프레임에 표시할 카메라들을 밀봉해 넘기므로 DX12
    /// 구현이 SceneManager/EngineSetting에 역의존하지 않는다.
    ///
    /// 카메라마다 독립한 표시 슬롯 집합(CameraView)을 굴려 씬뷰·게임뷰가
    /// 동시에 그려진다. 최대 kMaxLiveCameraViews개(초과분은 무시). 순서는
    /// 표시 우선순위가 아니라 제출 순서일 뿐이다 — 각 창은 자기 카메라로
    /// Get*Display*를 조회한다(MultiCameraRenderPlan.md).
    static constexpr uint32_t kMaxLiveCameraViews = 2;   // 씬뷰 + 게임뷰
    static void TickLive(float deltaSeconds, Camera* const* cameras,
        uint32_t cameraCount, bool sceneLoading);

    /// 진단용 호환 진입점. 메인 런타임은 TickLive에서 자체 상태를 밀봉한다.
    static void CaptureLiveFrame(const Camera* camera);

    /// 이 카메라의 그림을 DX12가 방금 그렸으면 그 SRV를 돌려준다.
    /// nullptr는 아직 첫 프레임이 준비되지 않았거나 다른 카메라라는 뜻이다.
    static ID3D11ShaderResourceView* GetLiveDisplaySrv(const Camera* camera);

    /// ImGui DX12 셸 모드용 표시 경로. 표시 슬롯의 공유 핸들을 셸 디바이스가
    /// 열어(OpenSharedHandle — DX11이 하던 일을 DX12가 이어받는다) ImTextureID
    /// 호환 64비트를 돌려준다. 셸이 없거나 표시할 것이 없으면 0.
    static uint64_t GetLiveDisplayImTextureId(const Camera* camera);

    /// 상태 한 줄 요약(dx12.live status).
    static std::string GetLiveStatus();

    /// 렌더 디버그 창용 스냅샷. GetLiveStatus가 콘솔 한 줄로 뭉개는 것을
    /// 항목별로 돌려주고, 패스별 GPU 시간과 검증 메시지를 함께 싣는다.
    ///
    /// 이 함수만은 다른 Live API와 스레드 규약이 다르다 — ImGui 그리기는
    /// 게임 스레드가 아니라 CE 렌더 스레드에서 돌기 때문이다(Dx11Main의
    /// ExecuteRenderPass → GUIRendering). GetLiveDisplaySrv가 그 자리에서
    /// 안전한 것은 고정 크기 뷰 배열에서 포인터·정수만 읽어 찢어질 것이
    /// 없어서이고(재할당 없음), 상태의 벡터와 셋을 순회하는 것은 전혀 다른
    /// 문제다 — 게임 스레드가 그것들을 매 프레임 교체하므로 순회 중
    /// 재할당을 만나면 해제된 메모리를 읽는다.
    ///
    /// 그래서 스냅샷은 게임 스레드가 TickLive에서 미리 완성해 두고, 이
    /// 함수는 락을 잡고 그 완성본을 복사만 한다. 값은 한 프레임 늦을 수
    /// 있지만 디버그 HUD에는 문제되지 않는다.
    static EnhancedLiveDebugSnapshot GetLiveDebugSnapshot();

    /// 현재 패스 파라미터. 파이프라인이 아직 없으면 기본값을 돌려준다.
    /// GetLiveDebugSnapshot과 같은 스레드 규약(락으로 복사)이다.
    static EnhancedLiveTuning GetLiveTuning();

    /// 패스 파라미터 변경을 요청한다. 실제 SetTuning은 다음 TickLive에서
    /// 게임 스레드가 수행한다 — 창이 패스를 직접 만지지 않는 이유는
    /// EnhancedLiveTuning 주석 참조.
    static void SetLiveTuning(const EnhancedLiveTuning& tuning);

    /// 최종 정리. 렌더 스레드 join 이후에만 부른다.
    static void ShutdownLive();

    /// DX11 vs DX12 API 오버헤드 실측 — 마이그레이션 전제("DX12가 실제로
    /// 빠른가")의 검증.
    ///
    /// Scale 테스트들의 "DX11과 직접 재지 않는다" 원칙과 정반대인 이유:
    /// 거기서는 API 차이가 잡음이었지만, 여기서는 그것이 측정 대상이다.
    /// 같은 어댑터·같은 기하·같은 셰이더·같은 드로우당 페이로드로 맞추고
    /// 드로우당 상수 갱신 경로만 각 엔진의 실제 패턴(DX11 Map/DISCARD vs
    /// DX12 업로드 링 + 루트 CBV)을 따른다. 드로우 수 스윕으로 기록 CPU ·
    /// 제출 CPU · GPU 시간을 중앙값으로 내고, 커버리지 픽셀 수 대조로
    /// '빨라진 것'과 '덜 그린 것'을 가른다. Release 전용 — Debug는 DX12만
    /// 검증 레이어 비용을 내서 비교가 성립하지 않는다.
    bool RunApiOverheadBench(std::string& outLog);
};

#endif
