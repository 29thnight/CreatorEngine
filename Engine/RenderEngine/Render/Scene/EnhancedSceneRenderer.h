#pragma once
#include "../Core/EnhancedLivePipelineDesc.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "../../FrameCameraSnapshot.h"
#include "../../ShaderMetaHandle.h"
#include "../../../Utility_Framework/TypeTrait.h"

// ID3D11ShaderResourceView 전방 선언이 여기 있었다 (E, 2026-08-09).
// GetLiveDisplaySrv가 D4에서 사라진 뒤로 쓰는 선언이 없다.
class RenderScene;
class Scene;
class Material;
struct EnhancedGizmoSceneData;
struct EnhancedGizmoIconTextures;
struct IRenderFeatureContributor;
struct IDisplayPresentationSink;
struct ShaderMeta;

inline constexpr uint32_t kEnhancedMaxLiveCameraViews = 2; // scene + game view

/// ImGui composition이 요청하는 논리 표시 대상. 카메라의 소유권이나 backend
/// 슬롯과 무관하며 Host가 요청마다 명시한다.
enum class EnhancedLiveDisplayTarget : uint8_t
{
    Editor = 0,
    Game = 1,
    Count,
};

enum class EnhancedLiveViewFlags : uint32_t
{
    None = 0,
    SceneOverlay = 1u << 0,
    ScreenSpaceUI = 1u << 1,
    CanvasPreview = 1u << 2,
};

inline EnhancedLiveViewFlags operator|(EnhancedLiveViewFlags left,
    EnhancedLiveViewFlags right)
{
    return static_cast<EnhancedLiveViewFlags>(
        static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

inline bool HasViewFlag(EnhancedLiveViewFlags value,
    EnhancedLiveViewFlags flag)
{
    return 0 != (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag));
}

inline constexpr uint64_t kEnhancedEditorViewId = 1;
inline constexpr uint64_t kEnhancedGameViewId = 2;

/// Stable identity for a logical output view. historyRevision is advanced by
/// the Director on a cut/source replacement; RenderCore never interprets it as
/// a camera-container slot.
struct EnhancedLiveViewKey
{
    uint64_t viewId{ 0 };
    uint64_t historyRevision{ 0 };

    bool IsValid() const { return 0 != viewId; }
};

inline bool operator==(const EnhancedLiveViewKey& left,
    const EnhancedLiveViewKey& right)
{
    return left.viewId == right.viewId &&
        left.historyRevision == right.historyRevision;
}

inline bool operator!=(const EnhancedLiveViewKey& left,
    const EnhancedLiveViewKey& right)
{
    return !(left == right);
}

/// One view's immutable game-thread capture. No Camera/Entity pointer is
/// carried across the render boundary. Gizmo input is also captured on the
/// producer side because collecting it walks the live Scene hierarchy.
struct EnhancedLiveViewPacket
{
    EnhancedLiveViewKey key;
    FrameCameraSnapshot camera;
    std::shared_ptr<const EnhancedGizmoSceneData> gizmos;
    EnhancedLiveDisplayTarget displayTarget{ EnhancedLiveDisplayTarget::Game };
    EnhancedLiveViewFlags viewFlags{ EnhancedLiveViewFlags::ScreenSpaceUI };
};

/// Host가 프레임 밀봉에 넘기는 뷰 요청 하나. 표시 대상과 도구 기능은
/// Camera 속성이 아니라 Editor/Player Host가 명시하는 뷰 정책이다.
struct EnhancedLiveViewRequest
{
    EnhancedLiveViewKey key{};
    FrameCameraSnapshot camera{};
    EnhancedLiveDisplayTarget displayTarget{ EnhancedLiveDisplayTarget::Game };
    EnhancedLiveViewFlags viewFlags{ EnhancedLiveViewFlags::ScreenSpaceUI };
};

/// GT가 DataSystem generation handle과 immutable 값을 한 쌍으로 밀봉한 셰이더 입력.
/// RenderThread는 DataSystem이나 authoring 파일을 다시 읽지 않는다. error가 있으면
/// 최초 부팅은 fail-closed하고, 이미 적용한 render request가 있으면 그 PSO를 유지한다.
struct EnhancedShaderMetaFrameSnapshot
{
    FileGuid guid{};
    ShaderMetaHandle handle{};
    std::shared_ptr<const ShaderMeta> value;
    std::string error;

    bool IsValid() const
    {
        return FileGuid{} != guid && handle.IsValid() && nullptr != value;
    }
};

/// Host가 현재 Scene의 실제 draw material에서 수집한 ShaderMeta 의존성.
/// 파일 이름이나 catalog의 대표 목록을 렌더러 안에 박아 넣지 않고, GT가
/// pass domain과 GUID만 선언한다. BuildLiveFramePacket은 이를 generation/value
/// owner로 해석하므로 RT는 DataSystem이나 authoring 파일을 다시 읽지 않는다.
enum class EnhancedShaderMetaDomain : uint8_t
{
    GBuffer,
    Forward,
};

struct EnhancedRequiredShaderMetaAsset
{
    EnhancedShaderMetaDomain domain{ EnhancedShaderMetaDomain::GBuffer };
    FileGuid guid{};

    bool operator==(const EnhancedRequiredShaderMetaAsset&) const = default;
};

struct EnhancedRequiredAssetPacket
{
    std::vector<EnhancedRequiredShaderMetaAsset> shaderMetas;

    void RequireShaderMeta(EnhancedShaderMetaDomain domain, const FileGuid& guid)
    {
        if (FileGuid{} == guid) return;
        const EnhancedRequiredShaderMetaAsset request{ domain, guid };
        if (std::find(shaderMetas.begin(), shaderMetas.end(), request)
            == shaderMetas.end())
        {
            shaderMetas.push_back(request);
        }
    }

    void Canonicalize()
    {
        std::erase_if(shaderMetas, [](const auto& request)
        {
            return FileGuid{} == request.guid;
        });
        std::sort(shaderMetas.begin(), shaderMetas.end(),
            [](const auto& left, const auto& right)
            {
                if (left.domain != right.domain)
                {
                    return static_cast<uint8_t>(left.domain)
                        < static_cast<uint8_t>(right.domain);
                }
                return left.guid < right.guid;
            });
        shaderMetas.erase(std::unique(shaderMetas.begin(), shaderMetas.end()),
            shaderMetas.end());
    }

    bool ContainsShaderMeta(EnhancedShaderMetaDomain domain,
        const FileGuid& guid) const
    {
        return std::find(shaderMetas.begin(), shaderMetas.end(),
            EnhancedRequiredShaderMetaAsset{ domain, guid }) != shaderMetas.end();
    }
};

/// Immutable per-frame message from the game thread to the renderer.
///
/// 3-2B first feeds this synchronously to TickLive. 3-2E can therefore insert
/// a bounded queue without changing the renderer's input contract again.
struct EnhancedLiveFramePacket
{
    uint64_t frameId{ 0 };
    uint64_t sceneEpoch{ 0 };
    uint64_t resizeGeneration{ 0 };
    float    deltaSeconds{ 0.f };
    float    totalSeconds{ 0.f };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    bool     sceneLoading{ false };
    // M6-P2d-d: Host가 실제 Scene material에서 수집해 넘긴 추가 의존성.
    // 아래 ShaderMeta owner 배열은 이 선언과 primary/cache 입력을 resolve한 결과다.
    EnhancedRequiredAssetPacket requiredAssets;
    // 첫 항목은 제품 GBuffer primary meta다. 뒤에는 GT가 DataSystem의 material
    // generation 집합에서 함께 밀봉한 material별 meta가 GUID 순서로 붙는다.
    std::vector<EnhancedShaderMetaFrameSnapshot> gbufferShaderMetas;
    // M6-P2b: 첫 항목은 제품 Forward primary meta다. P2c가 water/wind 등
    // material별 Forward pass를 연결할 때 같은 owner 배열을 확장한다.
    std::vector<EnhancedShaderMetaFrameSnapshot> forwardShaderMetas;
    uint32_t viewCount{ 0 };
    std::array<EnhancedLiveViewPacket, kEnhancedMaxLiveCameraViews> views{};

    const EnhancedShaderMetaFrameSnapshot* FindGBufferShaderMeta(
        const FileGuid& guid) const
    {
        const auto found = std::find_if(gbufferShaderMetas.begin(),
            gbufferShaderMetas.end(), [&guid](const auto& snapshot)
            {
                return snapshot.guid == guid;
            });
        return found == gbufferShaderMetas.end() ? nullptr : &*found;
    }

    const EnhancedShaderMetaFrameSnapshot* FindForwardShaderMeta(
        const FileGuid& guid) const
    {
        const auto found = std::find_if(forwardShaderMetas.begin(),
            forwardShaderMetas.end(), [&guid](const auto& snapshot)
            {
                return snapshot.guid == guid;
            });
        return found == forwardShaderMetas.end() ? nullptr : &*found;
    }
};

struct EnhancedRenderThreadStats
{
    uint64_t published{ 0 };
    uint64_t consumed{ 0 };
    uint64_t overflowEvents{ 0 };
    uint64_t coalescedFrames{ 0 };
    uint64_t coalescedDeltas{ 0 };
    uint64_t backPressureWaits{ 0 };
    uint64_t shutdownDrains{ 0 };
    uint64_t shutdownDiscardedDeltas{ 0 };
    uint32_t pending{ 0 };
    uint32_t inProgress{ 0 };
    uint32_t highWatermark{ 0 };
    uint32_t capacity{ 0 };
    /// 게임 스레드가 마지막으로 발행한 frame id. 읽는 순간의 값이다.
    uint64_t publishedFrameId{ 0 };
    /// RenderThread 가 TickLive 를 **끝낸** 마지막 frame id.
    /// ★ EnhancedLiveDebugSnapshot::consumedFrameId 는 TickLive **시작**에 적힌다 —
    ///   첫 프레임의 ShaderMeta 반영처럼 긴 프레임 도중에도 이미 그 id 를 가리킨다.
    ///   "이 시점 이후 발행된 프레임이 그려졌다"는 이 값으로만 판정한다.
    uint64_t completedFrameId{ 0 };
    bool running{ false };
    bool accepting{ false };
    bool producerConsumerSeparated{ false };
};

/// 라이브 씬이 부팅 시 고정할 RHI 백엔드. 엔트리 계층이
/// RuntimeSettings의 백엔드를 이 값으로 변환하고, 같은 선택을 ImGuiHost에도 적용한다.
enum class EnhancedLiveBackend : uint8_t
{
    DX12,
    Vulkan,
};

inline constexpr uint32_t kEnhancedLiveDisplayTargetCount =
    static_cast<uint32_t>(EnhancedLiveDisplayTarget::Count);

/// GPU 완료 결과와 UI의 텍스처 조회 관측. 둘 다 표시 수명 락으로 보호한다.
/// presentation handle은 구현 안에 숨으며 조회 성공은 GPU present 성공과 구분한다.
struct EnhancedLiveDisplayEntrySnapshot
{
    EnhancedLiveViewKey key{};
    uint64_t completedFrameId{ 0 };
    uint64_t promotionCount{ 0 };
    uint64_t completedResizeGeneration{ 0 };
    uint32_t completedWidth{ 0 };
    uint32_t completedHeight{ 0 };
    uint64_t textureQueries{ 0 };
    uint64_t missingTextureQueries{ 0 };
    uint64_t lastTextureFrameId{ 0 };
    uint64_t lastTextureResizeGeneration{ 0 };
    uint64_t lastMissingResizeGeneration{ 0 };
    double lastMissingTextureMs{ 0.0 };
    double maxMissingTextureMs{ 0.0 };
    bool lastTextureAvailable{ false };
    uint32_t promotedSlotMask{ 0 };
    bool active{ false };
    bool ready{ false };
};

/// RenderThread -> CE ImGui의 불변 출력 경계. sourceFrameId의 입력 역할과
/// GPU 완료된 표시 결과를 함께 담되 Camera*, DX12/Vulkan 객체는 담지 않는다.
struct EnhancedLiveDisplaySnapshot
{
    EnhancedLiveBackend backend{ EnhancedLiveBackend::DX12 };
    uint64_t iblGenerationCount{ 0 };
    uint64_t revision{ 0 };
    uint64_t sourceFrameId{ 0 };
    uint64_t resizeGeneration{ 0 };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    std::array<EnhancedLiveDisplayEntrySnapshot,
        kEnhancedLiveDisplayTargetCount> targets{};

    const EnhancedLiveDisplayEntrySnapshot& Get(
        EnhancedLiveDisplayTarget target) const
    {
        return targets[static_cast<uint32_t>(target)];
    }
};

// Texture identity and its completed extent are read under the same lifetime
// lock. The latest submitted frame may already belong to a different resize.
struct EnhancedLiveDisplayTexture
{
    uint64_t textureId{ 0 };
    uint32_t width{ 0 }, height{ 0 };
};

/// 패스 하나의 GPU 시간. DX12GpuProfiler::PassTiming을 에디터로 옮기는 값
/// 타입이다 — 그 헤더는 d3d12.h를 끌고 오므로 UI 계층에 노출하지 않는다.
/// 한 제출의 GPU 구간 요약. 합계를 하나로 정의하지 않는다(§3.4).
///
/// ★ queueSpan 은 첫 timestamp 부터 마지막까지이고, busy 는 겹치지 않는 실행
///   구간의 합이다. 둘의 차이가 공백이고, 그것을 보지 못하면 "GPU 가 바쁘다" 와
///   "큐가 잡혀 있다" 를 가를 수 없다.
struct EnhancedLiveGpuSpan
{
    double   queueSpanMs{ 0.0 };
    double   busyMs{ 0.0 };
    uint32_t sliceCount{ 0 };
    uint32_t droppedSlices{ 0 };

    /// 버린 조각 중 첫 번째의 패스 이름. 비어 있으면 버린 것이 없다는 뜻이다.
    std::string droppedSliceName;
    int64_t     droppedSliceDeltaTicks{ 0 };

    /// 길이 0 으로 찍힌 조각. 결함이 아니라 분해능 아래의 패스다.
    uint32_t    zeroLengthSlices{ 0 };

    /// queue span 을 CPU(QPC) 축으로 옮긴 것과, 그 축이 살아 있는지.
    uint64_t    queueBeginCpuTicks{ 0 };
    uint64_t    queueEndCpuTicks{ 0 };
    bool        cpuAligned{ false };

    /// 제출을 연 CPU 시각부터 변환한 GPU 시작까지. 음수면 GPU 가 제출보다 먼저
    /// 돌았다는 뜻이고, 그것은 두 시계가 맞지 않는다는 말이다.
    double      submitToGpuBeginMs{ 0.0 };
    /// 변환한 GPU 끝부터 수집한 CPU 시각까지. 음수면 아직 안 끝난 것을 읽었다.
    double      gpuEndToCollectMs{ 0.0 };

    /// 제출을 연 순간부터 수집까지의 경과.
    ///
    /// ★ 이것이 EngineDiagnostics 귀속의 전제다. capture_ring 은 이벤트를
    ///   **수집한 프레임**의 기록에 담는데 GPU 구간은 늦게 온다. 이 수가 한
    ///   프레임보다 크면 GPU 레인이 제 프레임 칸을 벗어난다는 뜻이다.
    double      submitToCollectMs{ 0.0 };
};

/// 두 시계를 맞춘 표본의 상태(§5.1).
struct EnhancedLiveGpuClock
{
    uint64_t gpuTicksPerSecond{ 0 };
    uint64_t cpuTicksPerSecond{ 0 };
    uint64_t sampleCount{ 0 };
    double   lastDriftMs{ 0.0 };
    double   maxAbsoluteDriftMs{ 0.0 };
    bool     valid{ false };
    std::string lastError;
};

/// GPU 조각 하나를 CPU(QPC) 축에 얹은 것. 이름으로 묶기 **전**의 원본이다.
struct EnhancedLiveGpuSlice
{
    std::string name;
    uint64_t    beginCpuTick{ 0 };
    uint64_t    endCpuTick{ 0 };
};

/// GPU 구간을 밖으로 흘리는 자리(§7.3 의 GPU Graphics queue).
///
/// ★ 이 층은 프로파일러를 모른다. 함수를 받아 두고 부르기만 한다 —
///   thread_pool::worker_hooks 와 같은 뒤집기다(P2). 그 경계가 없으면
///   RenderEngine 이 EngineDiagnostics 에 의존하게 되고, 그것은 이 트랙이
///   내내 피해 온 방향이다.
///
/// ★ name 은 **부르는 동안만** 유효하다. 받는 쪽이 곧바로 자기 표에 옮긴다.
struct EnhancedLiveGpuSpanSink
{
    void (*on_span)(const char* name, uint64_t beginCpuTick, uint64_t endCpuTick,
                    uint32_t engineFrameId) = nullptr;
    void (*on_flush)() = nullptr;
};

/// sink 를 건다. 렌더러가 서기 전에 걸어야 첫 수집부터 흐른다.
void SetEnhancedLiveGpuSpanSink(const EnhancedLiveGpuSpanSink& sink);

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
/// 여기 없는 패스는 조정 파라미터 자체가 없다(GBuffer·Deferred·Shadow·
/// SkyBox·Forward·Sprite·UI·Grid·Gizmo·Decal 계열). Sprite/UI도 라이브
/// 그래프에 배선되어 있지만 별도의 런타임 튜닝 값은 노출하지 않는다.
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
    EnhancedLiveBackend backend{ EnhancedLiveBackend::DX12 };
    bool     enabled{ false };
    bool     pipelineReady{ false };
    uint32_t width{ 0 };
    uint32_t height{ 0 };
    uint64_t framesRendered{ 0 };
    uint64_t framesIdle{ 0 };
    uint64_t framesInFlight{ 0 };
    uint64_t publishedFrameId{ 0 };
    uint64_t consumedFrameId{ 0 };
    uint64_t sceneEpoch{ 0 };
    uint64_t resizeGeneration{ 0 };
    uint32_t drawCount{ 0 };
    uint32_t batchCount{ 0 };

    /// 이번 프레임에 그린 데칼 수와 실제 발행한 드로우 수. 데칼은 씬에
    /// 프록시가 없으면 패스가 통째로 빠지므로, 0이 고장인지 '데칼이 없는
    /// 씬'인지는 이 값만으로는 갈리지 않는다 — 갈리는 것은 둘의 관계다.
    /// 데칼이 있는데 배치가 0이면 텍스처 운반이 실패한 것이고, 둘이 늘
    /// 같으면 인스턴싱이 죽은 것이다.
    uint32_t decalCount{ 0 };
    uint32_t decalBatchCount{ 0 };
    uint32_t spriteCount{ 0 };
    uint32_t spriteBatchCount{ 0 };
    uint32_t uiRectCount{ 0 };
    uint32_t uiBatchCount{ 0 };
    double   cpuMs{ 0.0 };
    double   gpuMs{ 0.0 };

    /// GPU 수집 장부. mismatches 가 0 이 아니면 그만큼의 수치가 **끝난 제출이
    /// 아닌 다른 제출의 것**이다 — Collect() 가 token 을 안 받기 때문이고,
    /// P4 가 GpuFrameToken 을 세우면 0 이어야 한다.
    uint64_t gpuCollects{ 0 };
    uint64_t gpuCollectMismatches{ 0 };

    /// 길이 셋의 관계를 **수집마다** 검산한 장부(§3.4).
    ///
    /// ★ 마지막 한 번의 lastGpuSpan 만 보면 씬뷰와 게임뷰 중 어느 쪽을 읽는지가
    ///   실행마다 갈린다 — 한쪽에만 있는 패스(선이 없는 GizmoLine 은 씬뷰에만
    ///   있다)는 그러면 절반의 확률로만 검사를 받는다. 그래서 검산을 수집하는
    ///   자리에 두고 수로 누적한다. 게이트는 마지막 값이 아니라 이 수를 묻는다.
    uint64_t gpuDroppedSlices{ 0 };      // 뒤집힌 조각 — 0 이 아니면 결함
    uint64_t gpuZeroLengthSlices{ 0 };   // 길이 0 인 조각 — 정보이지 판정이 아니다
    uint64_t gpuSpanViolations{ 0 };     // busy > queueSpan — 겹침을 중복으로 셌다
    uint64_t gpuSliceUnderflows{ 0 };    // 조각 < 이름 — 묶은 것이 원본보다 많다

    /// 변환한 GPU 구간이 **제출과 수집 사이**를 벗어난 수집.
    ///
    /// ★ 이것이 통합 축의 검산이다. CPU 가 제출을 열기 전에 GPU 가 돌았다거나,
    ///   수집한 뒤에 끝났다는 것은 있을 수 없다 — 나오면 두 시계가 맞지 않는
    ///   것이고, 그때 타임라인의 GPU 트랙은 그럴듯한 거짓말이 된다.
/// EngineDiagnostics 레인으로 흘려보낸 GPU 조각 수.
    ///
    /// ★ 0 이면 귀속이 죽은 것이다 — 정렬이 맞아도, 수집이 돌아도, sink 를
    ///   아무도 걸지 않았으면 레인은 조용히 빈다.
    uint64_t gpuSpansEmitted{ 0 };

    uint64_t gpuAlignmentViolations{ 0 };
    uint64_t gpuUnalignedCollects{ 0 };  // 표본이 없어 옮기지 못한 수집

    /// 정렬 여유의 최솟값(ms). 둘 다 0 보다 커야 한다.
    double   gpuMinSubmitToBeginMs{ 0.0 };
    double   gpuMinEndToCollectMs{ 0.0 };

    /// 제출→수집 경과의 최댓값. 귀속이 몇 프레임을 건너뛰는지를 말한다.
    double   gpuMaxSubmitToCollectMs{ 0.0 };

    /// 두 시계 표본의 상태.
    EnhancedLiveGpuClock gpuClock{};

    /// 마지막 성공 수집의 귀속. 패스별 숫자가 어느 프레임·어느 제출·어느
    /// 뷰의 것인지를 적는다. 이것이 없으면 숫자가 맞는지 물을 수가 없다.
    uint64_t    lastGpuFrameId{ 0 };
    uint64_t    lastGpuSubmissionId{ 0 };
    uint64_t    lastGpuViewId{ 0 };
    EnhancedLiveGpuSpan lastGpuSpan{};
    std::string lastGpuCollectError;
    size_t   graveyardCount{ 0 };
    std::string lastError;

    /// LivePipelineDesc가 만든 backend-neutral 패스 순서와 슬롯 연결.
    /// 파이프라인 재구축이나 활성 상태 변경 때만 다시 만들고 창에는 복사본을 준다.
    bool pipelineDescriptionValid{ false };
    std::string pipelineDescription;
    std::vector<LivePassNodeSnapshot> pipelineNodes;

    /// 마지막으로 수집에 성공한 프레임의 패스별 GPU 시간. 선언 순서 그대로다.
    std::vector<EnhancedLivePassTiming> passTimings;

    /// 활성 백엔드에서 지금까지 처음 관측한 검증 메시지(Debug 빌드에서만 쌓인다).
    std::vector<std::string> validationMessages;
};

// CreatorEngine의 단독 DX12 씬 렌더러(메인 런타임 facade). RHI self-test·
// benchmark 선언은 RenderTests의 DX12SelfTest.h로 옮겼다(E5 항목 2).
// 기존 SceneRenderer(DX11)는 메인 배선에서 제거된 dead code다.
//
// E5 실측대로 인스턴스 멤버 0의 순수 정적 facade라 class일 이유가 없어
// namespace로 정리했다(E7 잔여 소진, 2026-08-26). 호출 문법과 구현 파일의
// 한정 정의(EnhancedSceneRenderer::Foo)는 그대로다.
enum class EnhancedPbrCaptureState : uint8_t { None, Pending, Recording, Complete, Failed };

struct EnhancedLivePbrCaptureStatus
{
    EnhancedPbrCaptureState state{ EnhancedPbrCaptureState::None };
    uint64_t frameId{};
    std::string directory;
    std::string error;
};

namespace EnhancedSceneRenderer
{
    // Diagnostic only: capture one subsequent product frame on the render thread.
    // The output directory must not exist; incomplete data is never overwritten.
    bool RequestLivePbrCapture(const std::string& directory,
        EnhancedLiveDisplayTarget target, std::string& outError, bool controlled = false);
    EnhancedLivePbrCaptureStatus GetLivePbrCaptureStatus();
    void CancelLivePbrCapture();

    // ── 메인 런타임 렌더러 (PHASE 3-9 승격) ──
    //
    // DX12Test의 Run* 검증들과 달리 이쪽은 상태를 가진다: 켜 두면 매 프레임 활성
    // 씬을 DX12로 그려 공유 텍스처에 담고, 에디터의 씬 뷰·게임 뷰가 그것을
    // ImGui 셸(DX12)에서 공유 핸들로 직결해 표시한다. 인스턴스가 없는 이유:
    // 상시 상태는 프로세스에 하나뿐이어야 한다.
    // 상태는 구현 파일(EnhancedSceneRenderer.cpp) 내부에 숨겼고,
    // 교체(3-9) 때 이 API가 본체 인스턴스로 흡수된다.
    //
    // SceneRenderer(DX11)는 메인 런타임에서 생성하지 않는다. 이 facade가
    // RenderScene·에디터 카메라·프록시 입력·Sky/IBL을 직접 소유하고 유일한
    // 씬 렌더러로 동작한다. DX11은 에디터 ImGui 셸과 기존 Texture 자산을
    // DX12로 올리는 브리지에만 남는다.
    //
    // 스레드·수명 규약: 게임 스레드는 PublishLiveFrame까지만 수행하고,
    // TickLive는 전용 RenderThread가 소비한다. CE 렌더 스레드의 ImGui 빌드는
    // render-owned display snapshot의 논리 대상만 연다.
    // resize 파이프라인 교체와 표시 핸들 조회는 구현의 수명 잠금이 직렬화하고,
    // 이미 ImGui가 연 표시 리소스는 CE join 뒤 ShutdownLive에서 최종 해제한다.

    /// SceneRenderer가 예전에 소유하던 런타임 상태를 만든다.
    bool InitializeRuntime(EnhancedLiveBackend backend, std::string& outError);

    RenderScene* GetRenderScene();
    void SetActiveScene(Scene* scene);

    /// Editor Host가 준비한 gizmo 그림을 render 입력으로 설치한다. Core는
    /// EditorAssetPresentation이나 파일 경로를 모르며, 프레임 packet이 공유
    /// 소유권을 복사해 RenderThread 소비 완료까지 수명을 보장한다.
    void SetGizmoIconTextures(
        std::shared_ptr<const EnhancedGizmoIconTextures> textures);

    /// Host가 파이프라인 조립 기여자를 설치한다(E4-2, §4.4). 파이프라인이
    /// 설 때마다 UI 노드 뒤·live_present 앞에서 Contribute가 불린다.
    /// 조립은 RenderThread에서 일어나므로 Editor는 렌더러 초기화 전에
    /// 설치해야 첫 조립부터 실린다. Player는 설치하지 않는다.
    /// 해제({})는 이후 조립에만 영향을 준다 — 살아 있는 파이프라인의 기여
    /// 노드는 기여자가 아니라 자기 패스 묶음을 붙들므로 안전하다.
    void SetRenderFeatureContributor(
        std::shared_ptr<IRenderFeatureContributor> contributor);

    /// Host가 표시 sink를 설치한다(E4-6a). RT의 리드백 프레임 게시와
    /// GetLiveDisplayImTextureId의 ID 해석이 이 sink를 소비한다 — Core는
    /// ImGui 셸을 직접 부르지 않는다. 미설치면 표시 ID는 0이다.
    /// 렌더러 초기화(렌더 스레드 기동) 전에 설치하고, 렌더 스레드가 멎은
    /// 뒤에 해제({})한다.
    void SetDisplayPresentationSink(
        std::shared_ptr<IDisplayPresentationSink> sink);

    /// equirect HDR를 교체한다. 다음 프레임 시작에서 큐브맵과 IBL을 재생성한다.
    bool SetSkyBoxPath(const std::string& path, std::string& outError);

    /// 켠다. Enhanced-only 런타임에서는 초기화가 이 상태를 유지한다.
    void EnableLive();

    /// InitializeRuntime에서 고정된 scene pass backend를 조회한다. 실행 중
    /// 변경 API는 없다. 다른 backend 검증은 새 프로세스로 기동한다.
    EnhancedLiveBackend GetLiveBackend();

    /// 호환 API. 단독 운용 중에는 끌 수 없다.
    void DisableLive();

    bool IsLiveEnabled();

    /// PIX programmatic capture 종료 직전에 상시 러너가 제출한 GPU 작업을 모두
    /// 완료시킨다. 캡처 외의 정상 프레임 경로에서는 호출하지 않는다.
    void WaitForLiveGpu();

    /// 게임 스레드에서 카메라·기즈모·화면 크기를 불변 프레임 패킷으로
    /// 밀봉한다. 이 함수만 Camera/Scene을 읽으며 TickLive는 읽지 않는다.
    ///
    /// 카메라마다 독립한 표시 슬롯 집합(CameraView)을 굴려 씬뷰·게임뷰가
    /// 동시에 그려진다. 최대 kMaxLiveCameraViews개(초과분은 무시). 순서는
    /// 표시 우선순위가 아니라 제출 순서일 뿐이고, RenderThread가 Editor/Game
    /// 논리 대상으로 결과를 발행한다(MultiCameraRenderPlan.md).
    inline constexpr uint32_t kMaxLiveCameraViews = kEnhancedMaxLiveCameraViews;
    EnhancedRequiredAssetPacket BuildRequiredAssetPacket(
        std::span<const std::shared_ptr<Material>> materials);

    EnhancedLiveFramePacket BuildLiveFramePacket(float deltaSeconds,
        const EnhancedLiveViewRequest* views, uint32_t viewCount,
        bool sceneLoading, const EnhancedRequiredAssetPacket& requiredAssets);

    /// 게임 스레드가 packet과 그 시점까지의 proxy delta를 하나의 제출 단위로
    /// 발행한다. queue가 찼으면 가장 최신 pending frame을 교체하되 lifecycle
    /// delta는 보존하고 같은 대상의 update만 latest-wins로 접는다.
    bool PublishLiveFrame(EnhancedLiveFramePacket frame);

    /// 렌더 소비 상태는 전용 RenderThread만 만진다. 외부 호출은
    /// PublishLiveFrame을 사용한다.
    void TickLive(const EnhancedLiveFramePacket& frame);

    /// 종료 시 새 발행을 닫고 pending submission을 전부 소비한 뒤 join한다.
    /// SceneManager가 RenderScene을 Finalize하기 전에 호출해야 한다.
    void StopLiveRenderThread();
    EnhancedRenderThreadStats GetLiveRenderThreadStats();

    /// RenderThread 수명·프레임 구간 훅(PHASE 14 P2).
    ///
    /// 관측 도구(프로파일러)가 이 스레드의 시작·종료와 프레임 소비 구간을
    /// 알아야 하는데, RenderEngine 이 그 도구를 알면 계층 간선이 하나 늘어난다
    /// (지금 이 모듈이 참조하는 엔진 라이브러리는 Utility_Framework 하나다).
    /// 그래서 thread_pool::worker_hooks 와 같은 역전을 쓴다 — **함수를 받아 두고
    /// 부르기만 한다.**
    ///
    /// ★ StartLiveRenderThread **전에** 걸어야 한다. 스레드는 첫 발행에서
    ///   만들어지고, 그때 이미 걸려 있는 훅만 그 스레드의 시작을 본다.
    ///
    /// OnFrameBegin/OnFrameEnd 는 한 프레임 소비를 감싸며, 짝은 렌더러가
    /// RAII 로 묶어 보장한다(예외로 빠져나가도 닫힌다).
    struct RenderThreadHooks
    {
        void (*OnStart)() = nullptr;
        void (*OnStop)() = nullptr;
        void (*OnFrameBegin)() = nullptr;
        void (*OnFrameEnd)() = nullptr;
    };
    void SetRenderThreadHooks(const RenderThreadHooks& hooks);

    /// 진단/오프라인 검증 전용. 호출 시점까지 발행된 frame packet과 proxy delta를
    /// RenderThread가 모두 소비할 때까지 기다린다. 일반 프레임 경로에서는 호출하지 않는다.
    bool WaitForLiveRenderThreadIdle(uint32_t timeoutMilliseconds);

    /// RenderThread가 마지막으로 발행한 backend 중립 표시 스냅샷을 복사한다.
    /// Camera 객체나 backend 파이프라인을 CE/UI가 다시 조회하지 않는다.
    EnhancedLiveDisplaySnapshot GetLiveDisplaySnapshot();

    /// 스냅샷의 논리 표시 대상을 ImTextureID 호환 값으로 연다. DX12 공유
    /// 핸들과 Vulkan CPU upload key는 구현 안의 불투명 presentation key다.
    /// 셸이 없거나 해당 대상의 첫 GPU 완료 전이면 0.
    uint64_t GetLiveDisplayImTextureId(EnhancedLiveDisplayTarget target);
    EnhancedLiveDisplayTexture GetLiveDisplayTexture(EnhancedLiveDisplayTarget target);

    /// 상태 한 줄 요약(render.backend status / dx12.live 호환 명령).
    std::string GetLiveStatus();

    /// W8/W9 — 세대 밀봉 진단. 사람이 읽는 한 줄이 아니라 **판정에 쓰는 수**다.
    ///
    /// ★ GetLiveStatus의 문장을 게이트가 파싱하게 두면, 문장을 다듬는 순간
    ///   게이트가 조용히 아무것도 재지 않게 된다. 장시간 검사가 폴링할 값은
    ///   따로 낸다.
    struct LiveSealDiagnostics
    {
        uint64_t framesRendered{ 0 };
        uint64_t frameId{ 0 };
        uint64_t encoderDrops{ 0 };
        uint32_t lastDrawCount{ 0 };
        uint32_t lastBatchCount{ 0 };
        uint32_t gbufferStamped{ 0 };
        uint32_t gbufferUnstamped{ 0 };
        uint32_t gbufferViolations{ 0 };
        uint32_t forwardStamped{ 0 };
        uint32_t forwardUnstamped{ 0 };
        uint32_t forwardViolations{ 0 };
        uint32_t textureUploadFailures{ 0 };
        // W9 — 직전 GBuffer 밀봉이 본 모델 generation(재임포트 자극 여부).
        uint32_t modelGenerationPairs{ 0 };
        uint32_t mixedGenerationModels{ 0 };
        uint64_t mixedNewestGeneration{ 0 };
        bool     enabled{ false };
    };
    LiveSealDiagnostics GetLiveSealDiagnostics();

    /// Editor TickLive 표시 경로의 Slice 8-c 회귀 판정. 현재 파이프라인이
    /// 기대 크기로 재구축됐고, 씬뷰·게임뷰가 각각 준비됐으며, 각 뷰가 GPU
    /// 완료 뒤 서로 다른 표시/리드백 슬롯을 둘 이상 승격했는지 확인한다.
    /// 게임 스레드의 프레임 경계에서만 호출한다.
    bool RunLiveDisplayRegression(uint32_t expectedWidth,
        uint32_t expectedHeight, std::string& outLog);

    /// 렌더 디버그 창용 스냅샷. GetLiveStatus가 콘솔 한 줄로 뭉개는 것을
    /// 항목별로 돌려주고, 패스별 GPU 시간과 검증 메시지를 함께 싣는다.
    ///
    /// 이 함수만은 다른 Live API와 스레드 규약이 다르다 — ImGui 그리기는
    /// 게임 스레드가 아니라 CE 렌더 스레드에서 돌기 때문이다(EditorMain의
    /// ExecuteRenderPass → GUIRendering). 표시 경로와 이 디버그 경로 모두
    /// RenderThread가 미리 완성한 값 스냅샷만 복사하며 backend 뷰·벡터·셋을
    /// CE에서 순회하지 않는다.
    ///
    /// 그래서 스냅샷은 게임 스레드가 TickLive에서 미리 완성해 두고, 이
    /// 함수는 락을 잡고 그 완성본을 복사만 한다. 값은 한 프레임 늦을 수
    /// 있지만 디버그 HUD에는 문제되지 않는다.
    EnhancedLiveDebugSnapshot GetLiveDebugSnapshot();

    /// 현재 패스 파라미터. 파이프라인이 아직 없으면 기본값을 돌려준다.
    /// GetLiveDebugSnapshot과 같은 스레드 규약(락으로 복사)이다.
    EnhancedLiveTuning GetLiveTuning();

    /// 패스 파라미터 변경을 요청한다. 실제 SetTuning은 다음 TickLive에서
    /// 게임 스레드가 수행한다 — 창이 패스를 직접 만지지 않는 이유는
    /// EnhancedLiveTuning 주석 참조.
    void SetLiveTuning(const EnhancedLiveTuning& tuning);

    /// 최종 정리. 렌더 스레드 join 이후에만 부른다.
    void ShutdownLive();

} // namespace EnhancedSceneRenderer

