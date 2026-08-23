#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedSceneRenderer.h"
#include "EnhancedSceneRendererLiveDX12Adapter.h"

#include "../Graph/EnhancedRenderGraph.h"
#include "../Graph/EnhancedRenderPass.h"
#include "../Core/EnhancedLivePipelineDesc.h"
#include "../Passes/Geometry/EnhancedGBufferPass.h"
#include "../Passes/Geometry/EnhancedShadowPass.h"
#include "../Passes/Geometry/EnhancedDeferredPass.h"
#include "../Passes/Lighting/EnhancedSSGIPass.h"
#include "../Passes/Geometry/EnhancedForwardPass.h"
#include "../Passes/Geometry/EnhancedSpritePass.h"
#include "../Passes/Lighting/EnhancedSSAOPass.h"
#include "../Passes/Lighting/EnhancedSSRPass.h"
#include "../Passes/Lighting/EnhancedSSSPass.h"
#include "../Passes/Geometry/EnhancedDecalPass.h"
#include "../Passes/Lighting/EnhancedVolumetricFogPass.h"
#include "../Passes/Lighting/EnhancedSkyBoxPass.h"
#include "../../RHI/DX12/EnhancedIBLGenerator.h"
#include "../Passes/PostProcess/EnhancedPostChainPass.h"
#include "../Passes/UI/EnhancedUIPass.h"
#include "../Core/RenderFeatureContributor.h"
#include "../../RHI/IDisplayPresentationSink.h"
#include "../../RHI/Vulkan/VulkanDeviceResources.h"
#include "../../RHI/Vulkan/VulkanCommandBufferPool.h"
#include "../../RHI/Vulkan/VulkanPipelineCache.h"
#include "../../RHI/RHIShaderCompiler.h"
#include "../../RHI/Vulkan/VulkanLoader.h"
#include "../../RHI/ScreenSizedResource.h"
#include "../../RHI/RHISubmissionThread.h"
#include "../../EnhancedGizmoSceneBinding.h"

#include "../../Camera.h"
#include "../../RenderPassData.h"
#include "../../Material.h"
#include "../../RenderScene.h"
#include "../Core/EnhancedLightPacking.h"
#include "../../Texture.h"
#include "../../PrimitiveRenderProxy.h"
#include "../../UIRenderProxy.h"
#include "../../UIClipping.h"
#include "../../Skeleton.h"
#include "../../Mesh.h"
#include "../../RenderState.h"
#include "../../../Utility_Framework/PathFinder.h"

// ★ <d3d11_1.h> include가 여기 있었다 (E, 2026-08-09).
//   공유 텍스처를 DX11에서 열어 SRV를 만들던 자리를 D4에서 걷은 뒤로 이
//   파일에 DX11 타입이 하나도 남지 않았다.
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <array>
#include <cassert>
#include <thread>
#include <deque>
#include <condition_variable>
#include <chrono>
#include <unordered_map>

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

    // 블랙보드 슬롯 이름(LiveSlots)은 EnhancedLivePipelineDesc.h로 갔다 —
    // 파이프라인에 노드를 기여하는 Host도 같은 계약으로 잇기 때문이다(E4-2).

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
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        double lastNativeRecordMs{ 0.0 };

        // ★ SSGI는 여기 없다 — CameraView가 뷰마다 하나씩 든다.
        //   시간축 누적을 하는 유일한 라이브 패스라서 그렇다(아래 CameraView
        //   주석 참조). 나머지 패스는 프레임 안에서 입력→출력이 끝나므로
        //   카메라가 둘이어도 한 인스턴스로 충분하다.
        EnhancedGBufferPass   gbuffer;
        EnhancedShadowPass    shadow;
        EnhancedDecalPass     decal;
        EnhancedDeferredPass  deferred;
        EnhancedForwardPass   forward;
        EnhancedSpritePass    sprite;
        EnhancedSSAOPass      ssao;
        EnhancedSSSPass       sss;
        EnhancedSSRPass       ssr;
        EnhancedSkyBoxPass    skyBox;
        EnhancedIBLGenerator  ibl;
        EnhancedPostChainPass postChain;
        bool                  iblGenerated{ false };
        RHITextureHandle      fogCloudNeutralHandle;
        RHITextureHandle      fogBlueNoiseHandle;

        // 기즈모 체인(에디터 보조 표시)은 여기 없다 — Editor Host가
        // IRenderFeatureContributor로 기여하고 패스 수명도 기여 노드가
        // 소유한다(E4-2). UI는 런타임 게임 UI라 남는다(E4-1 재분류).
        EnhancedUIPass        ui;

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
            RHITextureHandle rhiTexture;
            EnhancedSceneRendererLiveDX12Adapter::DisplayToken interopToken{
                EnhancedSceneRendererLiveDX12Adapter::kInvalidDisplayToken };
            uint64_t fenceValue{ 0 };
            uint64_t frameId{ 0 };
            EnhancedLiveViewKey key{};

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
            EnhancedLiveViewKey key{};
            bool             isEditorView{ false };
            DisplaySlot      slots[kSlotsPerView];
            int              displaySlot{ -1 };   // DX11이 표시 중인 슬롯(-1 = 아직 없음)
            std::vector<int> pendingQueue;        // 제출 순서의 인플라이트 슬롯들
            uint64_t         promotionCount{ 0 }; // GPU 완료 뒤 표시로 승격한 횟수
            uint32_t         promotedSlotMask{ 0 }; // 실제 사용한 슬롯 인덱스 집합

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

    // Vulkan 공용 scene graph 라이브 경로. 에디터 창 자체는 아직 DX12 ImGui
    // 셸이므로 최종 LDR를 비동기 리드백한 뒤 셸에 넘긴다.
    // 그래프와 리드백 슬롯은 timeline completion까지 살아 있어 D3D12 경로의
    // 표시 슬롯과 같은 수명 계약을 지킨다. 이 브리지는 기능 동등성 단계이며,
    // external-memory 직접 공유는 별도 성능 단계다.
    struct VulkanLivePipeline
    {
        static constexpr uint32_t kSlotCount = 3;
        static constexpr uint64_t kDisplayKeyBase = 0x564B4C4956450000ull; // "VKLIVE"

        uint32_t width{ 0 };
        uint32_t height{ 0 };
        uint64_t frameCounter{ 0 };

        VulkanDeviceResources resources;
        VulkanPipelineCache pipelines;
        VulkanMeshCache meshCache;
        VulkanTextureCache textureCache;
        VulkanCommandBufferPool commandPool;

        EnhancedShadowPass shadow;
        EnhancedGBufferPass gbuffer;
        EnhancedDecalPass decal;
        EnhancedSSAOPass ssao;
        EnhancedDeferredPass deferred;
        EnhancedForwardPass forward;
        EnhancedSpritePass sprite;
        EnhancedSSSPass sss;
        EnhancedSSRPass ssr;
        EnhancedSkyBoxPass skyBox;
        EnhancedIBLGenerator ibl;
        EnhancedPostChainPass postChain;
        // 기즈모 체인은 Host 기여 노드가 소유한다(E4-2) — DX12 쪽과 같다.
        EnhancedUIPass ui;
        bool iblGenerated{ false };
        RHITextureHandle fogCloudNeutralHandle;
        RHITextureHandle fogBlueNoiseHandle;
        bool fogInputsReady{ false };
        EnhancedFrameContext frameContext{};
        LivePipelineDesc desc;
        LiveBlackboard blackboard;
        RGTransientPool transientPool;
        EnhancedRenderGraph::Stats lastGraphStats{};
        double lastNativeRecordMs{ 0.0 };
        uint32_t commandPoolFrame{ 0 };

        struct View
        {
            EnhancedLiveViewKey key{};
            bool isEditorView{ false };
            bool ready{ false };

            // temporal GI는 카메라별 히스토리·이전 행렬을 가진다. 공용
            // 인스턴스를 쓰면 여러 씬 뷰가 서로의 화면 공간 GI를 섞는다.
            EnhancedSSGIPass ssgi;
            EnhancedVolumetricFogPass fog;
            bool fogReady{ false };
            uint64_t promotionCount{ 0 };
            uint32_t promotedSlotMask{ 0 };
            uint64_t completedFrameId{ 0 };
        };
        mutable std::mutex viewMutex;
        View views[EnhancedSceneRenderer::kMaxLiveCameraViews];

        struct Slot
        {
            RHIReadback readback{};
            std::shared_ptr<EnhancedRenderGraph> graph;
            uint64_t fenceValue{ 0 };
            uint32_t viewIndex{ 0 };
            EnhancedLiveViewKey key{};
            uint64_t frameId{ 0 };
            bool pending{ false };
        };
        Slot slots[kSlotCount];

        static std::vector<uint8_t> TonemapToRgba8(const RHIReadbackImage& image)
        {
            std::vector<uint8_t> output(static_cast<size_t>(image.width) * image.height * 4u);
            if (RHIFormat::RGBA8Unorm == image.format ||
                RHIFormat::RGBA8UnormSrgb == image.format)
            {
                const size_t tightRow = static_cast<size_t>(image.width) * 4u;
                for (uint32_t y = 0; y < image.height; ++y)
                {
                    memcpy(output.data() + static_cast<size_t>(y) * tightRow,
                        image.data.data() + static_cast<size_t>(y) * image.rowPitch,
                        tightRow);
                }
                return output;
            }
            if (RHIFormat::RGBA16Float != image.format) return output;

            for (uint32_t y = 0; y < image.height; ++y)
            {
                const auto* source = reinterpret_cast<const uint16_t*>(
                    image.data.data() + static_cast<size_t>(y) * image.rowPitch);
                uint8_t* destination = output.data() + static_cast<size_t>(y) * image.width * 4u;
                for (uint32_t x = 0; x < image.width; ++x)
                {
                    for (uint32_t channel = 0; channel < 3; ++channel)
                    {
                        float value = RHIReadbackImage::DecodeHalf(source[x * 4u + channel]);
                        value = (std::max)(0.f, value);
                        // 예전 HDR 직접 리드백 슬롯을 읽을 수 있게 남긴 호환 경로다.
                        // 현재 공용 PostChain은 RGBA8 LDR를 내므로 위에서 끝난다.
                        value = value / (1.f + value);
                        value = std::pow((std::min)(1.f, value), 1.f / 2.2f);
                        destination[x * 4u + channel] = static_cast<uint8_t>(
                            (std::min)(255.f, value * 255.f + 0.5f));
                    }
                    const float alpha = (std::max)(0.f, (std::min)(1.f,
                        RHIReadbackImage::DecodeHalf(source[x * 4u + 3u])));
                    destination[x * 4u + 3u] = static_cast<uint8_t>(alpha * 255.f + 0.5f);
                }
            }
            return output;
        }

        bool Initialize(uint32_t newWidth, uint32_t newHeight,
            FrameCameraSnapshot& camera, std::vector<EnhancedDrawItem>& draws,
            std::vector<EnhancedDrawItem>& forwardDraws,
            std::vector<EnhancedLight>& lights, std::string& outError)
        {
            if (!VulkanApi::LoadLoader(outError)) return false;
            if (!resources.Initialize(newWidth, newHeight,
#if defined(_DEBUG)
                    true,
#else
                    false,
#endif
                    outError)) return false;

            pipelines.Initialize(resources.GetDevice());
            resources.SetPipelineCache(&pipelines);
            if (!meshCache.Initialize(&resources, outError) ||
                !textureCache.Initialize(&resources, outError) ||
                !commandPool.Initialize(resources, 4,
                    VulkanDeviceResources::kFrameCount, outError)) return false;

            width = newWidth;
            height = newHeight;
            frameContext = {};
            frameContext.resources = &resources;
            frameContext.psoManager = &pipelines;
            frameContext.rootSignatures = &pipelines;
            frameContext.meshCache = &meshCache;
            frameContext.textureCache = &textureCache;
            frameContext.width = width;
            frameContext.height = height;
            frameContext.camera = &camera;
            frameContext.draws = &draws;
            frameContext.forwardDraws = &forwardDraws;
            frameContext.lights = &lights;

            // 기여 노드(기즈모 체인)의 같은 규약은 Contribute가
            // RenderFeatureContext.ldrFormat으로 받는다(E4-2).
            ui.SetOutputFormat(EnhancedPostChainPass::kLDRFormat);
            sss.SetEnabled(ReadLivePostFlag("CREATOR_DX12_SSS", false));
            ssr.SetEnabled(ReadLivePostFlag("CREATOR_DX12_SSR", false));

            {
                EnhancedPostChainPass::Tuning tuning = postChain.GetTuning();
                tuning.bloomEnabled = ReadLivePostFlag(
                    "CREATOR_DX12_POST_BLOOM", tuning.bloomEnabled);
                tuning.toneMapEnabled = ReadLivePostFlag(
                    "CREATOR_DX12_POST_TONEMAP", tuning.toneMapEnabled);
                const std::string toneMapper = ReadLivePostEnvironment(
                    "CREATOR_DX12_POST_TONEMAPPER");
                if (toneMapper == "aces" || toneMapper == "0")
                    tuning.toneMapper = EnhancedPostChainPass::ToneMapper::ACES;
                else if (toneMapper == "agx" || toneMapper == "1")
                    tuning.toneMapper = EnhancedPostChainPass::ToneMapper::AgX;
                tuning.exposure = ReadLivePostFloat(
                    "CREATOR_DX12_POST_EXPOSURE", tuning.exposure);
                postChain.SetTuning(tuning);
            }

            RHIShaderCompiler::ScopedOutput spirv(RHIShaderBinary::SpirV);

            if (!desc.InitializeAll(frameContext,
                static_cast<uint32_t>(EnhancedSceneRenderer::kMaxLiveCameraViews),
                outError) || !ibl.Initialize(frameContext, outError)) return false;
            gbuffer.SetKeepAlive(false);
            // SSGI가 AO를 실제로 소비하므로 SSAO를 별도 루트로 살릴 필요가 없다.
            ssao.SetKeepAlive(false);

            for (Slot& slot : slots)
            {
                if (!resources.CreateReadback(width, height,
                    EnhancedPostChainPass::kLDRFormat, 1, slot.readback, outError))
                    return false;
            }
            return true;
        }

        void Shutdown()
        {
            if (resources.IsInitialized())
            {
                std::string lifecycleError;
                if (!resources.DrainForLifecycle(
                        RHILifecycleCommand::BackendShutdown, lifecycleError))
                {
                    OutputDebugStringA(("[Vulkan live] backend shutdown drain 실패: " +
                        lifecycleError + "\n").c_str());
                    std::string abandonError;
                    resources.DrainForLifecycle(
                        RHILifecycleCommand::UnrecoverableDeviceError,
                        abandonError);
                }
            }
            for (Slot& slot : slots)
            {
                slot.graph.reset();
                resources.ReleaseReadback(slot.readback);
                slot.pending = false;
            }
            desc.ShutdownAll(
                static_cast<uint32_t>(EnhancedSceneRenderer::kMaxLiveCameraViews));
            desc.Clear();
            ibl.Shutdown();
            commandPool.Shutdown();
            textureCache.Shutdown();
            meshCache.Shutdown();
            pipelines.Shutdown();
            resources.Shutdown();
        }

        int FindOrAssignView(const EnhancedLiveViewPacket& requested,
            const EnhancedLiveFramePacket& frame)
        {
            std::lock_guard<std::mutex> lock(viewMutex);
            for (uint32_t i = 0; i < EnhancedSceneRenderer::kMaxLiveCameraViews; ++i)
            {
                if (views[i].key == requested.key)
                {
                    views[i].isEditorView = requested.isEditorView;
                    return static_cast<int>(i);
                }
            }
            uint32_t selected = EnhancedSceneRenderer::kMaxLiveCameraViews;
            for (uint32_t i = 0; i < EnhancedSceneRenderer::kMaxLiveCameraViews; ++i)
            {
                if (!views[i].key.IsValid()) { selected = i; break; }
            }
            if (EnhancedSceneRenderer::kMaxLiveCameraViews == selected)
            {
                for (uint32_t i = 0; i < EnhancedSceneRenderer::kMaxLiveCameraViews; ++i)
                {
                    bool stillVisible = false;
                    for (uint32_t j = 0; j < frame.viewCount; ++j)
                    {
                        if (views[i].key == frame.views[j].key)
                        {
                            stillVisible = true;
                            break;
                        }
                    }
                    if (!stillVisible) { selected = i; break; }
                }
            }
            if (EnhancedSceneRenderer::kMaxLiveCameraViews == selected) return -1;
            views[selected].key = requested.key;
            views[selected].isEditorView = requested.isEditorView;
            views[selected].ready = false;
            views[selected].promotionCount = 0;
            views[selected].promotedSlotMask = 0;
            views[selected].completedFrameId = 0;
            views[selected].ssgi.ResetHistory();
            return static_cast<int>(selected);
        }

        uint32_t PendingCount() const
        {
            uint32_t count = 0;
            for (const Slot& slot : slots) if (slot.pending) ++count;
            return count;
        }

        void PromoteCompleted(
            const std::shared_ptr<IDisplayPresentationSink>& presentationSink,
            uint64_t& outPromoted, std::string& outValidation)
        {
            const uint64_t completed = resources.GetCompletedFenceValue();
            textureCache.SweepGraveyard(completed);
            meshCache.SweepGraveyard(completed);
            for (uint32_t slotIndex = 0; slotIndex < kSlotCount; ++slotIndex)
            {
                Slot& slot = slots[slotIndex];
                if (!slot.pending || completed < slot.fenceValue) continue;

                RHIReadbackImage image{};
                std::string readbackError;
                if (resources.MapReadback(slot.readback, image, readbackError) && image.IsValid())
                {
                    bool belongsToView = false;
                    {
                        std::lock_guard<std::mutex> lock(viewMutex);
                        View& view = views[slot.viewIndex];
                        belongsToView = view.key == slot.key;
                        if (belongsToView)
                        {
                            view.ready = true;
                            ++view.promotionCount;
                            view.promotedSlotMask |= (1u << slotIndex);
                            view.completedFrameId = slot.frameId;
                        }
                    }
                    if (belongsToView)
                    {
                        std::vector<uint8_t> rgba = TonemapToRgba8(image);
                        // Host가 설치한 표시 sink로 게시한다(E4-6a). 미설치면
                        // 프레임은 버려진다 — 표시할 곳이 없는 상태다.
                        if (presentationSink)
                        {
                            presentationSink->SubmitCpuFrame(
                                kDisplayKeyBase + slot.viewIndex + 1u,
                                image.width, image.height, rgba.data(),
                                image.width * 4u);
                        }
                        ++outPromoted;
                    }
                }
                else if (!readbackError.empty())
                {
                    outValidation += "Vulkan 라이브 리드백 실패: " + readbackError + "\n";
                }
                slot.graph.reset();
                slot.pending = false;
            }

#if defined(_DEBUG)
            std::string validation;
            if (0 != resources.DrainDebugMessages(validation) && !validation.empty())
                outValidation += validation;
#endif
        }

        bool Render(uint32_t viewIndex, const EnhancedLiveViewPacket& viewPacket,
            uint64_t sourceFrameId, uint64_t backendGeneration,
            const std::function<bool(std::string&)>& prepareFrame,
            std::string& outError)
        {
            Slot* slot = nullptr;
            for (Slot& candidate : slots)
            {
                if (!candidate.pending) { slot = &candidate; break; }
            }
            if (nullptr == slot) { outError = "Vulkan 라이브 리드백 슬롯이 모두 사용 중"; return false; }

            if (!resources.BeginFrame(outError)) return false;
            bool committed = false;
            struct FrameGuard
            {
                VulkanDeviceResources& resources;
                const bool& committed;
                ~FrameGuard() { if (!committed) resources.AbortFrame(); }
            } frameGuard{ resources, committed };

            const uint32_t frameIndex = static_cast<uint32_t>(frameCounter++);
            commandPool.BeginFrame(commandPoolFrame);
            textureCache.BeginFrame(frameIndex);
            meshCache.BeginFrame(frameIndex);
            const RHIDeviceMemoryPressureInfo pressureInfo = resources
                .GetPersistentMemoryBudgetCoordinator().GetMemoryPressureInfo();
            RHIAssetEvictionPass evictionPass = BeginRHIAssetEvictionPass(
                pressureInfo.memoryPressure, pressureInfo.targetReleaseBytes);
            textureCache.RetireUnused(resources.GetLastSignaledFenceValue(),
                &evictionPass);
            meshCache.RetireUnused(resources.GetLastSignaledFenceValue(),
                &evictionPass);
            if (!prepareFrame(outError)) return false;

            slot->graph = std::make_shared<EnhancedRenderGraph>(
                static_cast<IRenderDeviceServices&>(resources));
            EnhancedRenderGraph& graph = *slot->graph;
            graph.SetTransientPool(&transientPool);
            blackboard.Reset();

            LiveFrameBinding binding{};
            binding.viewIndex = viewIndex;
            binding.readbackTarget = slot->readback;
            binding.viewFlags = viewPacket.isEditorView
                ? LiveViewFlags::kSceneOverlay : LiveViewFlags::kScreenSpaceUI;
            desc.DeclareAll(blackboard, graph, frameContext, binding);

            if (!blackboard.Get(LiveSlots::kDisplayLdr).IsValid())
            {
                outError = "Vulkan 라이브 공통 scene graph의 표시 출력이 없다";
                return false;
            }

            RHIRecordedBatchDesc batchDesc{};
            batchDesc.frameId = sourceFrameId;
            batchDesc.backendGeneration = backendGeneration;
            batchDesc.displayToken = kDisplayKeyBase + viewIndex + 1u;
            batchDesc.lifetimeToken = slot->graph;
            RHIRecordedBatch batch;
            RHISubmissionTicket batchTicket;
            if (!graph.Compile(outError)) return false;
            LiveStopwatch recordWatch;
            recordWatch.Start();
            if (!graph.RecordParallel(commandPool, 4, batchDesc, batch, outError))
                return false;
            lastNativeRecordMs = recordWatch.ElapsedMs();
            if (!GetRHISubmissionThread().EnqueueRecordedBatch(&resources,
                    resources, std::move(batch), batchTicket, outError)) return false;
            lastGraphStats = graph.GetStats();
            if (!resources.EndFrame(outError)) return false;
            committed = true;
            commandPoolFrame = (commandPoolFrame + 1u) % VulkanDeviceResources::kFrameCount;

            slot->fenceValue = resources.GetLastSignaledFenceValue();
            slot->viewIndex = viewIndex;
            slot->key = viewPacket.key;
            slot->frameId = sourceFrameId;
            slot->pending = true;
            return true;
        }
    };

    struct LiveState
    {
        std::atomic_bool enabled{ false };
        bool runtimeInitialized{ false };
        EnhancedLiveBackend backend{ EnhancedLiveBackend::DX12 };
        EnhancedSceneRendererLiveDX12Adapter dx12;

        // SceneRenderer(DX11)에서 이관한 메인 런타임 소유권. 에디터 카메라는
        // 여기 없다(E4-5) — Editor 세션이 소유하고 뷰 요청으로 넘어온다.
        std::shared_ptr<RenderScene> renderScene;

        // Editor Host가 주입하는 presentation 입력. GT가 packet마다 shared_ptr을
        // snapshot하고 packet이 RT까지 수명을 운반한다.
        mutable std::mutex gizmoIconMutex;
        std::shared_ptr<const EnhancedGizmoIconTextures> gizmoIconTextures;

        // Host가 주입하는 파이프라인 기여자(E4-2). 조립은 RenderThread에서
        // 일어나므로 설치·해제와 mutex로 격리한다. 기여 노드는 기여자가 아니라
        // 자기 패스 묶음을 붙들므로, 해제 뒤에도 살아 있는 파이프라인은 안전하다.
        mutable std::mutex featureContributorMutex;
        std::shared_ptr<IRenderFeatureContributor> featureContributor;

        // Host가 주입하는 표시 sink(E4-6a). RT의 CPU 프레임 push와 CE의
        // 표시 ID 해석이 소비하므로 mutex 아래 shared_ptr 복사로 격리한다.
        // 미설치면 표시 ID는 0이다 — Core는 ImGui 셸을 모른다.
        mutable std::mutex presentationSinkMutex;
        std::shared_ptr<IDisplayPresentationSink> presentationSink;

        std::shared_ptr<IDisplayPresentationSink> CopyPresentationSink() const
        {
            std::lock_guard<std::mutex> lock(presentationSinkMutex);
            return presentationSink;
        }

        // 3-2E: GT는 immutable packet과 delta batch를 발행하고, 전용 RT만
        // TickLive 및 아래 render-owned 상태를 소비한다. CE는 완료 display만
        // displayLifetimeMutex 경계에서 조회한다.
        std::thread::id frameProducerThread{};
        std::thread::id frameConsumerThread{};
        std::atomic_ullong publishedFrameId{ 0 };
        std::atomic_ullong consumedFrameId{ 0 };
        std::atomic_ullong sceneEpoch{ 1 };
        std::atomic_ullong resizeGeneration{ 0 };
        uint32_t publishedWidth{ 0 };
        uint32_t publishedHeight{ 0 };
        float    publishedTotalSeconds{ 0.f };

        struct FrameSubmission
        {
            EnhancedLiveFramePacket frame;
            ProxyCommandQueueController::Batch deltas;
        };

        static constexpr uint32_t kRenderQueueCapacity = 2;
        static constexpr size_t kMaxDeltasPerSubmission = 65536;
        mutable std::mutex renderQueueMutex;
        std::condition_variable renderQueueWake;
        std::deque<FrameSubmission> renderQueue;
        std::thread renderThread;
        bool renderThreadStarted{ false };
        bool renderThreadStartFailed{ false };
        bool renderThreadRunning{ false };
        bool renderThreadAccepting{ false };
        bool renderThreadStopRequested{ false };
        uint32_t renderThreadTestDelayMs{ 0 };
        uint32_t renderQueueHighWatermark{ 0 };
        uint32_t renderInProgress{ 0 };
        uint64_t renderPublished{ 0 };
        uint64_t renderConsumed{ 0 };
        uint64_t renderOverflowEvents{ 0 };
        uint64_t renderCoalescedFrames{ 0 };
        uint64_t renderCoalescedDeltas{ 0 };
        uint64_t renderBackPressureWaits{ 0 };
        uint64_t renderShutdownDrains{ 0 };
        uint64_t renderShutdownDiscardedDeltas{ 0 };

        // status/검증/PIX wait가 RT의 pipeline 포인터와 통계를 직접 읽을 때만
        // 잡는다. 일반 CE display 조회는 더 좁은 displayLifetimeMutex를 쓴다.
        mutable std::mutex renderStateMutex;
        ProxyCommandQueueController::Batch activeDeltaBatch; // RT 전용

        bool StartRenderThread(std::string& outError);
        bool PublishFrame(FrameSubmission submission);
        void StopRenderThread();
        EnhancedRenderThreadStats GetRenderThreadStats() const;

        // 원본 HDR는 기존 자산 로더가 만든 DX11 Texture지만 소유자는 이쪽이다.
        // DX12TextureCache가 프레임 시작에 같은 어댑터의 리소스로 올린 뒤 IBL
        // 생성기가 큐브맵·조도·프리필터·BRDF LUT를 만든다.
        std::string                 skyBoxPath;
        std::unique_ptr<Texture> skyEquirect;
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
        // ★ 끝 상태를 ALL_SHADER_RESOURCE로 맞춘다. RHIResourceState에는
        //   PIXEL 전용 값이 없어 ShaderResource가 곧 ALL인데, textureCache는
        //   업로드를 PIXEL로 끝내므로 그대로 임포트하면 배리어의 before가
        //   실제와 어긋난다(검증 레이어가 잡는다).
        std::unique_ptr<Texture> fogBlueNoise;

        // ★ 핸들을 옆에 든다(V3). 예전에는 프레임마다 ImportTexture 의 포인터
        //   오버로드를 타서 표에 등록하고 그래프가 죽을 때 놓기를 반복했다 —
        //   그림은 같고 비용만 드는 왕복이다. 한 번 등록해 두면 프레임마다
        //   핸들만 넘긴다.
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
        uint64_t                    fogRetireFence{ 0 };

        // 렌더 backend가 소유하는 카메라별 뷰. CE/UI는 이 파이프라인을 직접
        // 순회하지 않고 아래 display snapshot의 Editor/Game 대상만 소비한다.
        std::unique_ptr<LivePipeline> pipeline;
        std::unique_ptr<VulkanLivePipeline> vulkanPipeline;

        // RenderThread는 완료된 슬롯을 아래 값 스냅샷으로 승격하고, CE는 그
        // 스냅샷과 불투명 presentation key만 읽는다. resize 해체와 DX12 공유
        // 핸들 open의 수명도 같은 락으로 직렬화한다. 실제 기록/제출에는 잡지 않는다.
        mutable std::mutex displayLifetimeMutex;
        EnhancedLiveDisplaySnapshot displaySnapshot{};
        std::array<uint64_t, kEnhancedLiveDisplayTargetCount>
            displayPresentationKeys{};

        static uint32_t DisplayTargetIndex(bool isEditorView)
        {
            return static_cast<uint32_t>(isEditorView
                ? EnhancedLiveDisplayTarget::Editor
                : EnhancedLiveDisplayTarget::Game);
        }

        void BeginDisplaySnapshot(const EnhancedLiveFramePacket& frame)
        {
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            displaySnapshot.backend = backend;
            displaySnapshot.sourceFrameId = frame.frameId;
            displaySnapshot.resizeGeneration = frame.resizeGeneration;
            displaySnapshot.width = frame.width;
            displaySnapshot.height = frame.height;

            std::array<bool, kEnhancedLiveDisplayTargetCount> active{};
            const uint32_t viewCount = (std::min)(frame.viewCount,
                EnhancedSceneRenderer::kMaxLiveCameraViews);
            for (uint32_t i = 0; i < viewCount; ++i)
            {
                const EnhancedLiveViewPacket& view = frame.views[i];
                const uint32_t targetIndex = DisplayTargetIndex(view.isEditorView);
                active[targetIndex] = true;
                EnhancedLiveDisplayEntrySnapshot& entry =
                    displaySnapshot.targets[targetIndex];
                if (entry.key != view.key)
                {
                    entry = {};
                    displayPresentationKeys[targetIndex] = 0;
                }
                entry.key = view.key;
                entry.active = true;
            }
            for (uint32_t i = 0; i < kEnhancedLiveDisplayTargetCount; ++i)
            {
                if (active[i]) continue;
                displaySnapshot.targets[i] = {};
                displayPresentationKeys[i] = 0;
            }
            ++displaySnapshot.revision;
        }

        void InvalidateDisplayResultsLocked()
        {
            for (uint32_t i = 0; i < kEnhancedLiveDisplayTargetCount; ++i)
            {
                EnhancedLiveDisplayEntrySnapshot& entry = displaySnapshot.targets[i];
                entry.ready = false;
                entry.completedFrameId = 0;
                entry.promotionCount = 0;
                entry.promotedSlotMask = 0;
                displayPresentationKeys[i] = 0;
            }
            ++displaySnapshot.revision;
        }

        void ResetDisplaySnapshot()
        {
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            const uint64_t nextRevision = displaySnapshot.revision + 1u;
            displaySnapshot = {};
            displaySnapshot.backend = backend;
            displaySnapshot.revision = nextRevision;
            displayPresentationKeys.fill(0);
        }

        void PublishDisplayResultLocked(bool isEditorView,
            const EnhancedLiveViewKey& key, uint64_t presentationKey,
            uint64_t completedFrameId, uint64_t promotionCount,
            uint32_t promotedSlotMask)
        {
            const uint32_t targetIndex = DisplayTargetIndex(isEditorView);
            EnhancedLiveDisplayEntrySnapshot& entry =
                displaySnapshot.targets[targetIndex];
            if (!entry.active || entry.key != key) return;
            entry.ready = 0 != presentationKey;
            entry.completedFrameId = completedFrameId;
            entry.promotionCount = promotionCount;
            entry.promotedSlotMask = promotedSlotMask;
            displayPresentationKeys[targetIndex] = presentationKey;
            ++displaySnapshot.revision;
        }

        void PublishVulkanDisplayResults()
        {
            if (!vulkanPipeline) return;
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            std::lock_guard<std::mutex> viewLock(vulkanPipeline->viewMutex);
            for (uint32_t i = 0; i < EnhancedSceneRenderer::kMaxLiveCameraViews; ++i)
            {
                const VulkanLivePipeline::View& view = vulkanPipeline->views[i];
                if (!view.ready || !view.key.IsValid()) continue;
                PublishDisplayResultLocked(view.isEditorView, view.key,
                    VulkanLivePipeline::kDisplayKeyBase + i + 1u,
                    view.completedFrameId, view.promotionCount,
                    view.promotedSlotMask);
            }
        }

        // 프레임 입력. frameContext가 이들의 주소를 들므로 파이프라인과 무관한
        // 여기 멤버로 둔다 — 주소가 흔들리면 패스가 든 포인터가 전부 무효가 된다.
        FrameCameraSnapshot           cameraSnapshot{};

        // 카메라와 무관한 수집 결과(BuildDrawPool). 뷰는 여기서 골라
        // draws/forwardDraws로 옮긴다 — 재질·본 팔레트 해석을 뷰마다
        // 반복하지 않기 위해서다.
        struct PooledDraw
        {
            EnhancedDrawItem     item{};
            DirectX::BoundingBox worldBounds{};
            bool                 hasBounds{ false };
            bool                 isTransparent{ false };
        };
        std::vector<PooledDraw>       drawPool;

        struct PooledSprite
        {
            Mathf::xMatrix worldMatrix{ XMMatrixIdentity() };
            std::shared_ptr<Texture> texture;
            BillboardType billboardType{ BillboardType::None };
            Mathf::Vector3 billboardAxis{ 0.f, 1.f, 0.f };
            int orderInLayer{ 0 };
            bool enableDepth{ false };
        };
        std::vector<PooledSprite> spritePool;
        RenderScene::UIProxySnapshot uiProxySnapshot;
        std::vector<UIRenderProxy*> uiProxyPointers;

        // 뷰마다 다시 만드는 최종 2D/3D 스프라이트 목록.
        std::vector<EnhancedSpritePass::Item> worldSprites;
        std::vector<EnhancedUIPass::Rect> uiRects;
        uint32_t                      lastPoolDraws{ 0 };
        uint32_t                      lastCulledDraws{ 0 };

        std::vector<EnhancedDrawItem> draws;
        std::vector<EnhancedDrawItem> forwardDraws;
        std::vector<EnhancedLight>    lights;
        // 마지막으로 민 뷰의 광원 선별 근거. status가 "씬에 몇 개인데 뷰가
        // 몇 개를 봤고 패스 한도에 몇 개가 걸리는지"를 답하는 데 쓴다 —
        // 목록만 보면 잘린 사실이 안 보인다.
        ViewLightSelection            lastLightSelection{};
        // 데칼은 frameContext가 아니라 패스에 직접 건넨다(SetDecals) —
        // 그리는 패스가 하나뿐이라 컨텍스트에 실을 이유가 없다. 여기 두는
        // 것은 매 프레임 재할당을 피하기 위해서다.
        std::vector<EnhancedDecalPass::Item> decals;
        EnhancedGizmoSceneData        gizmoData;   // 아이콘 벡터를 프레임 동안 소유

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
        uint64_t viewOverflowSkips{ 0 }; // 뷰 상한(kMaxLiveCameraViews) 초과로 건너뛴 수
        uint64_t frameFailures{ 0 };     // 프레임 기록 실패 누적(일시적인 것 포함)
        bool     vulkanFirstFrameReported{ false };
        // 연속 실패 수. 일시적 실패는 건너뛰고 다음 프레임에 다시 해 보되,
        // 계속 실패하면 그때는 접는다 — 매 프레임 같은 실패를 무한히 반복하는
        // 것도 "조용히 안 되는" 상태라 낫지 않다.
        uint32_t consecutiveFrameFailures{ 0 };
        static constexpr uint32_t kMaxConsecutiveFrameFailures = 60;
        // 검증 레이어 메시지. Debug에서만 쌓이고, 같은 문장이 매 프레임
        // 반복되므로 처음 본 것만 찍는다 — 안 그러면 콘솔이 도배돼 정작
        // 첫 원인을 못 본다.
        std::unordered_set<std::string> reportedValidation;

        uint32_t lastDrawCount{ 0 };    // 이번 프레임 GBuffer 드로우(0이면 빈 화면이다)
        uint32_t lastBatchCount{ 0 };
        uint32_t lastDecalCount{ 0 };
        uint32_t lastDecalBatchCount{ 0 };
        uint32_t lastSpriteCount{ 0 };
        uint32_t lastSpriteBatchCount{ 0 };
        uint32_t lastUIRectCount{ 0 };
        uint32_t lastUIBatchCount{ 0 };
        std::array<uint32_t, kEnhancedLiveDisplayTargetCount> viewSpriteCounts{};
        std::array<uint32_t, kEnhancedLiveDisplayTargetCount> viewUICounts{};
        double   lastGpuMs{ 0.0 };
        double   lastCpuMs{ 0.0 };
        double   lastNativeRecordMs{ 0.0 };
        double   totalNativeRecordMs{ 0.0 };
        double   maxNativeRecordMs{ 0.0 };
        uint64_t nativeRecordSamples{ 0 };
        std::string lastError;

        void AddNativeRecordSample(double milliseconds)
        {
            lastNativeRecordMs = milliseconds;
            totalNativeRecordMs += milliseconds;
            maxNativeRecordMs = (std::max)(maxNativeRecordMs, milliseconds);
            ++nativeRecordSamples;
        }

        double AverageNativeRecordMs() const
        {
            return 0 != nativeRecordSamples
                ? totalNativeRecordMs / static_cast<double>(nativeRecordSamples)
                : 0.0;
        }

        // 마지막으로 수집에 성공한 프레임의 패스별 GPU 시간. 수집은 매
        // 프레임 되지 않으므로(리드백이 준비된 프레임에만) 마지막 성공분을
        // 유지한다 — 비우면 창이 깜빡인다.
        std::vector<EnhancedLivePassTiming> lastPassTimings;

        // 렌더 디버그 창에 건네는 완성본. 창은 CE 렌더 스레드에서 그려지고
        // 위의 상태는 전부 RenderThread가 고치므로, 창이 상태를 직접 순회하면
        // 재할당 중인 벡터·셋을 읽는다. RenderThread가 여기까지 완성해 두고
        // 창은 락을 잡아 복사만 한다.
        std::mutex                debugMutex;
        EnhancedLiveDebugSnapshot debugSnapshot;

        // LivePipelineDesc 덤프는 구조가 다시 서거나 active 조건이 바뀔 때만
        // 만든다. 프레임마다 문자열을 조립하지 않고 기존 debug snapshot에
        // 실어 CE 렌더 스레드가 파이프라인 상태를 직접 잠그지 않게 한다.
        bool                      pipelineDescriptionValid{ false };
        std::string               pipelineDescription;

        // debugMutex를 잡은 상태에서만 호출한다.
        bool RefreshPipelineDescriptionLocked(const LivePipelineDesc& desc,
            std::string& outError)
        {
            outError.clear();
            pipelineDescriptionValid = desc.Validate(outError);
            pipelineDescription = desc.Dump();
            if (!pipelineDescriptionValid)
            {
                pipelineDescription += "Validate failed: " + outError + "\n";
            }
            return pipelineDescriptionValid;
        }

        // 파이프라인 설정 창과 주고받는 패스 파라미터. debugSnapshot과 같은
        // 뮤텍스로 보호한다 — 둘 다 같은 창이 같은 프레임에 만지므로 락을
        // 나눌 이유가 없고, 나누면 순서 규칙만 하나 더 생긴다.
        EnhancedLiveTuning        tuningMirror;     // RenderThread가 채우는 현재값
        EnhancedLiveTuning        pendingTuning;    // 창이 넣는 변경 요청
        bool                      hasPendingTuning{ false };

        /// RenderThread에서만 부른다. 창이 넣어 둔 변경을 실제 패스에 적용하고,
        /// 적용 후의 값을 미러에 되싣는다.
        void ApplyAndPublishTuning();

        /// RenderThread에서만 부른다. 위 상태를 debugSnapshot으로 옮긴다.
        void PublishDebugSnapshot()
        {
            std::lock_guard<std::mutex> lock(debugMutex);

            debugSnapshot.enabled = enabled;
            debugSnapshot.pipelineReady = (EnhancedLiveBackend::DX12 == backend)
                ? (nullptr != pipeline) : (nullptr != vulkanPipeline);
            debugSnapshot.width = (EnhancedLiveBackend::DX12 == backend)
                ? (pipeline ? pipeline->width : 0u)
                : (vulkanPipeline ? vulkanPipeline->width : 0u);
            debugSnapshot.height = (EnhancedLiveBackend::DX12 == backend)
                ? (pipeline ? pipeline->height : 0u)
                : (vulkanPipeline ? vulkanPipeline->height : 0u);
            debugSnapshot.framesRendered = framesRendered;
            debugSnapshot.framesIdle = framesIdle;
            debugSnapshot.framesInFlight = framesInFlight;
            debugSnapshot.publishedFrameId = publishedFrameId;
            debugSnapshot.consumedFrameId = consumedFrameId;
            debugSnapshot.sceneEpoch = sceneEpoch;
            debugSnapshot.resizeGeneration = resizeGeneration;
            debugSnapshot.drawCount = lastDrawCount;
            debugSnapshot.batchCount = lastBatchCount;
            debugSnapshot.decalCount = lastDecalCount;
            debugSnapshot.decalBatchCount = lastDecalBatchCount;
            debugSnapshot.spriteCount = lastSpriteCount;
            debugSnapshot.spriteBatchCount = lastSpriteBatchCount;
            debugSnapshot.uiRectCount = lastUIRectCount;
            debugSnapshot.uiBatchCount = lastUIBatchCount;
            debugSnapshot.cpuMs = lastCpuMs;
            debugSnapshot.gpuMs = lastGpuMs;
            debugSnapshot.graveyardCount = static_cast<uint32_t>(
                dx12.GetRetiredDisplayCount()) + dx12.GetAssetGraveyardCount();
            debugSnapshot.lastError = lastError;
            debugSnapshot.pipelineDescriptionValid = pipelineDescriptionValid;
            if (debugSnapshot.pipelineDescription != pipelineDescription)
            {
                debugSnapshot.pipelineDescription = pipelineDescription;
            }

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
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            InvalidateDisplayResultsLocked();
            pipeline = std::make_unique<LivePipeline>();
            LivePipeline& p = *pipeline;

            // ★ 어댑터를 DX11에 맞추던 것을 걷었다 (D4, 2026-08-08).
            //
            //   공유 텍스처가 같은 물리 어댑터를 요구하는 것은 그대로다. 바뀐
            //   것은 그 상대다 — 예전에는 DX11도 이 텍스처를 열어 SRV로 뷰에
            //   그렸고(셸이 없던 시절의 표시 경로), 그래서 DX11 어댑터가
            //   기준이었다. 그 폴백을 아래에서 함께 걷었으므로 지금 공유의
            //   상대는 ImGui 셸(DX12) 하나다.
            //
            //   LUID를 주지 않으면 DX12DeviceResources가 고성능 어댑터 0번을
            //   고른다. 그 선택이 결정적이라 라이브와 셸이 같은 것을 고르고,
            //   공유의 전제는 그대로 성립한다.
            if (!dx12.IsInitialized())
            {
                if (!dx12.Initialize(newWidth, newHeight, outError)) return false;
            }
            else if (!dx12.Resize(newWidth, newHeight, outError))
            {
                return false;
            }

            p.width = newWidth;
            p.height = newHeight;

            p.frameContext = {};
            p.frameContext.resources = &dx12.Resources();
            p.frameContext.psoManager = &dx12.Pipelines();
            p.frameContext.rootSignatures = &dx12.RootSignatures();
            p.frameContext.meshCache = &dx12.MeshCache();
            p.frameContext.textureCache = &dx12.TextureCache();
            p.frameContext.width = p.width;
            p.frameContext.height = p.height;
            p.frameContext.camera = &cameraSnapshot;
            p.frameContext.draws = &draws;
            p.frameContext.forwardDraws = &forwardDraws;
            p.frameContext.lights = &lights;

            // 패스 구성과 순서는 dx12.scene(RunSceneBindingTest)과 같다 — 그
            // 검증이 이 배선의 회귀 감시자다. Sprite는 Forward+ 뒤 HDR에,
            // 화면 UI는 PostChain 뒤 LDR에 합성한다.
            // ── 패스 초기화는 노드 목록이 정한다(슬라이스 2) ──
            //
            // 예전에는 여기에 Initialize 열여섯 줄이 순서대로 적혀 있었고,
            // 그 순서는 Declare·PrepareFrame·Shutdown 역순에 각각 따로 적힌
            // 같은 사실의 사본이었다. 이제 순서를 아는 곳은 노드 목록뿐이다.
            //
            // 노드보다 먼저 해야 하는 것만 여기 남는다 — 패스를 세우기 전에
            // 정해져야 하는 값들이다.

            // 화면 UI는 포스트 체인의 LDR 결과 위에 직접 그린다 — 입력
            // 텍스처에 그리는 구조라 PSO의 RTV 포맷을 거기 맞춰야 한다
            // (어긋나면 커맨드 리스트 무효 → 디바이스 제거, 실측으로 겪었다).
            // 기여 노드(기즈모 체인)의 같은 규약은 Contribute가
            // RenderFeatureContext.ldrFormat으로 받는다(E4-2).
            p.ui.SetOutputFormat(EnhancedPostChainPass::kLDRFormat);

            // 조립 기술을 먼저 짜고, 그 목록으로 초기화한다. 슬라이스 1에서는
            // 패스를 다 세운 뒤에 짰는데 순서가 뒤집혔다 — 노드가 패스를
            // 참조로만 잡으므로(초기화 여부와 무관) 이 순서가 성립하고,
            // 그래야 초기화 순서까지 목록이 정할 수 있다.
            if (!BuildPipelineDesc(p, true, outError)) return false;

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

            // ── 공유 텍스처 (뷰마다 슬롯 셋) ──
            //
            // 포스트 체인의 LDR 출력과 같은 RGBA8이라야 CopyTextureRegion이
            // 성립한다. 뷰 2 × 슬롯 3 = 6장 — 1920x1080 기준 뷰당 약 25MB.
            for (LivePipeline::CameraView& view : p.views)
            for (LivePipeline::DisplaySlot& slot : view.slots)
            {
                // 공유 리소스의 RHI 정체성은 슬롯 수명과 같다. 프레임 그래프가
                // raw 리소스를 매번 다시 등록하면 같은 물리 텍스처가 프레임마다
                // 새 표 칸을 차지하므로, 생성 직후 한 번만 등록해 계속 건넨다.
                if (!dx12.CreateDisplayTexture(p.width, p.height,
                    slot.rhiTexture, slot.interopToken, outError)) return false;
            }

            return true;
        }

        bool BuildVulkanPipeline(uint32_t newWidth, uint32_t newHeight,
            std::string& outError)
        {
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            InvalidateDisplayResultsLocked();
            vulkanPipeline = std::make_unique<VulkanLivePipeline>();
            if (!BuildPipelineDesc(*vulkanPipeline, true, outError))
            {
                vulkanPipeline.reset();
                return false;
            }
            if (!vulkanPipeline->Initialize(newWidth, newHeight, cameraSnapshot,
                draws, forwardDraws, lights, outError))
            {
                vulkanPipeline->Shutdown();
                vulkanPipeline.reset();
                return false;
            }
            return true;
        }

        void TeardownVulkanPipeline()
        {
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            if (!vulkanPipeline) return;
            InvalidateDisplayResultsLocked();
            vulkanPipeline->Shutdown();
            vulkanPipeline.reset();
        }

        /// 파이프라인 해체. DX11에 보이는 것은 묘지로 보낸다(수명 규약).
        void TeardownPipeline(bool preserveBackend = false,
            bool gpuAlreadyDrained = false)
        {
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            if (nullptr == pipeline) return;
            InvalidateDisplayResultsLocked();
            LivePipeline& p = *pipeline;

            if (!gpuAlreadyDrained)
            {
                std::string lifecycleError;
                const RHILifecycleCommand command = preserveBackend
                    ? RHILifecycleCommand::SwapChainResize
                    : RHILifecycleCommand::BackendShutdown;
                if (!dx12.DrainForLifecycle(command, lifecycleError))
                {
                    lastError = "DX12 lifecycle drain 실패: " + lifecycleError;
                    std::string abandonError;
                    dx12.DrainForLifecycle(
                        RHILifecycleCommand::UnrecoverableDeviceError,
                        abandonError);
                }
            }
            for (LivePipeline::CameraView& view : p.views)
            {
                for (LivePipeline::DisplaySlot& slot : view.slots) slot.graph.reset();
                view.pendingQueue.clear();
                view.displaySlot = -1;
                view.key = {};
                view.isEditorView = false;
                view.promotionCount = 0;
                view.promotedSlotMask = 0;

                for (LivePipeline::DisplaySlot& slot : view.slots)
                {
                    // WaitForGpu 뒤다. 그래프/인코더가 이 핸들을 더 이상 풀지
                    // 않으므로 이제 백엔드 표의 등록을 안전하게 해제할 수 있다.
                    if (slot.rhiTexture.IsValid())
                    {
                        dx12.Resources().ReleaseTexture(slot.rhiTexture);
                        slot.rhiTexture = {};
                    }
                    dx12.RetireDisplayTexture(slot.interopToken);
                    slot.interopToken =
                        EnhancedSceneRendererLiveDX12Adapter::kInvalidDisplayToken;
                    slot.key = {};
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
            if (p.fogCloudNeutralHandle.IsValid())
            {
                dx12.ReleaseFogCloudNeutral(p.fogCloudNeutralHandle);
            }
            p.fogBlueNoiseHandle = {};
            fogInputsReady = false;
            fogNoiseStateWidened = false;   // 새 캐시에는 다시 넓혀야 한다
            fogTeardownPending = false;
            fogRetireFence = 0;

            // 그래프들이 GPU 완료 뒤 반납한 transient 핸들은 파이프라인 소유다.
            // backend를 유지하는 resize에서는 리소스 표도 유지되므로 여기서
            // 명시적으로 해제해야 옛 해상도 풀이 영구 상주하지 않는다.
            for (auto& [key, entries] : p.transientPool.freeList)
            {
                (void)key;
                for (const RGTransientPool::Entry& entry : entries)
                {
                    if (entry.handle.IsValid())
                    {
                        dx12.Resources().ReleaseTexture(entry.handle);
                    }
                }
            }
            p.transientPool.freeList.clear();

            if (!preserveBackend) dx12.ShutdownPipeline();

            pipeline.reset();
        }

        /// 포그가 처음 켜질 때 부른다. 프레임이 열려 있어야 한다(업로드 링과
        /// 커맨드 리스트를 쓴다 — textureCache::GetOrUpload와 같은 계약).
        bool EnsureFogInputs(LivePipeline& p, std::string& outError)
        {
            if (fogInputsReady) return true;

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

            const RHITextureEntry noiseEntry =
                dx12.TextureCache().GetOrUpload(fogBlueNoise.get(), outError);
            if (!noiseEntry.IsValid())
            {
                outError = "블루 노이즈 운반 실패" +
                    (outError.empty() ? std::string{} : ": " + outError);
                return false;
            }
            p.fogBlueNoiseHandle = noiseEntry.handle;

            // ── 중립 구름 그림자 — 1x1 흰색 ──
            //
            // 흰색이어야 하는 이유가 있다. 셰이더가 구름의 켬/끔을 보지 않고
            // 알파를 그냥 곱하므로(EnhancedVolumetricFogPass.h의 발견 ②),
            // 검정이나 미바인딩이면 가려짐이 0이 되어 포그가 통째로 사라진다.
            if (!p.fogCloudNeutralHandle.IsValid())
            {
                if (!dx12.CreateFogCloudNeutral(
                    p.fogCloudNeutralHandle, outError)) return false;
            }

            // 블루 노이즈는 캐시가 PIXEL로 끝내 두었다. 그래프가 ShaderResource
            // (=ALL)로 임포트하므로 여기서 한 번 넓혀 둔다 — ALL은 PIXEL의
            // 상위집합이라 이 리소스를 픽셀로 읽는 쪽이 생겨도 그대로 맞다.
            // 두 번 걸면 before가 실제와 어긋나므로 캐시 수명당 한 번만 건다.
            if (!fogNoiseStateWidened)
            {
                const RHITransition widen[] = {
                    { noiseEntry.handle,
                      RHIResourceState::PixelShaderResource,
                      RHIResourceState::ShaderResource } };
                dx12.Resources().TransitionResources(widen);
                fogNoiseStateWidened = true;
            }

            fogInputsReady = true;
            return true;
        }

        bool EnsureFogInputs(VulkanLivePipeline& p, std::string& outError)
        {
            if (p.fogInputsReady) return true;

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
                    outError = "블루 노이즈 로드 실패: " +
                        std::string(exception.what());
                    return false;
                }
            }
            if (!fogBlueNoise)
            {
                outError = "블루 노이즈 로드 실패: VolumetricFog\\blueNoise.dds";
                return false;
            }

            std::string noiseError;
            const RHITextureEntry noise =
                p.textureCache.GetOrUpload(fogBlueNoise.get(), noiseError);
            if (!noise.IsValid())
            {
                outError = "Vulkan 블루 노이즈 운반 실패" +
                    (noiseError.empty() ? std::string{} : ": " + noiseError);
                return false;
            }

            std::string neutralError;
            const RHITextureEntry neutral =
                p.textureCache.GetOrUpload(nullptr, neutralError);
            if (!neutral.IsValid())
            {
                outError = "Vulkan 포그 중립 구름 텍스처 생성 실패" +
                    (neutralError.empty() ? std::string{} : ": " + neutralError);
                return false;
            }

            // Vulkan 자산 캐시는 둘을 ShaderResource layout으로 업로드한다.
            // DX12처럼 PIXEL→ALL 확대나 raw resource 등록을 별도로 할 필요가 없다.
            p.fogBlueNoiseHandle = noise.handle;
            p.fogCloudNeutralHandle = neutral.handle;
            p.fogInputsReady = true;
            return true;
        }

        bool InitializeFogPass(LivePipeline&, EnhancedVolumetricFogPass& fog,
            const EnhancedFrameContext& context, std::string& outError)
        {
            return fog.Initialize(context, outError);
        }

        bool InitializeFogPass(VulkanLivePipeline&, EnhancedVolumetricFogPass& fog,
            const EnhancedFrameContext& context, std::string& outError)
        {
            RHIShaderCompiler::ScopedOutput spirv(RHIShaderBinary::SpirV);
            return fog.Initialize(context, outError);
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
        template <typename PipelineT>
        bool BuildPipelineDesc(PipelineT& p, bool supportsFog,
            std::string& outError)
        {
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
                    auto& view = p.views[binding.viewIndex];

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

            // ── 월드 스프라이트 / 3D Canvas ──
            // SpriteRenderer와 Scene View의 Canvas 이미지, World Space UI를
            // 투명 패스 뒤 HDR에 얹는다. 깊이는 항목별로 선택한다.
            {
                LivePassNode node;
                node.name = "Sprite";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.sprite; };
                node.reads = { LiveSlots::kGBufferDepth };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedSpritePass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kLitColor);
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    p.sprite.SetInputs(inputs);
                    p.sprite.Declare(graph, ctx);
                    if (p.sprite.GetOutput().IsValid())
                        bb.Set(LiveSlots::kLitColor, p.sprite.GetOutput());
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
                node.prepare = [this, &p, supportsFog](const EnhancedFrameContext& ctx,
                    std::string& err, uint32_t viewIndex) -> bool
                {
                    if (!supportsFog) return true;
                    if (!fogEnabled) return true;

                    auto& view = p.views[viewIndex];

                    std::string fogError;
                    const bool ready = EnsureFogInputs(p, fogError) &&
                        (view.fogReady || InitializeFogPass(p, view.fog, ctx, fogError));
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

                node.shutdown = [&p]()
                {
                    for (auto& view : p.views)
                    {
                        if (view.fogReady) view.fog.Shutdown();
                        view.fogReady = false;
                    }
                };

                node.reads = { LiveSlots::kGBufferDepth, LiveSlots::kShadowMap };
                node.modifies = { LiveSlots::kLitColor };
                node.active = [this, &p, supportsFog]()
                {
                    if (!supportsFog || !fogEnabled) return false;
                    // 뷰마다 준비 상태가 다를 수 있어 하나라도 서 있으면 활성으로
                    // 본다. 실제 뷰의 준비 여부는 declare가 다시 확인한다.
                    for (const auto& view : p.views)
                    {
                        if (view.fogReady) return true;
                    }
                    return false;
                };
                node.declare = [this, &p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
                {
                    auto& view = p.views[binding.viewIndex];
                    if (!view.fogReady) return;

                    EnhancedVolumetricFogPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kLitColor);
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    inputs.shadowMap = bb.Get(LiveSlots::kShadowMap);
                    inputs.cloudShadow = graph.ImportTexture(p.fogCloudNeutralHandle,
                        RHIResourceState::ShaderResource, "Fog.CloudNeutral");
                    inputs.blueNoise = graph.ImportTexture(p.fogBlueNoiseHandle,
                        RHIResourceState::ShaderResource, "Fog.BlueNoise");
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

            // Screen Space Overlay는 Game View의 최종 LDR 위에 그린다.
            // Scene View에서는 같은 Canvas를 월드 스프라이트로 미리보기한다.
            {
                LivePassNode node;
                node.name = "UI";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.ui; };
                node.modifies = { LiveSlots::kDisplayLdr };
                node.declare = [this, &p](LiveBlackboard& bb,
                    EnhancedRenderGraph& graph, const EnhancedFrameContext& ctx,
                    const LiveFrameBinding& binding)
                {
                    if (0 == (binding.viewFlags & LiveViewFlags::kScreenSpaceUI) ||
                        uiRects.empty()) return;
                    EnhancedUIPass::Inputs inputs{};
                    inputs.color = bb.Get(LiveSlots::kDisplayLdr);
                    p.ui.SetInputs(inputs);
                    p.ui.Declare(graph, ctx);
                    if (p.ui.GetOutput().IsValid())
                        bb.Set(LiveSlots::kDisplayLdr, p.ui.GetOutput());
                };
                p.desc.AddNode(std::move(node));
            }

            // ── Host 기여 노드 (E4-2) ──
            //
            // 에디터 씬 오버레이(그리드·기즈모 체인)는 더 이상 여기 없다.
            // Editor Host가 IRenderFeatureContributor로 이 자리(UI 뒤,
            // live_present 앞)에 자기 노드를 끼워 넣는다 — Player는 기여자를
            // 설치하지 않으므로 노드 자체가 서지 않는다(§4.4, E4 판정 기준 2).
            // 기여 노드의 초기화·준비·해제·선언은 다른 노드와 같은 기계
            // (InitializeAll 등)를 타고, 패스 수명은 노드 람다가 붙드는 묶음이
            // 소유한다 — desc가 노드를 놓을 때 묶음도 함께 사라진다.
            {
                std::shared_ptr<IRenderFeatureContributor> contributor;
                {
                    std::lock_guard<std::mutex> lock(featureContributorMutex);
                    contributor = featureContributor;
                }
                if (contributor)
                {
                    RenderFeatureContext featureContext{};
                    featureContext.ldrFormat = EnhancedPostChainPass::kLDRFormat;
                    featureContext.gizmoScene = &gizmoData;
                    contributor->Contribute(p.desc, featureContext);
                }
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
                    if (!binding.sharedTarget.IsValid() &&
                        !binding.readbackTarget.IsValid()) return;

                    const RGHandle finalHandle = bb.Get(LiveSlots::kDisplayLdr);
                    if (!finalHandle.IsValid()) return;

                    if (binding.sharedTarget.IsValid())
                    {
                        const RGHandle sharedHandleRG = graph.ImportTexture(
                            binding.sharedTarget, RHIResourceState::CopyDest, "Live.Shared");

                        graph.AddPass("live_present",
                            { { finalHandle, RHIResourceState::CopySource },
                              { sharedHandleRG, RHIResourceState::CopyDest } },
                            [sharedHandleRG, finalHandle](
                                const EnhancedRenderGraph::ExecuteContext& executeContext)
                            {
                                // 공유 텍스처는 슬롯 생성 때 한 번 등록된다. 그래프는
                                // 백엔드 중립 핸들을 빌려 프레임 의존성만 기술한다.
                                executeContext.encoder->CopyTexture(
                                    executeContext.ResolveHandle(sharedHandleRG),
                                    executeContext.ResolveHandle(finalHandle));
                            }, true);
                        return;
                    }

                    const RHIReadback readback = binding.readbackTarget;
                    graph.AddPass("live_present",
                        { { finalHandle, RHIResourceState::CopySource } },
                        [readback, finalHandle](
                            const EnhancedRenderGraph::ExecuteContext& executeContext)
                        {
                            executeContext.encoder->CopyToReadback(
                                readback, executeContext.ResolveHandle(finalHandle));
                        }, true);
                };
                p.desc.AddNode(std::move(node));
            }

            // 세울 때 한 번 검증한다. 여기서 걸리면 배선이 잘못된 것이고,
            // 그것은 화면을 보기 전에 알아야 하는 종류다.
            {
                std::lock_guard<std::mutex> lock(debugMutex);
                if (!RefreshPipelineDescriptionLocked(p.desc, outError))
                {
                    outError = "파이프라인 기술 검증 실패: " + outError;
                    return false;
                }
            }

            return true;
        }

        /// 켬 → 끔 전환의 자원 해제. 파이프라인은 살아 있고 포그만 내린다 —
        /// 파이프라인을 통째로 헐 때는 포그 노드의 shutdown이 같은 일을 한다
        /// (BuildPipelineDesc의 VolumetricFog 노드). 둘을 합치지 않는 이유는
        /// 여기만 GPU 완주를 기다리고 텍스처 캐시 수명과 얽히기 때문이다.
        void ReleaseFogResources()
        {
            if (pipeline)
            {
                LivePipeline& p = *pipeline;
                bool anyReady = fogInputsReady;
                for (const auto& view : p.views) anyReady = anyReady || view.fogReady;
                if (!anyReady)
                {
                    fogTeardownPending = false;
                    fogRetireFence = 0;
                    return;
                }

                if (0 == fogRetireFence)
                    fogRetireFence = dx12.GetLastSignaledFenceValue();
                if (dx12.GetCompletedFenceValue() < fogRetireFence) return;
                for (auto& view : p.views)
                {
                    if (view.fogReady) view.fog.Shutdown();
                    view.fogReady = false;
                }
                if (p.fogCloudNeutralHandle.IsValid())
                {
                    dx12.ReleaseFogCloudNeutral(p.fogCloudNeutralHandle);
                }
                p.fogBlueNoiseHandle = {};
                fogInputsReady = false;
                fogTeardownPending = false;
                fogRetireFence = 0;
                return;
            }

            if (vulkanPipeline)
            {
                VulkanLivePipeline& p = *vulkanPipeline;
                bool anyReady = p.fogInputsReady;
                for (const auto& view : p.views) anyReady = anyReady || view.fogReady;
                if (!anyReady)
                {
                    fogTeardownPending = false;
                    fogRetireFence = 0;
                    return;
                }

                if (0 == fogRetireFence)
                    fogRetireFence = p.resources.GetLastSignaledFenceValue();
                if (p.resources.GetCompletedFenceValue() < fogRetireFence) return;
                for (auto& view : p.views)
                {
                    if (view.fogReady) view.fog.Shutdown();
                    view.fogReady = false;
                }
                // 입력 두 장은 texture cache 소유다. 핸들만 놓고 cache가
                // pipeline teardown 또는 retirement 때 실제 이미지를 해제한다.
                p.fogCloudNeutralHandle = {};
                p.fogBlueNoiseHandle = {};
                p.fogInputsReady = false;
                fogTeardownPending = false;
                fogRetireFence = 0;
            }
        }

        // ── 씬 밀봉 복사 ①: 카메라와 무관한 수집 (프레임당 1회) ──
        //
        // 규칙은 dx12.scene(RunSceneBindingTest [1/4])과 같다: 프록시·재질을
        // 들지 않고 필요한 것만 복사한다. 거기와 여기가 갈리면 dx12.scene은
        // 통과하는데 라이브만 틀리는 상태가 되므로, 규칙을 바꿀 때는 두 곳을
        // 함께 바꿀 것.
        //
        // ★ 왜 카메라 루프 밖으로 뺐나. 메시·재질·본 팔레트를 뽑는 일은
        //   카메라를 보지 않는데도 뷰마다 반복했다 — 뷰가 둘이면 같은 복사를
        //   두 번 했다(MultiCameraRenderPlan §14의 후속 과제). 여기서 한 번
        //   모으고, 뷰는 거르기만 한다.
        struct CanvasPlane
        {
            Mathf::xVector center{ XMVectorZero() };
            Mathf::xVector right{ XMVectorZero() };
            Mathf::xVector down{ XMVectorZero() };
            bool valid{ false };
        };

        static Mathf::xMatrix MakeSpriteMatrix(Mathf::xVector right,
            Mathf::xVector down, Mathf::xVector center)
        {
            right = XMVectorSetW(right, 0.f);
            down = XMVectorSetW(down, 0.f);
            center = XMVectorSetW(center, 1.f);
            const Mathf::xVector normal = XMVector3Normalize(
                XMVector3Cross(right, down));
            return Mathf::xMatrix(right, down, XMVectorSetW(normal, 0.f), center);
        }

        static CanvasPlane ResolveCanvasPlane(const UIRenderProxy::ImageData& image,
            const FrameCameraSnapshot* gameCamera)
        {
            CanvasPlane plane{};
            const float rootWidth = image.canvasRect.z;
            const float rootHeight = image.canvasRect.w;
            if (rootWidth <= 0.f || rootHeight <= 0.f) return plane;

            if (CanvasRenderMode::ScreenSpaceCamera == image.renderMode)
            {
                if (nullptr == gameCamera) return plane;
                const float projectionX = std::abs(XMVectorGetX(
                    gameCamera->projection.r[0]));
                const float projectionY = std::abs(XMVectorGetY(
                    gameCamera->projection.r[1]));
                if (projectionX < 1e-6f || projectionY < 1e-6f) return plane;

                float distance = (std::max)(image.planeDistance,
                    gameCamera->nearPlane + 0.01f);
                if (gameCamera->farPlane > gameCamera->nearPlane)
                    distance = (std::min)(distance, gameCamera->farPlane - 0.01f);

                const float planeWidth = gameCamera->isOrthographic
                    ? 2.f / projectionX : 2.f * distance / projectionX;
                const float planeHeight = gameCamera->isOrthographic
                    ? 2.f / projectionY : 2.f * distance / projectionY;

                plane.center = XMVectorAdd(gameCamera->eyePosition,
                    XMVectorScale(gameCamera->forward, distance));
                plane.right = XMVectorScale(
                    XMVector3Normalize(gameCamera->right), planeWidth);
                plane.down = XMVectorScale(
                    XMVector3Normalize(gameCamera->up), -planeHeight);
                plane.valid = true;
                return plane;
            }

            const float centerX = image.canvasRect.x + rootWidth * 0.5f;
            const float centerY = image.canvasRect.y + rootHeight * 0.5f;
            plane.center = XMVector3TransformCoord(
                XMVectorSet(centerX, -centerY, 0.f, 1.f), image.canvasWorld);
            plane.right = XMVector3TransformNormal(
                XMVectorSet(rootWidth, 0.f, 0.f, 0.f), image.canvasWorld);
            plane.down = XMVector3TransformNormal(
                XMVectorSet(0.f, -rootHeight, 0.f, 0.f), image.canvasWorld);
            plane.valid = XMVectorGetX(XMVector3LengthSq(plane.right)) > 1e-8f &&
                XMVectorGetX(XMVector3LengthSq(plane.down)) > 1e-8f;
            return plane;
        }

        static bool ResolveImageRect(const UIRenderProxy::ImageData& image,
            ClippedDestination& dst, Mathf::Vector4& uv)
        {
            if (nullptr == image.texture) return false;
            const auto size = image.texture->GetImageSize();
            const long texWidth = static_cast<long>(size.x);
            const long texHeight = static_cast<long>(size.y);
            if (texWidth <= 0 || texHeight <= 0) return false;

            const float halfWidth = image.origin.x * image.scale.x;
            const float halfHeight = image.origin.y * image.scale.y;
            const float left = image.position.x - halfWidth;
            const float top = image.position.y - halfHeight;
            const float right = left + texWidth * image.scale.x;
            const float bottom = top + texHeight * image.scale.y;

            ClippedSource src{};
            if (!CalculateClippedRects(image.clipDirection, image.clipPercent,
                texWidth, texHeight, left, top, right, bottom,
                image.scale.x, image.scale.y, src, dst)) return false;

            uv = {
                static_cast<float>(src.left) / texWidth,
                static_cast<float>(src.top) / texHeight,
                static_cast<float>(src.right) / texWidth,
                static_cast<float>(src.bottom) / texHeight,
            };
            if (0 != (image.filpEffect & SpriteEffects_FlipHorizontally))
                std::swap(uv.x, uv.z);
            if (0 != (image.filpEffect & SpriteEffects_FlipVertically))
                std::swap(uv.y, uv.w);
            return true;
        }

        static bool AppendImageToPlane(const UIRenderProxy::ImageData& image,
            const CanvasPlane& plane, bool enableDepth,
            std::vector<EnhancedSpritePass::Item>& output)
        {
            if (!plane.valid) return false;

            ClippedDestination dst{};
            Mathf::Vector4 uv{};
            if (!ResolveImageRect(image, dst, uv)) return false;

            const float rootWidth = image.canvasRect.z;
            const float rootHeight = image.canvasRect.w;
            const float rootCenterX = image.canvasRect.x + rootWidth * 0.5f;
            const float rootCenterY = image.canvasRect.y + rootHeight * 0.5f;
            const float rectCenterX = (dst.left + dst.right) * 0.5f;
            const float rectCenterY = (dst.top + dst.bottom) * 0.5f;
            const float rectWidth = dst.right - dst.left;
            const float rectHeight = dst.bottom - dst.top;

            const Mathf::xVector center = XMVectorAdd(plane.center,
                XMVectorAdd(
                    XMVectorScale(plane.right,
                        (rectCenterX - rootCenterX) / rootWidth),
                    XMVectorScale(plane.down,
                        (rectCenterY - rootCenterY) / rootHeight)));

            const float worldWidth = XMVectorGetX(XMVector3Length(plane.right)) *
                rectWidth / rootWidth;
            const float worldHeight = XMVectorGetX(XMVector3Length(plane.down)) *
                rectHeight / rootHeight;
            const Mathf::xVector rightUnit = XMVector3Normalize(plane.right);
            const Mathf::xVector downUnit = XMVector3Normalize(plane.down);
            const float c = std::cos(image.rotation);
            const float s = std::sin(image.rotation);
            const Mathf::xVector right = XMVectorScale(
                XMVectorAdd(XMVectorScale(rightUnit, c),
                    XMVectorScale(downUnit, s)), worldWidth);
            const Mathf::xVector down = XMVectorScale(
                XMVectorAdd(XMVectorScale(rightUnit, -s),
                    XMVectorScale(downUnit, c)), worldHeight);

            EnhancedSpritePass::Item item{};
            item.world = MakeSpriteMatrix(right, down, center);
            item.uv = uv;
            item.color = image.color;
            item.texture = image.texture.get();
            item.canvasOrder = image.canvasOrder;
            item.layerOrder = image.layerOrder;
            item.enableDepth = enableDepth;
            output.push_back(item);
            return true;
        }

        static void AppendCanvasOutline(const UIRenderProxy::ImageData& image,
            const CanvasPlane& plane, std::vector<EnhancedSpritePass::Item>& output)
        {
            if (!plane.valid) return;
            const float width = XMVectorGetX(XMVector3Length(plane.right));
            const float height = XMVectorGetX(XMVector3Length(plane.down));
            const float thickness = (std::max)(0.02f,
                (std::min)(width, height) * 0.004f);
            const auto rightUnit = XMVector3Normalize(plane.right);
            const auto downUnit = XMVector3Normalize(plane.down);

            const auto add = [&](Mathf::xVector center, Mathf::xVector right,
                Mathf::xVector down)
            {
                EnhancedSpritePass::Item border{};
                border.world = MakeSpriteMatrix(right, down, center);
                border.color = { 1.f, 0.72f, 0.08f, 0.8f };
                border.texture = nullptr;
                border.canvasOrder = image.canvasOrder;
                border.layerOrder = 0x7fffffff;
                border.enableDepth = false;
                output.push_back(border);
            };

            add(XMVectorSubtract(plane.center, XMVectorScale(plane.down, 0.5f)),
                plane.right, XMVectorScale(downUnit, thickness));
            add(XMVectorAdd(plane.center, XMVectorScale(plane.down, 0.5f)),
                plane.right, XMVectorScale(downUnit, thickness));
            add(XMVectorSubtract(plane.center, XMVectorScale(plane.right, 0.5f)),
                XMVectorScale(rightUnit, thickness), plane.down);
            add(XMVectorAdd(plane.center, XMVectorScale(plane.right, 0.5f)),
                XMVectorScale(rightUnit, thickness), plane.down);
        }

        void BuildDrawPool()
        {
            drawPool.clear();
            decals.clear();
            spritePool.clear();
            uiProxySnapshot.clear();
            uiProxyPointers.clear();

            if (nullptr == renderScene) return;

            const auto poolDecal = [this](const DecalRenderProxy* proxy)
            {
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
                item.diffuse = proxy->m_diffuseTexture.get();
                item.normal = proxy->m_normalTexture.get();
                item.occRoughMetal = proxy->m_occluroughmetalTexture.get();
                item.sliceX = proxy->m_sliceX;
                item.sliceY = proxy->m_sliceY;
                item.sliceNum = proxy->m_sliceNum;
                decals.push_back(item);
            };

            const auto poolMesh = [this](const MeshRenderProxy* proxy)
            {
                if (nullptr == proxy->m_Mesh) return;

                PooledDraw pooled{};
                pooled.item.mesh = proxy->m_Mesh.get();
                pooled.item.worldMatrix = proxy->m_worldMatrix;

                if (proxy->m_isAnimationEnabled
                    && (HashedGuid::INVAILD_ID != proxy->m_animatorGuid)
                    && proxy->m_finalTransforms)
                {
                    pooled.item.bonePalette = proxy->m_finalTransforms.get();
                    pooled.item.boneCount = Skeleton::MAX_BONES;
                    pooled.item.animatorKey = static_cast<uint64_t>(proxy->m_animatorGuid);
                }

                if (const auto* material = proxy->m_Material.get())
                {
                    pooled.item.baseColor = material->m_pBaseColor;
                    pooled.item.normalMap = material->m_pNormal;
                    pooled.item.occRoughMetal = material->m_pOccRoughMetal;
                    pooled.item.emissive = material->m_pEmissive;

                    pooled.item.baseColorFactor = material->m_materialInfo.m_baseColor;
                    pooled.item.metallic = material->m_materialInfo.m_metallic;
                    pooled.item.roughness = material->m_materialInfo.m_roughness;
                    pooled.item.useNormalMap =
                        (0 != material->m_materialInfo.m_useNormalMap) ? 1u : 0u;

                    pooled.isTransparent =
                        (MaterialRenderingMode::Transparent == material->m_renderingMode);
                }

                pooled.worldBounds = proxy->m_worldBounds;
                pooled.hasBounds = proxy->m_hasWorldBounds;

                drawPool.push_back(pooled);
            };

            const auto poolSprite = [this](const SpriteRenderProxy* proxy)
            {
                if (!proxy->m_isEnabled || nullptr == proxy->m_spriteTexture) return;
                PooledSprite pooled{};
                pooled.worldMatrix = proxy->m_worldMatrix;
                pooled.texture = proxy->m_spriteTexture;
                pooled.billboardType = proxy->m_billboardType;
                pooled.billboardAxis = proxy->m_billboardAxis;
                pooled.orderInLayer = proxy->m_orderInLayer;
                pooled.enableDepth = proxy->m_enableDepth;
                spritePool.push_back(std::move(pooled));
            };

            // shared_ptr 스냅샷이 이 함수가 재질·메시를 복사하는 동안 프록시를
            // 살려 둔다.
            //
            // ★ 데칼의 순서를 건드리지 않는다. 데칼 패스는 겹친 데칼의 순서가
            //   곧 블렌드 결과라 정렬하지 않고 '연속한 같은 묶음'만 배칭한다
            //   (EnhancedDecalPass 헤더). 프록시 순회 순서를 그대로 넘긴다.
            const RenderScene::ProxySnapshot proxies =
                renderScene->GetPrimitiveProxySnapshot();
            for (const auto& proxy : proxies)
            {
                if (nullptr == proxy) continue;

                if (const DecalRenderProxy* decal = proxy->As<DecalRenderProxy>())
                {
                    poolDecal(decal);
                    continue;
                }

                if (const MeshRenderProxy* mesh = proxy->As<MeshRenderProxy>())
                {
                    poolMesh(mesh);
                    continue;
                }

                if (const SpriteRenderProxy* sprite = proxy->As<SpriteRenderProxy>())
                {
                    poolSprite(sprite);
                }
            }

            uiProxySnapshot = renderScene->GetUIProxySnapshot();
            uiProxyPointers.reserve(uiProxySnapshot.size());
            for (const auto& proxy : uiProxySnapshot)
            {
                if (proxy) uiProxyPointers.push_back(proxy.get());
            }
        }

        // ── 렌더 입력 소비: 이 뷰의 몫 ──
        /// Camera/Scene을 다시 읽지 않고 게임 스레드가 밀봉한 값만 소비한다.
        bool CaptureFromView(const EnhancedLiveFramePacket& frame,
            const EnhancedLiveViewPacket& viewPacket)
        {
            if (!viewPacket.key.IsValid() || nullptr == renderScene ||
                0 == frame.width || 0 == frame.height) return false;

            cameraSnapshot = viewPacket.camera;
            totalSeconds = frame.totalSeconds;

            // Publish is producer-owned; consuming the packet advances the
            // legacy double buffer to the same immutable camera snapshot.
            if (RenderPassData* passData = renderScene->GetRenderPassData(
                static_cast<size_t>(viewPacket.key.cameraIndex)))
            {
                passData->LatchFrameSnapshot();
            }

            gizmoData = viewPacket.gizmos
                ? *viewPacket.gizmos : EnhancedGizmoSceneData{};

            draws.clear();
            forwardDraws.clear();
            lights.clear();
            worldSprites.clear();
            uiRects.clear();
            // decals는 비우지 않는다 — BuildDrawPool이 프레임당 한 번 채우고
            // 뷰 둘이 같은 목록을 본다. 여기서 비우면 두 번째 뷰가 빈 것을 쓴다.

            // SpriteRenderer는 모든 뷰에서 월드 쿼드로 보인다. None은 기존
            // XZ Quad의 축/UV를 그대로 옮기고, Billboard만 이 뷰의 카메라를 쓴다.
            for (const PooledSprite& sprite : spritePool)
            {
                const Mathf::xVector center = sprite.worldMatrix.r[3];
                const float width = 2.f * XMVectorGetX(
                    XMVector3Length(sprite.worldMatrix.r[0]));
                const float height = 2.f * XMVectorGetX(
                    XMVector3Length(sprite.worldMatrix.r[2]));
                if (width <= 1e-6f || height <= 1e-6f) continue;

                Mathf::xVector right{};
                Mathf::xVector down{};
                if (BillboardType::None == sprite.billboardType)
                {
                    right = XMVectorScale(sprite.worldMatrix.r[0], 2.f);
                    down = XMVectorScale(sprite.worldMatrix.r[2], -2.f);
                }
                else if (BillboardType::Spherical == sprite.billboardType)
                {
                    right = XMVectorScale(XMVector3Normalize(cameraSnapshot.right), width);
                    down = XMVectorScale(XMVector3Normalize(cameraSnapshot.up), -height);
                }
                else
                {
                    Mathf::xVector axis = XMVector3Normalize(
                        XMLoadFloat3(&sprite.billboardAxis));
                    if (XMVectorGetX(XMVector3LengthSq(axis)) < 1e-8f)
                        axis = XMVectorSet(0.f, 1.f, 0.f, 0.f);
                    const Mathf::xVector toCamera = XMVectorSubtract(
                        cameraSnapshot.eyePosition, center);
                    Mathf::xVector rightUnit = XMVector3Cross(toCamera, axis);
                    if (XMVectorGetX(XMVector3LengthSq(rightUnit)) < 1e-8f)
                        rightUnit = cameraSnapshot.right;
                    right = XMVectorScale(XMVector3Normalize(rightUnit), width);
                    down = XMVectorScale(axis, -height);
                }

                EnhancedSpritePass::Item item{};
                item.world = MakeSpriteMatrix(right, down, center);
                item.texture = sprite.texture.get();
                item.layerOrder = sprite.orderInLayer;
                item.enableDepth = sprite.enableDepth;
                worldSprites.push_back(item);
            }

            const FrameCameraSnapshot* gameCamera = nullptr;
            for (uint32_t i = 0; i < frame.viewCount; ++i)
            {
                if (!frame.views[i].isEditorView)
                {
                    gameCamera = &frame.views[i].camera;
                    break;
                }
            }

            // Overlay는 Game View의 최종 픽셀 좌표로 간다. 기존 RectTransform은
            // 중앙 원점이므로 BuildRectsFromQueue가 화면 절반만큼 이동한다.
            if (!viewPacket.isEditorView && !uiProxyPointers.empty())
            {
                EnhancedUIPass::BuildRectsFromQueue(uiProxyPointers.data(),
                    uiProxyPointers.size(), uiRects,
                    static_cast<float>(frame.width), static_cast<float>(frame.height));
            }

            // Scene View에서는 Overlay도 Canvas Transform 평면으로 미리 본다.
            // Camera/World Space는 Game View에서도 실제 3D 평면을 쓴다.
            std::unordered_set<size_t> outlinedCanvases;
            for (UIRenderProxy* proxy : uiProxyPointers)
            {
                if (nullptr == proxy) continue;
                const auto* image = std::get_if<UIRenderProxy::ImageData>(
                    &proxy->GetData());
                if (nullptr == image) continue;

                if (CanvasRenderMode::ScreenSpaceOverlay == image->renderMode &&
                    !viewPacket.isEditorView) continue;

                const CanvasPlane plane = ResolveCanvasPlane(*image, gameCamera);
                const bool depth = CanvasRenderMode::WorldSpace == image->renderMode;
                AppendImageToPlane(*image, plane, depth, worldSprites);

                if (viewPacket.isEditorView)
                {
                    const size_t canvasKey = image->canvasId.m_ID_Data;
                    if (outlinedCanvases.insert(canvasKey).second)
                        AppendCanvasOutline(*image, plane, worldSprites);
                }
            }

            // 이 카메라의 절두체로 거른다. 수집은 BuildDrawPool이 프레임당
            // 한 번 끝냈으므로 여기서는 고르고 정렬하기만 한다.
            //
            // ★ 절두체를 못 만드는 경우(직교 투영)에는 거르지 않는다.
            //   BoundingFrustum::CreateFromMatrix가 원근 전용이라, 없는
            //   절두체로 자르느니 다 그리는 쪽이 옳다.
            DirectX::BoundingFrustum frustum;
            const bool cullDraws = BuildViewFrustum(cameraSnapshot, frustum);

            uint32_t culled = 0;
            for (const PooledDraw& pooled : drawPool)
            {
                // 상자를 믿을 수 없는 것(스키닝)은 자르지 않는다.
                if (cullDraws && pooled.hasBounds &&
                    !frustum.Intersects(pooled.worldBounds))
                {
                    ++culled;
                    continue;
                }

                if (pooled.isTransparent) forwardDraws.push_back(pooled.item);
                else                      draws.push_back(pooled.item);
            }

            lastPoolDraws = static_cast<uint32_t>(drawPool.size());
            lastCulledDraws = culled;

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

            // 광원도 프리미티브와 같은 규약으로 밀봉한다 — 맵을 직접 훑지
            // 않고 스냅샷(shared_ptr 복사)을 받아 값만 옮겨 담는다.
            //
            // 여기서 뷰가 고른다(RenderSceneViewPlan ②): 절두체에 닿지 않는
            // 지역 광원을 빼고, 남은 것을 기여도 순으로 세운다. 패스마다
            // 다른 셰이더 배열 한도는 그대로지만, 목록이 정렬돼 있으므로
            // "앞의 N개"가 "가장 중요한 N개"가 된다 — 예전에는 등록 순서로
            // 잘렸다.
            const ViewLightSelection lightSelection =
                SelectLightsForView(renderScene->GetLightProxySnapshot(), cameraSnapshot);

            lights = lightSelection.lights;
            lastLightSelection = lightSelection;

            return true;
        }

        /// 두 백엔드가 같은 scene pass 입력을 준비한다. 호출 시점에는 해당
        /// 백엔드의 프레임이 열려 있어야 한다 — IBL과 자산 캐시 업로드가
        /// immediate encoder를 사용하고, 그 뒤 병렬 그래프 제출이 이어진다.
        template <typename PipelineT>
        bool PreparePipelineFrame(PipelineT& p, uint32_t viewIndex,
            std::string& outError)
        {
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
                const RHITextureEntry skyEntry =
                    p.frameContext.textureCache->GetOrUpload(
                        skyEquirect.get(), skyUploadError);
                if (!skyEntry.IsValid() || skyEntry.isCube)
                {
                    outError = "equirect HDR 운반 실패";
                    if (!skyUploadError.empty()) outError += ": " + skyUploadError;
                    return false;
                }

                if (!p.ibl.Generate(p.frameContext, skyEntry.handle, skyEntry.format,
                    512, 512, outError))
                {
                    outError = "HDR→Cube/IBL 생성 실패: " + outError;
                    return false;
                }

                p.skyBox.SetCubeMap(
                    p.ibl.GetCubeMap(), EnhancedIBLGenerator::kFormat, 1);
                p.deferred.SetIBL(p.ibl.GetIrradianceMap(), p.ibl.GetPrefilteredMap(),
                    EnhancedIBLGenerator::kPrefilterMips, p.ibl.GetBrdfLut());
                p.forward.SetIBL(p.ibl.GetIrradianceMap(), p.ibl.GetPrefilteredMap(),
                    EnhancedIBLGenerator::kPrefilterMips, p.ibl.GetBrdfLut());
                p.iblGenerated = true;
                skyBoxDirty = false;
            }

            // 기즈모 데이터(gizmoData)의 프레임별 feed는 기여 노드의 prepare가
            // 한다 — RenderFeatureContext가 안정 주소를 넘겼다(E4-2).
            p.sprite.SetItems(&worldSprites);
            p.ui.SetRects(&uiRects);

            return p.desc.PrepareAll(p.frameContext, viewIndex, outError);
        }

        // ── 한 프레임 ──
        //
        // 배선은 dx12.scene의 renderAndCount와 같다(리드백 프로브만 없다).
        // 마지막의 live_present가 이 러너의 고유 조각이다 — 포스트 체인 결과를
        // 공유 텍스처로 복사한다. 그 소비 선언이 있어야 그래프가 체인을
        // 걷어내지 않는다(post_probe가 하던 역할을 실전에서는 이 복사가 맡는다).
        bool RenderOnce(LivePipeline::CameraView& view, int slotIndex,
            uint64_t sourceFrameId,
            std::string& outError)
        {
            LivePipeline& p = *pipeline;
            LivePipeline::DisplaySlot& slot = view.slots[slotIndex];

            if (!dx12.BeginFrame(outError)) return false;

            // 여기서부터는 커맨드 리스트가 열려 있다. 아래 어느 지점에서
            // 실패로 빠져나가든 닫고 나가야 한다 — 열린 채 두면 다음
            // BeginFrame의 얼로케이터 Reset이 E_FAIL로 죽고, 원래 사유가
            // 그 2차 오류로 덮인다. 반환 지점이 스무 곳이 넘어 가드로 건다.
            bool frameCommitted = false;
            struct FrameGuard
            {
                EnhancedSceneRendererLiveDX12Adapter& backend;
                const bool& committed;
                ~FrameGuard() { if (!committed) backend.AbortFrame(); }
            } frameGuard{ dx12, frameCommitted };

            dx12.BeginProfilerFrame(frameCounter++);
            const uint32_t viewIndex = static_cast<uint32_t>(&view - &p.views[0]);
            if (!PreparePipelineFrame(p, viewIndex, outError)) return false;

            lastDrawCount = p.gbuffer.GetLastDrawCount();
            lastBatchCount = p.gbuffer.GetLastBatchCount();
            lastDecalCount = p.decal.GetLastDecalCount();
            lastDecalBatchCount = p.decal.GetLastBatchCount();
            lastSpriteCount = p.sprite.GetLastItemCount();
            lastSpriteBatchCount = p.sprite.GetLastBatchCount();
            lastUIRectCount = p.ui.GetLastRectCount();
            lastUIBatchCount = p.ui.GetLastBatchCount();
            const uint32_t targetIndex = DisplayTargetIndex(view.isEditorView);
            viewSpriteCounts[targetIndex] = lastSpriteCount;
            viewUICounts[targetIndex] = lastUIRectCount;

            slot.graph = std::make_unique<EnhancedRenderGraph>(dx12.Resources());
            EnhancedRenderGraph& graph = *slot.graph;
            graph.SetProfiler(dx12.Profiler());
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
            binding.sharedTarget = slot.rhiTexture;
            binding.viewFlags = view.isEditorView
                ? LiveViewFlags::kSceneOverlay : LiveViewFlags::kScreenSpaceUI;

            p.desc.DeclareAll(p.blackboard, graph, p.frameContext, binding);

            if (!p.blackboard.Get(LiveSlots::kDisplayLdr).IsValid())
            {
                outError = "포스트 체인 출력이 없다";
                return false;
            }

            if (!graph.Compile(outError)) return false;
            LiveStopwatch recordWatch;
            recordWatch.Start();
            if (!graph.Execute(outError)) return false;
            p.lastNativeRecordMs = recordWatch.ElapsedMs();

            dx12.ResolveProfilerFrame();

            if (!dx12.EndFrame(outError)) return false;
            frameCommitted = true;   // 제출됐다 — 가드가 닫을 것이 없다

            // 여기서 기다리지 않는다 — 이것이 이 슬라이스의 전부다.
            // EndFrame이 서명한 펜스 값을 슬롯에 적어 두고, TickLive가 다음
            // 프레임들에서 완료를 논블로킹으로 확인해 표시로 승격한다.
            // 1차 실측에서 게임 스레드의 동기 WaitForGpu가 프레임당 33ms를
            // 먹어 총 대기를 58ms로 만들었다(실제 GPU는 3.7ms) — 그 벽을
            // 여기서 없앤다.
            slot.fenceValue = dx12.GetLastSignaledFenceValue();
            slot.frameId = sourceFrameId;
            slot.key = view.key;

            view.pendingQueue.push_back(slotIndex);
            return true;
        }
    };

    void LiveState::ApplyAndPublishTuning()
    {
        // 파이프라인이 없으면 적용할 대상이 없다. 요청은 그대로 들고 있는다 —
        // 리사이즈로 파이프라인이 재구축되는 사이에 들어온 변경을 버리면
        // 사용자가 방금 움직인 슬라이더가 조용히 되돌아간다.
        if (nullptr == pipeline && nullptr == vulkanPipeline) return;

        std::lock_guard<std::mutex> lock(debugMutex);
        const bool refreshPipelineDescription = hasPendingTuning;
        const auto applyAndMirror = [this](auto& p, bool supportsFog)
        {

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
            for (auto& view : p.views)
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
            if (supportsFog)
            {
                if (fogEnabled && !pendingTuning.fog.enabled)
                {
                    fogTeardownPending = true;
                    fogRetireFence = 0;
                }
                else if (pendingTuning.fog.enabled)
                {
                    fogTeardownPending = false;
                    fogRetireFence = 0;
                }
                fogEnabled = pendingTuning.fog.enabled;
            }
            else
            {
                // Vulkan 라이브 그래프에도 포그 노드는 들어가지만 중립 입력
                // 텍스처 계약이 아직 없어 노드는 비활성이다. 나머지 공용 패스의
                // 편집기 튜닝은 같은 경로로 적용하되 UI에는 실제 상태(false)를
                // 되돌려 보낸다.
                fogEnabled = false;
            }
            for (auto& view : p.views)
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
        };

        if (pipeline)
        {
            applyAndMirror(*pipeline, true);
            if (refreshPipelineDescription)
            {
                std::string ignored;
                RefreshPipelineDescriptionLocked(pipeline->desc, ignored);
            }
        }
        else
        {
            applyAndMirror(*vulkanPipeline, true);
            if (refreshPipelineDescription)
            {
                std::string ignored;
                RefreshPipelineDescriptionLocked(vulkanPipeline->desc, ignored);
            }
        }
    }

    struct ProxyUpdateKey
    {
        uint64_t sceneEpoch{ 0 };
        size_t proxyGuid{ 0 };
        size_t payloadKind{ 0 };

        bool operator==(const ProxyUpdateKey&) const noexcept = default;
    };

    struct ProxyUpdateKeyHash
    {
        size_t operator()(const ProxyUpdateKey& key) const noexcept
        {
            size_t value = std::hash<uint64_t>{}(key.sceneEpoch);
            value ^= std::hash<size_t>{}(key.proxyGuid) + 0x9e3779b9u +
                (value << 6u) + (value >> 2u);
            value ^= std::hash<size_t>{}(key.payloadKind) + 0x9e3779b9u +
                (value << 6u) + (value >> 2u);
            return value;
        }
    };

    // lifecycle 경계는 순서를 그대로 보존한다. 그 사이의 같은
    // (epoch, proxy, update type)은 서로 독립인 값 대입이므로 마지막 것만 남긴다.
    // lifecycle 하나를 만나면 맵 전체를 비우는 보수적 규칙이라 다른 대상의
    // create/destroy를 가로질러 update 순서를 바꾸지도 않는다.
    uint64_t CompactProxyUpdates(ProxyCommandQueueController::Batch& batch)
    {
        ProxyCommandQueueController::Batch compacted;
        compacted.reserve(batch.size());
        std::unordered_map<ProxyUpdateKey, size_t, ProxyUpdateKeyHash> latestUpdates;
        uint64_t superseded = 0;

        for (ProxyCommand& command : batch)
        {
            if (!command.IsValid()) continue;
            if (!command.IsUpdate())
            {
                latestUpdates.clear();
                compacted.push_back(std::move(command));
                continue;
            }

            const ProxyUpdateKey key{
                command.GetSceneEpoch(), command.GetProxyGuidValue(),
                command.GetPayloadKind() };
            const auto found = latestUpdates.find(key);
            if (found == latestUpdates.end())
            {
                latestUpdates.emplace(key, compacted.size());
                compacted.push_back(std::move(command));
            }
            else
            {
                compacted[found->second] = std::move(command);
                ++superseded;
            }
        }

        batch = std::move(compacted);
        return superseded;
    }

    bool LiveState::StartRenderThread(std::string& outError)
    {
        std::unique_lock<std::mutex> lock(renderQueueMutex);
        if (renderThread.joinable()) return true;

        renderThreadStarted = false;
        renderThreadStartFailed = false;
        renderThreadStopRequested = false;
        renderThreadAccepting = true;

        renderThreadTestDelayMs = 0;
        char* delay = nullptr;
        size_t delayLength = 0;
        if (0 == _dupenv_s(&delay, &delayLength,
            "CREATOR_RENDER_THREAD_TEST_DELAY_MS") && nullptr != delay)
        {
            renderThreadTestDelayMs = static_cast<uint32_t>((std::min)(250,
                (std::max)(0, std::atoi(delay))));
            std::free(delay);
        }

        try
        {
            renderThread = std::thread([this]
            {
                const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                {
                    std::lock_guard<std::mutex> startLock(renderQueueMutex);
                    renderThreadStartFailed = FAILED(comResult);
                    renderThreadStarted = true;
                    renderThreadRunning = !renderThreadStartFailed;
                    if (!renderThreadStartFailed)
                    {
                        frameConsumerThread = std::this_thread::get_id();
                    }
                }
                renderQueueWake.notify_all();
                if (FAILED(comResult)) return;

                for (;;)
                {
                    FrameSubmission submission;
                    {
                        std::unique_lock<std::mutex> queueLock(renderQueueMutex);
                        renderQueueWake.wait(queueLock, [this]
                        {
                            return renderThreadStopRequested || !renderQueue.empty();
                        });
                        if (renderQueue.empty())
                        {
                            if (renderThreadStopRequested) break;
                            continue;
                        }

                        submission = std::move(renderQueue.front());
                        renderQueue.pop_front();
                        renderInProgress = 1;
                    }
                    renderQueueWake.notify_all();

                    if (0 != renderThreadTestDelayMs)
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(renderThreadTestDelayMs));
                    }

                    activeDeltaBatch = std::move(submission.deltas);
                    try
                    {
                        EnhancedSceneRenderer::TickLive(submission.frame);
                    }
                    catch (const std::exception& exception)
                    {
                        std::lock_guard<std::mutex> stateLock(renderStateMutex);
                        lastError = std::string("RenderThread frame exception: ") +
                            exception.what();
                        ++frameFailures;
                        Debug->LogError(lastError);
                    }
                    catch (...)
                    {
                        std::lock_guard<std::mutex> stateLock(renderStateMutex);
                        lastError = "RenderThread frame unknown exception";
                        ++frameFailures;
                        Debug->LogError(lastError);
                    }
                    activeDeltaBatch.clear();

                    {
                        std::lock_guard<std::mutex> queueLock(renderQueueMutex);
                        renderInProgress = 0;
                        ++renderConsumed;
                    }
                    renderQueueWake.notify_all();
                }

                {
                    std::lock_guard<std::mutex> queueLock(renderQueueMutex);
                    renderThreadRunning = false;
                }
                renderQueueWake.notify_all();
                CoUninitialize();
            });
        }
        catch (const std::exception& exception)
        {
            renderThreadAccepting = false;
            outError = std::string("RenderThread 생성 실패: ") + exception.what();
            return false;
        }

        renderQueueWake.wait(lock, [this] { return renderThreadStarted; });
        if (renderThreadStartFailed)
        {
            renderThreadAccepting = false;
            lock.unlock();
            if (renderThread.joinable()) renderThread.join();
            outError = "RenderThread COM 초기화 실패";
            return false;
        }

        if (0 != renderThreadTestDelayMs)
        {
            std::printf("[RenderThread] validation delay %u ms enabled\n",
                renderThreadTestDelayMs);
        }
        return true;
    }

    bool LiveState::PublishFrame(FrameSubmission submission)
    {
        uint64_t superseded = CompactProxyUpdates(submission.deltas);
        if (0 != superseded) ProxyCommandQueue->MarkSuperseded(superseded);

        std::unique_lock<std::mutex> lock(renderQueueMutex);
        renderCoalescedDeltas += superseded;
        if (!renderThreadAccepting)
        {
            renderShutdownDiscardedDeltas += submission.deltas.size();
            ProxyCommandQueue->MarkShutdownDiscarded(
                static_cast<uint64_t>(submission.deltas.size()));
            return false;
        }

        ++renderPublished;
        if (renderQueue.size() < kRenderQueueCapacity)
        {
            renderQueue.push_back(std::move(submission));
        }
        else
        {
            ++renderOverflowEvents;
            FrameSubmission& newest = renderQueue.back();
            const size_t mergedDeltaCount = newest.deltas.size() + submission.deltas.size();

            if (mergedDeltaCount > kMaxDeltasPerSubmission)
            {
                // lifecycle delta는 버릴 수 없다. 이 극단적인 경우에만 producer를
                // 잠깐 세워 packet queue와 payload 양쪽의 상한을 지킨다.
                ++renderBackPressureWaits;
                renderQueueWake.wait(lock, [this]
                {
                    return !renderThreadAccepting || renderQueue.size() < kRenderQueueCapacity;
                });
                if (!renderThreadAccepting) return false;
                renderQueue.push_back(std::move(submission));
            }
            else
            {
                ProxyCommandQueueController::Batch merged;
                merged.reserve(mergedDeltaCount);
                for (ProxyCommand& command : newest.deltas)
                    merged.push_back(std::move(command));
                for (ProxyCommand& command : submission.deltas)
                    merged.push_back(std::move(command));

                const uint64_t mergedSuperseded = CompactProxyUpdates(merged);
                renderCoalescedDeltas += mergedSuperseded;
                ProxyCommandQueue->MarkSuperseded(mergedSuperseded);
                submission.deltas = std::move(merged);
                newest = std::move(submission);
                ++renderCoalescedFrames;
            }
        }

        renderQueueHighWatermark = (std::max)(renderQueueHighWatermark,
            static_cast<uint32_t>(renderQueue.size()));
        lock.unlock();
        renderQueueWake.notify_one();
        return true;
    }

    void LiveState::StopRenderThread()
    {
        bool shouldJoin = false;
        {
            std::lock_guard<std::mutex> lock(renderQueueMutex);
            shouldJoin = renderThread.joinable();
            if (shouldJoin)
            {
                renderThreadAccepting = false;
                renderThreadStopRequested = true;
            }
        }
        renderQueueWake.notify_all();
        if (shouldJoin)
        {
            renderThread.join();
            std::lock_guard<std::mutex> lock(renderQueueMutex);
            ++renderShutdownDrains;
        }

        const uint64_t discarded = ProxyCommandQueue->DiscardPendingForShutdown();
        {
            std::lock_guard<std::mutex> lock(renderQueueMutex);
            renderShutdownDiscardedDeltas += discarded;
            if (shouldJoin)
            {
                const bool balanced = renderPublished ==
                    renderConsumed + renderCoalescedFrames + renderQueue.size() +
                    renderInProgress;
                std::printf("[RenderThread] shutdown drain — publish %llu / consume %llu"
                    " / latest-wins %llu / pending %zu / overflow %llu"
                    " / back-pressure %llu / delta-coalesce %llu / balanced %u\n",
                    static_cast<unsigned long long>(renderPublished),
                    static_cast<unsigned long long>(renderConsumed),
                    static_cast<unsigned long long>(renderCoalescedFrames),
                    renderQueue.size(),
                    static_cast<unsigned long long>(renderOverflowEvents),
                    static_cast<unsigned long long>(renderBackPressureWaits),
                    static_cast<unsigned long long>(renderCoalescedDeltas),
                    balanced ? 1u : 0u);
            }
            else if (0 != discarded)
            {
                std::printf("[RenderThread] post-stop teardown delta drain —"
                    " discarded %llu / pending 0\n",
                    static_cast<unsigned long long>(discarded));
            }
        }
    }

    EnhancedRenderThreadStats LiveState::GetRenderThreadStats() const
    {
        std::lock_guard<std::mutex> lock(renderQueueMutex);
        EnhancedRenderThreadStats stats{};
        stats.published = renderPublished;
        stats.consumed = renderConsumed;
        stats.overflowEvents = renderOverflowEvents;
        stats.coalescedFrames = renderCoalescedFrames;
        stats.coalescedDeltas = renderCoalescedDeltas;
        stats.backPressureWaits = renderBackPressureWaits;
        stats.shutdownDrains = renderShutdownDrains;
        stats.shutdownDiscardedDeltas = renderShutdownDiscardedDeltas;
        stats.pending = static_cast<uint32_t>(renderQueue.size());
        stats.inProgress = renderInProgress;
        stats.highWatermark = renderQueueHighWatermark;
        stats.capacity = kRenderQueueCapacity;
        stats.running = renderThreadRunning;
        stats.accepting = renderThreadAccepting;
        stats.producerConsumerSeparated =
            frameProducerThread != std::thread::id{} &&
            frameConsumerThread != std::thread::id{} &&
            frameProducerThread != frameConsumerThread;
        return stats;
    }

    LiveState& GetLiveState()
    {
        static LiveState state;
        return state;
    }
}

bool EnhancedSceneRenderer::InitializeRuntime(EnhancedLiveBackend backend,
    std::string& outError)
{
    LiveState& state = GetLiveState();
    if (state.runtimeInitialized)
    {
        if (state.backend != backend)
        {
            outError = "EnhancedRenderer backend는 프로세스 부팅 뒤 변경할 수 없다";
            state.lastError = outError;
            return false;
        }
        state.enabled = true;
        return true;
    }

    try
    {
        // backend는 어떤 장치·pipeline·공유 표시 리소스도 만들기 전에 한 번만
        // 기록한다. 이 뒤에는 변경 API가 없다(Slice 8-c).
        state.backend = backend;
        state.ResetDisplaySnapshot();
        state.renderScene = std::make_shared<RenderScene>();
        state.renderScene->Initialize();

        // 에디터 카메라는 여기서 만들지 않는다(E4-5) — Editor 세션이 소유하고
        // Host가 뷰 요청에 실어 넘긴다. Core는 씬 오버레이 뷰 판정도 하지 않는다.

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

        if (!state.StartRenderThread(outError))
        {
            state.runtimeInitialized = false;
            state.enabled = false;
            state.lastError = outError;
            return false;
        }

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

RenderScene* EnhancedSceneRenderer::GetRenderScene()
{
    return GetLiveState().renderScene.get();
}

void EnhancedSceneRenderer::SetActiveScene(Scene* scene)
{
    LiveState& state = GetLiveState();
    if (state.renderScene)
    {
        if (state.renderScene->GetScene() != scene) ++state.sceneEpoch;
        state.renderScene->SetScene(scene, state.sceneEpoch);
    }
}

void EnhancedSceneRenderer::SetGizmoIconTextures(
    std::shared_ptr<const EnhancedGizmoIconTextures> textures)
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.gizmoIconMutex);
    state.gizmoIconTextures = std::move(textures);
}

void EnhancedSceneRenderer::SetRenderFeatureContributor(
    std::shared_ptr<IRenderFeatureContributor> contributor)
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.featureContributorMutex);
    state.featureContributor = std::move(contributor);
}

void EnhancedSceneRenderer::SetDisplayPresentationSink(
    std::shared_ptr<IDisplayPresentationSink> sink)
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.presentationSinkMutex);
    state.presentationSink = std::move(sink);
}

bool EnhancedSceneRenderer::SetSkyBoxPath(const std::string& path, std::string& outError)
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
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
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
    state.enabled = true;
    state.lastError.clear();
}

EnhancedLiveBackend EnhancedSceneRenderer::GetLiveBackend()
{
    return GetLiveState().backend;
}

void EnhancedSceneRenderer::DisableLive()
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
    if (state.runtimeInitialized)
    {
        state.lastError = "Enhanced-only 모드에서는 메인 렌더러를 끌 수 없다";
        return;
    }
    state.enabled = false;
    state.TeardownPipeline();
    state.TeardownVulkanPipeline();
}

bool EnhancedSceneRenderer::IsLiveEnabled()
{
    return GetLiveState().enabled;
}

void EnhancedSceneRenderer::WaitForLiveGpu()
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
    if (nullptr != state.pipeline && state.dx12.IsInitialized())
    {
        state.dx12.WaitForGpu();
    }
    if (nullptr != state.vulkanPipeline && state.vulkanPipeline->resources.IsInitialized())
    {
        state.vulkanPipeline->resources.WaitForGpu();
    }
}

EnhancedLiveFramePacket EnhancedSceneRenderer::BuildLiveFramePacket(
    float deltaSeconds, const EnhancedLiveViewRequest* views, uint32_t viewCount,
    bool sceneLoading)
{
    LiveState& state = GetLiveState();
    const std::thread::id currentThread = std::this_thread::get_id();
    if (state.frameProducerThread == std::thread::id{})
        state.frameProducerThread = currentThread;
    assert(state.frameProducerThread == currentThread &&
        "BuildLiveFramePacket must stay on the game-thread producer");

    EnhancedLiveFramePacket frame{};
    std::shared_ptr<const EnhancedGizmoIconTextures> gizmoIconTextures;
    {
        std::lock_guard<std::mutex> lock(state.gizmoIconMutex);
        gizmoIconTextures = state.gizmoIconTextures;
    }
    frame.frameId = ++state.publishedFrameId;
    frame.sceneEpoch = state.sceneEpoch;
    frame.deltaSeconds = deltaSeconds;
    state.publishedTotalSeconds += deltaSeconds;
    frame.totalSeconds = state.publishedTotalSeconds;
    frame.sceneLoading = sceneLoading;
    frame.width = ScreenResizeBus::Get().GetWidth();
    frame.height = ScreenResizeBus::Get().GetHeight();
    if (frame.width != state.publishedWidth ||
        frame.height != state.publishedHeight)
    {
        state.publishedWidth = frame.width;
        state.publishedHeight = frame.height;
        ++state.resizeGeneration;
    }
    frame.resizeGeneration = state.resizeGeneration;

    const uint32_t inputCount = nullptr != views
        ? (std::min)(viewCount, kMaxLiveCameraViews) : 0u;
    const bool collectColliders = 0 != inputCount && ShouldCollectGizmoColliders();
    for (uint32_t i = 0; i < inputCount; ++i)
    {
        Camera* camera = views[i].camera;
        if (nullptr == camera || camera->m_cameraIndex < 0) continue;

        EnhancedLiveViewPacket& view = frame.views[frame.viewCount++];
        view.key.cameraIndex = camera->m_cameraIndex;
        view.key.generation = camera->m_generation;
        // Host가 선언한 값이다(E4-5) — 예전에는 Core 소유 카메라와의 항등
        // 비교였다.
        view.isEditorView = views[i].sceneOverlayView;
        view.camera.view = camera->CalculateView();
        view.camera.projection = camera->CalculateProjection();
        view.camera.inverseView = XMMatrixInverse(nullptr, view.camera.view);
        view.camera.inverseProjection =
            XMMatrixInverse(nullptr, view.camera.projection);
        view.camera.eyePosition = camera->m_eyePosition;
        view.camera.forward = camera->m_forward;
        view.camera.right = camera->m_right;
        view.camera.up = camera->m_up;
        view.camera.fov = camera->m_fov;
        view.camera.nearPlane = camera->m_nearPlane;
        view.camera.farPlane = camera->m_farPlane;
        view.camera.isOrthographic = camera->m_isOrthographic;

        // Legacy consumers latch the same snapshot that the packet carries.
        if (RenderPassData* passData = RenderPassData::GetData(camera))
            passData->PublishFrameSnapshot(view.camera);

        auto gizmos = std::make_shared<EnhancedGizmoSceneData>();
        CaptureEnhancedGizmoSceneData(view.camera, collectColliders,
            gizmoIconTextures, *gizmos);
        view.gizmos = std::move(gizmos);
    }

    return frame;
}

bool EnhancedSceneRenderer::PublishLiveFrame(EnhancedLiveFramePacket frame)
{
    LiveState& state = GetLiveState();
    LiveState::FrameSubmission submission{};
    submission.frame = std::move(frame);
    submission.deltas = ProxyCommandQueue->CapturePending();
    return state.PublishFrame(std::move(submission));
}

void EnhancedSceneRenderer::StopLiveRenderThread()
{
    GetLiveState().StopRenderThread();
}

EnhancedRenderThreadStats EnhancedSceneRenderer::GetLiveRenderThreadStats()
{
    return GetLiveState().GetRenderThreadStats();
}

void EnhancedSceneRenderer::TickLive(const EnhancedLiveFramePacket& frame)
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
    const std::thread::id currentThread = std::this_thread::get_id();
    if (state.frameConsumerThread == std::thread::id{})
        state.frameConsumerThread = currentThread;
    assert(state.frameConsumerThread == currentThread &&
        "TickLive consumer changed threads without an ownership handoff");
    assert(frame.frameId > state.consumedFrameId &&
        "RenderFramePacket must be consumed exactly once in publication order");
    state.consumedFrameId = frame.frameId;
    const uint32_t cameraCount = (std::min)(frame.viewCount, kMaxLiveCameraViews);
    const bool sceneLoading = frame.sceneLoading;
    state.BeginDisplaySnapshot(frame);

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

    if (!state.enabled)
    {
        ProxyCommandQueue->DeferBatch(std::move(state.activeDeltaBatch));
        return;
    }

    // 모든 뷰가 producer가 밀봉한 동일한 시간축을 본다.
    state.totalSeconds = frame.totalSeconds;

    // ── [프레임당 1회] 씬·프록시 갱신 ──
    //
    // SceneRenderer::CreateCommandListPass/EndOfFrame에서 이관한 프레임 입력
    // 단계. App이 두 렌더 배리어를 모두 지난 뒤 호출하므로 씬 구조 변경과
    // 에디터 카메라 조작이 끝난 안정된 프레임 경계다. 카메라 수와 무관하게
    // 프록시는 한 번만 민다 — 카메라 값은 이미 frame packet에 밀봉돼 있다.
    if (state.runtimeInitialized && state.renderScene && !sceneLoading)
    {
		if (state.renderScene->BeginProxyFrame(frame.sceneEpoch))
			ProxyCommandQueue->ExecuteBatch(*state.renderScene, frame.sceneEpoch,
				std::move(state.activeDeltaBatch));
		else
			ProxyCommandQueue->DeferBatch(std::move(state.activeDeltaBatch));
        state.renderScene->EraseRenderPassData();

        // 프록시가 확정된 뒤에 드로우 풀을 모은다. 카메라를 보지 않는
        // 일이라 뷰 루프 밖이고, 뷰는 이 풀을 절두체로 거르기만 한다
        // (RenderSceneViewPlan ③).
        state.BuildDrawPool();
    }
    else
    {
        // 로딩 중에는 frame은 건너뛰어도 delta를 잃으면 안 된다. consumer 보류
        // 큐에 두었다가 다음 renderable packet에서 epoch 규칙과 함께 적용한다.
        ProxyCommandQueue->DeferBatch(std::move(state.activeDeltaBatch));
        // 이번 프레임에 갱신하지 않았다면 풀을 비운다.
        //
        // ★ 풀은 Mesh*·Texture* 원시 포인터를 든다. 프록시 스냅샷이
        //   shared_ptr로 수명을 붙드는 것은 BuildDrawPool이 도는 동안뿐이라,
        //   갱신을 건너뛴 채 남겨 두면 씬이 언로드된 뒤에도 그 주소를 든
        //   목록이 살아 있다. "이번 프레임에 모은 것만 그린다"로 두면
        //   그 부류가 성립하지 않는다.
        state.drawPool.clear();
        state.decals.clear();
    }

    // ── Vulkan editor TickLive 공통 scene graph 경로 ──
    //
    // 씬/프록시 수집과 카메라별 밀봉은 위의 백엔드 중립 경로를 그대로 쓴다.
    // 갈리는 것은 backend 자원과 표시 슬롯뿐이다. DX12와 같은 LivePipelineDesc가
    // Vulkan services/encoder/cache를 받고, 완료된 timeline 슬롯만 CPU 표시
    // 브리지로 승격한다.
    if (EnhancedLiveBackend::Vulkan == state.backend)
    {
        if (state.vulkanPipeline)
        {
            uint64_t promoted = 0;
            std::string validation;
            state.vulkanPipeline->PromoteCompleted(
                state.CopyPresentationSink(), promoted, validation);
            state.PublishVulkanDisplayResults();
            state.framesRendered += promoted;
            if (0 != promoted && !state.vulkanFirstFrameReported)
            {
                state.vulkanFirstFrameReported = true;
                const VulkanMeshCache::Stats meshStats =
                    state.vulkanPipeline->meshCache.GetStats();
                Debug->LogWarning("[vulkan.live] editor TickLive 첫 프레임 완성"
                    " · 공통 LivePipelineDesc→live_present"
                    " · draw " + std::to_string(state.lastDrawCount) +
                    " / batch " + std::to_string(state.lastBatchCount) +
                    " · mesh resident " + std::to_string(meshStats.residentCount) +
                    " / upload " + std::to_string(meshStats.uploads) +
                    " / failure " + std::to_string(meshStats.failures) +
                    " · parallel workers " +
                    std::to_string(state.vulkanPipeline->lastGraphStats.recordWorkers) +
                    " / batch " +
                std::to_string(state.vulkanPipeline->lastGraphStats.recordedLists) +
                    " · persistent segment " +
                    std::to_string(meshStats.persistentHeap.activeSegments));
            }
            if (!validation.empty() && state.reportedValidation.insert(validation).second)
            {
                std::printf("[vulkan.live 검증] %s\n", validation.c_str());
                Debug->LogError("[vulkan.live 검증] " + validation);
            }
        }

        if (sceneLoading || 0 == cameraCount)
        {
            ++state.framesIdle;
            return;
        }

        const uint32_t rtWidth = frame.width;
        const uint32_t rtHeight = frame.height;
        if (0 == rtWidth || 0 == rtHeight)
        {
            ++state.framesIdle;
            return;
        }

        if (state.vulkanPipeline &&
            (rtWidth != state.vulkanPipeline->width || rtHeight != state.vulkanPipeline->height))
        {
            state.TeardownVulkanPipeline();
        }
        if (!state.vulkanPipeline)
        {
            std::string error;
            if (!state.BuildVulkanPipeline(rtWidth, rtHeight, error))
            {
                state.lastError = "Vulkan 파이프라인 구축 실패: " + error;
                state.enabled = false;
                Debug->LogError("[EnhancedRenderer] " + state.lastError);
                return;
            }
        }

        VulkanLivePipeline& p = *state.vulkanPipeline;
        uint32_t totalPending = p.PendingCount();
        LiveStopwatch watch;
        watch.Start();
        bool renderedAny = false;
        const uint32_t startIndex = state.viewRotation++ % cameraCount;

        for (uint32_t step = 0; step < cameraCount; ++step)
        {
            const EnhancedLiveViewPacket& viewPacket =
                frame.views[(startIndex + step) % cameraCount];
            if (!viewPacket.key.IsValid()) continue;
            if (totalPending >= 2)
            {
                ++state.framesInFlight;
                continue;
            }

            const int viewIndex = p.FindOrAssignView(viewPacket, frame);
            if (viewIndex < 0)
            {
                ++state.viewOverflowSkips;
                continue;
            }
            if (!state.CaptureFromView(frame, viewPacket))
            {
                ++state.framesIdle;
                continue;
            }

            std::string error;
            const auto prepareFrame = [&state, &p, viewIndex](std::string& prepareError)
            {
                return state.PreparePipelineFrame(
                    p, static_cast<uint32_t>(viewIndex), prepareError);
            };
            if (!p.Render(static_cast<uint32_t>(viewIndex), viewPacket,
                frame.frameId,
                GetRHISubmissionThread().GetOwnerGeneration(&p.resources),
                prepareFrame, error))
            {
                std::string validation;
                p.resources.DrainDebugMessages(validation);
                const uint32_t unimplemented = p.resources.GetUnimplementedCount()
                    + p.resources.GetEncoderUnimplementedCount()
                    + p.commandPool.GetEncoderUnimplementedCount();
                if (!validation.empty()) error += "\n" + validation;
                if (0 != unimplemented)
                {
                    error += "\nVulkan 미구현 호출 " + std::to_string(unimplemented);
                }
                state.lastError = "Vulkan 프레임 실패: " + error;
                std::printf("[vulkan.live 실패] %s\n", state.lastError.c_str());
                ++state.frameFailures;
                ++state.consecutiveFrameFailures;
                if (state.reportedValidation.insert(state.lastError).second)
                    Debug->LogError("[EnhancedRenderer] " + state.lastError);
                if (LiveState::kMaxConsecutiveFrameFailures <= state.consecutiveFrameFailures)
                {
                    state.TeardownVulkanPipeline();
                    state.enabled = false;
                    return;
                }
                continue;
            }

            state.consecutiveFrameFailures = 0;
            state.lastDrawCount = p.gbuffer.GetLastDrawCount();
            state.lastBatchCount = p.gbuffer.GetLastBatchCount();
            state.lastDecalCount = p.decal.GetLastDecalCount();
            state.lastDecalBatchCount = p.decal.GetLastBatchCount();
            state.lastSpriteCount = p.sprite.GetLastItemCount();
            state.lastSpriteBatchCount = p.sprite.GetLastBatchCount();
            state.lastUIRectCount = p.ui.GetLastRectCount();
            state.lastUIBatchCount = p.ui.GetLastBatchCount();
            const uint32_t targetIndex = LiveState::DisplayTargetIndex(
                viewPacket.isEditorView);
            state.viewSpriteCounts[targetIndex] = state.lastSpriteCount;
            state.viewUICounts[targetIndex] = state.lastUIRectCount;
            state.lastGpuMs = 0.0; // Vulkan timestamp profiler는 후속 성능 슬라이스
            state.lastPassTimings = {
                { "Shadow", 0.0 }, { "GBuffer", 0.0 },
                { "Decal", 0.0 }, { "SSAO.Compute", 0.0 }, { "SSAO.Filter", 0.0 },
                { "Deferred", 0.0 }, { "SkyBox", 0.0 },
                { "SSGI.HiZ", 0.0 }, { "SSGI.Trace", 0.0 },
                { "SSGI.Resolve", 0.0 }, { "SSGI.Filter", 0.0 },
                { "SSGI.Composite", 0.0 }, { "SSGI.StoreHistory", 0.0 },
                { "Forward+", 0.0 }, { "Sprite", 0.0 },
                { "SSS", 0.0 }, { "SSR", 0.0 },
                { "VolumetricFog", 0.0 }, { "PostChain", 0.0 }, { "UI", 0.0 },
                { "Grid", 0.0 }, { "WireFrame", 0.0 },
                { "GizmoIcon", 0.0 }, { "GizmoLine", 0.0 },
                { "live_present", 0.0 } };
            ++totalPending;
            state.AddNativeRecordSample(p.lastNativeRecordMs);
            renderedAny = true;
        }

        if (renderedAny) state.lastCpuMs = watch.ElapsedMs();
        return;
    }

    // 인플라이트 제출분의 완료 확인(논블로킹) — 뷰마다. 제출 순서대로,
    // 완료된 것을 전부 표시로 승격한다 — DX11이 읽는 슬롯은 항상 '펜스가
    // 끝난' 슬롯뿐이라는 것이 비동기의 계약이다.
    if (nullptr != state.pipeline)
    {
        LivePipeline& p = *state.pipeline;

        // ── 자산 상주 관리 (②-b · ③) ──
        //
        // 셋 다 같은 규약이다: 펜스가 지나야 놓는다. 슬롯 승격이 바로 아래에서
        // 같은 판정을 하고 있고, 그 옆에 두는 것이 규약을 하나로 유지하는
        // 방법이다 — "GPU 유휴 시점에 부르라"를 주석에만 적어 두었더니
        // 아무도 안 불러 128MB가 종료까지 잡혀 있었다(실측).
        // 프레임 번호 통지, pressure 은퇴, 펜스 완료 묘지 sweep은 backend가
        // 자기 캐시 구현을 아는 한 경계에서 수행한다.
        state.dx12.MaintainAssetCaches(state.frameCounter);

        for (LivePipeline::CameraView& view : p.views)
        {
            while (!view.pendingQueue.empty())
            {
                const int slotIndex = view.pendingQueue.front();
                if (state.dx12.GetCompletedFenceValue() <
                    view.slots[slotIndex].fenceValue)
                {
                    break;
                }

                const bool belongsToView =
                    view.slots[slotIndex].key == view.key;
                if (belongsToView)
                {
                    std::lock_guard<std::mutex> displayLock(
                        state.displayLifetimeMutex);
                    view.displaySlot = slotIndex;
                    ++view.promotionCount;
                    view.promotedSlotMask |=
                        (1u << static_cast<uint32_t>(slotIndex));
                    state.PublishDisplayResultLocked(view.isEditorView, view.key,
                        view.slots[slotIndex].interopToken,
                        view.slots[slotIndex].frameId, view.promotionCount,
                        view.promotedSlotMask);
                }
                view.pendingQueue.erase(view.pendingQueue.begin());
                view.slots[slotIndex].graph.reset();   // GPU가 끝났다 — transient가 풀로 돌아간다
                view.slots[slotIndex].key = {};

#if defined(_DEBUG)
                // ★ 검증 레이어를 읽는다. Debug 빌드에서 배선 오류(포맷·상태·
                //   디스크립터)는 여기에만 남는데 아무도 안 읽으면 증상만 보고
                //   추측하게 된다 — 실제로 그 상태로 며칠을 쫓았다.
                {
                    std::string validation;
                    if (0 != state.dx12.DrainDebugMessages(validation) &&
                        !validation.empty())
                    {
                        if (state.reportedValidation.insert(validation).second)
                        {
                            std::printf("[dx12.live 검증] %s\n", validation.c_str());
                        }
                    }
                }
#endif

                std::vector<EnhancedLivePassTiming> timings;
                std::string collectError;
                double totalMilliseconds = 0.0;
                if (state.dx12.CollectProfiler(
                    timings, totalMilliseconds, collectError))
                {
                    state.lastGpuMs = totalMilliseconds;
                    // 패스별 시간은 예전에는 여기서 버려졌다 — 합계만 남기면
                    // "느려졌다"까지만 알 수 있고 어느 패스인지는 알 수 없다.
                    // 렌더 디버그 창이 읽도록 마지막 성공분을 보관한다.
                    state.lastPassTimings = std::move(timings);
                }
                ++state.framesRendered;
            }
        }
    }

    if (sceneLoading || 0 == cameraCount)
    {
        ++state.framesIdle;
        return;
    }

    // 해상도는 밀봉이 카메라가 아니라 화면 크기 버스에서 가져오므로
    // 모든 뷰가 공유한다 — 재구축 판정도 프레임당 한 번이면 된다.
    // 크기가 바뀌면 크기 의존 파이프라인만 다시 세운다. 패스들은 Initialize에서
    // 화면 크기 리소스를 잡지만 backend 디바이스·PSO/루트 시그니처·자산 캐시는
    // 해상도와 무관하다. 그것까지 재부팅하면 두 번째 PSO 초기화가 장시간 CPU를
    // 점유하므로 GPU 완료 뒤 유지한 backend 위에 패스와 표시 슬롯만 다시 만든다.
    const uint32_t rtWidth = frame.width;
    const uint32_t rtHeight = frame.height;
    if (0 == rtWidth || 0 == rtHeight)
    {
        ++state.framesIdle;
        return;
    }

    if (state.pipeline &&
        (rtWidth != state.pipeline->width || rtHeight != state.pipeline->height))
    {
        std::string resizeError;
        if (!state.dx12.Resize(rtWidth, rtHeight, resizeError))
        {
            state.lastError = "DX12 resize lifecycle 실패: " + resizeError;
            Debug->LogError("[EnhancedRenderer] " + state.lastError);
            state.TeardownPipeline();
            state.enabled = false;
            return;
        }
        state.TeardownPipeline(true, true);
    }

    if (nullptr == state.pipeline)
    {
        std::string error;
        if (!state.BuildPipeline(rtWidth, rtHeight, error))
        {
            state.lastError = "파이프라인 구축 실패: " + error;
            Debug->LogError("[EnhancedRenderer] " + state.lastError);
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
        const uint32_t packetViewIndex = (startIndex + step) % cameraCount;
        const EnhancedLiveViewPacket& viewPacket = frame.views[packetViewIndex];
        if (!viewPacket.key.IsValid()) continue;

        if (totalPending >= 2)
        {
            ++state.framesInFlight;
            continue;
        }

        // 뷰 배정: 같은 카메라의 뷰 → 빈 뷰 → 이번 틱 목록에 없는 카메라의
        // 뷰(교체) 순으로 찾는다. 교체 시 표시 슬롯을 비우고, 각 제출 슬롯에
        // 기록한 view key가 현재 key와 다르면 완료 뒤에도 승격하지 않는다.
        LivePipeline::CameraView* view = nullptr;
        for (LivePipeline::CameraView& candidate : p.views)
        {
            if (candidate.key == viewPacket.key)
            {
                view = &candidate;
                break;
            }
        }
        if (nullptr == view)
        {
            for (LivePipeline::CameraView& candidate : p.views)
            {
                if (!candidate.key.IsValid()) { view = &candidate; break; }
            }
        }
        if (nullptr == view)
        {
            for (LivePipeline::CameraView& candidate : p.views)
            {
                bool inList = false;
                for (uint32_t j = 0; j < cameraCount; ++j)
                {
                    if (candidate.key == frame.views[j].key)
                    {
                        inList = true;
                        break;
                    }
                }
                if (!inList) { view = &candidate; break; }
            }
        }
        if (nullptr == view)
        {
            // 카메라가 kMaxCameraViews를 넘는다. 조용히 버리면 "카메라가
            // 있는데 화면이 없다"로만 드러나므로 수를 센다(status가 낸다).
            ++state.viewOverflowSkips;
            continue;
        }

        if (view->key != viewPacket.key)
        {
            std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
            view->key = viewPacket.key;
            view->isEditorView = viewPacket.isEditorView;
            view->displaySlot = -1;
            view->promotionCount = 0;
            view->promotedSlotMask = 0;
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

        if (!state.CaptureFromView(frame, viewPacket))
        {
            ++state.framesIdle;
            continue;
        }

        std::string error;
        if (!state.RenderOnce(*view, renderSlot, frame.frameId, error))
        {
            // 한 프레임 실패로 렌더러를 접지 않는다.
            //
            // 예전에는 여기서 바로 TeardownPipeline + enabled=false 였고,
            // 실패 사유는 lastError에만 남았다(로그로 안 갔다). 그래서 업로드
            // 링이 한 프레임 모자란 것 같은 *일시적* 사정 하나가 렌더러를
            // 영구히 끄고, 화면에는 "검은 씬 + 오류 0건 + 높은 FPS"로만
            // 나타났다 — 스폰자 배치에서 실제로 그랬고, 원인이 여기라는 것을
            // 알아내는 데 대부분의 시간이 들었다.
            //
            // 이제 사유를 남기고 이 프레임만 건너뛴다. 다음 프레임에 다시
            // 해 본다 — 링이 되감기므로 대개 그때는 통과한다.
            state.lastError = "프레임 실패: " + error;
            ++state.frameFailures;
            ++state.consecutiveFrameFailures;

            // 같은 문장이 매 프레임 반복되므로 처음 본 것만 찍는다
            // (검증 레이어 메시지와 같은 규약 — 안 그러면 콘솔이 도배돼
            // 정작 첫 원인을 못 본다).
            if (state.reportedValidation.insert(state.lastError).second)
            {
                Debug->LogError("[EnhancedRenderer] " + state.lastError);
            }

            if (LiveState::kMaxConsecutiveFrameFailures <= state.consecutiveFrameFailures)
            {
                Debug->LogError("[EnhancedRenderer] 프레임 실패가 "
                    + std::to_string(state.consecutiveFrameFailures)
                    + "회 연속이라 파이프라인을 접는다. 마지막 사유: " + error);
                state.TeardownPipeline();
                state.enabled = false;
                return;
            }
            continue;
        }
        state.consecutiveFrameFailures = 0;
        ++totalPending;
        state.AddNativeRecordSample(p.lastNativeRecordMs);
        renderedAny = true;
    }

    if (renderedAny) state.lastCpuMs = watch.ElapsedMs();
}


EnhancedLiveDisplaySnapshot EnhancedSceneRenderer::GetLiveDisplaySnapshot()
{
    const LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
    return state.displaySnapshot;
}

uint64_t EnhancedSceneRenderer::GetLiveDisplayImTextureId(
    EnhancedLiveDisplayTarget target)
{
    // CE는 Camera/backend view를 조회하지 않는다. RT가 발행한 논리 대상의
    // 불투명 key만 열고, resize/teardown과 공유 핸들 수명은 같은 락으로 막는다.
    const LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
    // Host가 설치한 표시 sink가 ID를 해석한다(E4-6a). Core는 표시 수명 락만
    // 소유하고 ImGui 셸을 모른다 — 미설치·비활성이면 표시할 수단이 없다.
    const std::shared_ptr<IDisplayPresentationSink> sink =
        state.CopyPresentationSink();
    if (!sink || !sink->IsActive()) return 0;
    if (!state.enabled) return 0;
    const uint32_t targetIndex = static_cast<uint32_t>(target);
    if (targetIndex >= kEnhancedLiveDisplayTargetCount) return 0;
    const EnhancedLiveDisplayEntrySnapshot& entry =
        state.displaySnapshot.targets[targetIndex];
    const uint64_t presentationKey = state.displayPresentationKeys[targetIndex];
    if (!entry.active || !entry.ready || 0 == presentationKey) return 0;

    if (EnhancedLiveBackend::Vulkan == state.displaySnapshot.backend)
    {
        return sink->GetCpuFrameTextureId(presentationKey);
    }
    return state.dx12.OpenDisplayTexture(*sink, presentationKey);
}

bool EnhancedSceneRenderer::RunLiveDisplayRegression(uint32_t expectedWidth,
    uint32_t expectedHeight, std::string& outLog)
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
    EnhancedLiveDisplaySnapshot displaySnapshot{};
    {
        std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
        displaySnapshot = state.displaySnapshot;
    }
    outLog.clear();

    const auto countBits = [](uint32_t mask)
    {
        uint32_t count = 0;
        while (0 != mask)
        {
            count += mask & 1u;
            mask >>= 1u;
        }
        return count;
    };

    const bool vulkan = EnhancedLiveBackend::Vulkan == state.backend;
    const bool pipelineReady = vulkan
        ? (nullptr != state.vulkanPipeline) : (nullptr != state.pipeline);
    const uint32_t width = vulkan
        ? (state.vulkanPipeline ? state.vulkanPipeline->width : 0u)
        : (state.pipeline ? state.pipeline->width : 0u);
    const uint32_t height = vulkan
        ? (state.vulkanPipeline ? state.vulkanPipeline->height : 0u)
        : (state.pipeline ? state.pipeline->height : 0u);

    // 3-13: 별도 CLI를 만들지 않고 기존 live 종단 회귀에서 실제 활성 backend의
    // 공용 기술을 다시 검증하고 Dump 호출까지 실행한다. 독립 negative test는
    // dx12.selftest 시작부에 있고, 여기는 실물 19-node 배선이 대상이다.
    const LivePipelineDesc* pipelineDesc = vulkan
        ? (state.vulkanPipeline ? &state.vulkanPipeline->desc : nullptr)
        : (state.pipeline ? &state.pipeline->desc : nullptr);
    std::string pipelineValidationError;
    const bool pipelineDescriptionValid = nullptr != pipelineDesc &&
        pipelineDesc->Validate(pipelineValidationError);
    const std::string pipelineDescription = nullptr != pipelineDesc
        ? pipelineDesc->Dump() : std::string{};
    const bool pipelineDescriptionReady = pipelineDescriptionValid &&
        !pipelineDescription.empty() && 0 != pipelineDesc->NodeCount();

    char line[512]{};
    std::snprintf(line, sizeof(line),
        "[8-c] backend=%s enabled=%u pipeline=%s size=%ux%u expected=%ux%u\n",
        vulkan ? "vulkan" : "dx12", state.enabled ? 1u : 0u,
        pipelineReady ? "ready" : "missing", width, height,
        expectedWidth, expectedHeight);
    outLog += line;
    std::snprintf(line, sizeof(line),
        "[3-13] pipeline-desc nodes=%zu dump-bytes=%zu validate=%s%s%s\n",
        nullptr != pipelineDesc ? pipelineDesc->NodeCount() : 0u,
        pipelineDescription.size(), pipelineDescriptionValid ? "pass" : "fail",
        pipelineValidationError.empty() ? "" : " error=",
        pipelineValidationError.empty() ? "" : pipelineValidationError.c_str());
    outLog += line;

    uint32_t activeViews = 0;
    uint32_t readyViews = 0;
    uint32_t rotatedViews = 0;
    uint32_t unimplemented = 0;

    const auto appendView = [&](uint32_t viewIndex, bool ready,
        uint64_t promotions, uint32_t slotMask)
    {
        const bool rotated = promotions >= 2 && countBits(slotMask) >= 2;
        ++activeViews;
        if (ready) ++readyViews;
        if (rotated) ++rotatedViews;

        char viewLine[256]{};
        std::snprintf(viewLine, sizeof(viewLine),
            "[8-c] view%u ready=%u promotions=%llu slot-mask=0x%X rotated=%u\n",
            viewIndex, ready ? 1u : 0u,
            static_cast<unsigned long long>(promotions), slotMask,
            rotated ? 1u : 0u);
        outLog += viewLine;
    };

    for (uint32_t i = 0; i < kEnhancedLiveDisplayTargetCount; ++i)
    {
        const EnhancedLiveDisplayEntrySnapshot& view = displaySnapshot.targets[i];
        if (!view.active) continue;
        appendView(i, view.ready, view.promotionCount, view.promotedSlotMask);
    }

    if (state.vulkanPipeline)
    {
        VulkanLivePipeline& pipeline = *state.vulkanPipeline;
        unimplemented = pipeline.resources.GetUnimplementedCount()
            + pipeline.resources.GetEncoderUnimplementedCount()
            + pipeline.commandPool.GetEncoderUnimplementedCount();
    }

    const bool dimensionsMatch = width == expectedWidth && height == expectedHeight &&
        displaySnapshot.width == expectedWidth &&
        displaySnapshot.height == expectedHeight;
    const bool viewsMatch = activeViews == kMaxLiveCameraViews &&
        readyViews == kMaxLiveCameraViews && rotatedViews == kMaxLiveCameraViews;
    const bool snapshotValid = displaySnapshot.backend == state.backend &&
        0 != displaySnapshot.revision && 0 != displaySnapshot.sourceFrameId;
    std::snprintf(line, sizeof(line),
        "[3-2F] display-snapshot revision=%llu source-frame=%llu resize=%llu"
        " editor=%u/%u game=%u/%u neutral=%u\n",
        static_cast<unsigned long long>(displaySnapshot.revision),
        static_cast<unsigned long long>(displaySnapshot.sourceFrameId),
        static_cast<unsigned long long>(displaySnapshot.resizeGeneration),
        displaySnapshot.Get(EnhancedLiveDisplayTarget::Editor).active ? 1u : 0u,
        displaySnapshot.Get(EnhancedLiveDisplayTarget::Editor).ready ? 1u : 0u,
        displaySnapshot.Get(EnhancedLiveDisplayTarget::Game).active ? 1u : 0u,
        displaySnapshot.Get(EnhancedLiveDisplayTarget::Game).ready ? 1u : 0u,
        snapshotValid ? 1u : 0u);
    outLog += line;
    const bool clean = 0 == state.frameFailures &&
        state.reportedValidation.empty() && 0 == unimplemented;
	std::snprintf(line, sizeof(line),
		"[8-c] views active=%u ready=%u rotated=%u/%u · failures=%llu validation=%zu unimplemented=%u\n",
        activeViews, readyViews, rotatedViews, kMaxLiveCameraViews,
        static_cast<unsigned long long>(state.frameFailures),
		state.reportedValidation.size(), unimplemented);
	outLog += line;
	const auto proxyStats = ProxyCommandQueue->GetStats();
	const bool proxyBalanced = proxyStats.enqueued ==
		proxyStats.applied + proxyStats.dropped + proxyStats.pending;
	std::snprintf(line, sizeof(line),
		"[3-2C] proxy-delta enqueue=%llu apply=%llu drop=%llu pending=%llu balanced=%u\n",
		static_cast<unsigned long long>(proxyStats.enqueued),
		static_cast<unsigned long long>(proxyStats.applied),
		static_cast<unsigned long long>(proxyStats.dropped),
		static_cast<unsigned long long>(proxyStats.pending),
		proxyBalanced ? 1u : 0u);
	outLog += line;

	const EnhancedRenderThreadStats threadStats = state.GetRenderThreadStats();
	const bool threadBalanced = threadStats.published == threadStats.consumed +
		threadStats.coalescedFrames + threadStats.pending + threadStats.inProgress;
	const bool threadHealthy = threadStats.running && threadStats.accepting &&
		threadStats.producerConsumerSeparated &&
		threadStats.highWatermark <= threadStats.capacity && threadBalanced;
	std::snprintf(line, sizeof(line),
		"[3-2E] render-thread publish=%llu consume=%llu latest-wins=%llu"
		" pending=%u active=%u high-water=%u/%u overflow=%llu back-pressure=%llu"
		" delta-coalesce=%llu separated=%u balanced=%u\n",
		static_cast<unsigned long long>(threadStats.published),
		static_cast<unsigned long long>(threadStats.consumed),
		static_cast<unsigned long long>(threadStats.coalescedFrames),
		threadStats.pending, threadStats.inProgress,
		threadStats.highWatermark, threadStats.capacity,
		static_cast<unsigned long long>(threadStats.overflowEvents),
		static_cast<unsigned long long>(threadStats.backPressureWaits),
		static_cast<unsigned long long>(threadStats.coalescedDeltas),
		threadStats.producerConsumerSeparated ? 1u : 0u,
		threadBalanced ? 1u : 0u);
	outLog += line;
	const bool passed = state.runtimeInitialized && state.enabled && pipelineReady &&
		dimensionsMatch && viewsMatch && snapshotValid && clean && proxyBalanced &&
		threadHealthy && pipelineDescriptionReady;
	outLog += std::string("[8-c] verdict=") + (passed ? "pass\n" : "fail\n");
    return passed;
}

std::string EnhancedSceneRenderer::GetLiveStatus()
{
    const LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);

    if (EnhancedLiveBackend::Vulkan == state.backend)
    {
        char line[512]{};
        const VulkanLivePipeline* pipeline = state.vulkanPipeline.get();
        const VulkanMeshCache::Stats meshStats = pipeline
            ? pipeline->meshCache.GetStats() : VulkanMeshCache::Stats{};
        const VulkanTextureCache::Stats textureStats = pipeline
            ? pipeline->textureCache.GetStats() : VulkanTextureCache::Stats{};
        std::snprintf(line, sizeof(line),
            "EnhancedRenderer(Vulkan) — %s · 공통 scene graph %s · %ux%u"
            " · 완성 %llu프레임(대기 %llu) · 인플라이트 %u · 드로우 %u(배치 %u)"
            " · CPU %.2f ms · 메시 캐시 %u개/업로드 %u/실패 %u",
            state.enabled ? "켜짐" : "꺼짐",
            pipeline ? "준비됨" : "없음",
            pipeline ? pipeline->width : 0u,
            pipeline ? pipeline->height : 0u,
            static_cast<unsigned long long>(state.framesRendered),
            static_cast<unsigned long long>(state.framesIdle),
            pipeline ? pipeline->PendingCount() : 0u,
            state.lastDrawCount, state.lastBatchCount, state.lastCpuMs,
            meshStats.residentCount, meshStats.uploads, meshStats.failures);

        std::string status = line;
        char recordLine[192]{};
        std::snprintf(recordLine, sizeof(recordLine),
            "\n  Native record CPU — last %.3f ms · avg %.3f ms · max %.3f ms · samples %llu",
            state.lastNativeRecordMs, state.AverageNativeRecordMs(),
            state.maxNativeRecordMs,
            static_cast<unsigned long long>(state.nativeRecordSamples));
        status += recordLine;
        status += "\n  데칼 " + std::to_string(state.lastDecalCount) +
            "개(배치 " + std::to_string(state.lastDecalBatchCount) + ")";
        status += " · 스프라이트 " + std::to_string(state.lastSpriteCount) +
            "개(배치 " + std::to_string(state.lastSpriteBatchCount) + ")" +
            " · 화면 UI " + std::to_string(state.lastUIRectCount) +
            "개(배치 " + std::to_string(state.lastUIBatchCount) + ")";
        status += "\n  뷰별 UI — Scene S/UI " +
            std::to_string(state.viewSpriteCounts[static_cast<uint32_t>(
                EnhancedLiveDisplayTarget::Editor)]) + "/" +
            std::to_string(state.viewUICounts[static_cast<uint32_t>(
                EnhancedLiveDisplayTarget::Editor)]) + " · Game S/UI " +
            std::to_string(state.viewSpriteCounts[static_cast<uint32_t>(
                EnhancedLiveDisplayTarget::Game)]) + "/" +
            std::to_string(state.viewUICounts[static_cast<uint32_t>(
                EnhancedLiveDisplayTarget::Game)]);
        status += "\n  자산 수명 — 텍스처 상주 " +
            std::to_string(textureStats.residentCount) + "개 · 메시 상주 " +
            std::to_string(meshStats.residentCount) + "개 · 묘지 " +
            std::to_string(textureStats.graveyardCount + meshStats.graveyardCount) +
            "개 · 격리 " +
             std::to_string(textureStats.quarantinedCount + meshStats.quarantinedCount) + "개";
        constexpr double kVulkanPersistentMB = 1024.0 * 1024.0;
        const RHIPersistentHeapStats& bufferHeap = meshStats.persistentHeap;
        const RHIPersistentHeapStats& textureHeap = textureStats.persistentHeap;
        const RHIPersistentHeapStats& budgetStats = 0 != textureHeap.budgetBytes
            ? textureHeap : bufferHeap;
        const RHIDeviceMemoryBudgetCoordinatorStats coordinatorStats = pipeline
            ? pipeline->resources.GetPersistentMemoryBudgetStats()
            : RHIDeviceMemoryBudgetCoordinatorStats{};
        char heapLine[768]{};
        std::snprintf(heapLine, sizeof(heapLine),
            "\n  Persistent heap — buffer %u segment %.1f/%.1f MB · texture %u segment %.1f/%.1f MB"
            " · dedicated live %u · trim %llu · fallback %llu"
            "\n  Device-local budget — %.1f/%.1f MB · %s · pressure %s"
            "\n  Budget coordinator — owner %u · ticket grant/deny %llu/%llu"
            " · pending/committed %.1f/%.1f MB · snapshot %llu",
            bufferHeap.activeSegments, bufferHeap.allocatedBytes / kVulkanPersistentMB,
            bufferHeap.segmentBytes / kVulkanPersistentMB,
            textureHeap.activeSegments, textureHeap.allocatedBytes / kVulkanPersistentMB,
            textureHeap.segmentBytes / kVulkanPersistentMB,
            bufferHeap.liveDedicatedAllocations + textureHeap.liveDedicatedAllocations,
            static_cast<unsigned long long>(bufferHeap.trimmedSegments +
                textureHeap.trimmedSegments),
            static_cast<unsigned long long>(bufferHeap.dedicatedFallbacks +
                textureHeap.dedicatedFallbacks),
            budgetStats.budgetUsageBytes / kVulkanPersistentMB,
            budgetStats.budgetBytes / kVulkanPersistentMB,
            budgetStats.budgetEstimated ? "heap-size estimate" : "VK_EXT_memory_budget",
            (bufferHeap.memoryPressure || textureHeap.memoryPressure) ? "ON" : "off",
            coordinatorStats.registeredOwners,
            static_cast<unsigned long long>(coordinatorStats.growthGrants),
            static_cast<unsigned long long>(coordinatorStats.growthDenials),
            coordinatorStats.reservedGrowthBytes / kVulkanPersistentMB,
            coordinatorStats.committedSinceRefreshBytes / kVulkanPersistentMB,
            static_cast<unsigned long long>(coordinatorStats.snapshotRefreshes));
        status += heapLine;
        char evictionLine[256]{};
        std::snprintf(evictionLine, sizeof(evictionLine),
            "\n  Pressure eviction — pass %llu · 퇴출 %llu개 %.1f MB"
            " · recent 보호 %llu · upload-pending 보호 %llu",
            static_cast<unsigned long long>(textureStats.eviction.pressurePasses +
                meshStats.eviction.pressurePasses),
            static_cast<unsigned long long>(textureStats.eviction.pressureRetired +
                meshStats.eviction.pressureRetired),
            (textureStats.eviction.pressureRetiredBytes +
                meshStats.eviction.pressureRetiredBytes) / kVulkanPersistentMB,
            static_cast<unsigned long long>(
                textureStats.eviction.pressureProtectedRecent +
                meshStats.eviction.pressureProtectedRecent),
            static_cast<unsigned long long>(
                textureStats.eviction.pressureUploadPending +
                meshStats.eviction.pressureUploadPending));
        status += evictionLine;
        const uint32_t unimplemented = pipeline
            ? pipeline->resources.GetUnimplementedCount()
                + pipeline->resources.GetEncoderUnimplementedCount()
                + pipeline->commandPool.GetEncoderUnimplementedCount()
            : 0u;
        if (pipeline)
        {
            const EnhancedRenderGraph::Stats& graphStats = pipeline->lastGraphStats;
            status += "\n  병렬 기록 — worker " +
                std::to_string(graphStats.recordWorkers) + " · batch " +
            std::to_string(graphStats.recordedLists) + " · unit " +
                std::to_string(graphStats.recordUnits) +
                (graphStats.parallelDeclined ? " · 순차 전환" : "");
        }
        status += "\n  Vulkan 검증 " +
            std::to_string(state.reportedValidation.size()) + " · 미구현 " +
            std::to_string(unimplemented);
        if (!state.lastPassTimings.empty())
        {
            status += "\n  패스: ";
            for (size_t i = 0; i < state.lastPassTimings.size(); ++i)
            {
                if (0 != i) status += " → ";
                status += state.lastPassTimings[i].name;
            }
        }
        {
            const auto sink = state.CopyPresentationSink();
            status += std::string("\n  표시: Vulkan LDR readback → CPU upload → ") +
                (sink ? sink->GetName() : "none") +
                " presentation texture (external-memory 직접 공유는 후속 성능 단계)";
        }
		status += "\n  프레임 패킷 — publish " +
			std::to_string(state.publishedFrameId) + " / consume " +
			std::to_string(state.consumedFrameId) + " · scene epoch " +
			std::to_string(state.sceneEpoch) + " · resize generation " +
			std::to_string(state.resizeGeneration);
		const auto proxyStats = ProxyCommandQueue->GetStats();
		status += "\n  프록시 delta — enqueue " +
			std::to_string(proxyStats.enqueued) + " / apply " +
			std::to_string(proxyStats.applied) + " / drop " +
			std::to_string(proxyStats.dropped) + " / pending " +
			std::to_string(proxyStats.pending) + " (stale " +
			std::to_string(proxyStats.staleEpoch) + " / missing " +
			std::to_string(proxyStats.missingTarget) + " / failed " +
			std::to_string(proxyStats.failed) + " / superseded " +
			std::to_string(proxyStats.superseded) + " / shutdown " +
			std::to_string(proxyStats.shutdownDiscarded) + ")";
		const EnhancedRenderThreadStats threadStats = state.GetRenderThreadStats();
		status += "\n  RenderThread queue — publish " +
			std::to_string(threadStats.published) + " / consume " +
			std::to_string(threadStats.consumed) + " / latest-wins " +
			std::to_string(threadStats.coalescedFrames) + " / pending " +
			std::to_string(threadStats.pending) + " + active " +
			std::to_string(threadStats.inProgress) + " · high-water " +
			std::to_string(threadStats.highWatermark) + "/" +
			std::to_string(threadStats.capacity) + " · overflow " +
			std::to_string(threadStats.overflowEvents) + " · back-pressure " +
			std::to_string(threadStats.backPressureWaits) + " · delta-coalesce " +
			std::to_string(threadStats.coalescedDeltas) +
			(threadStats.producerConsumerSeparated ? " · GT/RT 분리" : " · GT/RT 미확정");
        const RHISubmissionThreadStats rhiStats =
            GetRHISubmissionThread().GetStats();
        status += "\n  RHI submit queue — enqueue " +
            std::to_string(rhiStats.enqueued) + " / execute " +
            std::to_string(rhiStats.executed) + " · pending task/batch/retirement " +
            std::to_string(rhiStats.pendingTasks) + "/" +
            std::to_string(rhiStats.pendingBatches) + "/" +
            std::to_string(rhiStats.pendingRetirements) + " · high-water " +
            std::to_string(rhiStats.maxQueueDepth) + "/" +
            std::to_string(RHISubmissionThread::kQueueCapacity) +
            " · saturation " + std::to_string(rhiStats.saturationWaits) +
            " · retire " + std::to_string(rhiStats.retired) +
            " · lifecycle " + std::to_string(rhiStats.lifecycleCommands) +
            "/" + std::to_string(rhiStats.lifecycleFailures) +
            " · failure/order " + std::to_string(rhiStats.failed) + "/" +
            std::to_string(rhiStats.orderingErrors) +
            (rhiStats.running && 0 != rhiStats.threadIdHash
                ? " · dedicated thread" : " · thread stopped");
        char producerWaitLine[160]{};
        std::snprintf(producerWaitLine, sizeof(producerWaitLine),
            "\n  RHI producer stall — total %.3f ms · max %.3f ms",
            static_cast<double>(rhiStats.producerWaitNanoseconds) / 1.0e6,
            static_cast<double>(rhiStats.maxProducerWaitNanoseconds) / 1.0e6);
        status += producerWaitLine;
        if (!state.lastError.empty()) status += "\n  마지막 오류: " + state.lastError;
        return status;
    }

    char line[384]{};
    std::snprintf(line, sizeof(line),
        "EnhancedRenderer(DX12) — %s · 파이프라인 %s · %ux%u · 렌더 %llu프레임(대기 %llu)"
        " · 인플라이트 스킵 %llu · 드로우 %u(배치 %u) · CPU %.2f ms · GPU %.2f ms"
        " · 외부 격리 %zu",
        state.enabled ? "켜짐" : "꺼짐",
        state.pipeline ? "준비됨" : "없음",
        state.pipeline ? state.pipeline->width : 0u,
        state.pipeline ? state.pipeline->height : 0u,
        static_cast<unsigned long long>(state.framesRendered),
        static_cast<unsigned long long>(state.framesIdle),
        static_cast<unsigned long long>(state.framesInFlight),
        state.lastDrawCount, state.lastBatchCount,
        state.lastCpuMs, state.lastGpuMs,
        state.dx12.GetRetiredDisplayCount());

    std::string status = line;
    char recordLine[192]{};
    std::snprintf(recordLine, sizeof(recordLine),
        "\n  Native record CPU — last %.3f ms · avg %.3f ms · max %.3f ms · samples %llu",
        state.lastNativeRecordMs, state.AverageNativeRecordMs(),
        state.maxNativeRecordMs,
        static_cast<unsigned long long>(state.nativeRecordSamples));
    status += recordLine;

    char decalLine[128]{};
    std::snprintf(decalLine, sizeof(decalLine), "\n  데칼 %u개(배치 %u) · SSS %s · SSR %s",
        state.lastDecalCount, state.lastDecalBatchCount,
        (state.pipeline && state.pipeline->sss.IsEnabled()) ? "켜짐" : "꺼짐",
        (state.pipeline && state.pipeline->ssr.IsEnabled()) ? "켜짐" : "꺼짐");
    status += decalLine;
    status += " · 스프라이트 " + std::to_string(state.lastSpriteCount) +
        "개(배치 " + std::to_string(state.lastSpriteBatchCount) + ")" +
        " · 화면 UI " + std::to_string(state.lastUIRectCount) +
        "개(배치 " + std::to_string(state.lastUIBatchCount) + ")";
    status += "\n  뷰별 UI — Scene S/UI " +
        std::to_string(state.viewSpriteCounts[static_cast<uint32_t>(
            EnhancedLiveDisplayTarget::Editor)]) + "/" +
        std::to_string(state.viewUICounts[static_cast<uint32_t>(
            EnhancedLiveDisplayTarget::Editor)]) + " · Game S/UI " +
        std::to_string(state.viewSpriteCounts[static_cast<uint32_t>(
            EnhancedLiveDisplayTarget::Game)]) + "/" +
        std::to_string(state.viewUICounts[static_cast<uint32_t>(
            EnhancedLiveDisplayTarget::Game)]);

    if (0 != state.viewOverflowSkips)
    {
        char overflowLine[96]{};
        std::snprintf(overflowLine, sizeof(overflowLine),
            "\n  뷰 상한 초과로 건너뛴 카메라 %llu회",
            static_cast<unsigned long long>(state.viewOverflowSkips));
        status += overflowLine;
    }

    // 업로드 링은 이제 수요를 보고 자란다. 늘어난 사실이 안 보이면 "왜
    // 업로드 힙이 이만큼인가"를 나중에 설명할 수 없다.
    if (state.pipeline)
    {
        status += state.dx12.FormatUploadStatus();
    }

    // 실패는 건너뛰고 계속 가므로, 세어서 내지 않으면 "가끔 한 프레임씩
    // 빠지는" 상태가 화면으로만 보이고 지표에는 안 남는다.
    if (0 != state.frameFailures)
    {
        char failureLine[128]{};
        std::snprintf(failureLine, sizeof(failureLine),
            "\n  프레임 실패 %llu회(연속 %u · 접는 기준 %u)",
            static_cast<unsigned long long>(state.frameFailures),
            state.consecutiveFrameFailures,
            LiveState::kMaxConsecutiveFrameFailures);
        status += failureLine;
    }

    // ── 뷰의 광원 선별 (RenderSceneViewPlan ②) ──
    //
    // 목록만 보면 잘린 사실이 안 보인다. "씬에 몇 개인데 이 뷰가 몇 개를
    // 봤고, 패스 한도에 몇 개가 걸렸는지"를 한 줄로 낸다 — 어두워졌을 때
    // 그 원인이 컬링인지 한도인지 여기서 갈린다.
    {
        const ViewLightSelection& sel = state.lastLightSelection;
        const uint32_t visible = static_cast<uint32_t>(sel.lights.size());
        const uint32_t deferredCap = EnhancedDeferredPass::kMaxLights;
        const uint32_t overCap = (visible > deferredCap) ? (visible - deferredCap) : 0u;

        char lightLine[192]{};
        std::snprintf(lightLine, sizeof(lightLine),
            "\n  광원 — 씬 %u개 · 뷰 %u개(절두체 밖 %u) · Deferred 한도 %u",
            sel.sceneLights, visible, sel.culledByFrustum, deferredCap);
        status += lightLine;

        if (0 != overCap)
        {
            char overLine[96]{};
            std::snprintf(overLine, sizeof(overLine),
                " · 한도 초과 %u개(기여도 낮은 쪽부터 빠진다)", overCap);
            status += overLine;
        }

        // 소수(≤6개)일 때는 프록시 내용물을 그대로 보인다. "컬링이 안 된다"가
        // 판정 버그인지 낡은 데이터인지는 이 줄이 가른다 — PIX 검증에서 그
        // 구분에 반나절이 들었다(2026-08-09).
        if (nullptr != state.renderScene)
        {
            const auto proxies = state.renderScene->GetLightProxySnapshot();
            if (proxies.size() <= 6)
            {
                for (const auto& proxy : proxies)
                {
                    if (nullptr == proxy) continue;
                    char proxyLine[160]{};
                    std::snprintf(proxyLine, sizeof(proxyLine),
                        "\n    광원프록시 type=%d pos(%.1f %.1f %.1f) r=%.1f i=%.2f dir(%.2f %.2f %.2f)",
                        proxy->m_lightType,
                        proxy->m_worldPosition.x, proxy->m_worldPosition.y,
                        proxy->m_worldPosition.z, proxy->m_range, proxy->m_intensity,
                        proxy->m_direction.x, proxy->m_direction.y, proxy->m_direction.z);
                    status += proxyLine;
                }
            }
        }
    }

    // ── 뷰의 드로우 컬링 (RenderSceneViewPlan ③) ──
    //
    // 풀은 프레임당 한 번 모으고 뷰가 절두체로 거른다. 자른 수가 늘 0이면
    // 컬링이 도는지 자체가 의심스럽고, 씬 대비 너무 크면 절두체나 바운즈가
    // 어긋난 것이다 — 어느 쪽인지 이 줄에서 갈린다.
    {
        char cullLine[160]{};
        std::snprintf(cullLine, sizeof(cullLine),
            "\n  드로우 — 풀 %u개 · 절두체 밖 %u개 · 제출 %u개",
            state.lastPoolDraws, state.lastCulledDraws,
            (state.lastPoolDraws >= state.lastCulledDraws)
                ? (state.lastPoolDraws - state.lastCulledDraws) : 0u);
        status += cullLine;
    }

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
        status += state.dx12.FormatAssetStatus();
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
        status += state.dx12.FormatTargetHeapStatus();
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

	status += "\n  프레임 패킷 — publish " +
		std::to_string(state.publishedFrameId) + " / consume " +
		std::to_string(state.consumedFrameId) + " · scene epoch " +
		std::to_string(state.sceneEpoch) + " · resize generation " +
		std::to_string(state.resizeGeneration);
	const auto proxyStats = ProxyCommandQueue->GetStats();
	status += "\n  프록시 delta — enqueue " +
		std::to_string(proxyStats.enqueued) + " / apply " +
		std::to_string(proxyStats.applied) + " / drop " +
		std::to_string(proxyStats.dropped) + " / pending " +
		std::to_string(proxyStats.pending) + " (stale " +
		std::to_string(proxyStats.staleEpoch) + " / missing " +
		std::to_string(proxyStats.missingTarget) + " / failed " +
			std::to_string(proxyStats.failed) + " / superseded " +
			std::to_string(proxyStats.superseded) + " / shutdown " +
			std::to_string(proxyStats.shutdownDiscarded) + ")";
	const EnhancedRenderThreadStats threadStats = state.GetRenderThreadStats();
	status += "\n  RenderThread queue — publish " +
		std::to_string(threadStats.published) + " / consume " +
		std::to_string(threadStats.consumed) + " / latest-wins " +
		std::to_string(threadStats.coalescedFrames) + " / pending " +
		std::to_string(threadStats.pending) + " + active " +
		std::to_string(threadStats.inProgress) + " · high-water " +
		std::to_string(threadStats.highWatermark) + "/" +
		std::to_string(threadStats.capacity) + " · overflow " +
		std::to_string(threadStats.overflowEvents) + " · back-pressure " +
		std::to_string(threadStats.backPressureWaits) + " · delta-coalesce " +
		std::to_string(threadStats.coalescedDeltas) +
		(threadStats.producerConsumerSeparated ? " · GT/RT 분리" : " · GT/RT 미확정");
    const RHISubmissionThreadStats rhiStats = GetRHISubmissionThread().GetStats();
    status += "\n  RHI submit queue — enqueue " +
        std::to_string(rhiStats.enqueued) + " / execute " +
        std::to_string(rhiStats.executed) + " · pending task/batch/retirement " +
        std::to_string(rhiStats.pendingTasks) + "/" +
        std::to_string(rhiStats.pendingBatches) + "/" +
        std::to_string(rhiStats.pendingRetirements) + " · high-water " +
        std::to_string(rhiStats.maxQueueDepth) + "/" +
        std::to_string(RHISubmissionThread::kQueueCapacity) +
        " · saturation " + std::to_string(rhiStats.saturationWaits) +
        " · retire " + std::to_string(rhiStats.retired) +
        " · lifecycle " + std::to_string(rhiStats.lifecycleCommands) +
        "/" + std::to_string(rhiStats.lifecycleFailures) +
        " · failure/order " + std::to_string(rhiStats.failed) + "/" +
        std::to_string(rhiStats.orderingErrors) +
        (rhiStats.running && 0 != rhiStats.threadIdHash
            ? " · dedicated thread" : " · thread stopped");
    char producerWaitLine[160]{};
    std::snprintf(producerWaitLine, sizeof(producerWaitLine),
        "\n  RHI producer stall — total %.3f ms · max %.3f ms",
        static_cast<double>(rhiStats.producerWaitNanoseconds) / 1.0e6,
        static_cast<double>(rhiStats.maxProducerWaitNanoseconds) / 1.0e6);
    status += producerWaitLine;
    if (!state.lastError.empty()) status += "\n  마지막 오류: " + state.lastError;
    return status;
}

EnhancedLiveDebugSnapshot EnhancedSceneRenderer::GetLiveDebugSnapshot()
{
    // CE 렌더 스레드에서 불린다(헤더의 스레드 규약 참조). 상태를 직접 읽지
    // 않고 RenderThread가 완성해 둔 것을 락으로 복사만 한다.
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
    state.StopRenderThread();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
    state.enabled = false;
    state.TeardownPipeline();
    state.TeardownVulkanPipeline();
    state.ResetDisplaySnapshot();

    state.dx12.ShutdownInterop();

    if (state.runtimeInitialized)
    {
        // 카메라 관리 컨테이너는 RenderScene보다 먼저 내린다. 에디터 카메라는
        // Editor 세션 소유라(E4-5) 이 호출 전에 Editor가 반납해야 한다 —
        // EditorMain::Finalize가 그 순서를 지킨다.
        CameraManagement->Finalize();

        state.skyEquirect.reset();
        if (state.renderScene)
        {
            // SceneManager::Decommissioning이 활성 RenderScene을 먼저 Finalize한다.
            // 여기서 다시 부르면 프록시 맵/컨테이너를 이중 정리한다.
            state.renderScene.reset();
        }
        state.runtimeInitialized = false;
    }
}

#endif
