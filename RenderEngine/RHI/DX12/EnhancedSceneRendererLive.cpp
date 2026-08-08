#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSceneRenderer.h"

#include "DX12DeviceResources.h"
#include "DX12PSOManager.h"
#include "DX12RootSignatureCache.h"
#include "DX12MeshCache.h"
#include "DX12TextureCache.h"
#include "DX12GpuProfiler.h"
#include "EnhancedRenderGraph.h"
#include "EnhancedRenderPass.h"
#include "EnhancedLivePipelineDesc.h"
#include "EnhancedGBufferPass.h"
#include "EnhancedShadowPass.h"
#include "EnhancedDeferredPass.h"
#include "EnhancedSSGIPass.h"
#include "EnhancedForwardPass.h"
#include "EnhancedSSAOPass.h"
#include "EnhancedSSRPass.h"
#include "EnhancedSSSPass.h"
#include "EnhancedDecalPass.h"
#include "EnhancedVolumetricFogPass.h"
#include "EnhancedSkyBoxPass.h"
#include "EnhancedIBLGenerator.h"
#include "EnhancedPostChainPass.h"
#include "EnhancedGridPass.h"
#include "EnhancedWireFramePass.h"
#include "EnhancedGizmoIconPass.h"
#include "EnhancedGizmoLinePass.h"
#include "ImGuiDx12Shell.h"
#include "../../EnhancedGizmoSceneBinding.h"
#include "../../GizmoRenderer.h"

#include "../../Camera.h"
#include "../../RenderPassData.h"
#include "../../Material.h"
#include "../../RenderScene.h"
#include "../../LightController.h"
#include "../../Texture.h"
#include "../../MeshRendererProxy.h"
#include "../../Skeleton.h"
#include "../../Mesh.h"
#include "../../RenderState.h"
#include "../../../Utility_Framework/PathFinder.h"

#include <d3d11_1.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <unordered_set>
#include <mutex>

// EnhancedSceneRenderer의 단독 메인 런타임 구현.
//
// 공개 표면은 EnhancedSceneRenderer의 정적 Live API다(헤더의 규약 주석 참조).
// 상태를 이 파일 안의 싱글턴에 숨긴 이유: EnhancedSceneRenderer 인스턴스는
// 콘솔 명령마다 스택에 만들어지는 검증용이라 상시 상태를 들 수 없고, 그렇다고
// 별도 공개 클래스를 두면 "DX12 렌더러 = EnhancedSceneRenderer"라는 로드맵의
// 명칭 체계가 흐려진다(실제로 그렇게 만들었다가 물렸다).
namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.

    // 블랙보드 슬롯 이름. 문자열 오타는 Validate가 세울 때 잡지만(발행 안 된
    // 슬롯 읽기 = 오류), 상수로 두면 오타 자체가 컴파일 오류가 된다.
    namespace LiveSlots
    {
        constexpr const char* kGBufferDiffuse    = "GBuffer.Diffuse";
        constexpr const char* kGBufferMetalRough = "GBuffer.MetalRough";
        constexpr const char* kGBufferNormal     = "GBuffer.Normal";
        constexpr const char* kGBufferEmissive   = "GBuffer.Emissive";
        constexpr const char* kGBufferBitmask    = "GBuffer.Bitmask";
        constexpr const char* kGBufferDepth      = "GBuffer.Depth";

        constexpr const char* kShadowMap         = "Shadow.Map";
        constexpr const char* kAmbientOcclusion  = "Scene.AmbientOcclusion";

        /// 라이팅 결과. Deferred가 발행하고 SkyBox·SSGI·Forward+·SSS·SSR·Fog가
        /// 차례로 수정한다 — 지금 코드의 litColor 체인이 이 슬롯 하나다.
        constexpr const char* kLitColor          = "Scene.LitColor";

        /// 포스트 체인 뒤의 LDR. 기즈모 계열이 이 위에 얹힌다.
        constexpr const char* kDisplayLdr        = "Display.LDR";

        /// 그리드가 만드는 깊이. 와이어프레임이 같은 깊이로 가려져야 한다.
        constexpr const char* kGizmoDepth        = "Gizmo.Depth";
    }

    /// 블랙보드에 흩어져 있는 여섯 슬롯을 GBuffer 출력 구조로 다시 묶는다.
    ///
    /// 데칼과 Deferred가 Outputs를 통째로 받기 때문에 필요하다. 슬롯을 낱개로
    /// 두는 이유는 그것이 실제 의존 관계이기 때문이고(데칼은 셋만 수정한다),
    /// 묶는 비용은 핸들 여섯 개 복사뿐이다.
    EnhancedGBufferPass::Outputs GatherGBufferOutputs(const LiveBlackboard& blackboard)
    {
        EnhancedGBufferPass::Outputs outputs{};
        outputs.diffuse    = blackboard.Get(LiveSlots::kGBufferDiffuse);
        outputs.metalRough = blackboard.Get(LiveSlots::kGBufferMetalRough);
        outputs.normal     = blackboard.Get(LiveSlots::kGBufferNormal);
        outputs.emissive   = blackboard.Get(LiveSlots::kGBufferEmissive);
        outputs.bitmask    = blackboard.Get(LiveSlots::kGBufferBitmask);
        outputs.depth      = blackboard.Get(LiveSlots::kGBufferDepth);
        return outputs;
    }

    std::string ReadLivePostEnvironment(const char* name)
    {
        const DWORD length = GetEnvironmentVariableA(name, nullptr, 0);
        if (0 == length) return {};

        std::string value(length, '\0');
        GetEnvironmentVariableA(name, value.data(), length);
        value.resize(length - 1);
        return value;
    }

    bool ReadLivePostFlag(const char* name, bool fallback)
    {
        const std::string value = ReadLivePostEnvironment(name);
        if (value.empty()) return fallback;
        if (value == "1" || value == "true" || value == "on") return true;
        if (value == "0" || value == "false" || value == "off") return false;
        return fallback;
    }

    float ReadLivePostFloat(const char* name, float fallback)
    {
        const std::string value = ReadLivePostEnvironment(name);
        if (value.empty()) return fallback;

        char* end = nullptr;
        const float parsed = std::strtof(value.c_str(), &end);
        return (end != value.c_str() && '\0' == *end && std::isfinite(parsed))
            ? parsed : fallback;
    }

    struct LiveStopwatch
    {
        LARGE_INTEGER frequency{};
        LARGE_INTEGER started{};
        LiveStopwatch() { ::QueryPerformanceFrequency(&frequency); }
        void Start() { ::QueryPerformanceCounter(&started); }
        double ElapsedMs() const
        {
            LARGE_INTEGER now;
            ::QueryPerformanceCounter(&now);
            return static_cast<double>(now.QuadPart - started.QuadPart)
                * 1000.0 / static_cast<double>(frequency.QuadPart);
        }
    };

    // 파이프라인 번들. 켤 때마다 힙에 새로 만든다.
    //
    // ★ 멤버 재사용(Shutdown 후 같은 객체에 다시 Initialize)이 아니다.
    //   처음에 그렇게 했다가 off→on 재활성화에서 죽었다 — 디바이스·캐시·
    //   패스들은 전부 '새 객체에 한 번 Initialize'만 검증돼 있고(테스트가
    //   항상 스택에 새로 만든다), 재초기화 경로는 아무도 밟은 적이 없는
    //   길이었다. 검증된 수명 패턴을 그대로 쓰는 것이 맞다.
    struct LivePipeline
    {
        template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

        uint32_t width{ 0 };
        uint32_t height{ 0 };

        DX12DeviceResources    resources;
        DX12PSOManager         psoManager;
        DX12RootSignatureCache rootSignatures;
        DX12MeshCache          meshCache;
        DX12TextureCache       textureCache;
        DX12GpuProfiler        profiler;

        // ★ SSGI는 여기 없다 — CameraView가 뷰마다 하나씩 든다.
        //   시간축 누적을 하는 유일한 라이브 패스라서 그렇다(아래 CameraView
        //   주석 참조). 나머지 패스는 프레임 안에서 입력→출력이 끝나므로
        //   카메라가 둘이어도 한 인스턴스로 충분하다.
        EnhancedGBufferPass   gbuffer;
        EnhancedShadowPass    shadow;
        EnhancedDecalPass     decal;
        EnhancedDeferredPass  deferred;
        EnhancedForwardPass   forward;
        EnhancedSSAOPass      ssao;
        EnhancedSSSPass       sss;
        EnhancedSSRPass       ssr;
        EnhancedSkyBoxPass    skyBox;
        EnhancedIBLGenerator  ibl;
        EnhancedPostChainPass postChain;
        bool                  iblGenerated{ false };

        // 기즈모 체인(에디터 보조 표시 — 승격 판정 ④의 블로커였다).
        // 배선은 dx12.gizmoscene과 같고, 라이브에서는 포스트 체인 LDR 위에
        // GBuffer 깊이로 가려지며 얹힌다.
        EnhancedGridPass      grid;
        EnhancedWireFramePass wireframe;
        EnhancedGizmoIconPass gizmoIcon;
        EnhancedGizmoLinePass gizmoLine;

        EnhancedFrameContext frameContext{};

        // 이 파이프라인의 조립 기술. 파이프라인이 설 때 한 번 짜이고, 노드의
        // 접착 람다가 이 LivePipeline과 LiveState를 캡처한다 — 그래서 수명이
        // 파이프라인에 묶여야 하고, 여기 멤버로 두는 것이 그 계약이다.
        // 파이프라인을 헐면 노드도 함께 사라진다.
        LivePipelineDesc desc;

        // 프레임마다 비우고 다시 채운다. 뷰가 둘이어도 한 벌로 충분하다 —
        // 한 프레임의 한 뷰를 그리는 동안에만 살아 있는 값이다.
        LiveBlackboard blackboard;

        // 표시 슬롯 둘 — 비동기의 본체다. DX12가 한 슬롯에 그리는 동안
        // DX11은 다른 슬롯을 표시한다. 슬롯은 펜스 값이 완료된 뒤에만
        // 표시로 승격되므로(TickLive), DX11이 쓰기 중인 텍스처를 읽는
        // 일이 구조적으로 없다 — WaitForGpu가 필요 없어지는 이유다.
        struct DisplaySlot
        {
            ComPtr<ID3D12Resource>           sharedTexture;
            HANDLE                           sharedHandle{ nullptr };
            ComPtr<ID3D11Texture2D>          openedTexture;
            ComPtr<ID3D11ShaderResourceView> openedSrv;
            uint64_t                         fenceValue{ 0 };

            // 이 슬롯 프레임의 그래프. 규칙(dx12.compare 크래시의 교훈):
            // 그래프의 수명은 그 커맨드를 GPU가 끝낼 때까지다 — transient를
            // 그래프가 들고 있다. 승격(펜스 완료) 때 놓으면 풀로 반납된다.
            std::unique_ptr<EnhancedRenderGraph> graph;
        };
        // 카메라 하나가 쓰는 표시 슬롯 묶음. 씬뷰(에디터 카메라)와 게임뷰
        // (게임 카메라)가 서로 다른 카메라를 넘기는데, 슬롯이 한 벌이면 한
        // 프레임에 한 카메라만 그림을 받아 다른 뷰가 검거나 깜빡인다 —
        // 뷰마다 독립한 슬롯 집합을 굴려 각 뷰가 자기 최신 프레임을 계속
        // 표시한다(MultiCameraRenderPlan.md).
        //
        // 슬롯 셋 = 표시 1 + 인플라이트 최대 2. 인플라이트 1개로는 GPU 완료
        // 신호 지연(제출→관측 ~1ms대)이 2ms 틱을 넘겨 틱의 38%가 제출을
        // 쉬었다(실측 81/214) — 표시 프레임률이 절반이 된다. 2개를 겹치면
        // 매 틱 제출이 성립한다.
        //
        // ★ 단, '인플라이트 2 = 링 3의 안전 거리'라는 실측 근거는 제출
        //   총량 기준이지 뷰당이 아니다. 뷰마다 2씩 들면 총 4가 되어
        //   BeginFrame이 얼로케이터 펜스에서 블로킹한다 — TickLive가 뷰
        //   합산 인플라이트를 2로 묶는 이유다.
        static constexpr int kSlotsPerView = 3;   // 표시 1 + 인플라이트 2
        struct CameraView
        {
            const Camera*    camera{ nullptr };
            DisplaySlot      slots[kSlotsPerView];
            int              displaySlot{ -1 };   // DX11이 표시 중인 슬롯(-1 = 아직 없음)
            std::vector<int> pendingQueue;        // 제출 순서의 인플라이트 슬롯들

            // ★ SSGI를 뷰마다 따로 든다 — 라이브 그래프에서 프레임을 넘겨
            //   상태를 잇는 유일한 패스이기 때문이다(히스토리 텍스처 2장 +
            //   재투영 행렬 + 슬롯 인덱스).
            //
            //   한 인스턴스를 두 카메라가 쓰면 히스토리 슬롯 회전(2칸)이
            //   프레임당 두 번 돌아 각 카메라가 '상대 카메라의' 히스토리를
            //   읽고, 재투영 행렬도 직전에 렌더한 다른 카메라의 것이 된다.
            //   리졸브는 깊이 차이만 보고 히스토리를 받아들이므로(같은 씬을
            //   보면 대부분 통과한다) 다른 시점의 화면 공간 GI가 최대 32프레임
            //   지수 평균으로 섞인다 — 씬 뷰의 천이 통째로 얼룩지던 잔상이
            //   그것이었다(2026-08-07, 세 조건 대조로 확정).
            //
            //   PSO는 psoManager가 바이트코드 해시로 공유하므로 인스턴스가
            //   늘어도 컴파일은 한 번이다. 추가 비용은 히스토리 텍스처뿐 —
            //   GI가 절반 해상도라 1920x1080 기준 뷰당 약 12MB.
            EnhancedSSGIPass ssgi;

            // 포그도 같은 이유로 뷰마다 든다 — 프록셀 격자(m_voxelTemp 둘 +
            // m_voxelFinal)가 프레임을 넘겨 살고, m_readIndex 핑퐁과
            // m_previousViewProjection이 SSGI의 히스토리와 똑같은 역할을 한다.
            //
            // ★ 다만 Initialize를 미룬다. 격자가 160x90x128 RGBA16F 셋이라
            //   뷰당 42MB이고, 합성 출력과 힙까지 더한 실측 증가가 켤 때
            //   +127MB다(Private, 1437→1564). 기본이 꺼짐인데 미리 잡으면
            //   안 쓰는 기능이 그만큼을 묶는다 — 처음 켜지는 프레임에
            //   만든다(fogReady). 끈 채로는 증가가 없음을 실측으로 확인했다.
            EnhancedVolumetricFogPass fog;
            bool                      fogReady{ false };
        };
        static constexpr int kMaxCameraViews =
            static_cast<int>(EnhancedSceneRenderer::kMaxLiveCameraViews);
        CameraView views[kMaxCameraViews];

        // transient 풀 — 프레임당 CreateCommittedResource ~35건을 없앤다.
        // 그래프 소멸(펜스 완료 후)이 반납하므로 GPU 사용 중 재배포가 없다.
        RGTransientPool transientPool;
    };

    struct LiveState
    {
        template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

        bool enabled{ false };
        bool runtimeInitialized{ false };

        // SceneRenderer(DX11)에서 이관한 메인 런타임 소유권.
        std::shared_ptr<RenderScene> renderScene;
        std::shared_ptr<Camera>      editorCamera;

        // 원본 HDR는 기존 자산 로더가 만든 DX11 Texture지만 소유자는 이쪽이다.
        // DX12TextureCache가 프레임 시작에 같은 어댑터의 리소스로 올린 뒤 IBL
        // 생성기가 큐브맵·조도·프리필터·BRDF LUT를 만든다.
        std::string                 skyBoxPath;
        Managed::UniquePtr<Texture> skyEquirect;
        bool                        skyBoxDirty{ true };

        // ── 볼류메트릭 포그 입력 ──
        //
        // 기본이 꺼짐이라 처음 켜질 때 만든다(EnsureFogInputs).
        //
        // ★ 둘 다 포그 전용으로 둔다. textureCache의 흰색 폴백을 그대로 쓰면
        //   그것은 재질이 텍스처 없을 때 GBuffer가 디스크립터로 직접 묶는
        //   리소스라, 그래프가 상태를 옮기면 다음 프레임 GBuffer가 어긋난
        //   상태로 읽는다. 그래프는 임포트한 리소스를 원래 상태로 되돌려
        //   주지 않는다(stateWriteback은 '어디로 남았는지'만 알려 준다).
        //
        // ★ 끝 상태를 ALL_SHADER_RESOURCE로 맞춘다. RGResourceState에는
        //   PIXEL 전용 값이 없어 ShaderResource가 곧 ALL인데, textureCache는
        //   업로드를 PIXEL로 끝내므로 그대로 임포트하면 배리어의 before가
        //   실제와 어긋난다(검증 레이어가 잡는다).
        Managed::UniquePtr<Texture> fogBlueNoise;
        ComPtr<ID3D12Resource>      fogCloudNeutral;   // 1x1 흰색 = 구름 없음
        bool                        fogInputsReady{ false };

        // 블루 노이즈를 PIXEL에서 ALL_SHADER_RESOURCE로 한 번 넓혔는가.
        // 텍스처 캐시 수명에 묶인다(캐시가 파이프라인과 함께 죽으면 리소스도
        // 새로 올라가므로 다시 넓혀야 한다). 포그를 껐다 켜는 것으로는
        // 리셋하지 않는다 — 이미 넓힌 리소스에 또 배리어를 걸면 before가
        // 실제와 어긋나 검증 레이어가 잡는다.
        bool                        fogNoiseStateWidened{ false };

        // 창이 넣은 켬/끔. 꺼져 있으면 포그 패스를 아예 세우지 않는다.
        bool                        fogEnabled{ false };

        // 켬 → 끔 전환을 봤다. 자원 해제는 락 밖(TickLive)에서 한다.
        bool                        fogTeardownPending{ false };

        // 카메라→뷰 대응은 LivePipeline::views의 camera 포인터가 든다.
        // GetLiveDisplaySrv가 그 포인터와 대조해 '그 카메라의 뷰'만 교체
        // 표시한다 — 다른 카메라의 창이 엉뚱한 그림을 받는 것을 막는다.
        std::unique_ptr<LivePipeline> pipeline;

        // 프레임 입력. frameContext가 이들의 주소를 들므로 파이프라인과 무관한
        // 여기 멤버로 둔다 — 주소가 흔들리면 패스가 든 포인터가 전부 무효가 된다.
        FrameCameraSnapshot           cameraSnapshot{};
        std::vector<EnhancedDrawItem> draws;
        std::vector<EnhancedDrawItem> forwardDraws;
        std::vector<EnhancedLight>    lights;
        // 데칼은 frameContext가 아니라 패스에 직접 건넨다(SetDecals) —
        // 그리는 패스가 하나뿐이라 컨텍스트에 실을 이유가 없다. 여기 두는
        // 것은 매 프레임 재할당을 피하기 위해서다.
        std::vector<EnhancedDecalPass::Item> decals;
        EnhancedGizmoSceneData        gizmoData;   // 아이콘 벡터를 프레임 동안 소유

        // 묘지 — DisableLive가 즉시 못 놓는 것들(헤더의 수명 규약 참조).
        // ShutdownLive(렌더 스레드 join 후)가 비운다.
        struct Grave
        {
            ComPtr<ID3D12Resource>           sharedTexture;
            HANDLE                           sharedHandle{ nullptr };
            ComPtr<ID3D11Texture2D>          openedTexture;
            ComPtr<ID3D11ShaderResourceView> openedSrv;
        };
        std::vector<Grave> graveyard;


        uint32_t ssaoFrameIndex{ 0 };
        uint32_t frameCounter{ 0 };
        // 러너를 켠 뒤의 누적 초. SSR의 광선 잡음 씨앗이다 — DX11은
        // TimeSystem의 총 경과 초를 넘기는데, 여기서는 TickLive가 받는
        // 델타를 쌓는다(엔트리 계층에 역의존하지 않는다는 규약 그대로).
        float    totalSeconds{ 0.f };
        // 카메라 순회 시작점 회전. 총 인플라이트 예산이 1만 남는 틱에서
        // 항상 목록 앞(씬뷰)이 선점하면 게임뷰가 상대적으로 굶는다 —
        // 큐가 FIFO라 완전한 기아는 없지만 편향 자체를 없앤다.
        uint32_t viewRotation{ 0 };
        uint64_t framesRendered{ 0 };
        uint64_t framesIdle{ 0 };
        uint64_t framesInFlight{ 0 };   // 펜스 미완으로 새 제출을 쉰 틱 수
        // 검증 레이어 메시지. Debug에서만 쌓이고, 같은 문장이 매 프레임
        // 반복되므로 처음 본 것만 찍는다 — 안 그러면 콘솔이 도배돼 정작
        // 첫 원인을 못 본다.
        std::unordered_set<std::string> reportedValidation;

        uint32_t lastDrawCount{ 0 };    // 이번 프레임 GBuffer 드로우(0이면 빈 화면이다)
        uint32_t lastBatchCount{ 0 };
        uint32_t lastDecalCount{ 0 };
        uint32_t lastDecalBatchCount{ 0 };
        double   lastGpuMs{ 0.0 };
        double   lastCpuMs{ 0.0 };
        std::string lastError;

        // 마지막으로 수집에 성공한 프레임의 패스별 GPU 시간. 수집은 매
        // 프레임 되지 않으므로(리드백이 준비된 프레임에만) 마지막 성공분을
        // 유지한다 — 비우면 창이 깜빡인다.
        std::vector<DX12GpuProfiler::PassTiming> lastPassTimings;

        // 렌더 디버그 창에 건네는 완성본. 창은 CE 렌더 스레드에서 그려지고
        // 위의 상태는 전부 게임 스레드가 고치므로, 창이 상태를 직접 순회하면
        // 재할당 중인 벡터·셋을 읽는다. 게임 스레드가 여기까지 완성해 두고
        // 창은 락을 잡아 복사만 한다.
        std::mutex                debugMutex;
        EnhancedLiveDebugSnapshot debugSnapshot;

        // 파이프라인 설정 창과 주고받는 패스 파라미터. debugSnapshot과 같은
        // 뮤텍스로 보호한다 — 둘 다 같은 창이 같은 프레임에 만지므로 락을
        // 나눌 이유가 없고, 나누면 순서 규칙만 하나 더 생긴다.
        EnhancedLiveTuning        tuningMirror;     // 게임 스레드가 채우는 현재값
        EnhancedLiveTuning        pendingTuning;    // 창이 넣는 변경 요청
        bool                      hasPendingTuning{ false };

        /// 게임 스레드에서만 부른다. 창이 넣어 둔 변경을 실제 패스에 적용하고,
        /// 적용 후의 값을 미러에 되싣는다.
        void ApplyAndPublishTuning();

        /// 게임 스레드에서만 부른다. 위 상태를 debugSnapshot으로 옮긴다.
        void PublishDebugSnapshot()
        {
            std::lock_guard<std::mutex> lock(debugMutex);

            debugSnapshot.enabled = enabled;
            debugSnapshot.pipelineReady = (nullptr != pipeline);
            debugSnapshot.width = pipeline ? pipeline->width : 0u;
            debugSnapshot.height = pipeline ? pipeline->height : 0u;
            debugSnapshot.framesRendered = framesRendered;
            debugSnapshot.framesIdle = framesIdle;
            debugSnapshot.framesInFlight = framesInFlight;
            debugSnapshot.drawCount = lastDrawCount;
            debugSnapshot.batchCount = lastBatchCount;
            debugSnapshot.decalCount = lastDecalCount;
            debugSnapshot.decalBatchCount = lastDecalBatchCount;
            debugSnapshot.cpuMs = lastCpuMs;
            debugSnapshot.gpuMs = lastGpuMs;
            debugSnapshot.graveyardCount = graveyard.size();
            debugSnapshot.lastError = lastError;

            // 패스 목록은 크기가 같으면 이름이 그대로다(그래프 선언 순서가
            // 프레임마다 바뀌지 않는다). 문자열 재할당을 피해 값만 갱신한다.
            const size_t passCount = lastPassTimings.size();
            if (debugSnapshot.passTimings.size() != passCount)
            {
                debugSnapshot.passTimings.assign(passCount, EnhancedLivePassTiming{});
            }
            for (size_t i = 0; i < passCount; ++i)
            {
                EnhancedLivePassTiming& target = debugSnapshot.passTimings[i];
                if (target.name != lastPassTimings[i].name)
                {
                    target.name = lastPassTimings[i].name;
                }
                target.milliseconds = lastPassTimings[i].milliseconds;
            }

            // 검증 메시지는 처음 관측한 것만 쌓이므로 개수가 늘 때만 다시 만든다.
            if (debugSnapshot.validationMessages.size() != reportedValidation.size())
            {
                debugSnapshot.validationMessages.assign(
                    reportedValidation.begin(), reportedValidation.end());
            }
        }

        // ── 파이프라인 구축/해체 ──

        bool BuildPipeline(uint32_t newWidth, uint32_t newHeight, std::string& outError)
        {
            ID3D11Device3* dx11Device = DirectX11::DeviceStates->g_pDevice;
            if (nullptr == dx11Device)
            {
                outError = "DX11 디바이스가 없다";
                return false;
            }

            pipeline = std::make_unique<LivePipeline>();
            LivePipeline& p = *pipeline;

            // 공유는 같은 물리 어댑터에서만 성립한다 — LUID로 맞춘다
            // (RunSharedTextureTest와 같은 이유·같은 방법).
            LUID dx11Luid{};
            {
                ComPtr<IDXGIDevice>  dxgiDevice;
                ComPtr<IDXGIAdapter> adapter;
                if (FAILED(dx11Device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
                    FAILED(dxgiDevice->GetAdapter(&adapter)))
                {
                    outError = "DX11 어댑터 조회 실패";
                    return false;
                }
                DXGI_ADAPTER_DESC adapterDesc{};
                adapter->GetDesc(&adapterDesc);
                dx11Luid = adapterDesc.AdapterLuid;
            }

            if (!p.resources.Initialize(newWidth, newHeight, outError, dx11Luid)) return false;

            if (!p.psoManager.Initialize(p.resources.GetDevice(), L"dx12_live.cache", outError) ||
                !p.rootSignatures.Initialize(p.resources.GetDevice(), outError) ||
                !p.meshCache.Initialize(&p.resources, outError) ||
                !p.textureCache.Initialize(&p.resources, outError) ||
                !p.profiler.Initialize(p.resources.GetDevice(), p.resources.GetCommandQueue(),
                    64, DX12DeviceResources::kFrameCount, outError))
            {
                return false;
            }

            p.width = newWidth;
            p.height = newHeight;

            p.frameContext = {};
            p.frameContext.resources = &p.resources;
            p.frameContext.psoManager = &p.psoManager;
            p.frameContext.rootSignatures = &p.rootSignatures;
            p.frameContext.meshCache = &p.meshCache;
            p.frameContext.textureCache = &p.textureCache;
            p.frameContext.width = p.width;
            p.frameContext.height = p.height;
            p.frameContext.camera = &cameraSnapshot;
            p.frameContext.draws = &draws;
            p.frameContext.forwardDraws = &forwardDraws;
            p.frameContext.lights = &lights;

            // 패스 구성과 순서는 dx12.scene(RunSceneBindingTest)과 같다 — 그
            // 검증이 이 배선의 회귀 감시자다. UI 패스는 아직 안 태운다: UI
            // 출력이 HDR(R16G16B16A16)이라 LDR 공유 텍스처로의 복사가 성립하지
            // 않고, 에디터 씬 뷰의 주 대상은 씬 그림이다. UI 합성은 후속 슬라이스.
            // ── 패스 초기화는 노드 목록이 정한다(슬라이스 2) ──
            //
            // 예전에는 여기에 Initialize 열여섯 줄이 순서대로 적혀 있었고,
            // 그 순서는 Declare·PrepareFrame·Shutdown 역순에 각각 따로 적힌
            // 같은 사실의 사본이었다. 이제 순서를 아는 곳은 노드 목록뿐이다.
            //
            // 노드보다 먼저 해야 하는 것만 여기 남는다 — 패스를 세우기 전에
            // 정해져야 하는 값들이다.

            // 기즈모 체인은 포스트 체인의 LDR 결과 위에 직접 그린다 —
            // 입력 텍스처에 그리는 구조라 PSO의 RTV 포맷을 거기 맞춰야 한다
            // (어긋나면 커맨드 리스트 무효 → 디바이스 제거, 실측으로 겪었다).
            // DX11도 기즈모를 최종 표시 타깃에 그린다 — 톤맵 뒤라는 순서
            // 자체는 DX11과 같다.
            p.grid.SetOutputFormat(EnhancedPostChainPass::kLDRFormat);
            p.wireframe.SetOutputFormat(EnhancedPostChainPass::kLDRFormat);
            p.gizmoIcon.SetOutputFormat(EnhancedPostChainPass::kLDRFormat);
            p.gizmoLine.SetOutputFormat(EnhancedPostChainPass::kLDRFormat);

            // 조립 기술을 먼저 짜고, 그 목록으로 초기화한다. 슬라이스 1에서는
            // 패스를 다 세운 뒤에 짰는데 순서가 뒤집혔다 — 노드가 패스를
            // 참조로만 잡으므로(초기화 여부와 무관) 이 순서가 성립하고,
            // 그래야 초기화 순서까지 목록이 정할 수 있다.
            if (!BuildPipelineDesc(outError)) return false;

            if (!p.desc.InitializeAll(p.frameContext,
                static_cast<uint32_t>(LivePipeline::kMaxCameraViews), outError))
            {
                return false;
            }

            p.gbuffer.SetKeepAlive(false);

            // IBL 생성기는 패스가 아니라 생성기다 — 그래프에 선언하지 않고
            // 프레임 시작에 큐브맵·조도·프리필터를 만들어 소비 패스에 건넨다.
            // 노드가 될 것이 아니므로 여기 남는다.
            if (!p.ibl.Initialize(p.frameContext, outError)) return false;

            // 기본은 둘 다 꺼짐(EnhancedLiveTuning의 Sss·Ssr 주석 참조).
            //
            // 환경변수로 켤 수 있게 둔 이유는 포스트 체인과 같다 — 창을
            // 손으로 조작하지 않고 라이브 경로를 단정할 방법이 있어야 한다.
            // 이 둘은 특히 그렇다: 창에서만 켤 수 있으면 "배선했다"를
            // 자동화가 확인할 길이 없고, 그 상태가 곧 소비자 없는 패스가
            // 조용히 죽어 있던 자리다. 미러가 이 값을 되읽으므로 창에도
            // 그대로 보인다.
            p.sss.SetEnabled(ReadLivePostFlag("CREATOR_DX12_SSS", false));
            p.ssr.SetEnabled(ReadLivePostFlag("CREATOR_DX12_SSR", false));

            // 저장 씬·카메라·조명을 고정한 채 후처리 한 요소만 끄는 PIX 검증용.
            // 환경변수가 없으면 Tuning 기본값을 그대로 사용하므로 제품 실행에는
            // 영향이 없다.
            {
                EnhancedPostChainPass::Tuning tuning = p.postChain.GetTuning();
                tuning.bloomEnabled = ReadLivePostFlag(
                    "CREATOR_DX12_POST_BLOOM", tuning.bloomEnabled);
                tuning.toneMapEnabled = ReadLivePostFlag(
                    "CREATOR_DX12_POST_TONEMAP", tuning.toneMapEnabled);
                const std::string toneMapper = ReadLivePostEnvironment(
                    "CREATOR_DX12_POST_TONEMAPPER");
                if (toneMapper == "aces" || toneMapper == "0")
                {
                    tuning.toneMapper = EnhancedPostChainPass::ToneMapper::ACES;
                }
                else if (toneMapper == "agx" || toneMapper == "1")
                {
                    tuning.toneMapper = EnhancedPostChainPass::ToneMapper::AgX;
                }
                tuning.exposure = ReadLivePostFloat(
                    "CREATOR_DX12_POST_EXPOSURE", tuning.exposure);
                p.postChain.SetTuning(tuning);
            }

            // ── 공유 텍스처 + DX11 SRV (뷰마다 슬롯 셋) ──
            //
            // 포스트 체인의 LDR 출력과 같은 RGBA8이라야 CopyTextureRegion이
            // 성립한다. 뷰 2 × 슬롯 3 = 6장 — 1920x1080 기준 뷰당 약 25MB.
            for (LivePipeline::CameraView& view : p.views)
            for (LivePipeline::DisplaySlot& slot : view.slots)
            {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_DEFAULT;

                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = p.width;
                desc.Height = p.height;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

                if (FAILED(p.resources.GetDevice()->CreateCommittedResource(&heap,
                    D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                    nullptr, IID_PPV_ARGS(&slot.sharedTexture))))
                {
                    outError = "공유 텍스처 생성 실패";
                    return false;
                }

                if (FAILED(p.resources.GetDevice()->CreateSharedHandle(slot.sharedTexture.Get(),
                        nullptr, GENERIC_ALL, nullptr, &slot.sharedHandle)) ||
                    nullptr == slot.sharedHandle)
                {
                    outError = "공유 핸들 생성 실패";
                    return false;
                }

                ComPtr<ID3D11Device1> device1;
                if (FAILED(dx11Device->QueryInterface(IID_PPV_ARGS(&device1))) ||
                    FAILED(device1->OpenSharedResource1(slot.sharedHandle,
                        IID_PPV_ARGS(&slot.openedTexture))))
                {
                    outError = "DX11에서 공유 텍스처 열기 실패";
                    return false;
                }

                if (FAILED(dx11Device->CreateShaderResourceView(slot.openedTexture.Get(),
                    nullptr, &slot.openedSrv)))
                {
                    outError = "DX11 SRV 생성 실패";
                    return false;
                }
            }

            return true;
        }

        /// 파이프라인 해체. DX11에 보이는 것은 묘지로 보낸다(수명 규약).
        void TeardownPipeline()
        {
            if (nullptr == pipeline) return;
            LivePipeline& p = *pipeline;

            if (p.resources.IsInitialized()) p.resources.WaitForGpu();
            for (LivePipeline::CameraView& view : p.views)
            {
                for (LivePipeline::DisplaySlot& slot : view.slots) slot.graph.reset();
                view.pendingQueue.clear();
                view.displaySlot = -1;
                view.camera = nullptr;

                for (LivePipeline::DisplaySlot& slot : view.slots)
                {
                    if (!slot.sharedTexture && !slot.openedSrv) continue;
                    Grave grave;
                    grave.sharedTexture = std::move(slot.sharedTexture);
                    grave.sharedHandle = slot.sharedHandle;
                    grave.openedTexture = std::move(slot.openedTexture);
                    grave.openedSrv = std::move(slot.openedSrv);
                    graveyard.push_back(std::move(grave));
                    slot.sharedHandle = nullptr;
                }
            }

            // 패스 해제는 노드 목록의 역순이다(슬라이스 2). 예전에는 그 역순을
            // 사람이 두 곳에 나눠 적었고(ssr·sss 자리와 decal 자리가 달랐다),
            // 틀리면 '가끔 죽는' 종류의 실패였다.
            p.desc.ShutdownAll(static_cast<uint32_t>(LivePipeline::kMaxCameraViews));
            p.desc.Clear();

            // IBL은 노드가 아니므로 따로 푼다(초기화도 따로 했다).
            p.ibl.Shutdown();

            // 포그 입력은 파이프라인 수명에 묶인다(텍스처 캐시가 함께 죽는다).
            // DX11 원본 Texture는 남겨 두고 다음 파이프라인에서 다시 올린다.
            // 포그 패스 자체와 fogReady는 그 노드의 shutdown이 이미 처리했다.
            fogCloudNeutral.Reset();
            fogInputsReady = false;
            fogNoiseStateWidened = false;   // 새 캐시에는 다시 넓혀야 한다
            fogTeardownPending = false;

            p.profiler.Shutdown();
            p.textureCache.Shutdown();
            p.meshCache.Shutdown();
            p.rootSignatures.Shutdown();
            p.psoManager.Shutdown();
            p.resources.Shutdown();

            pipeline.reset();
        }

        /// 포그가 처음 켜질 때 부른다. 프레임이 열려 있어야 한다(업로드 링과
        /// 커맨드 리스트를 쓴다 — textureCache::GetOrUpload와 같은 계약).
        bool EnsureFogInputs(std::string& outError)
        {
            if (fogInputsReady) return true;

            LivePipeline& p = *pipeline;
            // 이 함수는 실패하면 다음 프레임에 다시 불린다. 이미 만든 것을
            // 또 만들지 않도록 각 단계를 개별로 잠근다.

            // ── 블루 노이즈 — 프록셀 지터의 씨앗 ──
            if (!fogBlueNoise)
            {
                const file::path path =
                    PathFinder::Relative("VolumetricFog\\blueNoise.dds");
                try
                {
                    fogBlueNoise = Texture::LoadManagedFromPath(path);
                }
                catch (const std::exception& exception)
                {
                    outError = "블루 노이즈 로드 실패: " + std::string(exception.what());
                    return false;
                }
            }
            if (!fogBlueNoise)
            {
                outError = "블루 노이즈 로드 실패: VolumetricFog\\blueNoise.dds";
                return false;
            }

            const DX12TextureCache::Entry noiseEntry =
                p.textureCache.GetOrUpload(fogBlueNoise.get(), outError);
            if (!noiseEntry.IsValid())
            {
                outError = "블루 노이즈 운반 실패" +
                    (outError.empty() ? std::string{} : ": " + outError);
                return false;
            }

            // ── 중립 구름 그림자 — 1x1 흰색 ──
            //
            // 흰색이어야 하는 이유가 있다. 셰이더가 구름의 켬/끔을 보지 않고
            // 알파를 그냥 곱하므로(EnhancedVolumetricFogPass.h의 발견 ②),
            // 검정이나 미바인딩이면 가려짐이 0이 되어 포그가 통째로 사라진다.
            if (!fogCloudNeutral)
            {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_DEFAULT;

                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = 1;
                desc.Height = 1;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;

                if (FAILED(p.resources.GetDevice()->CreateCommittedResource(&heap,
                    D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                    nullptr, IID_PPV_ARGS(&fogCloudNeutral))))
                {
                    outError = "포그 중립 구름 텍스처 생성 실패";
                    return false;
                }
                fogCloudNeutral->SetName(L"Fog.CloudNeutral");

                const auto staging = p.resources.GetUploadRing().Allocate(
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
                    DX12UploadRing::kTexturePlacementAlignment);
                if (!staging.IsValid())
                {
                    outError = "포그 중립 구름 업로드 링 할당 실패";
                    return false;
                }
                const uint8_t white[4]{ 255, 255, 255, 255 };
                memcpy(staging.cpuAddress, white, sizeof(white));

                auto* commandList = p.resources.GetCommandList();

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = fogCloudNeutral.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = staging.resource;
                src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = staging.offset;
                src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                src.PlacedFootprint.Footprint.Width = 1;
                src.PlacedFootprint.Footprint.Height = 1;
                src.PlacedFootprint.Footprint.Depth = 1;
                src.PlacedFootprint.Footprint.RowPitch =
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

                commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = fogCloudNeutral.Get();
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                commandList->ResourceBarrier(1, &barrier);
            }

            // 블루 노이즈는 캐시가 PIXEL로 끝내 두었다. 그래프가 ShaderResource
            // (=ALL)로 임포트하므로 여기서 한 번 넓혀 둔다 — ALL은 PIXEL의
            // 상위집합이라 이 리소스를 픽셀로 읽는 쪽이 생겨도 그대로 맞다.
            // 두 번 걸면 before가 실제와 어긋나므로 캐시 수명당 한 번만 건다.
            if (!fogNoiseStateWidened)
            {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = noiseEntry.resource;
                barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                p.resources.GetCommandList()->ResourceBarrier(1, &barrier);
                fogNoiseStateWidened = true;
            }

            fogInputsReady = true;
            return true;
        }

        /// 포그를 끌 때 격자를 놓는다. 켰다 끄는 것만으로 자원이 남으면
        /// "끄면 안 잡는다"는 설계가 '한 번도 안 켰을 때만' 성립하게 된다.
        ///
        /// GPU가 아직 격자를 읽는 중일 수 있으므로 완주를 기다린 뒤에 놓는다
        /// (TeardownPipeline과 같은 계약). 끄기는 사람이 누르는 드문 사건이라
        /// 여기서 한 번 멈추는 비용은 문제되지 않는다.
        // ── 파이프라인 조립 기술 짜기 (PHASE 3-10 슬라이스 1) ──
        //
        // 이 함수 하나가 "라이브가 무엇을 어떤 순서로 그리는가"의 단일 출처다.
        // 예전에는 그 사실이 RenderOnce 안에 200줄로 흩어져 있었고, 패스를
        // 하나 이으려면 그중 순서를 지켜야 하는 자리 넷을 사람이 찾아 고쳤다.
        //
        // 노드 순서 = 실행 순서다(3-5 계약을 위층에서 잇는다). 노드를 옮기는
        // 것이 곧 파이프라인을 바꾸는 것이고, 그것 말고 순서를 정하는 곳은
        // 없다.
        //
        // ★ 람다가 `p`(자기 소유자)와 `this`(LiveState 싱글턴)를 캡처한다.
        //   파이프라인을 헐 때 desc도 함께 사라지므로 대롱거리는 참조가
        //   생기지 않는다 — 파이프라인 생성 순서에 이 함수를 포함시키는 것이
        //   그 계약이다.
        //
        // ★ 패스가 아직 초기화되기 전에 불린다(슬라이스 2). 노드는 패스를
        //   참조로만 잡으므로 그래도 성립하고, 그래야 초기화 순서까지 이
        //   목록이 정할 수 있다 — InitializeAll이 이 목록을 읽는다.
        bool BuildPipelineDesc(std::string& outError)
        {
            if (nullptr == pipeline)
            {
                outError = "파이프라인이 없다";
                return false;
            }

            LivePipeline& p = *pipeline;
            p.desc.Clear();

            // ── 그림자 ──
            {
                LivePassNode node;
                node.name = "Shadow";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.shadow; };
                node.writes = { LiveSlots::kShadowMap };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    p.shadow.Declare(graph, ctx);
                    bb.Set(LiveSlots::kShadowMap, p.shadow.GetShadowMap());
                };
                p.desc.AddNode(std::move(node));
            }

            // ── GBuffer ──
            {
                LivePassNode node;
                node.name = "GBuffer";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.gbuffer; };
                node.writes = {
                    LiveSlots::kGBufferDiffuse, LiveSlots::kGBufferMetalRough,
                    LiveSlots::kGBufferNormal,  LiveSlots::kGBufferEmissive,
                    LiveSlots::kGBufferBitmask, LiveSlots::kGBufferDepth,
                };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    p.gbuffer.Declare(graph, ctx);
                    const auto outputs = p.gbuffer.GetOutputs();
                    bb.Set(LiveSlots::kGBufferDiffuse,    outputs.diffuse);
                    bb.Set(LiveSlots::kGBufferMetalRough, outputs.metalRough);
                    bb.Set(LiveSlots::kGBufferNormal,     outputs.normal);
                    bb.Set(LiveSlots::kGBufferEmissive,   outputs.emissive);
                    bb.Set(LiveSlots::kGBufferBitmask,    outputs.bitmask);
                    bb.Set(LiveSlots::kGBufferDepth,      outputs.depth);
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 데칼 ──
            //
            // GBuffer 바로 뒤, SSAO·Deferred 앞이다(DX11과 같은 자리).
            // 새 타깃을 만들지 않고 확산·노멀·ORM에 덧칠하므로 핸들이 그대로다 —
            // 그래서 writes가 아니라 modifies이고, 값이 안 바뀌어도 규약은 같다.
            // 데칼이 하나도 없으면 패스가 아무것도 선언하지 않는다.
            {
                LivePassNode node;
                node.name = "Decal";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.decal; };
                // 데칼만 준비가 특별하다 — PrepareFrame이 텍스처를 올리고
                // 배치를 짜므로 이번 프레임 목록이 그 앞에 들어가야 한다.
                node.prepare = [this, &p](const EnhancedFrameContext& ctx,
                    std::string& err, uint32_t) -> bool
                {
                    p.decal.SetDecals(decals);
                    return p.decal.PrepareFrame(ctx, err);
                };
                node.reads = { LiveSlots::kGBufferDepth };
                node.modifies = {
                    LiveSlots::kGBufferDiffuse, LiveSlots::kGBufferNormal,
                    LiveSlots::kGBufferMetalRough,
                };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    p.decal.SetInputs(GatherGBufferOutputs(bb));
                    p.decal.Declare(graph, ctx);
                };
                p.desc.AddNode(std::move(node));
            }

            // ── SSAO ──
            {
                LivePassNode node;
                node.name = "SSAO";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.ssao; };
                node.reads = { LiveSlots::kGBufferDepth, LiveSlots::kGBufferNormal };
                node.writes = { LiveSlots::kAmbientOcclusion };
                node.declare = [this, &p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedSSAOPass::Inputs inputs{};
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    inputs.normal = bb.Get(LiveSlots::kGBufferNormal);
                    p.ssao.SetInputs(inputs);
                    p.ssao.SetFrameIndex(ssaoFrameIndex++);
                    p.ssao.Declare(graph, ctx);
                    bb.Set(LiveSlots::kAmbientOcclusion, p.ssao.GetOutput());
                };
                p.desc.AddNode(std::move(node));
            }

            // ── Deferred (라이팅 결과의 최초 발행자) ──
            //
            // 그림자를 Forward+에도 같이 준다. 한쪽만 받으면 같은 자리에서
            // 불투명은 그늘인데 투명만 밝은, 물체와 무관한 경계가 생긴다.
            // 받는 쪽만이다 — 투명은 그림자 맵에 그려지지 않는다.
            {
                LivePassNode node;
                node.name = "Deferred";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.deferred; };
                node.reads = {
                    LiveSlots::kGBufferDiffuse, LiveSlots::kGBufferMetalRough,
                    LiveSlots::kGBufferNormal,  LiveSlots::kGBufferEmissive,
                    LiveSlots::kGBufferDepth,   LiveSlots::kShadowMap,
                };
                node.writes = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    p.deferred.SetInputs(GatherGBufferOutputs(bb));
                    p.deferred.SetShadow(p.shadow.GetShadowMap(), p.shadow.GetShadowData());
                    p.forward.SetShadow(p.shadow.GetShadowMap(), p.shadow.GetShadowData());
                    p.deferred.Declare(graph, ctx);
                    bb.Set(LiveSlots::kLitColor, p.deferred.GetOutput());
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 스카이박스 ──
            //
            // HDR 조명 타깃에, SSGI보다 먼저 합성한다. Deferred 출력은 RTV
            // 사용이 가능하지만 SSGI 출력은 UAV 전용이므로 SSGI 뒤에 붙이면
            // RTV 생성부터 잘못된다.
            {
                LivePassNode node;
                node.name = "SkyBox";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.skyBox; };
                node.reads = { LiveSlots::kGBufferDepth };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedSkyBoxPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kLitColor);
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    p.skyBox.SetInputs(inputs);
                    p.skyBox.Declare(graph, ctx);
                    if (p.skyBox.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kLitColor, p.skyBox.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            // ── SSGI (뷰마다 인스턴스) ──
            {
                LivePassNode node;
                node.name = "SSGI";
                // 뷰마다 인스턴스다 — 프레임을 넘겨 상태를 잇는(히스토리 2장 +
                // 재투영 행렬) 패스라, 공유하면 각 카메라가 상대의 히스토리를
                // 읽어 잔상이 남는다(2026-08-07 세 조건 대조로 확정).
                node.perView = true;
                node.instance = [&p](uint32_t viewIndex) -> EnhancedRenderPass*
                {
                    return &p.views[viewIndex].ssgi;
                };
                node.reads = {
                    LiveSlots::kGBufferDepth, LiveSlots::kGBufferNormal,
                    LiveSlots::kGBufferDiffuse, LiveSlots::kGBufferMetalRough,
                    LiveSlots::kAmbientOcclusion,
                };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
                {
                    LivePipeline::CameraView& view = p.views[binding.viewIndex];

                    EnhancedSSGIPass::Inputs inputs{};
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    inputs.normal = bb.Get(LiveSlots::kGBufferNormal);
                    inputs.diffuse = bb.Get(LiveSlots::kGBufferDiffuse);
                    inputs.metalRough = bb.Get(LiveSlots::kGBufferMetalRough);
                    inputs.lighting = bb.Get(LiveSlots::kLitColor);
                    inputs.ambientOcclusion = bb.Get(LiveSlots::kAmbientOcclusion);
                    view.ssgi.SetInputs(inputs);
                    view.ssgi.Declare(graph, ctx);
                    if (view.ssgi.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kLitColor, view.ssgi.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 투명(Forward+) ──
            //
            // 라이팅 결과에 직접 알파 블렌딩으로 얹는다. 별도 타깃에 그려 두고
            // 나중에 합성하지 않는 이유는 EnhancedForwardPass::Declare의 주석에
            // 있다(프리멀티플라이드 알파와 겹친 면의 누적 알파).
            {
                LivePassNode node;
                node.name = "Forward+";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.forward; };
                node.reads = { LiveSlots::kGBufferDepth };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedForwardPass::Inputs inputs{};
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    inputs.lighting = bb.Get(LiveSlots::kLitColor);
                    p.forward.SetInputs(inputs);
                    p.forward.Declare(graph, ctx);
                    if (p.forward.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kLitColor, p.forward.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 서브서피스 스캐터링 ──
            //
            // 투명 뒤, SSR 앞이다(DX11과 같은 자리 — Forward → SSS → SSR).
            // 꺼져 있으면 패스가 스스로 입력을 흘리므로 active를 달지 않는다 —
            // 노드를 건너뛰는 것과 결과가 같고, 패스 하나에 규약을 두는 편이
            // 자가 검증(dx12.sss)이 보는 것과 어긋나지 않는다.
            {
                LivePassNode node;
                node.name = "SSS";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.sss; };
                node.reads = { LiveSlots::kGBufferDepth };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedSSSPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kLitColor);
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    p.sss.SetInputs(inputs);
                    p.sss.Declare(graph, ctx);
                    if (p.sss.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kLitColor, p.sss.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 스크린 스페이스 반사 ──
            //
            // SSS 뒤, 포그 앞이다. 반사색을 뜨는 원본이 곧 반사를 얹을 그림이라
            // color가 둘 다를 겸한다.
            {
                LivePassNode node;
                node.name = "SSR";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.ssr; };
                node.reads = {
                    LiveSlots::kGBufferDepth, LiveSlots::kGBufferMetalRough,
                    LiveSlots::kGBufferNormal, LiveSlots::kGBufferBitmask,
                };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [this, &p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedSSRPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kLitColor);
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    inputs.metalRough = bb.Get(LiveSlots::kGBufferMetalRough);
                    inputs.normal = bb.Get(LiveSlots::kGBufferNormal);
                    inputs.bitmask = bb.Get(LiveSlots::kGBufferBitmask);
                    p.ssr.SetInputs(inputs);
                    // 광선 잡음의 씨앗. DX11이 총 경과 초를 넘긴다.
                    p.ssr.SetTime(totalSeconds);
                    p.ssr.Declare(graph, ctx);
                    if (p.ssr.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kLitColor, p.ssr.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 볼류메트릭 포그 (뷰마다 인스턴스 · 꺼질 수 있다) ──
            //
            // 라이팅 결과 위에, 톤맵 앞에 얹는다(DX11과 같은 자리).
            // 투명 합성 뒤라 투명에도 포그가 걸린다.
            //
            // 꺼져 있으면 노드를 통째로 건너뛴다 — 그러면 LitColor 슬롯이 그대로
            // 남아 포스트 체인이 이전 값을 받는다. 이것이 modifies 규약이 하는
            // 일이고, 예전 코드의 `if (fogEnabled && view.fogReady)` 블록과 같다.
            {
                LivePassNode node;
                node.name = "VolumetricFog";
                node.perView = true;
                node.instance = [&p](uint32_t viewIndex) -> EnhancedRenderPass*
                {
                    return &p.views[viewIndex].fog;
                };

                // ★ 초기화를 미룬다. 격자가 160x90x128 RGBA16F 셋이라 뷰당
                //   42MB이고 켤 때 실측 증가가 +127MB다. 기본이 꺼짐인데 미리
                //   잡으면 안 쓰는 기능이 그만큼을 묶는다 — 빈 람다로 기본
                //   초기화를 끄고, 처음 켜지는 프레임에 prepare가 세운다.
                node.initialize = [](const EnhancedFrameContext&, std::string&, uint32_t)
                {
                    return true;
                };

                // 자원 확보 실패는 렌더러를 내리지 않고 포그만 끈다. 포그는
                // 선택 기능이라(기본 꺼짐) 에셋 하나가 없다고 화면 전체를
                // 잃는 것은 대가가 맞지 않는다.
                node.prepare = [this, &p](const EnhancedFrameContext& ctx,
                    std::string& err, uint32_t viewIndex) -> bool
                {
                    if (!fogEnabled) return true;

                    LivePipeline::CameraView& view = p.views[viewIndex];

                    std::string fogError;
                    const bool ready = EnsureFogInputs(fogError) &&
                        (view.fogReady || view.fog.Initialize(ctx, fogError));
                    if (!ready)
                    {
                        lastError = "볼류메트릭 포그를 끈다: " + fogError;
                        fogEnabled = false;
                        fogTeardownPending = true;
                        return true;
                    }

                    view.fogReady = true;
                    return view.fog.PrepareFrame(ctx, err);
                };

                node.shutdown = [this]()
                {
                    if (nullptr == pipeline) return;
                    for (LivePipeline::CameraView& view : pipeline->views)
                    {
                        if (view.fogReady) view.fog.Shutdown();
                        view.fogReady = false;
                    }
                };

                node.reads = { LiveSlots::kGBufferDepth, LiveSlots::kShadowMap };
                node.modifies = { LiveSlots::kLitColor };
                node.active = [this]()
                {
                    if (!fogEnabled || nullptr == pipeline) return false;
                    // 뷰마다 준비 상태가 다를 수 있어 하나라도 서 있으면 활성으로
                    // 본다. 실제 뷰의 준비 여부는 declare가 다시 확인한다.
                    for (const LivePipeline::CameraView& view : pipeline->views)
                    {
                        if (view.fogReady) return true;
                    }
                    return false;
                };
                node.declare = [this, &p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
                {
                    LivePipeline::CameraView& view = p.views[binding.viewIndex];
                    if (!view.fogReady) return;

                    EnhancedVolumetricFogPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kLitColor);
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    inputs.shadowMap = bb.Get(LiveSlots::kShadowMap);
                    inputs.cloudShadow = graph.ImportTexture(fogCloudNeutral.Get(),
                        RGResourceState::ShaderResource, "Fog.CloudNeutral");
                    {
                        std::string noiseError;
                        const DX12TextureCache::Entry noiseEntry =
                            p.textureCache.GetOrUpload(fogBlueNoise.get(), noiseError);
                        if (noiseEntry.IsValid())
                        {
                            inputs.blueNoise = graph.ImportTexture(noiseEntry.resource,
                                RGResourceState::ShaderResource, "Fog.BlueNoise");
                        }
                    }
                    view.fog.SetInputs(inputs);
                    // 셰이더가 캐스케이드 슬라이스 2를 짚는다 — DX11이 마지막
                    // 캐스케이드 행렬을 넘기는 것과 짝이다.
                    view.fog.SetShadowMatrix(p.shadow.GetShadowData()
                        .lightViewProjection[EnhancedShadowPass::kCascadeCount - 1]);
                    view.fog.SetFrameIndex(frameCounter);
                    view.fog.Declare(graph, ctx);

                    if (view.fog.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kLitColor, view.fog.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 포스트 체인 (LDR의 최초 발행자) ──
            {
                LivePassNode node;
                node.name = "PostChain";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.postChain; };
                node.reads = { LiveSlots::kLitColor };
                node.writes = { LiveSlots::kDisplayLdr };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedPostChainPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kLitColor);
                    p.postChain.SetInputs(inputs);
                    p.postChain.Declare(graph, ctx);
                    bb.Set(LiveSlots::kDisplayLdr, p.postChain.GetOutput());
                };
                p.desc.AddNode(std::move(node));
            }

            // ── 기즈모 체인 — dx12.gizmoscene의 배선 그대로(DX11
            // GizmoRenderer::OnDrawGizmos 순서): Grid → WireFrame → Icon → Line.
            // 포스트 체인 LDR 위에 얹고 GBuffer 깊이로 가린다.
            {
                LivePassNode node;
                node.name = "Grid";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.grid; };
                node.reads = { LiveSlots::kGBufferDepth };
                node.modifies = { LiveSlots::kDisplayLdr };
                node.writes = { LiveSlots::kGizmoDepth };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedGridPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kDisplayLdr);
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    p.grid.SetInputs(inputs);
                    p.grid.Declare(graph, ctx);
                    if (p.grid.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kDisplayLdr, p.grid.GetOutput());
                    }
                    bb.Set(LiveSlots::kGizmoDepth, p.grid.GetDepth());
                };
                p.desc.AddNode(std::move(node));
            }

            // ★ 와이어프레임은 모드일 때만 그린다(DX11 GizmoRenderer.cpp:67의
            //   if (m_buseWireFrame)와 같은 조건). 무조건 그렸더니 초록 와이어가
            //   씬 전체를 덮었다 — 에디터 오버레이는 '언제 그리는가'가 패스의
            //   일부다. 꺼지면 LDR 슬롯이 그대로 남아 아이콘이 그리드 결과를
            //   이어받는다(예전의 삼항 분기가 여기서 사라진다).
            {
                LivePassNode node;
                node.name = "WireFrame";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.wireframe; };
                node.reads = { LiveSlots::kGizmoDepth };
                node.modifies = { LiveSlots::kDisplayLdr };
                node.active = []()
                {
                    const GizmoRenderer* gizmoRenderer = GizmoRenderer::GetActive();
                    return (nullptr != gizmoRenderer) && gizmoRenderer->IsWireFrameEnabled();
                };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedWireFramePass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kDisplayLdr);
                    inputs.depth = bb.Get(LiveSlots::kGizmoDepth);
                    p.wireframe.SetInputs(inputs);
                    p.wireframe.Declare(graph, ctx);
                    if (p.wireframe.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kDisplayLdr, p.wireframe.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            {
                LivePassNode node;
                node.name = "GizmoIcon";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.gizmoIcon; };
                node.modifies = { LiveSlots::kDisplayLdr };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedGizmoIconPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kDisplayLdr);
                    p.gizmoIcon.SetInputs(inputs);
                    p.gizmoIcon.Declare(graph, ctx);
                    if (p.gizmoIcon.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kDisplayLdr, p.gizmoIcon.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            {
                LivePassNode node;
                node.name = "GizmoLine";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.gizmoLine; };
                node.modifies = { LiveSlots::kDisplayLdr };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedGizmoLinePass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kDisplayLdr);
                    p.gizmoLine.SetInputs(inputs);
                    p.gizmoLine.Declare(graph, ctx);
                    if (p.gizmoLine.GetOutput().IsValid())
                    {
                        bb.Set(LiveSlots::kDisplayLdr, p.gizmoLine.GetOutput());
                    }
                };
                p.desc.AddNode(std::move(node));
            }

            // ── live_present: 최종 결과 → 표시 슬롯의 공유 텍스처 ──
            //
            // 대상이 프레임마다 회전하므로 캡처가 아니라 binding으로 받는다.
            {
                LivePassNode node;
                node.name = "live_present";
                node.reads = { LiveSlots::kDisplayLdr };
                node.declare = [](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext&, const LiveFrameBinding& binding)
                {
                    if (nullptr == binding.sharedTarget) return;

                    const RGHandle finalHandle = bb.Get(LiveSlots::kDisplayLdr);
                    if (!finalHandle.IsValid()) return;

                    const RGHandle sharedHandleRG = graph.ImportTexture(
                        binding.sharedTarget, RGResourceState::CopyDest, "Live.Shared");

                    ID3D12Resource* const sharedResource = binding.sharedTarget;
                    graph.AddPass("live_present",
                        { { finalHandle, RGResourceState::CopySource },
                          { sharedHandleRG, RGResourceState::CopyDest } },
                        [sharedResource, finalHandle](
                            const EnhancedRenderGraph::ExecuteContext& executeContext)
                        {
                            D3D12_TEXTURE_COPY_LOCATION src{};
                            src.pResource = executeContext.Resolve(finalHandle);
                            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                            D3D12_TEXTURE_COPY_LOCATION dst{};
                            dst.pResource = sharedResource;
                            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

                            executeContext.commandList->CopyTextureRegion(
                                &dst, 0, 0, 0, &src, nullptr);
                        }, true);
                };
                p.desc.AddNode(std::move(node));
            }

            // 세울 때 한 번 검증한다. 여기서 걸리면 배선이 잘못된 것이고,
            // 그것은 화면을 보기 전에 알아야 하는 종류다.
            if (!p.desc.Validate(outError))
            {
                outError = "파이프라인 기술 검증 실패: " + outError;
                return false;
            }

            return true;
        }

        /// 켬 → 끔 전환의 자원 해제. 파이프라인은 살아 있고 포그만 내린다 —
        /// 파이프라인을 통째로 헐 때는 포그 노드의 shutdown이 같은 일을 한다
        /// (BuildPipelineDesc의 VolumetricFog 노드). 둘을 합치지 않는 이유는
        /// 여기만 GPU 완주를 기다리고 텍스처 캐시 수명과 얽히기 때문이다.
        void ReleaseFogResources()
        {
            fogTeardownPending = false;
            if (nullptr == pipeline) return;

            LivePipeline& p = *pipeline;
            bool anyReady = fogInputsReady;
            for (const LivePipeline::CameraView& view : p.views)
            {
                anyReady = anyReady || view.fogReady;
            }
            if (!anyReady) return;

            if (p.resources.IsInitialized()) p.resources.WaitForGpu();
            for (LivePipeline::CameraView& view : p.views)
            {
                if (view.fogReady) view.fog.Shutdown();
                view.fogReady = false;
            }
            fogCloudNeutral.Reset();
            fogInputsReady = false;
        }

        // ── 씬 밀봉 복사 ──
        //
        // 규칙은 dx12.scene(RunSceneBindingTest [1/4])과 같다: 프록시·재질을
        // 들지 않고 필요한 것만 복사한다. 거기와 여기가 갈리면 dx12.scene은
        // 통과하는데 라이브만 틀리는 상태가 되므로, 규칙을 바꿀 때는 두 곳을
        // 함께 바꿀 것.
        /// 프레임 경계에서 자체 RenderScene을 밀봉한다. DX11 RenderPassData와
        /// 카메라별 culling queue는 SceneRenderer와 함께 런타임 경로에서 빠졌다.
        bool CaptureFromCamera(const Camera* sceneCamera)
        {
            if (nullptr == sceneCamera || nullptr == renderScene) return false;

            const uint32_t rtWidth = static_cast<uint32_t>(
                DirectX11::DeviceStates->g_Viewport.Width);
            const uint32_t rtHeight = static_cast<uint32_t>(
                DirectX11::DeviceStates->g_Viewport.Height);
            if (0 == rtWidth || 0 == rtHeight) return false;

            cameraSnapshot.view = sceneCamera->CalculateView();
            cameraSnapshot.projection =
                const_cast<Camera*>(sceneCamera)->CalculateProjection();
            cameraSnapshot.inverseView = XMMatrixInverse(nullptr, cameraSnapshot.view);
            cameraSnapshot.inverseProjection =
                XMMatrixInverse(nullptr, cameraSnapshot.projection);
            cameraSnapshot.eyePosition = sceneCamera->m_eyePosition;
            cameraSnapshot.forward = sceneCamera->m_forward;
            cameraSnapshot.right = sceneCamera->m_right;
            cameraSnapshot.up = sceneCamera->m_up;
            cameraSnapshot.fov = sceneCamera->m_fov;
            cameraSnapshot.nearPlane = sceneCamera->m_nearPlane;
            cameraSnapshot.farPlane = sceneCamera->m_farPlane;
            cameraSnapshot.isOrthographic = sceneCamera->m_isOrthographic;

            // ── 밖으로도 게시한다 ──
            //
            // ★ 이 두 줄이 없어서 RenderPassData의 프레임 스냅샷이 죽어 있었다.
            //   게시(UpdateData)와 래치를 부르던 것이 DX11 렌더 루프였는데 그
            //   루프가 은퇴하면서(ccca6964) 호출자가 0이 됐고, 그 뒤로
            //   GetFrameSnapshot은 영행렬과 near=far=0을 돌려주고 있었다.
            //   위 주석이 "RenderPassData는 SceneRenderer와 함께 런타임 경로에서
            //   빠졌다"고 적어 둔 것이 이 사실이고, 소비자는 그대로 남아 있었다.
            //
            //   조용히 틀리는 부류다. 영행렬을 받은 쪽은 '아무것도 안 보인다'로만
            //   드러나고, 어서션까지 가면 폭 0인 직교 투영을 만들려다 죽는다 —
            //   자가 검증 dx12.scene·dx12.gizmoscene이 걸려 있던 자리가 여기다.
            //
            //   여기가 옳은 자리인 이유: 이 함수가 프레임 경계의 밀봉 지점이고,
            //   바로 위에서 만든 값이 곧 이 프레임의 정답이다. 다른 데서 다시
            //   계산하면 라이브가 쓰는 값과 밖이 보는 값이 어긋날 수 있다.
            if (RenderPassData* passData = RenderPassData::GetData(
                const_cast<Camera*>(sceneCamera)))
            {
                passData->PublishFrameSnapshot(cameraSnapshot);

                // 게시 직후에 래치한다. 이 지점이 프레임 경계라 '이 프레임의
                // 모든 읽기가 같은 면을 본다'는 3-2의 계약이 그대로 성립한다.
                passData->LatchFrameSnapshot();
            }

            draws.clear();
            forwardDraws.clear();
            lights.clear();
            decals.clear();

            const auto copyProxy = [](const PrimitiveRenderProxy* proxy,
                std::vector<EnhancedDrawItem>& out)
            {
                if (nullptr == proxy ||
                    PrimitiveProxyType::MeshRenderer != proxy->m_proxyType ||
                    nullptr == proxy->m_Mesh)
                {
                    return;
                }

                EnhancedDrawItem item{};
                item.mesh = proxy->m_Mesh.get();
                item.worldMatrix = proxy->m_worldMatrix;

                if (proxy->m_isAnimationEnabled
                    && (HashedGuid::INVAILD_ID != proxy->m_animatorGuid)
                    && proxy->m_finalTransforms)
                {
                    item.bonePalette = proxy->m_finalTransforms.get();
                    item.boneCount = Skeleton::MAX_BONES;
                    item.animatorKey = static_cast<uint64_t>(proxy->m_animatorGuid);
                }

                if (auto* material = proxy->m_Material.get())
                {
                    item.baseColor = material->m_pBaseColor;
                    item.normalMap = material->m_pNormal;
                    item.occRoughMetal = material->m_pOccRoughMetal;
                    item.emissive = material->m_pEmissive;

                    item.baseColorFactor = material->m_materialInfo.m_baseColor;
                    item.metallic = material->m_materialInfo.m_metallic;
                    item.roughness = material->m_materialInfo.m_roughness;
                    item.useNormalMap =
                        (0 != material->m_materialInfo.m_useNormalMap) ? 1u : 0u;
                }

                out.push_back(item);
            };

            // shared_ptr 스냅샷이 이 함수가 재질·메시를 복사하는 동안 프록시를
            // 살려 둔다. 카메라별 큐를 거치지 않으므로 모든 MeshRenderer를
            // 수집하고 재질 모드로 deferred/forward를 직접 분류한다.
            // 데칼도 같은 순회에서 모은다. 재질·메시가 아니라 텍스처 셋과
            // 월드 행렬만 드는 별개 종류라 copyProxy가 아니라 따로 받는다.
            //
            // ★ 순서를 건드리지 않는다. 데칼 패스는 겹친 데칼의 순서가 곧
            //   블렌드 결과라 정렬하지 않고 '연속한 같은 묶음'만 배칭한다
            //   (EnhancedDecalPass 헤더). 여기서 순서를 바꾸면 그 계약이
            //   깨지므로 프록시 순회 순서를 그대로 넘긴다.
            const auto copyDecal = [this](const PrimitiveRenderProxy* proxy)
            {
                if (nullptr == proxy ||
                    PrimitiveProxyType::DecalComponent != proxy->m_proxyType)
                {
                    return;
                }

                // 셋 다 없으면 그릴 것이 없다 — DX11 RenderPassData의
                // 데칼 큐 적재 조건과 같다(RenderPassData.cpp:232).
                if (nullptr == proxy->m_diffuseTexture &&
                    nullptr == proxy->m_normalTexture &&
                    nullptr == proxy->m_occluroughmetalTexture)
                {
                    return;
                }

                EnhancedDecalPass::Item item{};
                item.worldMatrix = proxy->m_worldMatrix;
                item.diffuse = proxy->m_diffuseTexture;
                item.normal = proxy->m_normalTexture;
                item.occRoughMetal = proxy->m_occluroughmetalTexture;
                item.sliceX = proxy->m_sliceX;
                item.sliceY = proxy->m_sliceY;
                item.sliceNum = proxy->m_sliceNum;
                decals.push_back(item);
            };

            const RenderScene::ProxySnapshot proxies =
                renderScene->GetPrimitiveProxySnapshot();
            for (const auto& proxy : proxies)
            {
                if (nullptr != proxy &&
                    PrimitiveProxyType::DecalComponent == proxy->m_proxyType)
                {
                    copyDecal(proxy.get());
                    continue;
                }

                const Material* material = proxy ? proxy->m_Material.get() : nullptr;
                if (nullptr != material &&
                    MaterialRenderingMode::Transparent == material->m_renderingMode)
                {
                    copyProxy(proxy.get(), forwardDraws);
                }
                else
                {
                    copyProxy(proxy.get(), draws);
                }
            }

            // ── 투명은 먼 것부터 ──
            //
            // 알파 블렌딩은 순서 의존이라 정렬하지 않으면 겹친 투명면이
            // 수집 순서에 따라 달라진다(프록시 순회 순서 = 임의). 카메라를
            // 아는 이 단계에서 정렬하므로 패스는 받은 순서대로 그리기만
            // 하면 되고, 그래서 패스의 자가 검증이 결정적으로 남는다.
            //
            // 기준은 오브젝트 원점의 카메라 전방 거리다. 물체 단위 근사라
            // 서로 관통하는 투명면은 여전히 어긋날 수 있는데, 그것을 고치려면
            // 삼각형 단위 정렬이나 OIT가 필요하다 — 여기서 할 일이 아니다.
            if (forwardDraws.size() > 1)
            {
                const Mathf::xVector eye = cameraSnapshot.eyePosition;
                const Mathf::xVector forward = cameraSnapshot.forward;

                // 월드 행렬의 r[3]이 이동 성분이다(오브젝트 원점).
                const auto viewDepth = [eye, forward](const EnhancedDrawItem& item)
                {
                    const Mathf::xVector delta =
                        XMVectorSubtract(item.worldMatrix.r[3], eye);
                    return XMVectorGetX(XMVector3Dot(delta, forward));
                };
                // ★ 깊이가 같을 때를 메시 포인터로 가른다.
                //
                //   같은 원점을 쓰는 투명면(십자 빌보드, 같은 피벗의 유리
                //   여러 장)은 깊이가 정확히 같다. 그때 비교자가 false만
                //   돌려주면 순서가 미정이라, 프록시 스냅샷 순서가 바뀔
                //   때마다 앞뒤가 뒤집혀 깜빡인다 — 정렬을 넣은 이유가
                //   '순서를 고정한다'인데 그 자리에서 새는 셈이다.
                std::stable_sort(forwardDraws.begin(), forwardDraws.end(),
                    [&viewDepth](const EnhancedDrawItem& a, const EnhancedDrawItem& b)
                    {
                        const float depthA = viewDepth(a);
                        const float depthB = viewDepth(b);
                        if (depthA != depthB) return depthA > depthB;
                        return a.mesh < b.mesh;
                    });
            }

            auto* lightController = renderScene->m_LightController;
            if (nullptr != lightController)
            {
                for (uint32 i = 0; i < lightController->m_lightCount; ++i)
                {
                    const Light& source = lightController->GetLight(i);

                    EnhancedLight light{};
                    light.position = source.m_position;
                    light.position.w = static_cast<float>(source.m_lightType);
                    light.direction = source.m_direction;
                    light.direction.w = XMConvertToRadians(source.m_spotLightAngle);
                    light.color = source.m_color;
                    light.color.w = source.m_intencity;
                    light.attenuation = Mathf::Vector4{
                        source.m_constantAttenuation, source.m_linearAttenuation,
                        source.m_quadraticAttenuation, source.m_range };

                    lights.push_back(light);
                }
            }

            return true;
        }

        // ── 한 프레임 ──
        //
        // 배선은 dx12.scene의 renderAndCount와 같다(리드백 프로브만 없다).
        // 마지막의 live_present가 이 러너의 고유 조각이다 — 포스트 체인 결과를
        // 공유 텍스처로 복사한다. 그 소비 선언이 있어야 그래프가 체인을
        // 걷어내지 않는다(post_probe가 하던 역할을 실전에서는 이 복사가 맡는다).
        bool RenderOnce(LivePipeline::CameraView& view, int slotIndex,
            std::string& outError)
        {
            LivePipeline& p = *pipeline;
            LivePipeline::DisplaySlot& slot = view.slots[slotIndex];

            if (!p.resources.BeginFrame(outError)) return false;
            p.profiler.BeginFrame(frameCounter % DX12DeviceResources::kFrameCount);
            ++frameCounter;

            // HDR 원본과 생성물의 소유권은 EnhancedRenderer에 있다. 예전처럼
            // SceneRenderer의 DX11 SkyBoxPass가 만든 큐브맵을 다시 운반하지
            // 않는다. 원본 equirect를 한 번 올린 뒤 같은 DX12 커맨드 리스트에서
            // cube/irradiance/prefilter/BRDF LUT를 생성하고 실제 소비 패스에 묶는다.
            if (!p.iblGenerated || skyBoxDirty)
            {
                if (!skyEquirect)
                {
                    try
                    {
                        skyEquirect = Texture::LoadManagedFromPath(file::path(skyBoxPath));
                    }
                    catch (const std::exception& exception)
                    {
                        outError = "HDR 로드 실패: " + std::string(exception.what());
                        return false;
                    }
                }

                if (!skyEquirect)
                {
                    outError = "HDR 로드 실패: " + skyBoxPath;
                    return false;
                }

                std::string skyUploadError;
                const DX12TextureCache::Entry skyEntry =
                    p.textureCache.GetOrUpload(skyEquirect.get(), skyUploadError);
                if (!skyEntry.IsValid() || skyEntry.isCube)
                {
                    outError = "equirect HDR 운반 실패";
                    if (!skyUploadError.empty()) outError += ": " + skyUploadError;
                    return false;
                }

                if (!p.ibl.Generate(p.frameContext, skyEntry.resource, skyEntry.format,
                    512, 512, outError))
                {
                    outError = "HDR→Cube/IBL 생성 실패: " + outError;
                    return false;
                }

                p.skyBox.SetCubeMap(p.ibl.GetCubeMap(), EnhancedIBLGenerator::kFormat, 1);
                p.deferred.SetIBL(p.ibl.GetIrradianceMap(), p.ibl.GetPrefilteredMap(),
                    EnhancedIBLGenerator::kPrefilterMips, p.ibl.GetBrdfLut());
                // 투명(Forward+)에도 같은 자원을 준다. 한쪽만 앰비언트를
                // 받으면 같은 재질이 투명일 때만 어둡게 보인다.
                p.forward.SetIBL(p.ibl.GetIrradianceMap(), p.ibl.GetPrefilteredMap(),
                    EnhancedIBLGenerator::kPrefilterMips, p.ibl.GetBrdfLut());
                p.iblGenerated = true;
                skyBoxDirty = false;
            }

            // 기즈모 씬 수집 — 파이프라인이 있어야 라인 패스에 쌓을 수 있어
            // CaptureScene이 아니라 여기(게임 스레드, 프레임 시작)에서 한다.
            // 콜라이더는 DX11과 같은 판정(디버그 모드)을 따른다.
            p.gizmoLine.ResetLines();
            gizmoData = {};
            BuildEnhancedGizmoSceneData(cameraSnapshot,
                ShouldCollectGizmoColliders(), p.gizmoLine, gizmoData);
            p.gizmoIcon.SetIcons(&gizmoData.icons);

            // ── 프레임 준비도 노드 목록 순서다(슬라이스 2) ──
            //
            // 데칼의 SetDecals 선행과 포그의 지연 초기화는 각 노드의 prepare
            // 람다로 옮겼다 — 예전에는 그 둘이 이 자리에서 순서 규칙으로
            // 존재했고, 노드를 옮길 때 함께 옮겨야 한다는 사실이 코드 어디에도
            // 적혀 있지 않았다.
            const uint32_t viewIndex = static_cast<uint32_t>(&view - &p.views[0]);
            if (!p.desc.PrepareAll(p.frameContext, viewIndex, outError)) return false;

            lastDrawCount = p.gbuffer.GetLastDrawCount();
            lastBatchCount = p.gbuffer.GetLastBatchCount();
            lastDecalCount = p.decal.GetLastDecalCount();
            lastDecalBatchCount = p.decal.GetLastBatchCount();

            slot.graph = std::make_unique<EnhancedRenderGraph>();
            EnhancedRenderGraph& graph = *slot.graph;
            graph.SetProfiler(&p.profiler);
            graph.SetTransientPool(&p.transientPool);

            // ── 조립은 노드 목록이 정한다(PHASE 3-10 슬라이스 1) ──
            //
            // 예전에는 여기 200줄이 "무엇을 어떤 순서로 잇는가"를 직접 적었다.
            // 지금은 BuildPipelineDesc가 짜 둔 노드 목록을 순서대로 돌 뿐이고,
            // 패스를 하나 잇는 일은 그 목록에 노드 하나를 넣는 일이 됐다.
            //
            // litColor 폴백 체인(`X.GetOutput().IsValid() ? X : 이전값`)이 통째로
            // 사라진 것에 주목할 것. 그 일은 이제 블랙보드 슬롯이 한다 —
            // 수정 노드가 꺼지면 슬롯 값이 그대로 남는다.
            p.blackboard.Reset();

            LiveFrameBinding binding{};
            binding.viewIndex = viewIndex;
            binding.sharedTarget = slot.sharedTexture.Get();

            p.desc.DeclareAll(p.blackboard, graph, p.frameContext, binding);

            if (!p.blackboard.Get(LiveSlots::kDisplayLdr).IsValid())
            {
                outError = "포스트 체인 출력이 없다";
                return false;
            }

            if (!graph.Compile(p.resources.GetDevice(), outError)) return false;
            if (!graph.Execute(p.resources.GetCommandList(), outError)) return false;

            p.profiler.ResolveFrame(p.resources.GetCommandList());

            if (!p.resources.EndFrame(outError)) return false;

            // 여기서 기다리지 않는다 — 이것이 이 슬라이스의 전부다.
            // EndFrame이 서명한 펜스 값을 슬롯에 적어 두고, TickLive가 다음
            // 프레임들에서 완료를 논블로킹으로 확인해 표시로 승격한다.
            // 1차 실측에서 게임 스레드의 동기 WaitForGpu가 프레임당 33ms를
            // 먹어 총 대기를 58ms로 만들었다(실제 GPU는 3.7ms) — 그 벽을
            // 여기서 없앤다.
            slot.fenceValue = p.resources.GetLastSignaledFenceValue();

            // 이번 프레임에 기록된 대형 텍스처 스테이징에 같은 펜스를 단다.
            // 슬롯과 같은 값이고 이유도 같다 — 이 제출이 끝나야 놓을 수 있다.
            p.textureCache.MarkStagingSubmitted(slot.fenceValue);

            view.pendingQueue.push_back(slotIndex);
            return true;
        }
    };

    void LiveState::ApplyAndPublishTuning()
    {
        // 파이프라인이 없으면 적용할 대상이 없다. 요청은 그대로 들고 있는다 —
        // 리사이즈로 파이프라인이 재구축되는 사이에 들어온 변경을 버리면
        // 사용자가 방금 움직인 슬라이더가 조용히 되돌아간다.
        if (nullptr == pipeline) return;

        LivePipeline& p = *pipeline;
        std::lock_guard<std::mutex> lock(debugMutex);

        if (hasPendingTuning)
        {
            {
                EnhancedSSAOPass::Tuning tuning = p.ssao.GetTuning();
                tuning.radius = pendingTuning.ssao.radius;
                tuning.thickness = pendingTuning.ssao.thickness;
                tuning.intensity = pendingTuning.ssao.intensity;
                tuning.filterDepthSigma = pendingTuning.ssao.filterDepthSigma;
                p.ssao.SetTuning(tuning);
            }
            // 뷰마다 SSGI 인스턴스가 따로이므로 전부에 적용한다 — 하나만
            // 고치면 씬 뷰와 게임 뷰의 GI 설정이 갈린다.
            for (LivePipeline::CameraView& view : p.views)
            {
                EnhancedSSGIPass::Tuning tuning = view.ssgi.GetTuning();
                tuning.traceDistance = pendingTuning.ssgi.traceDistance;
                tuning.traceThickness = pendingTuning.ssgi.traceThickness;
                tuning.accumDepthTolerance = pendingTuning.ssgi.accumDepthTolerance;
                tuning.filterDepthSigma = pendingTuning.ssgi.filterDepthSigma;
                tuning.filterNormalPower = pendingTuning.ssgi.filterNormalPower;
                tuning.compositeDepthSigma = pendingTuning.ssgi.compositeDepthSigma;
                tuning.intensity = pendingTuning.ssgi.intensity;
                view.ssgi.SetTuning(tuning);
            }
            // 포그도 뷰마다 인스턴스다 — 같은 이유로 전부에 건다.
            //
            // 끄는 전환은 여기서 표시만 하고 실제 해제는 TickLive가 한다 —
            // 해제가 GPU 완주를 기다리는데, 그 대기를 이 락 안에서 하면
            // 디버그 스냅샷을 읽는 CE 렌더 스레드가 함께 선다.
            if (fogEnabled && !pendingTuning.fog.enabled) fogTeardownPending = true;
            fogEnabled = pendingTuning.fog.enabled;
            for (LivePipeline::CameraView& view : p.views)
            {
                EnhancedVolumetricFogPass::Tuning tuning = view.fog.GetTuning();
                tuning.anisotropy = pendingTuning.fog.anisotropy;
                tuning.density = pendingTuning.fog.density;
                tuning.strength = pendingTuning.fog.strength;
                tuning.thicknessFactor = pendingTuning.fog.thicknessFactor;
                tuning.blendingWithSceneColorFactor =
                    pendingTuning.fog.blendingWithSceneColorFactor;
                tuning.previousFrameBlendFactor =
                    pendingTuning.fog.previousFrameBlendFactor;
                tuning.customNearPlane = pendingTuning.fog.customNearPlane;
                tuning.customFarPlane = pendingTuning.fog.customFarPlane;
                view.fog.SetTuning(tuning);
                view.fog.SetEnabled(fogEnabled);
            }
            {
                EnhancedSSSPass::Tuning tuning = p.sss.GetTuning();
                tuning.strength = pendingTuning.sss.strength;
                tuning.width = pendingTuning.sss.width;
                p.sss.SetTuning(tuning);
                p.sss.SetEnabled(pendingTuning.sss.enabled);
            }
            {
                EnhancedSSRPass::Tuning tuning = p.ssr.GetTuning();
                tuning.stepSize = pendingTuning.ssr.stepSize;
                tuning.maxThickness = pendingTuning.ssr.maxThickness;
                tuning.maxRayCount = pendingTuning.ssr.maxRayCount;
                p.ssr.SetTuning(tuning);
                p.ssr.SetEnabled(pendingTuning.ssr.enabled);
            }
            {
                EnhancedPostChainPass::Tuning tuning = p.postChain.GetTuning();
                tuning.bloomEnabled = pendingTuning.postChain.bloomEnabled;
                tuning.bloomThreshold = pendingTuning.postChain.bloomThreshold;
                tuning.bloomKnee = pendingTuning.postChain.bloomKnee;
                tuning.bloomIntensity = pendingTuning.postChain.bloomIntensity;
                tuning.toneMapEnabled = pendingTuning.postChain.toneMapEnabled;
                tuning.toneMapper = (0 == pendingTuning.postChain.toneMapper)
                    ? EnhancedPostChainPass::ToneMapper::ACES
                    : EnhancedPostChainPass::ToneMapper::AgX;
                tuning.exposure = pendingTuning.postChain.exposure;
                tuning.vignetteEnabled = pendingTuning.postChain.vignetteEnabled;
                tuning.vignetteRadius = pendingTuning.postChain.vignetteRadius;
                tuning.vignetteSoftness = pendingTuning.postChain.vignetteSoftness;
                tuning.vignetteIntensity = pendingTuning.postChain.vignetteIntensity;
                tuning.gradingEnabled = pendingTuning.postChain.gradingEnabled;
                tuning.saturation = pendingTuning.postChain.saturation;
                tuning.contrast = pendingTuning.postChain.contrast;
                tuning.fxaaEnabled = pendingTuning.postChain.fxaaEnabled;
                tuning.fxaaBias = pendingTuning.postChain.fxaaBias;
                tuning.fxaaBiasMin = pendingTuning.postChain.fxaaBiasMin;
                tuning.fxaaSpanMax = pendingTuning.postChain.fxaaSpanMax;
                p.postChain.SetTuning(tuning);
            }
            hasPendingTuning = false;
        }

        // 적용 후의 실제 값을 미러에 싣는다. 패스가 값을 보정하거나 다른
        // 경로(환경변수 초기화 등)가 바꿨을 수 있으므로 요청값이 아니라
        // 패스에서 되읽는다 — 창이 거짓 값을 보여주지 않게 하는 유일한 방법이다.
        {
            const EnhancedSSAOPass::Tuning& tuning = p.ssao.GetTuning();
            tuningMirror.ssao.radius = tuning.radius;
            tuningMirror.ssao.thickness = tuning.thickness;
            tuningMirror.ssao.intensity = tuning.intensity;
            tuningMirror.ssao.filterDepthSigma = tuning.filterDepthSigma;
        }
        {
            // 전 뷰가 같은 값이므로 대표로 하나만 되읽는다.
            const EnhancedSSGIPass::Tuning& tuning = p.views[0].ssgi.GetTuning();
            tuningMirror.ssgi.traceDistance = tuning.traceDistance;
            tuningMirror.ssgi.traceThickness = tuning.traceThickness;
            tuningMirror.ssgi.accumDepthTolerance = tuning.accumDepthTolerance;
            tuningMirror.ssgi.filterDepthSigma = tuning.filterDepthSigma;
            tuningMirror.ssgi.filterNormalPower = tuning.filterNormalPower;
            tuningMirror.ssgi.compositeDepthSigma = tuning.compositeDepthSigma;
            tuningMirror.ssgi.intensity = tuning.intensity;
        }
        {
            const EnhancedVolumetricFogPass::Tuning& tuning = p.views[0].fog.GetTuning();
            tuningMirror.fog.enabled = fogEnabled;
            tuningMirror.fog.anisotropy = tuning.anisotropy;
            tuningMirror.fog.density = tuning.density;
            tuningMirror.fog.strength = tuning.strength;
            tuningMirror.fog.thicknessFactor = tuning.thicknessFactor;
            tuningMirror.fog.blendingWithSceneColorFactor =
                tuning.blendingWithSceneColorFactor;
            tuningMirror.fog.previousFrameBlendFactor = tuning.previousFrameBlendFactor;
            tuningMirror.fog.customNearPlane = tuning.customNearPlane;
            tuningMirror.fog.customFarPlane = tuning.customFarPlane;
        }
        {
            const EnhancedSSSPass::Tuning& tuning = p.sss.GetTuning();
            tuningMirror.sss.enabled = p.sss.IsEnabled();
            tuningMirror.sss.strength = tuning.strength;
            tuningMirror.sss.width = tuning.width;
        }
        {
            const EnhancedSSRPass::Tuning& tuning = p.ssr.GetTuning();
            tuningMirror.ssr.enabled = p.ssr.IsEnabled();
            tuningMirror.ssr.stepSize = tuning.stepSize;
            tuningMirror.ssr.maxThickness = tuning.maxThickness;
            tuningMirror.ssr.maxRayCount = tuning.maxRayCount;
        }
        {
            const EnhancedPostChainPass::Tuning& tuning = p.postChain.GetTuning();
            tuningMirror.postChain.bloomEnabled = tuning.bloomEnabled;
            tuningMirror.postChain.bloomThreshold = tuning.bloomThreshold;
            tuningMirror.postChain.bloomKnee = tuning.bloomKnee;
            tuningMirror.postChain.bloomIntensity = tuning.bloomIntensity;
            tuningMirror.postChain.toneMapEnabled = tuning.toneMapEnabled;
            tuningMirror.postChain.toneMapper =
                (EnhancedPostChainPass::ToneMapper::ACES == tuning.toneMapper) ? 0 : 1;
            tuningMirror.postChain.exposure = tuning.exposure;
            tuningMirror.postChain.vignetteEnabled = tuning.vignetteEnabled;
            tuningMirror.postChain.vignetteRadius = tuning.vignetteRadius;
            tuningMirror.postChain.vignetteSoftness = tuning.vignetteSoftness;
            tuningMirror.postChain.vignetteIntensity = tuning.vignetteIntensity;
            tuningMirror.postChain.gradingEnabled = tuning.gradingEnabled;
            tuningMirror.postChain.saturation = tuning.saturation;
            tuningMirror.postChain.contrast = tuning.contrast;
            tuningMirror.postChain.fxaaEnabled = tuning.fxaaEnabled;
            tuningMirror.postChain.fxaaBias = tuning.fxaaBias;
            tuningMirror.postChain.fxaaBiasMin = tuning.fxaaBiasMin;
            tuningMirror.postChain.fxaaSpanMax = tuning.fxaaSpanMax;
        }
    }

    LiveState& GetLiveState()
    {
        static LiveState state;
        return state;
    }
}

bool EnhancedSceneRenderer::InitializeRuntime(std::string& outError)
{
    LiveState& state = GetLiveState();
    if (state.runtimeInitialized)
    {
        state.enabled = true;
        return true;
    }

    if (nullptr == DirectX11::DeviceStates->g_pDevice)
    {
        outError = "DX11 에디터 셸 디바이스 상태가 초기화되지 않았다";
        return false;
    }

    try
    {
        state.renderScene = std::make_shared<RenderScene>();
        state.renderScene->Initialize();

        // 카메라는 에디터 도구와 EnhancedRenderer의 프레임 입력 객체다.
        // SceneRenderer의 카메라별 RenderPassData를 생성하거나 읽지 않는다.
        state.editorCamera = std::make_shared<Camera>();
        state.editorCamera->RegisterContainer();
        state.editorCamera->m_avoidRenderPass.Set((flag)RenderPipelinePass::BlitPass);
        state.editorCamera->m_avoidRenderPass.Set((flag)RenderPipelinePass::AutoExposurePass);

        ShadowMapRenderDesc& desc = RenderScene::g_shadowMapDesc;
        desc.m_lookAt = XMVectorSet(0, 0, 0, 1);
        desc.m_eyePosition = Mathf::Vector4{ -1, -1, 1, 0 } * -50.f;
        desc.m_viewWidth = 100.f;
        desc.m_viewHeight = 100.f;
        desc.m_nearPlane = 0.1f;
        desc.m_farPlane = 1000.f;
        desc.m_textureWidth = 2048;
        desc.m_textureHeight = 2048;

        state.skyBoxPath =
            PathFinder::Relative("HDR\\kloofendal_43d_clear_puresky_4k.hdr").string();
        state.skyEquirect.reset();
        state.skyBoxDirty = true;

        // 포그 초기 켬/끔. 무인 검증이 UI 없이 켤 수 있어야 해서 둔다
        // (BuildPipeline의 후처리 환경변수와 같은 취지). 한 번만 읽는 이유는
        // 파이프라인 재구축(리사이즈)이 사용자가 창에서 고른 값을 되돌리면
        // 안 되기 때문이다.
        state.fogEnabled = ReadLivePostFlag("CREATOR_DX12_FOG", false);
        state.runtimeInitialized = true;
        state.enabled = true;
        state.lastError.clear();

        return true;
    }
    catch (const std::exception& exception)
    {
        outError = "EnhancedRenderer 런타임 초기화 실패: " +
            std::string(exception.what());
        state.lastError = outError;
        state.enabled = false;
        return false;
    }
}

Camera* EnhancedSceneRenderer::GetEditorCamera()
{
    return GetLiveState().editorCamera.get();
}

RenderScene* EnhancedSceneRenderer::GetRenderScene()
{
    return GetLiveState().renderScene.get();
}

void EnhancedSceneRenderer::SetActiveScene(Scene* scene)
{
    LiveState& state = GetLiveState();
    if (state.renderScene)
    {
        state.renderScene->SetScene(scene);
        state.renderScene->Update(0.f);
    }
}

bool EnhancedSceneRenderer::SetSkyBoxPath(const std::string& path, std::string& outError)
{
    LiveState& state = GetLiveState();
    if (!state.runtimeInitialized)
    {
        outError = "EnhancedRenderer 런타임이 초기화되지 않았다";
        return false;
    }
    if (path.empty())
    {
        outError = "HDR 경로가 비어 있다";
        return false;
    }

    state.skyBoxPath = path;
    state.skyEquirect.reset();
    state.skyBoxDirty = true;
    if (state.pipeline) state.pipeline->iblGenerated = false;
    state.lastError.clear();
    return true;
}

void EnhancedSceneRenderer::EnableLive()
{
    LiveState& state = GetLiveState();
    state.enabled = true;
    state.lastError.clear();
}

void EnhancedSceneRenderer::DisableLive()
{
    LiveState& state = GetLiveState();
    if (state.runtimeInitialized)
    {
        state.lastError = "Enhanced-only 모드에서는 메인 렌더러를 끌 수 없다";
        return;
    }
    state.enabled = false;
    state.TeardownPipeline();
}

bool EnhancedSceneRenderer::IsLiveEnabled()
{
    return GetLiveState().enabled;
}

void EnhancedSceneRenderer::WaitForLiveGpu()
{
    LiveState& state = GetLiveState();
    if (nullptr != state.pipeline && state.pipeline->resources.IsInitialized())
    {
        state.pipeline->resources.WaitForGpu();
    }
}

void EnhancedSceneRenderer::CaptureLiveFrame(const Camera* camera)
{
    LiveState& state = GetLiveState();
    if (!state.enabled) return;
    state.CaptureFromCamera(camera);
}

void EnhancedSceneRenderer::TickLive(float deltaSeconds, Camera* const* cameras,
    uint32_t cameraCount, bool sceneLoading)
{
    LiveState& state = GetLiveState();

    // 렌더 디버그 창이 읽을 값을 여기서 확정한다. 창은 CE 렌더 스레드에서
    // 그려지므로 상태를 직접 순회하게 두면 이 함수가 고치는 중인 벡터를
    // 읽는다. enabled 검사보다 앞에 두어 러너가 꺼져 있어도 창이 그 사실을
    // 볼 수 있게 한다.
    state.PublishDebugSnapshot();

    // 창이 넣어 둔 패스 파라미터 변경을 여기서 적용한다. 프레임 입력을
    // 밀봉하기 전이어야 이번 프레임부터 반영된다.
    state.ApplyAndPublishTuning();

    // 포그를 끈 전환의 실제 해제. 위가 락 안에서 표시만 해 둔 것을 여기서
    // 처리한다 — GPU 완주 대기를 락 안에서 하면 CE 렌더 스레드가 함께 선다.
    if (state.fogTeardownPending) state.ReleaseFogResources();

    if (!state.enabled) return;

    // SSR의 잡음 씨앗이 읽는다. 카메라 수와 무관하게 틱마다 한 번 쌓아야
    // 두 뷰가 같은 프레임에서 같은 씨앗을 본다 — 뷰마다 더하면 씬 뷰와
    // 게임 뷰의 반사 잡음이 갈린다.
    state.totalSeconds += deltaSeconds;

    // ── [프레임당 1회] 씬·프록시 갱신 ──
    //
    // SceneRenderer::CreateCommandListPass/EndOfFrame에서 이관한 프레임 입력
    // 단계. App이 두 렌더 배리어를 모두 지난 뒤 호출하므로 씬 구조 변경과
    // 에디터 카메라 조작이 끝난 안정된 프레임 경계다. 카메라 수와 무관하게
    // 프록시는 한 번만 민다 — 밀봉(CaptureFromCamera)만 카메라별이다.
    if (state.runtimeInitialized && state.renderScene && !sceneLoading)
    {
        ProxyCommandQueue->Execute();
        state.renderScene->EraseRenderPassData();
        state.renderScene->Update(deltaSeconds);
        state.renderScene->OnProxyDestroy();
    }

    // 인플라이트 제출분의 완료 확인(논블로킹) — 뷰마다. 제출 순서대로,
    // 완료된 것을 전부 표시로 승격한다 — DX11이 읽는 슬롯은 항상 '펜스가
    // 끝난' 슬롯뿐이라는 것이 비동기의 계약이다.
    if (nullptr != state.pipeline)
    {
        LivePipeline& p = *state.pipeline;

        // ── 대형 텍스처 스테이징 반납 (자산 상주 관리 ②-b) ──
        //
        // 슬롯 승격과 같은 판정을 같은 자리에서 한다. 예전에는
        // ReleaseStagingBuffers를 "GPU 유휴 시점에 부르라"고 주석에만 적어
        // 두었는데 아무도 안 불렀다 — 4K HDR equirect의 128MB가 종료까지
        // 살아 있었고 그것이 자산 상주 260MB의 절반이었다(실측).
        p.textureCache.SweepStagingBuffers(p.resources.GetCompletedFenceValue());

        for (LivePipeline::CameraView& view : p.views)
        {
            while (!view.pendingQueue.empty())
            {
                const int slotIndex = view.pendingQueue.front();
                if (p.resources.GetCompletedFenceValue() <
                    view.slots[slotIndex].fenceValue)
                {
                    break;
                }

                view.displaySlot = slotIndex;
                view.pendingQueue.erase(view.pendingQueue.begin());
                view.slots[slotIndex].graph.reset();   // GPU가 끝났다 — transient가 풀로 돌아간다

#if defined(_DEBUG)
                // ★ 검증 레이어를 읽는다. Debug 빌드에서 배선 오류(포맷·상태·
                //   디스크립터)는 여기에만 남는데 아무도 안 읽으면 증상만 보고
                //   추측하게 된다 — 실제로 그 상태로 며칠을 쫓았다.
                {
                    std::string validation;
                    if (0 != p.resources.DrainDebugMessages(validation) && !validation.empty())
                    {
                        if (state.reportedValidation.insert(validation).second)
                        {
                            std::printf("[dx12.live 검증] %s\n", validation.c_str());
                        }
                    }
                }
#endif

                std::vector<DX12GpuProfiler::PassTiming> timings;
                std::string collectError;
                if (p.profiler.Collect(timings, collectError))
                {
                    state.lastGpuMs = p.profiler.GetLastTotalMilliseconds();
                    // 패스별 시간은 예전에는 여기서 버려졌다 — 합계만 남기면
                    // "느려졌다"까지만 알 수 있고 어느 패스인지는 알 수 없다.
                    // 렌더 디버그 창이 읽도록 마지막 성공분을 보관한다.
                    state.lastPassTimings = std::move(timings);
                }
                ++state.framesRendered;
            }
        }
    }

    if (sceneLoading || 0 == cameraCount || nullptr == cameras)
    {
        ++state.framesIdle;
        return;
    }

    // 해상도는 밀봉이 카메라가 아니라 DeviceStates 뷰포트에서 가져오므로
    // 모든 뷰가 공유한다 — 재구축 판정도 프레임당 한 번이면 된다.
    // 크기가 바뀌면 통째로 다시 세운다. 패스들이 화면 크기 리소스를
    // Initialize에서 잡으므로 부분 리사이즈보다 재구축이 단순하고,
    // 에디터 뷰 리사이즈는 드문 사건이라 비용이 문제되지 않는다.
    const uint32_t rtWidth = static_cast<uint32_t>(
        DirectX11::DeviceStates->g_Viewport.Width);
    const uint32_t rtHeight = static_cast<uint32_t>(
        DirectX11::DeviceStates->g_Viewport.Height);
    if (0 == rtWidth || 0 == rtHeight)
    {
        ++state.framesIdle;
        return;
    }

    if (state.pipeline &&
        (rtWidth != state.pipeline->width || rtHeight != state.pipeline->height))
    {
        state.TeardownPipeline();
    }

    if (nullptr == state.pipeline)
    {
        std::string error;
        if (!state.BuildPipeline(rtWidth, rtHeight, error))
        {
            state.lastError = "파이프라인 구축 실패: " + error;
            state.TeardownPipeline();
            state.enabled = false;   // 실패를 조용히 반복하지 않는다
            return;
        }
    }

    LivePipeline& p = *state.pipeline;

    // 뷰 합산 인플라이트. '인플라이트 2 = 링(kFrameCount=3)의 안전 거리'는
    // 제출 총량 기준의 실측이다 — 뷰당 2씩 총 4를 들면 BeginFrame이
    // 얼로케이터 펜스에서 블로킹해 비동기 계약이 깨진다.
    size_t totalPending = 0;
    for (const LivePipeline::CameraView& view : p.views)
    {
        totalPending += view.pendingQueue.size();
    }

    LiveStopwatch watch;
    watch.Start();
    bool renderedAny = false;

    // ── [카메라마다] 뷰 배정 → 밀봉 → 렌더 제출 ──
    //
    // frameContext가 draws/lights 벡터의 주소를 들므로 '뷰1 제출 완료 →
    // 뷰2 밀봉'의 순차 흐름만 성립한다. 밀봉을 몰아서 하고 렌더를 몰아서
    // 하면 뷰1의 기록 입력이 뷰2 밀봉으로 재구성되어 무효가 된다.
    const uint32_t startIndex = state.viewRotation++ % cameraCount;
    for (uint32_t step = 0; step < cameraCount; ++step)
    {
        const uint32_t cameraIndex = (startIndex + step) % cameraCount;
        const Camera* camera = cameras[cameraIndex];
        if (nullptr == camera) continue;

        if (totalPending >= 2)
        {
            ++state.framesInFlight;
            continue;
        }

        // 뷰 배정: 같은 카메라의 뷰 → 빈 뷰 → 이번 틱 목록에 없는 카메라의
        // 뷰(교체) 순으로 찾는다. 교체 시 표시 슬롯을 비워 옛 카메라의
        // 그림이 새 카메라의 창에 남지 않게 한다 — 아직 인플라이트인 옛
        // 프레임이 한두 틱 승격될 수 있으나 곧 새 프레임이 덮는다.
        LivePipeline::CameraView* view = nullptr;
        for (LivePipeline::CameraView& candidate : p.views)
        {
            if (candidate.camera == camera) { view = &candidate; break; }
        }
        if (nullptr == view)
        {
            for (LivePipeline::CameraView& candidate : p.views)
            {
                if (nullptr == candidate.camera) { view = &candidate; break; }
            }
        }
        if (nullptr == view)
        {
            for (LivePipeline::CameraView& candidate : p.views)
            {
                bool inList = false;
                for (uint32_t j = 0; j < cameraCount; ++j)
                {
                    if (candidate.camera == cameras[j]) { inList = true; break; }
                }
                if (!inList) { view = &candidate; break; }
            }
        }
        if (nullptr == view) continue;   // 카메라가 kMaxCameraViews를 넘는다

        if (view->camera != camera)
        {
            view->camera = camera;
            view->displaySlot = -1;
        }

        // 표시 중도 인플라이트도 아닌 슬롯에 그린다.
        int renderSlot = -1;
        for (int i = 0; i < LivePipeline::kSlotsPerView; ++i)
        {
            if (i == view->displaySlot) continue;
            bool pending = false;
            for (const int pendingIndex : view->pendingQueue)
            {
                if (pendingIndex == i) { pending = true; break; }
            }
            if (!pending) { renderSlot = i; break; }
        }
        if (renderSlot < 0)
        {
            ++state.framesInFlight;
            continue;
        }

        // 밀봉이 cameraSnapshot과 draws를 이 카메라 기준으로 재구성한다.
        if (!state.CaptureFromCamera(camera))
        {
            ++state.framesIdle;
            continue;
        }

        std::string error;
        if (!state.RenderOnce(*view, renderSlot, error))
        {
            state.lastError = "프레임 실패: " + error;
            state.TeardownPipeline();
            state.enabled = false;
            return;
        }
        ++totalPending;
        renderedAny = true;
    }

    if (renderedAny) state.lastCpuMs = watch.ElapsedMs();
}

ID3D11ShaderResourceView* EnhancedSceneRenderer::GetLiveDisplaySrv(const Camera* camera)
{
    const LiveState& state = GetLiveState();
    if (!state.enabled || nullptr == state.pipeline) return nullptr;
    if (nullptr == camera) return nullptr;

    for (const LivePipeline::CameraView& view : state.pipeline->views)
    {
        if (view.camera != camera) continue;
        if (view.displaySlot < 0) return nullptr;
        return view.slots[view.displaySlot].openedSrv.Get();
    }
    return nullptr;
}

uint64_t EnhancedSceneRenderer::GetLiveDisplayImTextureId(const Camera* camera)
{
    // CE 스레드(ImGui 빌드)에서 불리고 게임 스레드가 슬롯을 갱신한다 —
    // GetLiveDisplaySrv와 같은 무해한 경합이다: 한 프레임 이전 슬롯을 읽어도
    // 그 슬롯 역시 펜스가 끝난 그림이고, 핸들은 파이프라인 수명 동안 불변이다.
    const LiveState& state = GetLiveState();
    if (!ImGuiDx12Shell::Get().IsActive()) return 0;
    if (!state.enabled || nullptr == state.pipeline) return 0;
    if (nullptr == camera) return 0;

    for (const LivePipeline::CameraView& view : state.pipeline->views)
    {
        if (view.camera != camera) continue;
        if (view.displaySlot < 0) return 0;
        return ImGuiDx12Shell::Get().OpenSharedTexture(
            view.slots[view.displaySlot].sharedHandle);
    }
    return 0;
}

std::string EnhancedSceneRenderer::GetLiveStatus()
{
    const LiveState& state = GetLiveState();

    char line[384]{};
    std::snprintf(line, sizeof(line),
        "EnhancedRenderer(DX12) — %s · 파이프라인 %s · %ux%u · 렌더 %llu프레임(대기 %llu)"
        " · 인플라이트 스킵 %llu · 드로우 %u(배치 %u) · CPU %.2f ms · GPU %.2f ms"
        " · 묘지 %zu",
        state.enabled ? "켜짐" : "꺼짐",
        state.pipeline ? "준비됨" : "없음",
        state.pipeline ? state.pipeline->width : 0u,
        state.pipeline ? state.pipeline->height : 0u,
        static_cast<unsigned long long>(state.framesRendered),
        static_cast<unsigned long long>(state.framesIdle),
        static_cast<unsigned long long>(state.framesInFlight),
        state.lastDrawCount, state.lastBatchCount,
        state.lastCpuMs, state.lastGpuMs,
        state.graveyard.size());

    std::string status = line;

    char decalLine[128]{};
    std::snprintf(decalLine, sizeof(decalLine), "\n  데칼 %u개(배치 %u) · SSS %s · SSR %s",
        state.lastDecalCount, state.lastDecalBatchCount,
        (state.pipeline && state.pipeline->sss.IsEnabled()) ? "켜짐" : "꺼짐",
        (state.pipeline && state.pipeline->ssr.IsEnabled()) ? "켜짐" : "꺼짐");
    status += decalLine;

    // ── 텍스처 업로드 (T1 → T4) ──
    //
    // 예전에는 'CPU 직결 : DX11 경유'의 비를 냈고 그 비가 T1의 진척이었다.
    // 실측이 DX11 경유 0에 닿아 T4에서 그 경로를 걷었으므로 이제 경로는
    // 하나다 — 직결 수가 업로드 수와 갈리면 어딘가 다른 길이 생긴 것이다.
    //
    // ★ 실패 수를 계속 본다: CPU 픽셀이 없는 텍스처(런타임 생성·지형 배열
    //   등)가 오면 흰색으로 대체되고 여기 잡힌다. 그것이 T5·T6의 남은 양이다.
    if (state.pipeline)
    {
        const auto texStats = state.pipeline->textureCache.GetStats();
        char textureLine[160]{};
        std::snprintf(textureLine, sizeof(textureLine),
            "\n  텍스처 업로드 %u건 (CPU 직결 %u) · 히트 %u · 실패 %u",
            texStats.uploads, texStats.fromCpuPixels,
            texStats.hits, texStats.failures);
        status += textureLine;

        // ── 자산 상주량 (자산 상주 관리 ②) ──
        //
        // 위 '업로드'는 누적이라 지금 VRAM을 얼마나 먹고 있는지 답하지 못한다.
        // 두 캐시는 항목을 버리는 코드가 없어서(erase·Evict 0건) 이 값이
        // 단조 증가한다 — 씬을 바꿔도 안 줄어든다.
        //
        // ★ 판정: 씬 A → B → A 왕복 후 상주가 기준선으로 돌아오는가.
        //   ③(미사용 기반 은퇴)이 들어오기 전까지는 증가가 정상이고,
        //   그 증가폭이 곧 ③이 회수할 양이다.
        constexpr double kBytesPerMB = 1024.0 * 1024.0;
        const auto meshStats = state.pipeline->meshCache.GetStats();

        char residentLine[224]{};
        std::snprintf(residentLine, sizeof(residentLine),
            "\n  자산 상주 — 텍스처 %u개 %.1f MB · 메시 %u개 %.1f MB"
            " · 스테이징 %u개 %.1f MB · 합계 %.1f MB",
            texStats.residentCount,  texStats.residentBytes  / kBytesPerMB,
            meshStats.residentCount, meshStats.residentBytes / kBytesPerMB,
            texStats.stagingCount,   texStats.stagingBytes   / kBytesPerMB,
            (texStats.residentBytes + meshStats.residentBytes
                + texStats.stagingBytes) / kBytesPerMB);
        status += residentLine;
    }

    // ── RTV/DSV 힙 사용량 (R2b) ──
    //
    // 용량을 잡을 때 "정확한 값은 재서 정한다"고 적었는데, 재는 자리가 없으면
    // 그 말은 빈말이다. 두 수를 여기 낸다:
    //
    //   최대/용량 — 이 값이 용량을 정하는 유일한 근거다(지금 잡은 수는 여유다)
    //   넘침      — 0이 아니면 그 프레임의 어떤 패스가 렌더 타깃을 못 만들고
    //               조용히 돌아섰다는 뜻이다. 화면에는 '한 패스가 안 그려졌다'로만
    //               나타나므로 이 수가 아니면 알아낼 길이 없다.
    if (state.pipeline)
    {
        const auto rtvStats = state.pipeline->resources.GetRtvViewHeap().GetStats();
        const auto dsvStats = state.pipeline->resources.GetDsvViewHeap().GetStats();
        char targetLine[192]{};
        std::snprintf(targetLine, sizeof(targetLine),
            "\n  타깃 뷰 힙 — RTV 최대 %u/%u(넘침 %llu) · DSV 최대 %u/%u(넘침 %llu)",
            rtvStats.peakFrameDescriptors,
            state.pipeline->resources.GetRtvViewHeap().GetCapacity(),
            static_cast<unsigned long long>(rtvStats.overflows),
            dsvStats.peakFrameDescriptors,
            state.pipeline->resources.GetDsvViewHeap().GetCapacity(),
            static_cast<unsigned long long>(dsvStats.overflows));
        status += targetLine;
    }

    // ── 마지막 프레임의 패스 이름 ──
    //
    // '배선했다'를 콘솔에서 단정할 수 있는 유일한 값이다. 패스가 그래프에
    // 들어갔는지는 화면만 봐서는 알 수 없고(꺼진 패스는 입력을 그대로
    // 흘리므로 그림이 같다), 소비자가 없어 컬링당한 패스도 조용히 사라진다
    // — 이름이 여기 뜨는 것이 곧 그 패스가 실제로 실행됐다는 뜻이다.
    if (!state.lastPassTimings.empty())
    {
        status += "\n  패스: ";
        for (size_t i = 0; i < state.lastPassTimings.size(); ++i)
        {
            if (0 != i) status += " → ";
            status += state.lastPassTimings[i].name;
        }
    }

    if (!state.lastError.empty()) status += "\n  마지막 오류: " + state.lastError;
    return status;
}

EnhancedLiveDebugSnapshot EnhancedSceneRenderer::GetLiveDebugSnapshot()
{
    // CE 렌더 스레드에서 불린다(헤더의 스레드 규약 참조). 상태를 직접 읽지
    // 않고 게임 스레드가 완성해 둔 것을 락으로 복사만 한다.
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.debugMutex);
    return state.debugSnapshot;
}

EnhancedLiveTuning EnhancedSceneRenderer::GetLiveTuning()
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.debugMutex);
    return state.tuningMirror;
}

void EnhancedSceneRenderer::SetLiveTuning(const EnhancedLiveTuning& tuning)
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.debugMutex);
    state.pendingTuning = tuning;
    state.hasPendingTuning = true;
}

void EnhancedSceneRenderer::ShutdownLive()
{
    LiveState& state = GetLiveState();
    state.enabled = false;
    state.TeardownPipeline();

    for (LiveState::Grave& grave : state.graveyard)
    {
        grave.openedSrv.Reset();
        grave.openedTexture.Reset();
        if (nullptr != grave.sharedHandle) ::CloseHandle(grave.sharedHandle);
        grave.sharedTexture.Reset();
    }
    state.graveyard.clear();

    if (state.runtimeInitialized)
    {
        // 카메라 관리 컨테이너는 RenderScene보다 먼저 내린다.
        if (state.editorCamera)
        {
            CameraManagement->DeleteCamera(state.editorCamera->m_cameraIndex);
            state.editorCamera.reset();
        }
        CameraManagement->Finalize();

        state.skyEquirect.reset();
        if (state.renderScene)
        {
            // SceneManager::Decommissioning이 활성 RenderScene을 먼저 Finalize한다.
            // 여기서 다시 부르면 LightController/컨테이너를 이중 정리한다.
            state.renderScene.reset();
        }
        state.runtimeInitialized = false;
    }
}

#endif
