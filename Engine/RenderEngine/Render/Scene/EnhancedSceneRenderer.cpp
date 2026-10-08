#include <cstdlib>
#include <cstdio>
#include "../Graph/ShadowCasterBounds.h"
#include "EnhancedSceneRenderer.h"
#include "EnhancedSceneRendererLiveDX12Adapter.h"
#include "EnhancedPbrCapture.h"
#include "RuntimeSettings.h"
#include "../../../Utility_Framework/WarmupLedger.h"

#include "../Graph/EnhancedRenderGraph.h"
#include "../Graph/EnhancedRenderPass.h"
#include "../Graph/EnhancedMaterialSealHash.h"
#include "AuthoredMaterialDigest.h"
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
#include "../../RHI/RHIShaderSource.h"
#include "../../RHI/Vulkan/VulkanLoader.h"
#include "../../RHI/Vulkan/VulkanCaptureGpuProfiler.h"
#include "../../RHI/ScreenSizedResource.h"
#include "../../RHI/RHISubmissionThread.h"
#include "../../EnhancedGizmoSceneBinding.h"
#include "../../DataSystem.h"
#include "../../ShaderMeta.h"
#include "../../StandardMaterialProperty.h"

#include "../../Camera.h"
#include "../../Material.h"
#include "../../MaterialGraphSceneInput.h"
#include "../../MaterialGraphSceneHost.h"
#include "../../MaterialGraphSceneCompiler.h"
#include "../../RenderScene.h"
#include "../Core/EnhancedLightPacking.h"
#include "../../Texture.h"
#include "../../PrimitiveRenderProxy.h"
#include "../../UIRenderProxy.h"
#include "../../UIClipping.h"
#include "../../BoneRegion.h" // kMaxBones
#include "../../Mesh.h"
#include "../../RenderState.h"
#include "../../../Utility_Framework/PathFinder.h"
#include "../../../Utility_Framework/JobScheduler.h"

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
#include <string_view>
#include <type_traits>
#include <exception>
#include <utility>
#include <unordered_map>
#include <set>
#include <fstream>
#include <wrl/wrappers/corewrappers.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#include <mathematics/transform.hpp>
#include <mathematics/scalar.hpp>

// EnhancedSceneRenderer의 단독 메인 런타임 구현.
//
// 공개 표면은 EnhancedSceneRenderer의 정적 Live API다(헤더의 규약 주석 참조).
// 상태를 이 파일 안의 싱글턴에 숨긴 이유: EnhancedSceneRenderer 인스턴스는
// 콘솔 명령마다 스택에 만들어지는 검증용이라 상시 상태를 들 수 없고, 그렇다고
// 별도 공개 클래스를 두면 "DX12 렌더러 = EnhancedSceneRenderer"라는 로드맵의
// 명칭 체계가 흐려진다(실제로 그렇게 만들었다가 물렸다).
namespace EnhancedSceneRenderer
{
    using LiveGraphSnapshot = std::shared_ptr<const EnhancedRenderGraph::DiagnosticSnapshot>;

    LiveGraphSnapshot CaptureLiveGraphSnapshot(const EnhancedRenderGraph& graph,
        uint64_t viewId, uint64_t historyRevision, uint64_t frameId, uint64_t sceneEpoch,
        uint32_t width, uint32_t height)
    {
        const auto started = std::chrono::steady_clock::now();
        auto snapshot = std::make_shared<EnhancedRenderGraph::DiagnosticSnapshot>();
        if (!graph.CaptureDiagnosticSnapshot(*snapshot))
        {
            return {};
        }
        snapshot->viewId = viewId;
        snapshot->sceneEpoch = sceneEpoch;
        snapshot->historyRevision = historyRevision;
        snapshot->frameId = frameId;
        snapshot->width = width;
        snapshot->height = height;
        snapshot->copyNanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        return snapshot;
    }

    // PHASE 14 P2 — 받아 둔 수명 훅. 익명 네임스페이스에 두지 않는다: 유니티
    // 빌드의 격리 단위는 파일이 아니라 blob 이라, 같은 blob 에 든 다른 파일의
    // 같은 이름과 조용히 한 덩어리가 된다. 여기 이름은 링커가 본다.
    //
    // 함수 지역 static 인 이유는 정적 초기화 순서다 — 훅을 거는 쪽(부트스트랩)이
    // 이 번역 단위의 정적 초기화보다 먼저 돌 수 있다.
    RenderThreadHooks& MutableRenderThreadHooks()
    {
        static RenderThreadHooks hooks{};
        return hooks;
    }

    // 훅은 begin/end 두 개로 갈려 오지만, 부르는 자리에서는 다시 한 문장으로
    // 묶는다 — 프레임 본문이 예외로 빠져나가도 닫혀야 한다. 실제로 TickLive 는
    // try/catch 로 감싸인 자리다.
    struct RenderThreadFrameScope
    {
        RenderThreadFrameScope()
        {
            const RenderThreadHooks& hooks = MutableRenderThreadHooks();
            if (hooks.OnFrameBegin) hooks.OnFrameBegin();
        }
        ~RenderThreadFrameScope()
        {
            const RenderThreadHooks& hooks = MutableRenderThreadHooks();
            if (hooks.OnFrameEnd) hooks.OnFrameEnd();
        }
        RenderThreadFrameScope(const RenderThreadFrameScope&) = delete;
        RenderThreadFrameScope& operator=(const RenderThreadFrameScope&) = delete;
    };

    struct RenderThreadPhaseScope
    {
        explicit RenderThreadPhaseScope(RenderPhase phase)
        {
            const RenderThreadHooks& hooks = MutableRenderThreadHooks();
            if (hooks.OnPhaseBegin) hooks.OnPhaseBegin(phase);
        }
        ~RenderThreadPhaseScope()
        {
            const RenderThreadHooks& hooks = MutableRenderThreadHooks();
            if (hooks.OnPhaseEnd) hooks.OnPhaseEnd();
        }
        RenderThreadPhaseScope(const RenderThreadPhaseScope&) = delete;
        RenderThreadPhaseScope& operator=(const RenderThreadPhaseScope&) = delete;
    };
}

// GPU 구간을 받아 갈 자리. 프로세스 하나에 하나이고, 렌더러가 서기 전에
// 걸어 둔다. 이 층은 받는 쪽이 무엇인지 모른다 — 함수만 들고 있다.
namespace
{
    EnhancedLiveGpuSpanSink g_gpuSpanSink{};
}

void SetEnhancedLiveGpuSpanSink(const EnhancedLiveGpuSpanSink& sink)
{
    g_gpuSpanSink = sink;
}

namespace
{
    const EnhancedLiveGpuSpanSink& GpuSpanSink() { return g_gpuSpanSink; }

    // Diagnostic A/B build only. Ordinary builds contain no runtime rollback switch.
#if defined(CE_RG6_REFERENCE_DECLARATION_ORDER)
    constexpr auto kLiveGraphScheduling = RGSchedulingMode::DeclarationOrder;
#else
    constexpr auto kLiveGraphScheduling = RGSchedulingMode::ExplicitVersioned;
#endif

    void finish_gpu_capture(GpuFrameToken& token, bool complete, const char* reason)
    {
        const uint64_t generation = std::exchange(token.captureGeneration, 0);
        const EnhancedLiveGpuSpanSink& sink = GpuSpanSink();
        if (generation != 0 && sink.on_finish_capture)
        {
            sink.on_finish_capture(generation, static_cast<uint32_t>(token.engineFrameId), complete, reason);
        }
    }

    struct GpuCaptureCompletion
    {
        GpuFrameToken& token;
        bool complete{ false };
        const char* reason{ "GPU query collection did not complete" };

        ~GpuCaptureCompletion()
        {
            const EnhancedLiveGpuSpanSink& sink = GpuSpanSink();
            if (token.captureGeneration != 0 && sink.on_flush)
            {
                // Also publish any partial result if collection exits by exception.
                sink.on_flush();
            }
            finish_gpu_capture(token, complete, reason);
        }
    };

    // 살아 있는 화면의 재질 IBL 룩업은 바뀐 픽셀을 split-sum 근사로 굽고 끝낸다.
    // 기준 적분(1024/4096 표본)은 한 화소 45~75 us 라 카메라 회전에서 화면 전체가
    // 다시 구워지면 프레임당 1.5 s 까지 GPU 를 막았다(10-04 캡처). 기준값 수렴은
    // 하지 않는다. 검사 도구는 SceneHostBudget 기본값(false, 기준 적분)을 쓴다.
    constexpr bool kLiveLookupApproximate = true;
}

namespace
{
    uint64_t capture_steady_nanoseconds()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    double capture_age_milliseconds(uint64_t captured, uint64_t now)
    {
        return captured != 0 && now >= captured ? static_cast<double>(now - captured) / 1.0e6 : 0.0;
    }

    // Built-in preview geometry has a reserved, stable identity and immutable
    // storage. SceneViewInput copies/seals it before any GPU submission.
    struct MaterialPreviewSphere
    {
        std::vector<std::byte> vertices;
        std::vector<uint32_t> indices;
        RHIModelMeshView mesh;
        MaterialPreviewSphere()
        {
            Uuid::TryParse("e590a6f7-075f-8f70-83cb-3f427aa960bc", mesh.handle.modelId);
            Uuid::TryParse("991d13a0-1288-8ac2-8777-f6f77fb8ed63", mesh.handle.meshId);
            mesh.handle.generation = 1;
            mesh.vertexAttributeMask = assets::kCoreVertexAttributes;
            mesh.vertexStride = assets::StrideOf(mesh.vertexAttributeMask);
            mesh.vertexLayoutHash = assets::VertexLayoutHash(mesh.vertexAttributeMask);
            constexpr uint32_t rings = 32, segments = 64;
            vertices.resize((rings + 1) * (segments + 1) * mesh.vertexStride);
            for (uint32_t y = 0; y <= rings; ++y)
                for (uint32_t x = 0; x <= segments; ++x)
                {
                    const float theta = float(y) * 3.14159265359f / rings;
                    const float phi = float(x) * 6.28318530718f / segments;
                    const math::vector3 normal{std::sin(theta) * std::cos(phi), std::cos(theta),
                                               std::sin(theta) * std::sin(phi)};
                    const math::vector2 uv{float(x) / segments, float(y) / rings};
                    const math::vector4 tangent{-std::sin(phi), 0.f, std::cos(phi), 1.f};
                    auto* vertex = vertices.data() + (y * (segments + 1) + x) * mesh.vertexStride;
                    const auto write = [&](assets::VertexAttribute attribute, const auto& value) {
                        std::memcpy(vertex + assets::OffsetOf(mesh.vertexAttributeMask, attribute), &value, sizeof(value));
                    };
                    write(assets::VertexAttribute::Position, normal);
                    write(assets::VertexAttribute::Normal, normal);
                    write(assets::VertexAttribute::Uv0, uv);
                    write(assets::VertexAttribute::Tangent, tangent);
                }
            for (uint32_t y = 0; y < rings; ++y)
                for (uint32_t x = 0; x < segments; ++x)
                {
                    const auto a = y * (segments + 1) + x, b = a + segments + 1;
                    if (y != 0) indices.insert(indices.end(), {a, a + 1, b});
                    if (y + 1 != rings) indices.insert(indices.end(), {a + 1, b + 1, b});
                }
            mesh.vertexData = vertices.data();
            mesh.vertexBytes = vertices.size();
            mesh.indexData = indices.data();
            mesh.indexCount = static_cast<uint32_t>(indices.size());
        }
    };

    struct MaterialPreviewFloor
    {
        std::vector<std::byte> vertices;
        std::vector<uint32_t> indices;
        RHIModelMeshView mesh;
        explicit MaterialPreviewFloor(unsigned color)
        {
            Uuid::TryParse("e590a6f7-075f-8f70-83cb-3f427aa960bc", mesh.handle.modelId);
            Uuid::TryParse(color ? "4a0c084a-ea1a-83a7-9cb4-c13acfe9c702" : "4a0c084a-ea1a-83a7-9cb4-c13acfe9c701", mesh.handle.meshId);
            mesh.handle.generation = 1;
            mesh.vertexAttributeMask = assets::kCoreVertexAttributes;
            mesh.vertexStride = assets::StrideOf(mesh.vertexAttributeMask);
            mesh.vertexLayoutHash = assets::VertexLayoutHash(mesh.vertexAttributeMask);
            for (int z = -8; z < 8; ++z)
                for (int x = -8; x < 8; ++x)
                {
                    if (unsigned((x + z + 16) % 2) != color) continue;
                    const auto first = static_cast<uint32_t>(vertices.size() / mesh.vertexStride);
                    for (const auto corner : {math::vector2{0, 0}, math::vector2{0, 1}, math::vector2{1, 1}, math::vector2{1, 0}})
                    {
                        const auto offset = vertices.size();
                        vertices.resize(offset + mesh.vertexStride);
                        auto* vertex = vertices.data() + offset;
                        const auto write = [&](assets::VertexAttribute attribute, const auto& value) {
                            std::memcpy(vertex + assets::OffsetOf(mesh.vertexAttributeMask, attribute), &value, sizeof(value));
                        };
                        write(assets::VertexAttribute::Position, math::vector3{(x + corner.x) * .65f, -1.01f, (z + corner.y) * .65f});
                        write(assets::VertexAttribute::Normal, math::vector3{0, 1, 0});
                        write(assets::VertexAttribute::Uv0, corner);
                        write(assets::VertexAttribute::Tangent, math::vector4{1, 0, 0, 1});
                    }
                    indices.insert(indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
                }
            mesh.vertexData = vertices.data();
            mesh.vertexBytes = vertices.size();
            mesh.indexData = indices.data();
            mesh.indexCount = static_cast<uint32_t>(indices.size());
        }
    };

    FrameCameraSnapshot MaterialPreviewCamera(uint32_t width, uint32_t height)
    {
        FrameCameraSnapshot camera;
        camera.eyePosition = {0.f, 1.65f, -3.8f};
        camera.forward = math::normalize(-camera.eyePosition);
        camera.right = {1.f, 0.f, 0.f};
        camera.up = math::cross(camera.forward, camera.right);
        camera.fov = 45.f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 20.f;
        camera.view = math::look_at_lh(camera.eyePosition, {}, camera.up);
        camera.projection = math::perspective_fov_lh(math::radians(camera.fov),
            float(width) / std::max(height, 1u), camera.nearPlane, camera.farPlane);
        camera.inverseView = math::inverse(camera.view);
        camera.inverseProjection = math::inverse(camera.projection);
        return camera;
    }
    using EnhancedSceneRenderer::LiveGraphSnapshot;
    using EnhancedSceneRenderer::CaptureLiveGraphSnapshot;
    using EnhancedSceneRenderer::RenderPhase;
    using EnhancedSceneRenderer::RenderThreadPhaseScope;
    // I6-C — 신원 키 정본. experiment 핸들의 stableKey가 우선이고, 없으면
    // legacy Mesh 신원(m_hashingMesh)이다. 두 키는 D4b가 적은 대로 같은
    // 64비트 공간을 쓰므로 섞여도 충돌 가정이 같다.
    //
    // 포인터로 정렬하던 것을 값으로 바꾼다 — 주소는 할당 순서에 따라 달라지고,
    // 무엇보다 렌더가 게임 객체 주소를 신원으로 쓰는 것 자체가 I6이 지우려는
    // 결합이다.
    [[nodiscard]] std::size_t MakeGeometryKey(const EnhancedDrawItem& item)
    {
        // MBC7 — typed 뷰가 첫 축(EnhancedDrawIdentity::GeometryKey와 같은 순서).
        if (item.modelMeshView.handle.IsValid())
        {
            return HashModelMeshHandle(item.modelMeshView.handle);
        }
        return nullptr != item.mesh
            ? static_cast<std::size_t>(item.mesh->m_hashingMesh.m_ID_Data)
            : std::size_t{ 0 };
    }

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

    // (I5-M4). legacy Material을 읽던 SealMaterialTextureBindings는 그 치환으로
    // 소비자가 0이 되어 제거됐다.

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

    // 뷰 하나를 준비한 직후의 그림자 수치. 그림자 패스와 재질 그래프 호스트가
    // 모두 그 뷰의 Prepare 를 마친 뒤에 불러야 같은 뷰의 값이 모인다.
    template <typename PipelineT>
    EnhancedLiveShadowStats CaptureShadowStats(const PipelineT& p)
    {
        const EnhancedShadowPass::DebugStats source = p.shadow.GetDebugStats();
        const std::array<uint32_t, 3> graphCasters = p.graphMaterials.ShadowCasterCounts();
        EnhancedLiveShadowStats stats;
        stats.valid = true;
        stats.hasDirectionalLight = source.hasDirectionalLight;
        stats.lightIndex = source.lightIndex;
        stats.lightDirection = { source.lightDirection.x, source.lightDirection.y, source.lightDirection.z };
        stats.shadowDistance = source.shadowDistance;
        stats.slopeScale = source.slopeScale;
        stats.casterCandidates = source.casterCandidates;
        stats.gpuVisibilityActive = source.gpuVisibilityActive;
        stats.visibilityCountsExact = source.visibilityCountsExact;
        stats.gpuSubmittedCandidates = source.gpuSubmittedCandidates;
        stats.gpuSubmittedBins = source.gpuSubmittedBins;
        for (uint32_t index = 0; index < kShadowCascadeCount; ++index)
        {
            const EnhancedShadowPass::CascadeStats& cascade = source.cascades[index];
            stats.cascades[index] = { cascade.splitDepth, cascade.radius, cascade.worldTexel,
                cascade.depthSpan, cascade.constantBias, graphCasters[index] };
        }
        return stats;
    }

    // 파이프라인 번들. 켤 때마다 힙에 새로 만든다.
    //
    // ★ 멤버 재사용(Shutdown 후 같은 객체에 다시 Initialize)이 아니다.
    //   처음에 그렇게 했다가 off→on 재활성화에서 죽었다 — 디바이스·캐시·
    //   패스들은 전부 '새 객체에 한 번 Initialize'만 검증돼 있고(테스트가
    //   항상 스택에 새로 만든다), 재초기화 경로는 아무도 밟은 적이 없는
    //   길이었다. 검증된 수명 패턴을 그대로 쓰는 것이 맞다.
    struct LivePipeline
    {
        uint64_t resizeGeneration{ 0 };
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        double lastNativeRecordMs{ 0.0 };
        EnhancedRenderGraph::Stats lastGraphStats{};

        // ★ SSGI는 여기 없다 — CameraView가 뷰마다 하나씩 든다.
        //   시간축 누적을 하는 유일한 라이브 패스라서 그렇다(아래 CameraView
        //   주석 참조). 나머지 패스는 프레임 안에서 입력→출력이 끝나므로
        //   카메라가 둘이어도 한 인스턴스로 충분하다.
        EnhancedGBufferPass   gbuffer;
        material_graph::SceneHost graphMaterials;
        std::shared_ptr<const material_graph::SceneViewInput> graphInput;
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

        AnimationPaletteFrame animationPalettes{};
        EnhancedFrameContext frameContext{};

        // 이 파이프라인의 조립 기술. 파이프라인이 설 때 한 번 짜이고, 노드의
        // 접착 람다가 이 LivePipeline과 LiveState를 캡처한다 — 그래서 수명이
        // 파이프라인에 묶여야 하고, 여기 멤버로 두는 것이 그 계약이다.
        // 파이프라인을 헐면 노드도 함께 사라진다.
        LivePipelineDesc desc;

        // 프레임마다 비우고 다시 채운다. 뷰가 둘이어도 한 벌로 충분하다 —
        // 한 프레임의 한 뷰를 그리는 동안에만 살아 있는 값이다.
        LiveBlackboard blackboard;

        // 생산자 완료는 게시 조건일 뿐 재사용 조건이 아니다. 슬롯 선택과 Host 조회는
        // displayLifetimeMutex로 직렬화하고, 소비자 lease도 없어야 덮어쓸 수 있다.
        // 링 슬롯 수만으로 GPU 소유권을 대신하지 않는다.
        struct DisplaySlot
        {
            bool previewComplete{false};
            RHITextureHandle rhiTexture;
            EnhancedSceneRendererLiveDX12Adapter::DisplayToken interopToken{
                EnhancedSceneRendererLiveDX12Adapter::kInvalidDisplayToken };
            uint64_t fenceValue{ 0 };
            uint64_t frameId{ 0 };
            uint64_t sourceCaptureNanoseconds{ 0 };
            double completedAgeMs{ 0.0 };
            EnhancedLiveViewKey key{};
            uint64_t sceneEpoch{ 0 };
            FrameCameraSnapshot camera{};

            // 이 제출의 GPU 프로파일 표. 부른 자리에서 받아 보관했다가 펜스가
            // 끝났을 때 그대로 Collect 에 넘긴다.
            //
            // ★ 이것이 없었을 때 수집은 "지금 기록 중인 슬롯" 을 읽었고, 실측에서
            //   수집의 83% 가 남의 제출을 읽고 있었다(§0.5.10).
            GpuFrameToken profilerToken{};

            // 이 슬롯 프레임의 그래프. 규칙(dx12.compare 크래시의 교훈):
            // 그래프의 수명은 그 커맨드를 GPU가 끝낼 때까지다 — transient를
            // 그래프가 들고 있다. 승격(펜스 완료) 때 놓으면 풀로 반납된다.
            std::shared_ptr<EnhancedRenderGraph> graph;
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
            EnhancedLiveDisplayTarget displayTarget{ EnhancedLiveDisplayTarget::Game };
            EnhancedLiveViewFlags viewFlags{ EnhancedLiveViewFlags::ScreenSpaceUI };
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

    // Residency precedes the recording boundary. Recording-owned palettes,
    // visibility and material packets must all be allocated after that boundary.
    template <typename PipelineT, typename CommandPoolT>
    bool PrepareSceneRecording(PipelineT& pipeline, EnhancedRenderGraph& graph,
        CommandPoolT& commandPool, RHIShaderBinary output, bool& preparationDeferred,
        EnhancedPbrCapture* capture, std::string& error)
    {
        RHIShaderCompiler::ScopedOutput outputScope(output);
        auto& context = pipeline.frameContext;
        if (!pipeline.graphMaterials.PrepareResidency(context, pipeline.graphInput, error))
        {
            return false;
        }
        if (!graph.PrepareParallel(commandPool, error))
        {
            return false;
        }
        if (!pipeline.animationPalettes.UploadForCurrentRecording(*context.resources))
        {
            error = "Sealed animation palette upload failed after the upload-prefix boundary.";
            return false;
        }
        if (!pipeline.decal.PrepareGpuVisibility(context, error)
            || !pipeline.sprite.PrepareGpuVisibility(context, error))
        {
            return false;
        }
        if (!pipeline.graphMaterials.Prepare(context, pipeline.graphInput,
                pipeline.ibl.GetCubeMap(), pipeline.ibl.GetIrradianceMap(),
                pipeline.ibl.GetPrefilteredMap(), pipeline.shadow.GetShadowData(),
                material_graph::SceneHostBudget{.lookupApproximate = kLiveLookupApproximate,
                    .lookupRuntimeEvaluation = true}, error, pipeline.ibl.GetGeneration(),
                pipeline.ibl.GetImportanceMaps(), pipeline.ibl.GetSourceMap()))
        {
            preparationDeferred = pipeline.graphMaterials.PreparationDeferred();
            return false;
        }
        if (capture)
        {
            capture->RecordLatticeInput(pipeline.graphInput);
        }
        return true;
    }

    // Vulkan 공용 scene graph 라이브 경로. 에디터 창 자체는 아직 DX12 ImGui
    // 셸이므로 최종 LDR를 비동기 리드백한 뒤 셸에 넘긴다.
    // 그래프와 리드백 슬롯은 timeline completion까지 살아 있어 D3D12 경로의
    // 표시 슬롯과 같은 수명 계약을 지킨다. 이 브리지는 기능 동등성 단계이며,
    // external-memory 직접 공유는 별도 성능 단계다.
    struct VulkanLivePipeline
    {
        bool shutdownComplete{ false };

        ~VulkanLivePipeline()
        {
            if (!shutdownComplete && resources.IsInitialized())
            {
                std::string error;
                if (!Shutdown(error))
                {
                    OutputDebugStringA("[Vulkan live] Forced destruction cannot prove GPU idle or device loss; refusing unsafe member destruction.\n");
                    std::terminate();
                }
            }
        }

        static constexpr uint32_t kSlotCount = 3;
        static constexpr uint64_t kDisplayKeyBase = 0x564B4C4956450000ull; // "VKLIVE"

        uint32_t width{ 0 };
        uint32_t height{ 0 };
        uint64_t frameCounter{ 0 };

        VulkanDeviceResources resources;
        std::shared_ptr<VulkanCaptureGpuProfiler> retainedCaptureProfiler;
        VulkanPipelineCache pipelines;
        VulkanMeshCache meshCache;
        VulkanTextureCache textureCache;
        VulkanCommandBufferPool commandPool;

        // W8 — capture 프레임은 Render 안에서 인코더 drop을 비워 manifest에
        // 싣는다. 그렇게 비운 수를 여기 맡겨 두어야 TickLive의 프레임 집계가
        // 그 프레임만 0으로 세지 않는다.
        uint64_t    stashedEncoderDrops{ 0 };
        std::string stashedLastEncoderDrop;

        EnhancedShadowPass shadow;
        EnhancedGBufferPass gbuffer;
        material_graph::SceneHost graphMaterials;
        std::shared_ptr<const material_graph::SceneViewInput> graphInput;
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
        AnimationPaletteFrame animationPalettes{};
        EnhancedFrameContext frameContext{};
        LivePipelineDesc desc;
        LiveBlackboard blackboard;
        RGTransientPool transientPool;
        EnhancedRenderGraph::Stats lastGraphStats{};
        double lastNativeRecordMs{ 0.0 };
        uint32_t commandPoolFrame{ 0 };

        struct View
        {
            bool previewComplete{false};
            EnhancedLiveViewKey key{};
            EnhancedLiveDisplayTarget displayTarget{ EnhancedLiveDisplayTarget::Game };
            EnhancedLiveViewFlags viewFlags{ EnhancedLiveViewFlags::ScreenSpaceUI };
            bool ready{ false };

            // temporal GI는 카메라별 히스토리·이전 행렬을 가진다. 공용
            // 인스턴스를 쓰면 여러 씬 뷰가 서로의 화면 공간 GI를 섞는다.
            EnhancedSSGIPass ssgi;
            EnhancedVolumetricFogPass fog;
            bool fogReady{ false };
            uint64_t promotionCount{ 0 };
            uint32_t promotedSlotMask{ 0 };
            uint64_t completedFrameId{ 0 };
            uint64_t completedSceneEpoch{ 0 };
            uint64_t completedCaptureNanoseconds{ 0 };
            double completedAgeMs{ 0.0 };
            uint64_t completedResizeGeneration{ 0 };
            FrameCameraSnapshot completedCamera{};
        };
        mutable std::mutex viewMutex;
        View views[EnhancedSceneRenderer::kMaxLiveCameraViews];

        struct Slot
        {
            bool previewComplete{false};
            RHIReadback readback{};
            std::shared_ptr<EnhancedRenderGraph> graph;
            uint64_t fenceValue{ 0 };
            uint32_t viewIndex{ 0 };
            EnhancedLiveViewKey key{};
            uint64_t frameId{ 0 };
            uint64_t sourceCaptureNanoseconds{ 0 };
            uint64_t sceneEpoch{ 0 };
            uint64_t resizeGeneration{ 0 };
            FrameCameraSnapshot camera{};
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
            FrameCameraSnapshot& camera, const std::vector<EnhancedDrawItem>& draws,
            const std::vector<EnhancedDrawItem>& forwardDraws,
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
            frameContext.animationPalettes = &animationPalettes;
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

        bool Shutdown(std::string& outError, EnhancedPbrCapture* capture = nullptr)
        {
            if (shutdownComplete)
            {
                return true;
            }
            if (resources.IsInitialized())
            {
                std::string lifecycleError;
                bool drained = resources.DrainForLifecycle(
                    RHILifecycleCommand::BackendShutdown, lifecycleError);
                if (!drained && GetRHISubmissionThread().GetOwnerStats(&resources).faulted)
                {
                    drained = resources.DrainForLifecycle(
                        RHILifecycleCommand::UnrecoverableDeviceError, lifecycleError);
                }
                if (!drained)
                {
                    OutputDebugStringA(("[Vulkan live] backend shutdown drain 실패: " +
                        lifecycleError + "\n").c_str());
                    outError = "Vulkan pipeline retained: GPU idle or device loss was not established: " + lifecycleError;
                    return false;
                }
            }
            if (retainedCaptureProfiler)
            {
                retainedCaptureProfiler->ReleaseAfterIdle();
                retainedCaptureProfiler.reset();
            }
            if (capture && capture->resourceBackend == EnhancedLiveBackend::Vulkan)
            {
                capture->Release(resources);
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
            graphMaterials.ShutdownAfterIdle();
            ibl.Shutdown();
            commandPool.Shutdown();
            textureCache.Shutdown();
            meshCache.Shutdown();
            pipelines.Shutdown();
            resources.Shutdown();
            shutdownComplete = true;
            return true;
        }

        int FindOrAssignView(const EnhancedLiveViewPacket& requested,
            const EnhancedLiveFramePacket& frame)
        {
            std::lock_guard<std::mutex> lock(viewMutex);
            for (uint32_t i = 0; i < EnhancedSceneRenderer::kMaxLiveCameraViews; ++i)
            {
                if (views[i].key == requested.key)
                {
                    views[i].displayTarget = requested.displayTarget;
                    views[i].viewFlags = requested.viewFlags;
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
            views[selected].displayTarget = requested.displayTarget;
            views[selected].viewFlags = requested.viewFlags;
            views[selected].ready = false;
            views[selected].previewComplete = false;
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
                        // 슬롯 인덱스 순회가 완료 프레임을 역순으로 승격하지 않게 한다.
                        belongsToView = view.key == slot.key && slot.frameId > view.completedFrameId;
                        if (belongsToView)
                        {
                            view.ready = true;
                            ++view.promotionCount;
                            view.promotedSlotMask |= (1u << slotIndex);
                            view.completedFrameId = slot.frameId;
                            view.completedSceneEpoch = slot.sceneEpoch;
                            view.completedCaptureNanoseconds = slot.sourceCaptureNanoseconds;
                            view.completedAgeMs = capture_age_milliseconds(
                                slot.sourceCaptureNanoseconds, capture_steady_nanoseconds());
                            view.completedResizeGeneration = slot.resizeGeneration;
                            view.completedCamera = slot.camera;
                            view.previewComplete = slot.previewComplete;
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
                                image.width * 4u,
                                RHIDisplayFrameMetadata{slot.frameId, slot.sceneEpoch,
                                    slot.resizeGeneration, slot.key.viewId,
                                    slot.key.historyRevision, slot.camera, slot.sourceCaptureNanoseconds});
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
            uint64_t sourceFrameId, uint64_t sourceCaptureNanoseconds,
            uint64_t resizeGeneration, uint64_t backendGeneration,
            const std::function<bool(std::string&)>& prepareFrame,
            std::string& outError, EnhancedPbrCapture* capture,
            LiveGraphSnapshot* diagnosticOutput, bool& preparationDeferred)
        {
            preparationDeferred = false;
            if (retainedCaptureProfiler)
            {
                if (!resources.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, outError))
                {
                    return false;
                }
                retainedCaptureProfiler->ReleaseAfterIdle();
                retainedCaptureProfiler.reset();
                backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&resources);
                if (GetRHISubmissionThread().GetOwnerStats(&resources).faulted)
                {
                    outError = "Vulkan capture cleanup observed device loss.";
                    return false;
                }
            }
            Slot* slot = nullptr;
            for (Slot& candidate : slots)
            {
                if (!candidate.pending) { slot = &candidate; break; }
            }
            if (nullptr == slot) { outError = "Vulkan 라이브 리드백 슬롯이 모두 사용 중"; return false; }

            std::string environmentCacheError;
            if (!ibl.FinishCookedCapture(resources.GetCompletedFenceValue(),environmentCacheError))
                Debug::PrintLog(spdlog::level::warn,"[EnvironmentCache] " + environmentCacheError);

            {
                RenderThreadPhaseScope begin(RenderPhase::begin_frame);
                if (!resources.BeginFrame(outError))
                {
                    if (capture) capture->Fail(outError);
                    return false;
                }
            }
            bool committed = false;
            struct FrameGuard
            {
                VulkanDeviceResources& resources;
                const bool& committed;
                EnhancedPbrCapture* capture;
                std::string& error;
                std::shared_ptr<VulkanCaptureGpuProfiler>& profiler;
                ~FrameGuard()
                {
                    if (!committed) resources.AbortFrame();
                    if (capture)
                    {
                        std::string releaseError;
                        bool safe = resources.DrainForLifecycle(
                            RHILifecycleCommand::OfflineReadbackCapture, releaseError);
                        if (!safe && GetRHISubmissionThread().GetOwnerStats(&resources).faulted)
                        {
                            safe = resources.DrainForLifecycle(
                                RHILifecycleCommand::UnrecoverableDeviceError, releaseError);
                        }
                        if (safe)
                        {
                            capture->Release(resources);
                            if (profiler)
                            {
                                profiler->ReleaseAfterIdle();
                                profiler.reset();
                            }
                        }
                        else
                        {
                            capture->Fail("Capture resources retained until GPU idle: " + releaseError);
                        }
                        if (capture->result.state == EnhancedPbrCaptureState::Recording)
                            capture->Fail(error);
                    }
                }
            } frameGuard{ resources, committed, capture, outError, retainedCaptureProfiler };

            // The live Vulkan path has no calibrated timestamp producer. Its
            // offline capture profiler cannot be drained here without a GPU wait.
            // Account for this admitted submission explicitly instead of claiming
            // a complete GPU capture with an empty lane.
            const EnhancedLiveGpuSpanSink& sink = GpuSpanSink();
            if (sink.on_begin_capture && sink.on_finish_capture)
            {
                const uint64_t generation = sink.on_begin_capture(static_cast<uint32_t>(sourceFrameId));
                if (generation != 0)
                {
                    sink.on_finish_capture(generation, static_cast<uint32_t>(sourceFrameId), false,
                        "Vulkan live GPU timestamps are unavailable");
                }
            }

            // Keep the diagnostic reset guard alive through recording/submission,
            // just as DX12 does. Resetting at prepare-scope exit changes the SSGI
            // sample index/history ring after PrepareFrame has advanced them.
            struct CaptureHistoryGuard
            {
                View& view;
                bool active;
                void Reset() { view.ssgi.ResetHistory(); view.fog.ResetHistory(); }
                ~CaptureHistoryGuard() { if (active) Reset(); }
            } historyGuard{ views[viewIndex], capture && capture->controlled };
            if (historyGuard.active) historyGuard.Reset();
            const uint32_t frameIndex = static_cast<uint32_t>(frameCounter++);
            {
                RenderThreadPhaseScope prepare(RenderPhase::resource_prepare);
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
                if (!prepareFrame(outError))
                {
                    preparationDeferred = graphMaterials.SelectionDeferred();
                    return false;
                }
            }

            slot->graph = std::make_shared<EnhancedRenderGraph>(
                static_cast<IRenderDeviceServices&>(resources),
                kLiveGraphScheduling, RGOrderPolicy::DependencyOrder);
            EnhancedRenderGraph& graph = *slot->graph;
            graph.SetTransientPool(&transientPool);
            if (!PrepareSceneRecording(*this, graph, commandPool, RHIShaderBinary::SpirV,
                    preparationDeferred, capture, outError))
            {
                return false;
            }
            double compileMs = 0.0;
            LiveStopwatch compileWatch;
            {
                RenderThreadPhaseScope build(RenderPhase::graph_build);
                blackboard.Reset();

                LiveFrameBinding binding{};
                binding.viewIndex = viewIndex;
                binding.readbackTarget = slot->readback;
                binding.viewFlags = HasViewFlag(viewPacket.viewFlags,
                    EnhancedLiveViewFlags::SceneOverlay)
                    ? LiveViewFlags::kSceneOverlay : LiveViewFlags::kScreenSpaceUI;
                if (HasViewFlag(viewPacket.viewFlags, EnhancedLiveViewFlags::HideSkyBox))
                    binding.viewFlags |= LiveViewFlags::kHideSkyBox;
                bool stageCaptureOk = true;
                desc.DeclareAll(blackboard, graph, frameContext, binding,
                    [&](const LivePassNode& node, const LiveBlackboard& board) {
                        if (capture && stageCaptureOk)
                            stageCaptureOk = capture->DeclareStage(resources, graph, board, node,
                                width, height, outError);
                    });
                if (!stageCaptureOk) return false;
                if (capture && !capture->Declare(resources, graph, blackboard,
                        width, height, outError)) return false;

                if (!blackboard.Get(LiveSlots::kDisplayLdr).IsValid())
                {
                    outError = "Vulkan 라이브 공통 scene graph의 표시 출력이 없다";
                    return false;
                }
                compileWatch.Start();
                if (!graph.Compile(outError)) return false;
                compileMs = compileWatch.ElapsedMs();
            }

            if (diagnosticOutput)
            {
                *diagnosticOutput = CaptureLiveGraphSnapshot(graph, viewPacket.key.viewId,
                    viewPacket.key.historyRevision, sourceFrameId, frameContext.sceneEpoch, width, height);
            }

            // The pipeline retains a query owner if a capture cannot prove idle.
            // A stack destructor must not free queries still referenced by the GPU.
            if (capture)
            {
                retainedCaptureProfiler = std::make_shared<VulkanCaptureGpuProfiler>(resources);
            }
            // Graph slots persist across frames; never retain this stack-owned
            // diagnostic profiler when the slot returns to ordinary rendering.
            struct CaptureProfilerReset
            {
                EnhancedRenderGraph& graph;
                ~CaptureProfilerReset() { graph.SetProfiler(nullptr); }
            } profilerReset{graph};
            graph.SetProfiler(nullptr);
            if (capture)
            {
                if (!retainedCaptureProfiler->Initialize(outError)) return false;
                graph.SetProfiler(retainedCaptureProfiler.get());
            }
            RHIRecordedBatchDesc batchDesc{};
            batchDesc.frameId = sourceFrameId;
            batchDesc.backendGeneration = backendGeneration;
            batchDesc.displayToken = kDisplayKeyBase + viewIndex + 1u;
            batchDesc.lifetimeToken = slot->graph;
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
            batchDesc.captureContext = { sourceFrameId, 0, viewPacket.key.viewId, false };
#endif
            RHIRecordedBatch batch;
            RHISubmissionTicket batchTicket;
            LiveStopwatch recordWatch;
            recordWatch.Start();
            {
                RenderThreadPhaseScope record(RenderPhase::command_record);
                if (!graph.RecordParallel(commandPool, 4, batchDesc, batch, outError))
                    return false;
            }
            lastNativeRecordMs = recordWatch.ElapsedMs();
            if (capture && !capture->RecordCompiledGraph(graph, lastNativeRecordMs, compileMs))
            { outError = "capture graph is not compiled"; return false; }
            {
                RenderThreadPhaseScope submit(RenderPhase::submit);
                if (!GetRHISubmissionThread().EnqueueRecordedBatch(&resources,
                        resources, std::move(batch), batchTicket, outError))
                {
                    return false;
                }
                // Queue admission already transferred graph ownership. Attach its
                // completion before a separate immediate tail submission can fail.
                const auto graphCompletion = batchTicket.GetRecordedBatch()->GetCompletionPoint();
                if (!graphMaterials.PublishSubmittedCache(sourceFrameId,
                        graphCompletion, outError, batchTicket))
                {
                    return false;
                }
                lastGraphStats = graph.GetStats();
                if (!resources.EndFrame(outError))
                {
                    return false;
                }
            }
            committed = true;
            commandPoolFrame = (commandPoolFrame + 1u) % VulkanDeviceResources::kFrameCount;

            slot->fenceValue = resources.GetLastSignaledFenceValue();
            slot->viewIndex = viewIndex;
            slot->key = viewPacket.key;
            slot->frameId = sourceFrameId;
            slot->sourceCaptureNanoseconds = sourceCaptureNanoseconds;
            slot->sceneEpoch = frameContext.sceneEpoch;
            slot->resizeGeneration = resizeGeneration;
            // 최신 요청이 아니라 실제 패스가 소비한 카메라를 완료 슬롯에 붙인다.
            slot->camera = *frameContext.camera;
            slot->previewComplete = graphInput && !graphInput->Draws().empty() &&
                (!viewPacket.materialPreview || graphInput->Draws()[0].material == viewPacket.materialPreview->instance);
            slot->pending = true;
            if (capture)
            {
                if (!resources.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, outError)
                    || GetRHISubmissionThread().GetOwnerStats(&resources).faulted)
                {
                    capture->Fail(outError.empty() ? "Vulkan capture cannot read data after device loss." : outError);
                    return false;
                }
                std::string validation;
                const uint32_t validationCount = resources.DrainDebugMessages(validation);
                // W8: 패스가 배치를 확정한 뒤라야 알 수 있는 축을 여기서 싣는다.
                std::string lastDrop;
                const uint32_t drops = commandPool.DrainEncoderDrops(lastDrop);
                stashedEncoderDrops += drops;
                if (!lastDrop.empty()) stashedLastEncoderDrop = lastDrop;
                capture->RecordSealLedger(gbuffer.GetSealLedger(),
                    gbuffer.GetSamplerIdentity(), forward.GetSealLedger(),
                    forward.GetSamplerIdentity(), stashedEncoderDrops,
                    stashedLastEncoderDrop, textureCache.GetUploadFailureCount());
                capture->RecordIblContract(EnhancedIBLGenerator::kImportanceSampleCount,
                    EnhancedIBLGenerator::kSceneReflectionSampleCount,
                    resources.DescribeTexture(ibl.GetImportanceMaps()[2]));
                capture->manifest.rootref()["measurement"]["validationLayerEnabled"] << resources.IsValidationEnabled();
                const auto memory = resources.QueryVideoMemory();
                capture->RecordMemory(memory.usedMB, memory.budgetMB, memory.budgetMB > 0);
                std::vector<VulkanCaptureGpuProfiler::Timing> nativeTimings;
                EnhancedLiveGpuSpan captureSpan;
                std::string timingError;
                if (!retainedCaptureProfiler->Collect(nativeTimings, captureSpan.queueSpanMs,
                        captureSpan.busyMs, timingError))
                {
                    outError = timingError;
                    capture->Fail(outError);
                    return false;
                }
                captureSpan.sliceCount = retainedCaptureProfiler->SliceCount();
                std::vector<EnhancedLivePassTiming> captureTimings;
                for (const auto& timing : nativeTimings)
                    captureTimings.push_back({timing.name, timing.milliseconds, timing.milliseconds});
                capture->RecordGpuTiming(captureTimings, captureSpan, captureSpan.busyMs, timingError);
                if (!capture->Save(resources, graph.GetStats(), outError,
                        validationCount, validation)) return false;
            }
            return true;
        }
    };

    struct EnvironmentPreparationRequest
    {
        uint64_t id{ 0 };
        uint64_t generation{ 0 };
        std::string selection;
        file::path source;
        file::path shaders;
        file::path cache;
        file::path project;
    };

    struct PreparedEnvironment
    {
        EnvironmentPreparationRequest request;
        std::optional<assets::CookedEnvironment> cooked;
        std::shared_ptr<Texture> equirect;
        std::optional<assets::EnvironmentIdentity> identity;
        file::path cachePath;
    };

    // Workers own only this CPU mailbox, never LiveState, settings, or an RHI
    // object. At most one job runs and one newer request waits to replace it.
    struct EnvironmentPreparationState
    {
        std::mutex mutex;
        bool accepting{ false };
        bool running{ false };
        uint64_t generation{ 1 };
        uint64_t nextRequest{ 0 };
        std::optional<EnvironmentPreparationRequest> pending;
        std::unique_ptr<PreparedEnvironment> ready;
        EnhancedSceneRenderer::EnvironmentPreparationProgress progress;
    };

    bool SetEnvironmentPreparationPhase(EnvironmentPreparationState& state,
        const EnvironmentPreparationRequest& request, const char* phase)
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.accepting || state.generation != request.generation
            || state.progress.requestId != request.id)
        {
            return false;
        }
        state.progress.phase = phase;
        return true;
    }

    bool PrepareEnvironmentCpu(EnvironmentPreparationState& state,
        PreparedEnvironment& result, std::string& error)
    {
        const auto& request = result.request;
        if (!SetEnvironmentPreparationPhase(state, request, "Reading environment"))
        {
            return false;
        }
        std::error_code fileError;
        if (!file::is_regular_file(request.source, fileError))
        {
            error = "Select an existing HDR or cooked .ceibl environment";
            return false;
        }
        assets::CookedEnvironment cooked;
        if (request.source.extension() == ".ceibl")
        {
            if (!assets::ReadCookedEnvironment(request.source, cooked, error))
            {
                return false;
            }
            if (!SetEnvironmentPreparationPhase(state, request, "Validating environment"))
            {
                return false;
            }
            Hash::Sha256Digest recipe;
            if (!assets::EnvironmentRecipeIdentity(request.shaders,
                    cooked.cubeSize, cooked.brdfSize, recipe, error))
            {
                return false;
            }
            if (cooked.identity.recipe != recipe)
            {
                error = "Cooked environment recipe changed; recook the selected environment";
                return false;
            }
            result.cooked = std::move(cooked);
            return true;
        }

        assets::EnvironmentIdentity identity;
        if (!assets::EnvironmentSourceIdentity(request.source, identity.source, error)
            || !assets::EnvironmentRecipeIdentity(request.shaders, 512, 512, identity.recipe, error))
        {
            return false;
        }
        if (!SetEnvironmentPreparationPhase(state, request, "Reading environment cache"))
        {
            return false;
        }
        result.identity = identity;
        result.cachePath = request.cache / assets::EnvironmentCacheName(identity);
        std::string cacheError;
        if (assets::ReadCookedEnvironment(result.cachePath, cooked, cacheError, &identity))
        {
            result.cooked = std::move(cooked);
            return true;
        }
        if (!SetEnvironmentPreparationPhase(state, request, "Decoding HDR environment"))
        {
            return false;
        }
        // Decode owned bytes rather than consulting mutable PathFinder/settings
        // from a worker. The second digest rejects a source changed after lookup.
        std::ifstream input(request.source, std::ios::binary | std::ios::ate);
        if (!input || input.tellg() <= 0)
        {
            error = "HDR source could not be read";
            return false;
        }
        const auto length = static_cast<std::streamsize>(input.tellg());
        std::vector<std::byte> bytes(static_cast<size_t>(length));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), length))
        {
            error = "HDR source read failed";
            return false;
        }
        Hash::Sha256 hash;
        hash.Update(bytes.data(), bytes.size());
        if (hash.Finish() != identity.source)
        {
            error = "HDR source changed during preparation; select it again";
            return false;
        }
        const std::string_view encoded(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        if (!encoded.starts_with("#?RADIANCE") && !encoded.starts_with("#?RGBE"))
        {
            error = "Selected .hdr source is not a Radiance HDR image";
            return false;
        }
        result.equirect = Texture::LoadSharedFromMemory(bytes);
        if (!result.equirect)
        {
            error = "HDR decode failed";
            return false;
        }
        const auto image = result.equirect->GetImageView();
        if (image.IsEmpty() || image.IsCube() || image.ArraySize() != 1
            || image.MipLevels() != 1 || image.Format() != RHIFormat::RGBA32Float)
        {
            error = "HDR environment must decode to one 2D linear float image";
            return false;
        }
        return true;
    }

    void RunEnvironmentPreparation(const std::shared_ptr<EnvironmentPreparationState>& state)
    {
        for (;;)
        {
            EnvironmentPreparationRequest request;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->accepting || !state->pending)
                {
                    state->running = false;
                    return;
                }
                request = std::move(*state->pending);
                state->pending.reset();
            }
            const uint64_t requestId = request.id;
            const uint64_t generation = request.generation;
            std::unique_ptr<PreparedEnvironment> result;
            std::string error;
            bool prepared = false;
            try
            {
                result = std::make_unique<PreparedEnvironment>();
                result->request = std::move(request);
                prepared = PrepareEnvironmentCpu(*state, *result, error);
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
            }
            catch (...)
            {
                error = "Environment preparation failed";
            }
            std::unique_ptr<PreparedEnvironment> retired;
            bool failed = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->accepting && state->generation == generation
                    && state->progress.requestId == requestId)
                {
                    if (prepared)
                    {
                        retired = std::move(state->ready);
                        state->ready = std::move(result);
                        state->progress.phase = "Waiting for render thread";
                    }
                    else
                    {
                        state->progress.activeRequests = 0;
                        state->progress.phase = "Failed";
                        state->progress.error = error;
                        failed = true;
                    }
                }
            }
            if (failed)
            {
                Debug::PrintLog(spdlog::level::err, "[EnvironmentPreparation] " + error);
            }
            // Large stale payloads are destroyed without either renderer lock.
        }
    }

    void InvalidateEnvironmentPreparation(EnvironmentPreparationState& state, bool stop)
    {
        std::unique_ptr<PreparedEnvironment> retired;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            ++state.generation;
            if (stop)
            {
                state.accepting = false;
                state.progress.appliedRequestId = 0;
            }
            state.pending.reset();
            retired = std::move(state.ready);
            state.progress.activeRequests = 0;
            state.progress.applied = false;
            state.progress.phase = "Cancelled";
            state.progress.error.clear();
        }
    }

    std::unique_ptr<PreparedEnvironment> FailEnvironmentPreparation(
        EnvironmentPreparationState& state, const std::string& error)
    {
        std::unique_ptr<PreparedEnvironment> retired;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.progress.activeRequests == 0)
            {
                return {};
            }
            ++state.generation;
            state.pending.reset();
            retired = std::move(state.ready);
            state.progress.activeRequests = 0;
            state.progress.applied = false;
            state.progress.phase = "Failed";
            state.progress.error = error.empty() ? "Renderer is unavailable" : error;
        }
        return retired;
    }

    struct LiveState
    {
        ~LiveState()
        {
            InvalidateEnvironmentPreparation(*environmentPreparation, true);
            // The submission service intentionally has process lifetime. Drain
            // while every pass/cache member is still alive, before member teardown.
            StopRenderThread();
            const bool dx12Released = TeardownPipeline();
            const bool vulkanReleased = TeardownVulkanPipeline();
            if (!dx12Released || !vulkanReleased)
            {
                OutputDebugStringA("[EnhancedRenderer] Forced destruction cannot prove GPU idle or device loss; retaining GPU owners is no longer possible.\n");
                std::terminate();
            }
        }

        std::unique_ptr<EnhancedPbrCapture> pbrCapture;

        EnhancedPbrCapture* BeginPbrCapture(const EnhancedLiveFramePacket& frame,
            const EnhancedLiveViewPacket& view)
        {
            if (!pbrCapture || pbrCapture->result.state != EnhancedPbrCaptureState::Pending
                || pbrCapture->target != view.displayTarget
                || frame.frameId <= pbrCapture->afterFrameId) return nullptr;
            try { pbrCapture->Begin(frame, view, backend, emptyReferenceDraws, emptyReferenceDraws, lights, skyBoxPath); }
            catch (const std::exception& error)
            {
                pbrCapture->Fail(error.what());
                return nullptr;
            }
            return pbrCapture.get();
        }

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

        // 교체 가능한 대기 입력 하나와 TickLive가 이미 소비 중인 불변 입력 하나.
        // lifecycle delta는 순서대로 보존하고, 적재 상한에서는 생산자에 역압을 건다.
        static constexpr uint32_t kRenderQueueCapacity = 1;
        static constexpr double kSceneSoftAgeBudgetMs = 50.0;
        static constexpr uint64_t kSceneProgressNanoseconds = 250000000;
        static constexpr uint32_t kSceneCompletionPollMs = 2;
        // GT 는 앞 packet 이 소비될 때까지 이만큼만 기다린다. 넘으면 기존처럼 병합한다.
        // 렌더 스레드가 셰이더 준비로 오래 멈춰도 GT 가 이 주기로는 계속 돈다.
        static constexpr uint32_t kProducerPacingMs = 50;
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
        uint64_t renderCompletedFrameId{ 0 };
        uint64_t renderOverflowEvents{ 0 };
        uint64_t renderCoalescedFrames{ 0 };
        uint64_t renderCoalescedDeltas{ 0 };
        uint64_t renderBackPressureWaits{ 0 };
        uint64_t renderShutdownDrains{ 0 };
        uint64_t renderShutdownDiscardedDeltas{ 0 };
        uint64_t renderGpuAdmissionWaits{ 0 };
        uint64_t renderStalePixelSkips{ 0 };
        uint64_t renderOverBudgetAdmissions{ 0 };
        uint64_t renderDisplayLeaseSkips{ 0 };
        uint64_t renderProducerPacingWaits{ 0 };
        uint64_t renderDisplayLeaseWaits{ 0 };
        // RT 전용. 직전 프레임에서 고려한 뷰가 모두 표시 lease 때문에 건너뛰어졌다.
        bool renderDisplayLeaseBlocked{ false };
        uint64_t renderAdmittedFrameId{ 0 };
        uint64_t renderLastAdmissionNanoseconds{ 0 };
        double renderLastAdmissionAgeMs{ 0.0 };
        double renderMaxAdmissionAgeMs{ 0.0 };
        uint64_t renderDisplayPacingWaits{ 0 };

        // ── RT 깨움과 화면 주기 맞춤 ──
        //
        // RT 는 시간 조회로 깨지 않는다. 2 ms 로 적은 wait_for 가 이 기계에서
        // 실제로는 15.6 ms 타이머 눈금으로 자서, 눈금마다 두 장만 그리고
        // 나머지 시간을 잤다(초당 129장, 10-06 실측). 대신 GT 발행·정지는
        // renderWakeEvent 로, GPU 완료는 가장 오래된 진행 중 제출의 펜스 값에
        // 건 gpuCompletionEvent 로 깨운다. 둘 다 자동 재설정이라, 잠금을 놓은 뒤
        // 신호가 와도 다음 대기가 바로 돌아온다.
        //
        // 깨움만 바꾸면 RT 가 화면이 실을 수 없는 프레임을 초당 500장 넘게 그린다.
        // 표시 쪽은 수직 동기 없이 완성본마다 출력하므로 화면 주기의 기준점은
        // 합성기 시계뿐이다. 진입은 시계 한 번에 한 번이다.
        //
        // ★ 합성기 프레임 번호(DCompositionGetFrameId)로 세면 안 된다. 수직 동기
        //   없는 출력마다 합성 프레임이 생겨 에디터가 돌면 초당 250 가까이 오른다
        //   (60 Hz 화면, 10-06 실측). 시계 대기의 반환값만이 화면 주기를 센다.
        static constexpr DWORD kRenderWakeBackstopMs = 100;
        // 시계 대기의 상한. 시계가 멈추면(화면 꺼짐 등) 이 간격으로 깨어 아래
        // 경과 시간 규칙으로 진입한다.
        static constexpr DWORD kCompositorStallMs = 50;
        // RT 가 바빠 시계를 놓쳤으면 한 주기에서 이만큼 모자라도 진입한다.
        static constexpr uint64_t kMissedTickSlackNanoseconds = 2000000;
        Microsoft::WRL::Wrappers::Event renderWakeEvent{
            CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS) };
        Microsoft::WRL::Wrappers::Event gpuCompletionEvent{
            CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS) };
        // FixedRate 의 다음 진입 시각. 시간 대기는 타이머 눈금에 묶이므로
        // 고해상도 대기 타이머를 같은 대기 목록에 넣는다.
        Microsoft::WRL::Wrappers::HandleT<Microsoft::WRL::Wrappers::HandleTraits::HANDLENullTraits> pacingTimer{
            CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS) };
        std::atomic<EnhancedLivePacing> livePacing{};
        uint64_t armedGpuFenceValue{ 0 };          // RT 전용
        bool compositorTickSinceAdmission{ true };  // RT 전용
        uint64_t pacedAdmissionNanoseconds{ 0 };    // RT 전용
        // 윈도우 11 의 dcomp 함수. 없으면 Display 진입을 제한하지 않는다.
        using CompositorWaitFn = DWORD(WINAPI*)(UINT, const HANDLE*, DWORD);
        CompositorWaitFn compositorWait{ nullptr };

        // status/검증/PIX wait가 RT의 pipeline 포인터와 통계를 직접 읽을 때만
        // 잡는다. 일반 CE display 조회는 더 좁은 displayLifetimeMutex를 쓴다.
        mutable std::mutex renderStateMutex;
        ProxyCommandQueueController::Batch activeDeltaBatch; // RT 전용
        bool activeFrameDrainOnly{ false }; // 종료는 delta만 순서대로 소비하고 새 GPU 작업은 제출하지 않는다

        // RT가 renderStateMutex 아래에서만 호출한다. 상태 조회의 잠금 순서와
        // 맞추기 위해 renderQueueMutex를 잡은 채 renderStateMutex를 잡지 않는다.
        void CollectCompletedDisplays();
        void FailPendingGpuCaptures(const char* reason);
        uint32_t PendingGpuSubmissions() const;
        // RT 전용. Arm 은 renderStateMutex 아래에서, 나머지는 잠금 없이 부른다.
        void ArmGpuCompletionEvent(uint32_t pendingGpu);
        bool PacingAllowsAdmission() const;
        void MarkPacedAdmission();
        void WaitForRenderWork(bool untilPacingSlot, DWORD timeoutMs);
        bool ShouldSkipScenePixels(const EnhancedLiveFramePacket& frame);
        void RecordSceneAdmission(const EnhancedLiveFramePacket& frame);
        bool StartRenderThread(std::string& outError);
        bool PublishFrame(FrameSubmission submission);
        void StopRenderThread();
        EnhancedRenderThreadStats GetRenderThreadStats() const;
        bool WaitForRenderThreadIdle(uint32_t timeoutMilliseconds);

        // Cooked data is loaded before decode/generation. A raw HDR cache miss
        // generates the four maps once and publishes their pixels asynchronously.
        std::string                 skyBoxPath;
        std::shared_ptr<Texture> skyEquirect;
        std::optional<assets::CookedEnvironment> skyCooked;
        std::optional<assets::EnvironmentIdentity> skyCookIdentity;
        std::filesystem::path skyCookCachePath;
        bool                        skyBoxDirty{ true };
        bool                        skyBoxEnabled{ true };
        std::shared_ptr<EnvironmentPreparationState> environmentPreparation{
            std::make_shared<EnvironmentPreparationState>() };
        uint64_t skyPreparationRequest{ 0 };
        uint64_t skyPreparationGeneration{ 0 };

        void ApplyPreparedEnvironment(std::unique_ptr<PreparedEnvironment>& retired)
        {
            auto& preparation = *environmentPreparation;
            std::lock_guard<std::mutex> lock(preparation.mutex);
            if (!preparation.ready)
            {
                return;
            }
            retired = std::move(preparation.ready);
            const auto& request = retired->request;
            if (!preparation.accepting || preparation.generation != request.generation
                || preparation.progress.requestId != request.id
                || request.project != PathFinder::BaseProjectPath())
            {
                preparation.progress.activeRequests = 0;
                preparation.progress.phase = "Cancelled";
                return;
            }
            skyBoxPath = request.selection;
            skyEquirect.swap(retired->equirect);
            skyCooked.swap(retired->cooked);
            skyCookIdentity.swap(retired->identity);
            skyCookCachePath.swap(retired->cachePath);
            skyPreparationRequest = request.id;
            skyPreparationGeneration = request.generation;
            skyBoxDirty = true;
            preparation.progress.phase = "Applying environment on render thread";
            if (auto* settings = RuntimeSettings::TryGet())
            {
                settings->SetEnvironmentSelection(skyBoxPath, true);
            }
            lastError.clear();
        }

        void FinishEnvironmentPreparation(const std::string& error)
        {
            auto& preparation = *environmentPreparation;
            std::lock_guard<std::mutex> lock(preparation.mutex);
            if (skyPreparationRequest != 0 && preparation.generation == skyPreparationGeneration)
            {
                if (error.empty())
                {
                    // A newer request may arrive while this upload is recorded.
                    // Keep the actual installation event independently of it.
                    preparation.progress.appliedRequestId = skyPreparationRequest;
                }
                if (preparation.progress.requestId == skyPreparationRequest)
                {
                    preparation.progress.activeRequests = 0;
                    preparation.progress.applied = error.empty();
                    preparation.progress.phase = error.empty() ? "Upload recorded" : "Failed";
                    preparation.progress.error = error;
                }
            }
            skyPreparationRequest = 0;
        }

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
        std::array<std::chrono::steady_clock::time_point,
            kEnhancedLiveDisplayTargetCount> displayMissingSince{};

        static uint32_t DisplayTargetIndex(EnhancedLiveDisplayTarget target)
        {
            return static_cast<uint32_t>(target);
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
                const uint32_t targetIndex = DisplayTargetIndex(view.displayTarget);
                active[targetIndex] = true;
                EnhancedLiveDisplayEntrySnapshot& entry =
                    displaySnapshot.targets[targetIndex];
                if (entry.key != view.key ||
                    (entry.ready && entry.completedSceneEpoch != frame.sceneEpoch))
                {
                    entry = {};
                    displayPresentationKeys[targetIndex] = 0;
                    displayMissingSince[targetIndex] = {};
                }
                entry.key = view.key;
                entry.active = true;
                entry.sourceFrameId = frame.frameId;
                entry.sourceCaptureNanoseconds = frame.sourceCaptureNanoseconds;
                entry.sourceInputSequence = view.camera.editorInputSequence;
                entry.sourceCameraRevision = view.camera.editorCameraRevision;
            }
            for (uint32_t i = 0; i < kEnhancedLiveDisplayTargetCount; ++i)
            {
                if (active[i]) continue;
                if (displaySnapshot.targets[i].active)
                {
                    displaySnapshot.targets[i] = {};
                    displayMissingSince[i] = {};
                }
                displayPresentationKeys[i] = 0;
            }
            ++displaySnapshot.revision;
        }

        void InvalidateDisplayResultsLocked()
        {
            {
                std::lock_guard<std::mutex> lock(debugMutex);
                graphSnapshots.fill({});
            }
            for (uint32_t i = 0; i < kEnhancedLiveDisplayTargetCount; ++i)
            {
                EnhancedLiveDisplayEntrySnapshot& entry = displaySnapshot.targets[i];
                entry.ready = false;
                entry.completedFrameId = 0;
                entry.completedSceneEpoch = 0;
                entry.completedCamera = {};
                entry.completedCaptureNanoseconds = 0;
                entry.completedAgeMs = 0.0;
                entry.completedResizeGeneration = 0;
                entry.completedWidth = entry.completedHeight = 0;
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
            displayMissingSince.fill({});
        }

        bool PublishDisplayResultLocked(EnhancedLiveDisplayTarget displayTarget,
            const EnhancedLiveViewKey& key, uint64_t presentationKey,
            uint64_t completedFrameId, uint64_t promotionCount,
            uint32_t promotedSlotMask, uint32_t width, uint32_t height,
            uint64_t completedSceneEpoch, const FrameCameraSnapshot& completedCamera,
            uint64_t resultResizeGeneration = 0, bool previewComplete = false,
            uint64_t completedCaptureNanoseconds = 0, double completedAgeMs = 0.0)
        {
            const uint32_t targetIndex = DisplayTargetIndex(displayTarget);
            EnhancedLiveDisplayEntrySnapshot& entry =
                displaySnapshot.targets[targetIndex];
            if (!entry.active || entry.key != key || completedSceneEpoch != sceneEpoch.load())
            {
                return false;
            }
            entry.ready = 0 != presentationKey;
            entry.previewComplete = previewComplete;
            entry.completedFrameId = completedFrameId;
            entry.completedSceneEpoch = completedSceneEpoch;
            entry.completedCamera = completedCamera;
            entry.completedCaptureNanoseconds = completedCaptureNanoseconds;
            entry.completedAgeMs = completedAgeMs;
            entry.completedWidth = width;
            entry.completedHeight = height;
            entry.completedResizeGeneration = resultResizeGeneration != 0
                ? resultResizeGeneration : displaySnapshot.resizeGeneration;
            entry.promotionCount = promotionCount;
            entry.promotedSlotMask = promotedSlotMask;
            displayPresentationKeys[targetIndex] = presentationKey;
            ++displaySnapshot.revision;
            return true;
        }

        bool PublishVulkanDisplayResults()
        {
            if (!vulkanPipeline)
            {
                return false;
            }
            bool published = false;
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            std::lock_guard<std::mutex> viewLock(vulkanPipeline->viewMutex);
            for (uint32_t i = 0; i < EnhancedSceneRenderer::kMaxLiveCameraViews; ++i)
            {
                const VulkanLivePipeline::View& view = vulkanPipeline->views[i];
                if (!view.ready || !view.key.IsValid())
                {
                    continue;
                }
                const auto& entry = displaySnapshot.targets[DisplayTargetIndex(view.displayTarget)];
                if (entry.ready && entry.key == view.key && entry.completedFrameId == view.completedFrameId &&
                    entry.completedSceneEpoch == view.completedSceneEpoch &&
                    entry.completedResizeGeneration == view.completedResizeGeneration)
                {
                    continue;
                }
                published |= PublishDisplayResultLocked(view.displayTarget, view.key,
                    VulkanLivePipeline::kDisplayKeyBase + i + 1u,
                    view.completedFrameId, view.promotionCount,
                    view.promotedSlotMask, vulkanPipeline->width, vulkanPipeline->height,
                    view.completedSceneEpoch, view.completedCamera,
                    view.completedResizeGeneration, view.previewComplete,
                    view.completedCaptureNanoseconds, view.completedAgeMs);
            }
            return published;
        }

        // 프레임 입력. frameContext가 이들의 주소를 들므로 파이프라인과 무관한
        // 여기 멤버로 둔다 — 주소가 흔들리면 패스가 든 포인터가 전부 무효가 된다.
        FrameCameraSnapshot           cameraSnapshot{};

        // 카메라와 무관한 수집 결과(BuildDrawPool). 뷰는 여기서 골라
        // graphDraws/shadowDraws로 옮긴다 — 재질·본 팔레트 해석을 뷰마다
        // 반복하지 않기 위해서다.
        struct PooledDraw
        {
            EnhancedDrawItem     item{};
            // drawPool이 view 선별과 graph 기록까지 Mesh raw 주소를 운반하므로
            // 프록시 snapshot을 놓은 뒤에도 같은 generation을 명시적으로 붙든다.
            std::shared_ptr<Mesh> meshSource{};
            // item.modelMeshView의 정점·인덱스 저장소를 소유하는 immutable generation.
            std::shared_ptr<const assets::ModelAssetGeneration> generationSource{};
            std::shared_ptr<const material_graph::SceneMaterialSource> graphMaterialSource;
            math::aabb           worldBounds{};
            bool                 hasBounds{ false };
        };
        std::vector<PooledDraw>       drawPool;

        struct PooledSprite
        {
            math::matrix4x4 worldMatrix{ math::matrix4x4::identity() };
            std::shared_ptr<Texture> texture;
            BillboardType billboardType{ BillboardType::None };
            math::vector3 billboardAxis{ 0.f, 1.f, 0.f };
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

        // Empty live native lists retained for the standalone capture/replay
        // and common pass-input contracts. Scene materials use graphViewInput.
        // Legacy reference and capture APIs receive an explicitly empty stream.
        const std::vector<EnhancedDrawItem> emptyReferenceDraws;
        std::vector<EnhancedDrawItem> shadowDraws;

        std::vector<EnhancedDrawItem> graphDraws;
        // Same ordering as graphDraws/SceneDrawInput::sourceIndex. Eligibility
        // remains aligned when optional graph candidates are removed.
        std::vector<bool> graphShadowEligible;
        std::vector<bool> graphViewRequired;
        std::shared_ptr<const material_graph::SceneViewInput> graphViewInput;
        std::vector<EnhancedLight>    lights;
        // 마지막으로 민 뷰의 광원 선별 근거. status가 "씬에 몇 개인데 뷰가
        // 몇 개를 봤고 패스 한도에 몇 개가 걸리는지"를 답하는 데 쓴다 —
        // 목록만 보면 잘린 사실이 안 보인다.
        ViewLightSelection            lastLightSelection{};
        // 데칼은 frameContext가 아니라 패스에 직접 건넨다(SetDecals) —
        // 그리는 패스가 하나뿐이라 컨텍스트에 실을 이유가 없다. 여기 두는
        // 것은 매 프레임 재할당을 피하기 위해서다.
        std::vector<EnhancedDecalPass::Item> decals;
        const std::vector<EnhancedDecalPass::Item> previewDecals;
        bool materialPreviewView{false};
        EnhancedGizmoSceneData        gizmoData;   // 아이콘 벡터를 프레임 동안 소유

        uint32_t ssaoFrameIndex{ 0 };
        bool controlledCaptureFrame{ false };
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

        // GPU 수집 장부. mismatches 가 0 이 아니면 그만큼의 수치가 **다른
        // 제출의 것**이다(수집 직전 주석).
        uint64_t gpuCollects{ 0 };
        uint64_t gpuDroppedSlices{ 0 };
        uint64_t gpuZeroLengthSlices{ 0 };
        uint64_t gpuSpanViolations{ 0 };
        uint64_t gpuSliceUnderflows{ 0 };
        double   gpuMaxSubmitToCollectMs{ 0.0 };
        uint64_t gpuSpansEmitted{ 0 };
        uint64_t gpuAlignmentViolations{ 0 };
        uint64_t gpuUnalignedCollects{ 0 };
        uint64_t gpuAlignedCollects{ 0 };
        double   gpuMinSubmitToBeginMs{ 0.0 };
        double   gpuMinEndToCollectMs{ 0.0 };
        uint64_t gpuCollectMismatches{ 0 };
        uint64_t gpuQueryOverflowPasses{ 0 };

        // 마지막으로 수집에 성공한 것의 귀속. 숫자만 내고 **어느 프레임·어느 뷰
        // 것인지를 적지 않으면** 그 숫자가 맞는지 물을 수 없다 — 그것이 §0.5.10 이
        // 드러난 이유였다.
        uint64_t    lastGpuFrameId{ 0 };
        uint64_t    lastGpuSubmissionId{ 0 };
        uint64_t    lastGpuViewId{ 0 };
        EnhancedLiveGpuSpan lastGpuSpan{};
        std::string lastGpuCollectError;
        uint64_t framesInFlight{ 0 };   // 펜스 미완으로 새 제출을 쉰 틱 수
        uint32_t gpuMaxPendingSubmissions{ 0 };
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

        // W8 — 인코더가 조용히 버린 명령. 놓인 PSO 핸들·만료된 descriptor
        // 버전·주소 0이 여기 모인다. 0이 아니면 그 프레임의 그림을 믿으면 안 된다.
        uint64_t encoderDrops{ 0 };
        std::string lastEncoderDrop;

        uint32_t lastDrawCount{ 0 };    // 이번 프레임 GBuffer 드로우(0이면 빈 화면이다)
        uint32_t lastBatchCount{ 0 };
		uint32_t profileFrameDrawCount{ 0 }; // sum across views in one submission
		uint32_t profileFrameBatchCount{ 0 };
        uint32_t lastDecalCount{ 0 };
        uint32_t lastDecalBatchCount{ 0 };
        uint32_t lastSpriteCount{ 0 };
        uint32_t lastSpriteBatchCount{ 0 };
        uint32_t lastUIRectCount{ 0 };
        uint32_t lastUIBatchCount{ 0 };
        // W9 soak — 직전 GBuffer 밀봉이 본 모델 generation. 재임포트 뒤 옛 인스턴스와
        // 새 인스턴스가 한 프레임에 함께 그려질 때만 mixed 가 1 이상이 된다. 이 수가
        // 0 인 채로 soak 이 통과하면 재임포트 축은 자극되지 않은 것이다.
        uint32_t lastModelGenerationPairs{ 0 };   // 서로 다른 (modelId, generation)
        uint32_t lastMixedGenerationModels{ 0 };  // generation 이 둘 이상인 modelId
        // mixed 모델 중 가장 새 generation. 모든 모델의 최댓값을 쓰면 다른 모델의
        // 큰 번호가 재임포트의 전진을 가린다.
        uint64_t lastMixedNewestGeneration{ 0 };
        std::array<uint32_t, kEnhancedLiveDisplayTargetCount> viewSpriteCounts{};
        std::array<uint32_t, kEnhancedLiveDisplayTargetCount> viewUICounts{};
        std::array<EnhancedLiveShadowStats, kEnhancedLiveDisplayTargetCount> viewShadowStats{};
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

        void AppendGBufferMaterialStatus(std::string& status) const
        {
            status += "\n  GBuffer material route — Graph only";
        }

        void AppendForwardMaterialStatus(std::string& status) const
        {
            status += "\n  Forward material route — Graph only";
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
        std::array<LiveGraphSnapshot, kEnhancedLiveDisplayTargetCount> graphSnapshots{};
        std::array<bool, kEnhancedLiveDisplayTargetCount> graphSnapshotRequests{};

        bool HasGraphSnapshotRequest(EnhancedLiveDisplayTarget target)
        {
            std::lock_guard<std::mutex> lock(debugMutex);
            return graphSnapshotRequests[DisplayTargetIndex(target)];
        }

        bool ConsumeGraphSnapshotRequest(EnhancedLiveDisplayTarget target)
        {
            std::lock_guard<std::mutex> lock(debugMutex);
            const uint32_t index = DisplayTargetIndex(target);
            const bool requested = graphSnapshotRequests[index];
            graphSnapshotRequests[index] = false;
            return requested;
        }

        void PublishGraphSnapshot(EnhancedLiveDisplayTarget target, LiveGraphSnapshot snapshot)
        {
            std::lock_guard<std::mutex> lock(debugMutex);
            graphSnapshots[DisplayTargetIndex(target)] = std::move(snapshot);
        }

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
            debugSnapshot.pipelineNodes = desc.SnapshotNodes();
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

            debugSnapshot.backend = backend;
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
            const auto geometryStats = pipeline ? pipeline->graphMaterials.GeometryStats() :
                (vulkanPipeline ? vulkanPipeline->graphMaterials.GeometryStats() : material_graph::MeshSurfaceCacheStats{});
            debugSnapshot.materialGeometryUploads = geometryStats.uploads;
            debugSnapshot.materialGeometryUploadBytes = geometryStats.uploadedBytes;
            debugSnapshot.materialGeometryCacheHits = geometryStats.hits;
            debugSnapshot.materialGeometryResidentBytes = geometryStats.residentBytes;
            debugSnapshot.materialGeometryEntries = geometryStats.entries;
            debugSnapshot.materialGeometryTransforms = geometryStats.transforms;
            debugSnapshot.materialGeometryTransformHits = geometryStats.transformHits;
            debugSnapshot.materialGeometryOutputBytes = geometryStats.cachedOutputBytes;
            debugSnapshot.gpuMaxPendingSubmissions = gpuMaxPendingSubmissions;
            debugSnapshot.publishedFrameId = publishedFrameId;
            debugSnapshot.consumedFrameId = consumedFrameId;
            debugSnapshot.sceneEpoch = sceneEpoch;
            debugSnapshot.resizeGeneration = resizeGeneration;
            debugSnapshot.drawCount = lastDrawCount;
            debugSnapshot.batchCount = lastBatchCount;
            const auto* geometryPass = pipeline ? &pipeline->gbuffer
                : (vulkanPipeline ? &vulkanPipeline->gbuffer : nullptr);
            debugSnapshot.preparedMeshletBatchCount = geometryPass
                ? geometryPass->GetLastMeshletBatchCount() : 0u;
            debugSnapshot.meshletFallback = geometryPass
                ? geometryPass->GetLastMeshletFallback() : std::string{};
            debugSnapshot.currentFrameOcclusion = geometryPass && geometryPass->HasCurrentFrameOcclusion();
            debugSnapshot.occlusionFallback = geometryPass
                ? geometryPass->GetLastOcclusionFallback() : std::string{};
            debugSnapshot.skinningFallback = geometryPass
                ? geometryPass->GetLastSkinningFallback() : std::string{};
            debugSnapshot.indexedIndirectSupported = false;
            debugSnapshot.nonIndexedIndirectSupported = false;
            debugSnapshot.preparedGpuCandidates = 0;
            debugSnapshot.preparedGpuCompactedBins = 0;
            debugSnapshot.preparedGpuPreservedBins = 0;
            debugSnapshot.preparedGpuConservativeCandidates = 0;
            const auto addVisibility = [&](GpuGeometryVisibility::PreparedStats stats)
            {
                debugSnapshot.preparedGpuCandidates += stats.candidateCount;
                debugSnapshot.preparedGpuCompactedBins += stats.compactedBins;
                debugSnapshot.preparedGpuPreservedBins += stats.preservedBins;
                debugSnapshot.preparedGpuConservativeCandidates += stats.conservativeCandidates;
            };
            const auto addPipelineVisibility = [&](const auto& source)
            {
                if (source.frameContext.resources)
                {
                    const auto capabilities = source.frameContext.resources->GetIndirectDrawCapabilities();
                    debugSnapshot.indexedIndirectSupported = capabilities.indexedDraw;
                    debugSnapshot.nonIndexedIndirectSupported = capabilities.nonIndexedDraw;
                }
                addVisibility(source.gbuffer.GetGpuVisibilityStats());
                addVisibility(source.forward.GetGpuVisibilityStats());
                addVisibility(source.decal.GetGpuVisibilityStats());
                addVisibility(source.sprite.GetGpuVisibilityStats());
                addVisibility(source.graphMaterials.CameraVisibilityStats());
            };
            if (pipeline)
            {
                addPipelineVisibility(*pipeline);
            }
            else if (vulkanPipeline)
            {
                addPipelineVisibility(*vulkanPipeline);
            }
            debugSnapshot.decalCount = lastDecalCount;
            debugSnapshot.decalBatchCount = lastDecalBatchCount;
            debugSnapshot.spriteCount = lastSpriteCount;
            debugSnapshot.spriteBatchCount = lastSpriteBatchCount;
            debugSnapshot.uiRectCount = lastUIRectCount;
            debugSnapshot.uiBatchCount = lastUIBatchCount;
            debugSnapshot.shadow = viewShadowStats;
            debugSnapshot.cpuMs = lastCpuMs;
            debugSnapshot.gpuMs = lastGpuMs;
            debugSnapshot.gpuCollects = gpuCollects;
            debugSnapshot.gpuDroppedSlices = gpuDroppedSlices;
            debugSnapshot.gpuZeroLengthSlices = gpuZeroLengthSlices;
            debugSnapshot.gpuSpanViolations = gpuSpanViolations;
            debugSnapshot.gpuSliceUnderflows = gpuSliceUnderflows;
            debugSnapshot.gpuMaxSubmitToCollectMs = gpuMaxSubmitToCollectMs;
            debugSnapshot.gpuSpansEmitted = gpuSpansEmitted;
            debugSnapshot.gpuAlignmentViolations = gpuAlignmentViolations;
            debugSnapshot.gpuUnalignedCollects = gpuUnalignedCollects;
            debugSnapshot.gpuMinSubmitToBeginMs = gpuMinSubmitToBeginMs;
            debugSnapshot.gpuMinEndToCollectMs = gpuMinEndToCollectMs;
            debugSnapshot.gpuClock = dx12.ProfilerClock();
            debugSnapshot.gpuCollectMismatches = gpuCollectMismatches;
            debugSnapshot.gpuQueryOverflowPasses = gpuQueryOverflowPasses;
            debugSnapshot.lastGpuFrameId = lastGpuFrameId;
            debugSnapshot.lastGpuSubmissionId = lastGpuSubmissionId;
            debugSnapshot.lastGpuViewId = lastGpuViewId;
            debugSnapshot.lastGpuSpan = lastGpuSpan;
            debugSnapshot.lastGpuCollectError = lastGpuCollectError;
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
                target.spanMilliseconds = lastPassTimings[i].spanMilliseconds;
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
            if (pipeline)
            {
                outError = "DX12 pipeline is still retained; complete its safe teardown before rebuilding.";
                return false;
            }
            const auto traceBuild = [](const char* phase) {
                const char* value = std::getenv("CE_RENDER_PROGRESS_TRACE");
                if (value && std::string_view(value) == "1")
                {
                    std::printf("[render.pipeline.progress] phase=%s\n", phase);
                    std::fflush(stdout);
                }
            };
            traceBuild("invalidate.begin");

            // ★ 락은 **구축 전체가 아니라 무효화에만** 잡는다 (2026-09-14 실측).
            //
            //   예전에는 이 함수가 반환할 때까지 displayLifetimeMutex를 쥐고
            //   있었다. 그런데 구축의 대부분은 패스 초기화(desc.InitializeAll)이고
            //   그것이 셰이더 모듈 161개를 훑어 **6.6초**가 걸린다. 그동안 UI
            //   스레드는 씬뷰 본문의 GetLiveDisplayTexture에서 같은 락을 기다리고,
            //   PresentFrame이 m_sceneStructureMutex 안에서 도는 탓에 게임
            //   스레드까지 따라 멈춘다 — 메시지 펌프가 죽어 창이 '응답 없음'이
            //   되는 구간이 부팅마다 6.8초였다(실측 boot_timeline).
            //
            //   락이 실제로 지키는 것은 '표시 결과와 그 텍스처의 수명'이다.
            //   여기서 만드는 슬롯은 RT가 PublishDisplayResultLocked로 게시하기
            //   전까지 아무도 볼 수 없으므로 구축 자체는 락 밖이어도 된다.
            //   무효화만 락 아래서 끝내 두면, 구축 중에 조회하는 UI는 옛 텍스처가
            //   아니라 '아직 그림 없음'(textureId=0)을 보고 준비 중 화면을 그린다.
            {
                std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
                InvalidateDisplayResultsLocked();
                pipeline = std::make_unique<LivePipeline>();
                pipeline->resizeGeneration = displaySnapshot.resizeGeneration;
            }
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
            traceBuild("device.begin");
            if (!dx12.IsInitialized())
            {
                if (!dx12.Initialize(newWidth, newHeight, outError)) return false;
            }
            else if (!dx12.Resize(newWidth, newHeight, outError))
            {
                return false;
            }

            traceBuild("device.end");
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
            p.frameContext.draws = &emptyReferenceDraws;
            p.frameContext.forwardDraws = &emptyReferenceDraws;
            p.frameContext.animationPalettes = &p.animationPalettes;
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
            traceBuild("desc.begin");
            if (!BuildPipelineDesc(p, true, outError)) return false;
            traceBuild("passes.begin");

            if (!p.desc.InitializeAll(p.frameContext,
                static_cast<uint32_t>(LivePipeline::kMaxCameraViews), outError))
            {
                return false;
            }

            traceBuild("passes.end");
            p.gbuffer.SetKeepAlive(false);

            // IBL 생성기는 패스가 아니라 생성기다 — 그래프에 선언하지 않고
            // 프레임 시작에 큐브맵·조도·프리필터를 만들어 소비 패스에 건넨다.
            // 노드가 될 것이 아니므로 여기 남는다.
            traceBuild("ibl.begin");
            if (!p.ibl.Initialize(p.frameContext, outError)) return false;
            traceBuild("ibl.end");

            // 예열 장부: 파이프라인 구축이 끝난 때. 2026-09-14 실측으로 이
            // 구간이 6.84s 였고 그 내내 표시 락을 쥐어 게임 스레드까지 멈췄다 —
            // 예열을 밖에서 볼 수 있어야 하는 이유가 바로 이 구간이다.
            engine::warmup::mark(engine::warmup::stage::render_pipeline);

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

            traceBuild("display.begin");
            const bool displayCreated = CreateDisplaySlots(p, outError);
            traceBuild("display.end");
            return displayCreated;
        }

        bool CreateDisplaySlots(LivePipeline& p, std::string& outError)
        {
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

        // Caller holds displayLifetimeMutex after draining the live GPU queue.
        void ReleaseSizeResources(LivePipeline& p)
        {
            for (LivePipeline::CameraView& view : p.views)
            {
                // Graph destruction returns its transient textures to the pool.
                for (LivePipeline::DisplaySlot& slot : view.slots)
                {
                    finish_gpu_capture(slot.profilerToken, false,
                        "DX12 display resources retired before GPU query collection");
                    slot.graph.reset();
                }
                view.pendingQueue.clear();
                view.displaySlot = -1;
                view.promotionCount = 0;
                view.promotedSlotMask = 0;
                for (LivePipeline::DisplaySlot& slot : view.slots)
                {
                    if (slot.rhiTexture.IsValid())
                        dx12.Resources().ReleaseTexture(slot.rhiTexture);
                    // The shell may still reference an imported shared texture.
                    dx12.RetireDisplayTexture(slot.interopToken);
                    slot = {};
                }
                view.ssgi.ReleaseHistory(p.frameContext);
            }
            for (auto& [key, entries] : p.transientPool.freeList)
            {
                (void)key;
                for (const RGTransientPool::Entry& entry : entries)
                {
                    if (entry.handle.IsValid())
                        dx12.Resources().ReleaseTexture(entry.handle);
                }
            }
            p.transientPool.freeList.clear();
            p.transientPool.ClearAliasingCache();
        }

        bool ResizePipeline(uint32_t newWidth, uint32_t newHeight, std::string& outError)
        {
            LiveStopwatch timer;
            timer.Start();
            // Resize drains submitted live work before any graph/history is released.
            if (!dx12.Resize(newWidth, newHeight, outError)) return false;
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            InvalidateDisplayResultsLocked();
            LivePipeline& p = *pipeline;
            ReleaseSizeResources(p);
            p.width = newWidth;
            p.height = newHeight;
            p.resizeGeneration = displaySnapshot.resizeGeneration;
            p.frameContext.width = newWidth;
            p.frameContext.height = newHeight;
            // Passes derive their extent in PrepareFrame/Declare. SSGI resizes its
            // history there; PSOs, ShaderMeta variants, IBL and fixed fog volumes stay.
            if (!CreateDisplaySlots(p, outError)) return false;
            std::printf("[LiveResize] backend=dx12 generation=%llu size=%ux%u ms=%.3f\n",
                static_cast<unsigned long long>(p.resizeGeneration), newWidth, newHeight,
                timer.ElapsedMs());
            return true;
        }

        bool BuildVulkanPipeline(uint32_t newWidth, uint32_t newHeight,
            std::string& outError)
        {
            if (vulkanPipeline)
            {
                outError = "Vulkan pipeline is still retained; complete its safe teardown before rebuilding.";
                return false;
            }
            // 락 범위는 DX12 쪽 BuildPipeline과 같은 규약이다 — 무효화만 잡고
            // 구축은 밖에서 한다. 두 백엔드에 같은 규약을 적어 두지 않으면
            // 한쪽만 고쳐진 채로 남는다.
            {
                std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
                InvalidateDisplayResultsLocked();
            }
            vulkanPipeline = std::make_unique<VulkanLivePipeline>();
            if (!BuildPipelineDesc(*vulkanPipeline, true, outError))
            {
                vulkanPipeline.reset();
                return false;
            }
            if (!vulkanPipeline->Initialize(newWidth, newHeight, cameraSnapshot,
                emptyReferenceDraws, emptyReferenceDraws, lights, outError))
            {
                std::string shutdownError;
                if (!vulkanPipeline->Shutdown(shutdownError))
                {
                    outError += "\n" + shutdownError;
                    return false;
                }
                vulkanPipeline.reset();
                return false;
            }
            return true;
        }

        bool TeardownVulkanPipeline()
        {
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            if (!vulkanPipeline)
            {
                return true;
            }
            InvalidateDisplayResultsLocked();
            std::string error;
            if (!vulkanPipeline->Shutdown(error, pbrCapture.get()))
            {
                lastError = error;
                enabled = false;
                return false;
            }
            vulkanPipeline.reset();
            return true;
        }

        /// 파이프라인 해체. DX11에 보이는 것은 묘지로 보낸다(수명 규약).
        bool TeardownPipeline()
        {
            std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
            if (nullptr == pipeline)
            {
                return true;
            }
            InvalidateDisplayResultsLocked();
            LivePipeline& p = *pipeline;

            {
                std::string lifecycleError;
                bool drained = dx12.DrainForLifecycle(RHILifecycleCommand::BackendShutdown, lifecycleError);
                if (!drained && dx12.HasDeviceLossProof())
                {
                    drained = dx12.DrainForLifecycle(RHILifecycleCommand::UnrecoverableDeviceError, lifecycleError);
                }
                if (!drained)
                {
                    lastError = "DX12 lifecycle drain 실패: " + lifecycleError;
                    enabled = false;
                    return false;
                }
            }
            if (pbrCapture && pbrCapture->resourceBackend == EnhancedLiveBackend::DX12)
            {
                pbrCapture->Release(dx12.Resources());
            }
            ReleaseSizeResources(p);

            // 패스 해제는 노드 목록의 역순이다(슬라이스 2). 예전에는 그 역순을
            // 사람이 두 곳에 나눠 적었고(ssr·sss 자리와 decal 자리가 달랐다),
            // 틀리면 '가끔 죽는' 종류의 실패였다.
            p.desc.ShutdownAll(static_cast<uint32_t>(LivePipeline::kMaxCameraViews));
            p.desc.Clear();

            p.graphMaterials.ShutdownAfterIdle();
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
            fogTeardownPending = false;
            fogRetireFence = 0;

            dx12.ShutdownPipeline();

            pipeline.reset();
            return true;
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

            // The graph imports the cache-owned Pixel state and restores it
            // after the last consumer. Do not widen a shared asset out of graph.

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
        // 노드 목록은 저작 슬롯의 버전 배선을 정한다. 실제 실행 순서는
        // 같은 EnhancedRenderGraph의 명시적 리소스 의존성에서 컴파일한다.
        // 독립 패스의 안정 tie-break에만 저작 순서를 사용한다.
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
                node.initialize = [](const EnhancedFrameContext&, std::string&, uint32_t) { return true; };
                node.prepare = [&p](const EnhancedFrameContext& ctx, std::string& error, uint32_t)
                {
                    return p.shadow.PrepareGraphFrame(ctx, error);
                };
                node.writes = { LiveSlots::kShadowMap };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    p.shadow.DeclareGraphTargets(graph, ctx);
                    bb.Set(LiveSlots::kShadowMap, p.graphMaterials.DeclareShadow(graph, p.shadow.GetShadowMap()));
                };
                p.desc.AddNode(std::move(node));
            }

            // ── GBuffer ──
            {
                LivePassNode node;
                node.name = "GBuffer";
                node.instance = [&p](uint32_t) -> EnhancedRenderPass* { return &p.gbuffer; };
                node.initialize = [](const EnhancedFrameContext&, std::string&, uint32_t) { return true; };
                node.prepare = [](const EnhancedFrameContext&, std::string&, uint32_t) { return true; };
                node.writes = {
                    LiveSlots::kGBufferDiffuse, LiveSlots::kGBufferMetalRough,
                    LiveSlots::kGBufferNormal,  LiveSlots::kGBufferEmissive,
                    LiveSlots::kGBufferBitmask, LiveSlots::kGBufferDepth,
                };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    p.gbuffer.DeclareGraphTargets(graph, ctx);
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

            {
                LivePassNode node;
                node.name = "LX.Scene.GBuffer";
                node.modifies = {
                    LiveSlots::kGBufferDiffuse, LiveSlots::kGBufferMetalRough,
                    LiveSlots::kGBufferNormal, LiveSlots::kGBufferEmissive,
                    LiveSlots::kGBufferBitmask, LiveSlots::kGBufferDepth,
                };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext&, const LiveFrameBinding&)
                {
                    const auto outputs = p.graphMaterials.DeclareGBuffer(graph, GatherGBufferOutputs(bb), false);
                    bb.Set(LiveSlots::kGBufferDiffuse, outputs.diffuse);
                    bb.Set(LiveSlots::kGBufferMetalRough, outputs.metalRough);
                    bb.Set(LiveSlots::kGBufferNormal, outputs.normal);
                    bb.Set(LiveSlots::kGBufferEmissive, outputs.emissive);
                    bb.Set(LiveSlots::kGBufferBitmask, outputs.bitmask);
                    bb.Set(LiveSlots::kGBufferDepth, outputs.depth);
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
                    p.decal.SetDecals(materialPreviewView ? previewDecals : decals);
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
                    const auto inputs = GatherGBufferOutputs(bb);
                    p.decal.SetInputs(inputs);
                    p.decal.Declare(graph, ctx);
                    const auto outputs = p.decal.GetOutputs();
                    bb.Set(LiveSlots::kGBufferDiffuse, outputs.diffuse);
                    bb.Set(LiveSlots::kGBufferNormal, outputs.normal);
                    bb.Set(LiveSlots::kGBufferMetalRough, outputs.metalRough);
                    if (p.decal.HasPreparedDecals())
                    {
                        p.graphMaterials.DeclareDecalInputs(graph, outputs, p.decal.GetBaseline());
                    }
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
                    p.ssao.SetFrameIndex(controlledCaptureFrame ? 0 : ssaoFrameIndex++);
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
                    LiveSlots::kAmbientOcclusion,
                };
                node.writes = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    p.deferred.SetInputs(GatherGBufferOutputs(bb));
                    p.deferred.SetAmbientOcclusion(bb.Get(LiveSlots::kAmbientOcclusion));
                    p.deferred.SetShadow(bb.Get(LiveSlots::kShadowMap), p.shadow.GetShadowData());
                    p.forward.SetShadow(bb.Get(LiveSlots::kShadowMap), p.shadow.GetShadowData());
                    p.deferred.Declare(graph, ctx);
                    bb.Set(LiveSlots::kLitColor, p.deferred.GetOutput());
                };
                p.desc.AddNode(std::move(node));
            }

            {
                LivePassNode node;
                node.name = "LX.Scene.Color";
                node.reads = {
                    LiveSlots::kAmbientOcclusion, LiveSlots::kShadowMap,
                };
                // Refraction completes the opaque HDR first, then installs the
                // closest transmitting surface in the shared GBuffer and depth.
                node.modifies = {
                    LiveSlots::kLitColor, LiveSlots::kGBufferDepth, LiveSlots::kGBufferBitmask,
                    LiveSlots::kGBufferDiffuse, LiveSlots::kGBufferMetalRough,
                    LiveSlots::kGBufferNormal, LiveSlots::kGBufferEmissive,
                };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext&, const LiveFrameBinding&)
                {
                    bb.Set(LiveSlots::kLitColor, p.graphMaterials.DeclareColor(graph, GatherGBufferOutputs(bb),
                        bb.Get(LiveSlots::kLitColor), bb.Get(LiveSlots::kAmbientOcclusion),
                        bb.Get(LiveSlots::kShadowMap)));
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
                node.declare = [this, &p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding& binding)
                {
                    p.skyBox.SetShowEnvironment(skyBoxEnabled &&
                        !(binding.viewFlags & LiveViewFlags::kHideSkyBox));
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
                node.initialize = [&p](const EnhancedFrameContext& ctx, std::string& error, uint32_t)
                {
                    return p.forward.InitializeGraphLighting(ctx, error);
                };
                node.reads = { LiveSlots::kGBufferDepth };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext& ctx, const LiveFrameBinding&)
                {
                    EnhancedForwardPass::Inputs inputs{};
                    inputs.depth = bb.Get(LiveSlots::kGBufferDepth);
                    inputs.lighting = bb.Get(LiveSlots::kLitColor);
                    p.forward.SetInputs(inputs);
                    p.forward.SetGraphMaterials(&p.graphMaterials);
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
                    {
                        bb.Set(LiveSlots::kLitColor, p.sprite.GetOutput());
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

            // Material media compose the completed Scene before tone mapping.
            // Volume-only boundaries never become opaque GBuffer depth owners.
            {
                LivePassNode node;
                node.name = "LX.Scene.Volume";
                node.reads = { LiveSlots::kGBufferDepth, LiveSlots::kShadowMap };
                node.modifies = { LiveSlots::kLitColor };
                node.declare = [&p](LiveBlackboard& bb, EnhancedRenderGraph& graph,
                    const EnhancedFrameContext&, const LiveFrameBinding&)
                {
                    bb.Set(LiveSlots::kLitColor, p.graphMaterials.DeclareVolume(graph,
                        bb.Get(LiveSlots::kLitColor), bb.Get(LiveSlots::kGBufferDepth), bb.Get(LiveSlots::kShadowMap)));
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
                    inputs.cloudShadow = graph.FindImportedTexture(p.fogCloudNeutralHandle);
                    if (!inputs.cloudShadow.IsValid())
                    {
                        inputs.cloudShadow = graph.ImportTexture(p.fogCloudNeutralHandle,
                            RHIResourceState::ShaderResource, "Fog.CloudNeutral");
                    }
                    constexpr auto fogNoiseState = std::is_same_v<PipelineT, LivePipeline>
                        ? RHIResourceState::PixelShaderResource : RHIResourceState::ShaderResource;
                    inputs.blueNoise = graph.FindImportedTexture(p.fogBlueNoiseHandle);
                    if (!inputs.blueNoise.IsValid())
                    {
                        inputs.blueNoise = graph.ImportTexture(p.fogBlueNoiseHandle,
                            fogNoiseState, "Fog.BlueNoise");
                    }
                    graph.RequireImportedFinalState(inputs.blueNoise, fogNoiseState);
                    graph.RequireImportedFinalState(inputs.cloudShadow, RHIResourceState::ShaderResource);
                    view.fog.SetInputs(inputs);
                    // 셰이더가 캐스케이드 슬라이스 2를 짚는다 — DX11이 마지막
                    // 캐스케이드 행렬을 넘기는 것과 짝이다.
                    view.fog.SetShadowMatrix(p.shadow.GetShadowData()
                        .lightViewProjection[EnhancedShadowPass::kCascadeCount - 1]);
                    view.fog.SetFrameIndex(controlledCaptureFrame ? 0 : frameCounter);
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
                        const RGHandle sharedInitial = graph.ImportTexture(
                            binding.sharedTarget, RHIResourceState::CopyDest, "Live.Shared");
                        const bool explicitAccess = graph.GetSchedulingMode()
                            != RGSchedulingMode::DeclarationOrder;
                        const RGHandle sharedHandleRG = graph.GetSchedulingMode()
                            == RGSchedulingMode::ExplicitVersioned
                            ? graph.Write(sharedInitial) : sharedInitial;

                        graph.AddPass("live_present",
                            { { finalHandle, RHIResourceState::CopySource,
                                explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState },
                              { sharedHandleRG, RHIResourceState::CopyDest,
                                explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState } },
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
                        { { finalHandle, RHIResourceState::CopySource,
                            graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder
                                ? RGAccessMode::Read : RGAccessMode::LegacyState } },
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
            math::vector3 center{};
            math::vector3 right{};
            math::vector3 down{};
            bool valid{ false };
        };

        static math::matrix4x4 MakeSpriteMatrix(const math::vector3& right,
            const math::vector3& down, const math::vector3& center)
        {
            const math::vector3 normal = math::normalize(math::cross(right, down));
            return math::matrix4x4{
                right.x, right.y, right.z, 0.f,
                down.x, down.y, down.z, 0.f,
                normal.x, normal.y, normal.z, 0.f,
                center.x, center.y, center.z, 1.f };
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
                const float projectionX = std::abs(gameCamera->projection(0, 0));
                const float projectionY = std::abs(gameCamera->projection(1, 1));
                if (projectionX < 1e-6f || projectionY < 1e-6f) return plane;

                float distance = (std::max)(image.planeDistance,
                    gameCamera->nearPlane + 0.01f);
                if (gameCamera->farPlane > gameCamera->nearPlane)
                    distance = (std::min)(distance, gameCamera->farPlane - 0.01f);

                const float planeWidth = gameCamera->isOrthographic
                    ? 2.f / projectionX : 2.f * distance / projectionX;
                const float planeHeight = gameCamera->isOrthographic
                    ? 2.f / projectionY : 2.f * distance / projectionY;

                plane.center = gameCamera->eyePosition + gameCamera->forward * distance;
                plane.right = math::normalize(gameCamera->right) * planeWidth;
                plane.down = math::normalize(gameCamera->up) * -planeHeight;
                plane.valid = true;
                return plane;
            }

            const float centerX = image.canvasRect.x + rootWidth * 0.5f;
            const float centerY = image.canvasRect.y + rootHeight * 0.5f;
        const math::matrix4x4& canvasWorld = image.canvasWorld;
            plane.center = math::transform_point(
                math::vector3{ centerX, -centerY, 0.f }, canvasWorld);
            plane.right = math::transform_direction(
                math::vector3{ rootWidth, 0.f, 0.f }, canvasWorld);
            plane.down = math::transform_direction(
                math::vector3{ 0.f, -rootHeight, 0.f }, canvasWorld);
            plane.valid = math::length_sq(plane.right) > 1e-8f &&
                math::length_sq(plane.down) > 1e-8f;
            return plane;
        }

        static bool ResolveImageRect(const UIRenderProxy::ImageData& image,
            ClippedDestination& dst, math::vector4& uv)
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
            if (HasUIEffect(image.filpEffect, UIEffects::UIEffects_FlipHorizontally))
                std::swap(uv.x, uv.z);
            if (HasUIEffect(image.filpEffect, UIEffects::UIEffects_FlipVertically))
                std::swap(uv.y, uv.w);
            return true;
        }

        static bool AppendImageToPlane(const UIRenderProxy::ImageData& image,
            const CanvasPlane& plane, bool enableDepth,
            std::vector<EnhancedSpritePass::Item>& output)
        {
            if (!plane.valid) return false;

            ClippedDestination dst{};
            math::vector4 uv{};
            if (!ResolveImageRect(image, dst, uv)) return false;

            const float rootWidth = image.canvasRect.z;
            const float rootHeight = image.canvasRect.w;
            const float rootCenterX = image.canvasRect.x + rootWidth * 0.5f;
            const float rootCenterY = image.canvasRect.y + rootHeight * 0.5f;
            const float rectCenterX = (dst.left + dst.right) * 0.5f;
            const float rectCenterY = (dst.top + dst.bottom) * 0.5f;
            const float rectWidth = dst.right - dst.left;
            const float rectHeight = dst.bottom - dst.top;

            const math::vector3 center = plane.center +
                plane.right * ((rectCenterX - rootCenterX) / rootWidth) +
                plane.down * ((rectCenterY - rootCenterY) / rootHeight);

            const float worldWidth = math::length(plane.right) * rectWidth / rootWidth;
            const float worldHeight = math::length(plane.down) * rectHeight / rootHeight;
            const math::vector3 rightUnit = math::normalize(plane.right);
            const math::vector3 downUnit = math::normalize(plane.down);
            const float c = std::cos(image.rotation);
            const float s = std::sin(image.rotation);
            const math::vector3 right = (rightUnit * c + downUnit * s) * worldWidth;
            const math::vector3 down = (rightUnit * -s + downUnit * c) * worldHeight;

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
            const float width = math::length(plane.right);
            const float height = math::length(plane.down);
            const float thickness = (std::max)(0.02f,
                (std::min)(width, height) * 0.004f);
            const math::vector3 rightUnit = math::normalize(plane.right);
            const math::vector3 downUnit = math::normalize(plane.down);

            const auto add = [&](const math::vector3& center, const math::vector3& right,
                const math::vector3& down)
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

            add(plane.center - plane.down * 0.5f, plane.right, downUnit * thickness);
            add(plane.center + plane.down * 0.5f, plane.right, downUnit * thickness);
            add(plane.center - plane.right * 0.5f, rightUnit * thickness, plane.down);
            add(plane.center + plane.right * 0.5f, rightUnit * thickness, plane.down);
        }

        void BuildDrawPool()
        {
            drawPool.clear();
            graphDraws.clear();
            graphShadowEligible.clear();
            graphViewRequired.clear();
            graphViewInput.reset();
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
                // Visibility is published by Scene even when the Animator is disabled.
                // Exclude the mesh before building draws for any render pass.
                if (!proxy->m_isEnabled) return;

                // PHASE 3.75 MBC7 — typed generation 뷰가 정본이다. generation
                // descriptor와 immutable 저장소를 대조해 뷰를 짓고(BuildRHIModelMeshView),
                // 실패·부재면 experiment 핸들 → legacy Mesh 순으로 내려간다(MBC9 은퇴).
                RHIModelMeshView modelView{};
                if (!proxy->m_modelGeneration
                    || !BuildRHIModelMeshView(*proxy->m_modelGeneration,
                        proxy->m_modelMeshIndex, modelView))
                {
                    return;
                }

                PooledDraw pooled{};
                pooled.item.worldMatrix = proxy->m_worldMatrix;
                pooled.item.modelMeshView = modelView;
                pooled.generationSource = proxy->m_modelGeneration;
                // I6-C — 신원 키와 반경을 값으로 싣는다.
                pooled.item.geometryKey = MakeGeometryKey(pooled.item);
                {
                    const math::aabb& bounds = proxy->m_modelGeneration
                        ->Meshes()[proxy->m_modelMeshIndex].bounds;
                    pooled.item.boundCenter = bounds.is_empty() ? math::vector3{} : bounds.center;
                    pooled.item.boundRadius = bounds.is_empty()
                        ? 0.f : math::length(bounds.extents);
                }

                const math::matrix4x4* palette = proxy->m_paletteArena
                    ? proxy->m_paletteArena->resolve(proxy->m_paletteOffset,
                        proxy->m_boneCount) : nullptr;
                if (proxy->m_isAnimationEnabled
                    && (HashedGuid::kInvalidId != proxy->m_animatorGuid)
                    && palette)
                {
                    pooled.item.bonePalette = palette;
                    pooled.item.boneCount = proxy->m_boneCount;
                    pooled.item.animatorKey = static_cast<uint64_t>(proxy->m_animatorGuid);
                }

                if (proxy->m_graphMaterialSource)
                {
                    pooled.graphMaterialSource = proxy->m_graphMaterialSource;
                    pooled.item.materialGraphInstance = pooled.graphMaterialSource->instance;
                    pooled.item.materialGraphSlot = pooled.graphMaterialSource->materialSlot;
                    pooled.item.coverage = pooled.graphMaterialSource->coverage;
                }

                pooled.worldBounds = proxy->m_worldBounds;
                pooled.hasBounds = proxy->m_hasWorldBounds;

                drawPool.push_back(pooled);
            };

            const auto poolFoliage = [this](const FoliageRenderProxy* proxy)
            {
                if (!proxy->m_isEnabled || proxy->m_isCulled) return;

                for (FoliageRenderProxy::DrawSource source :
                    proxy->CaptureDrawSources())
                {
                    // PHASE 3.75 MBC8 — poolMesh와 같은 typed 축이 첫째다.
                    RHIModelMeshView modelView{};
                    if (!source.modelGeneration
                        || !BuildRHIModelMeshView(*source.modelGeneration,
                            source.modelMeshIndex, modelView))
                    {
                        continue;
                    }

                    PooledDraw pooled{};
                    pooled.item.worldMatrix = source.worldMatrix;
                    pooled.worldBounds = source.worldBounds;
                    pooled.hasBounds = !source.worldBounds.is_empty();
                    pooled.item.modelMeshView = modelView;
                    pooled.generationSource = source.modelGeneration;

                    if (source.graphMaterialSource)
                    {
                        pooled.graphMaterialSource = std::move(source.graphMaterialSource);
                        pooled.item.materialGraphInstance = pooled.graphMaterialSource->instance;
                        pooled.item.materialGraphSlot = pooled.graphMaterialSource->materialSlot;
                        pooled.item.coverage = pooled.graphMaterialSource->coverage;
                    }

                    // I6-C — poolMesh와 같은 규약: 신원 키와 반경을 값으로
                    //   싣는다(패스가 Mesh를 역참조하지 않게).
                    pooled.item.geometryKey = MakeGeometryKey(pooled.item);
                    {
                        const math::aabb& bounds = source.modelGeneration
                            ->Meshes()[source.modelMeshIndex].bounds;
                        pooled.item.boundCenter = bounds.is_empty() ? math::vector3{} : bounds.center;
                    pooled.item.boundRadius = bounds.is_empty()
                            ? 0.f : math::length(bounds.extents);
                    }

                    drawPool.push_back(std::move(pooled));
                }
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

                if (const FoliageRenderProxy* foliage =
                    proxy->As<FoliageRenderProxy>())
                {
                    poolFoliage(foliage);
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
            materialPreviewView = viewPacket.displayTarget == EnhancedLiveDisplayTarget::MaterialPreview;
            totalSeconds = frame.totalSeconds;

            gizmoData = viewPacket.gizmos
                ? *viewPacket.gizmos : EnhancedGizmoSceneData{};

            shadowDraws.clear();
            graphDraws.clear();
            graphShadowEligible.clear();
            graphViewRequired.clear();
            graphViewInput.reset();
            lights.clear();
            worldSprites.clear();
            uiRects.clear();
            if (materialPreviewView)
            {
                const auto& source = viewPacket.materialPreview;
                if (!source || !source->instance) return false;
                static const MaterialPreviewSphere sphere;
                EnhancedDrawItem draw;
                draw.geometryKey = HashModelMeshHandle(sphere.mesh.handle);
                draw.modelMeshView = sphere.mesh;
                draw.worldMatrix = math::matrix4x4::identity();
                draw.boundRadius = 1.f;
                draw.coverage = source->coverage;
                draw.materialGraphInstance = source->instance;
                draw.materialGraphSlot = source->materialSlot ? source->materialSlot : 1;
                material_graph::SceneInputView inputView;
                inputView.frameId = frame.frameId;
                inputView.sceneEpoch = frame.sceneEpoch;
                inputView.viewId = viewPacket.key.viewId;
                inputView.historyRevision = viewPacket.key.historyRevision;
                inputView.width = frame.width;
                inputView.height = frame.height;
                inputView.camera = cameraSnapshot;
                std::string error;
                static const std::array<MaterialPreviewFloor, 2> floor{MaterialPreviewFloor{0}, MaterialPreviewFloor{1}};
                std::vector<EnhancedDrawItem> previewDraws{draw};
                for (unsigned i = 0; i < floor.size(); ++i)
                {
                    const auto& ground = viewPacket.materialPreviewFloor[i];
                    if (!ground || !ground->instance) continue;
                    EnhancedDrawItem tile;
                    tile.geometryKey = HashModelMeshHandle(floor[i].mesh.handle);
                    tile.modelMeshView = floor[i].mesh;
                    tile.worldMatrix = math::matrix4x4::identity();
                    tile.boundRadius = 8.f;
                    tile.coverage = ground->coverage;
                    tile.materialGraphInstance = ground->instance;
                    tile.materialGraphSlot = ground->materialSlot;
                    previewDraws.push_back(std::move(tile));
                }
                if (!material_graph::SceneViewInput::Seal(inputView, previewDraws, {}, graphViewInput, error))
                {
                    lastError = "Material preview: " + error;
                    return false;
                }
                EnhancedLight key;
                key.direction = {0.4f, -0.6f, 0.7f, 0.f};
                key.color = {1.f, 0.95f, 0.9f, 3.f};
                lights.push_back(key);
                lastPoolDraws = static_cast<uint32_t>(previewDraws.size());
                lastCulledDraws = 0;
                lastLightSelection = {};
                return true;
            }
            // decals는 비우지 않는다 — BuildDrawPool이 프레임당 한 번 채우고
            // 뷰 둘이 같은 목록을 본다. 여기서 비우면 두 번째 뷰가 빈 것을 쓴다.

            // SpriteRenderer는 모든 뷰에서 월드 쿼드로 보인다. None은 기존
            // XZ Quad의 축/UV를 그대로 옮기고, Billboard만 이 뷰의 카메라를 쓴다.
            for (const PooledSprite& sprite : spritePool)
            {
                const math::vector3 center = sprite.worldMatrix.translation();
                const float width = 2.f * math::length(sprite.worldMatrix.right());
                const float height = 2.f * math::length(sprite.worldMatrix.forward());
                if (width <= 1e-6f || height <= 1e-6f) continue;

                math::vector3 right{};
                math::vector3 down{};
                if (BillboardType::None == sprite.billboardType)
                {
                    right = sprite.worldMatrix.right() * 2.f;
                    down = sprite.worldMatrix.forward() * -2.f;
                }
                else if (BillboardType::Spherical == sprite.billboardType)
                {
                    right = math::normalize(cameraSnapshot.right) * width;
                    down = math::normalize(cameraSnapshot.up) * -height;
                }
                else
                {
                    math::vector3 axis = sprite.billboardAxis;
                    if (math::length_sq(axis) < 1e-8f)
                        axis = math::vector3::unit_y();
                    else
                        axis = math::normalize(axis);
                    const math::vector3 toCamera = cameraSnapshot.eyePosition - center;
                    math::vector3 rightUnit = math::cross(toCamera, axis);
                    if (math::length_sq(rightUnit) < 1e-8f)
                        rightUnit = cameraSnapshot.right;
                    right = math::normalize(rightUnit) * width;
                    down = axis * -height;
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
                if (EnhancedLiveDisplayTarget::Game == frame.views[i].displayTarget)
                {
                    gameCamera = &frame.views[i].camera;
                    break;
                }
            }

            // Overlay는 Game View의 최종 픽셀 좌표로 간다. 기존 RectTransform은
            // 중앙 원점이므로 BuildRectsFromQueue가 화면 절반만큼 이동한다.
            if (HasViewFlag(viewPacket.viewFlags,
                EnhancedLiveViewFlags::ScreenSpaceUI) && !uiProxyPointers.empty())
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
                    HasViewFlag(viewPacket.viewFlags,
                        EnhancedLiveViewFlags::ScreenSpaceUI)) continue;

                const CanvasPlane plane = ResolveCanvasPlane(*image, gameCamera);
                const bool depth = CanvasRenderMode::WorldSpace == image->renderMode;
                AppendImageToPlane(*image, plane, depth, worldSprites);

                if (HasViewFlag(viewPacket.viewFlags,
                    EnhancedLiveViewFlags::CanvasPreview))
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
            //   projection 기반 frustum 생성이 원근 전용이라, 없는
            //   절두체로 자르느니 다 그리는 쪽이 옳다.
            math::bounding_frustum frustum;
            const bool cullDraws = BuildViewFrustum(cameraSnapshot, frustum);

            const ViewLightSelection lightSelection =
                SelectLightsForView(renderScene->GetLightProxySnapshot(), cameraSnapshot);
            lights = lightSelection.lights;
            lastLightSelection = lightSelection;
            math::vector3 shadowDirection{0, -1, 0};
            float strongestShadowLight = -1.f;
            bool hasShadowLight = false;
            for (const auto& light : lights)
            {
                if (uint32_t(light.position.w) != 0 || light.color.a <= strongestShadowLight) continue;
                strongestShadowLight = light.color.a;
                shadowDirection = {light.direction.x, light.direction.y, light.direction.z};
                hasShadowLight = true;
            }
            if (math::length_sq(shadowDirection) < 1e-6f) shadowDirection = {0, -1, 0};
            shadowDirection = math::normalize(shadowDirection);
            const EnhancedShadowPass* shadowPass = backend == EnhancedLiveBackend::DX12
                ? (pipeline ? &pipeline->shadow : nullptr)
                : (vulkanPipeline ? &vulkanPipeline->shadow : nullptr);
            const auto receivers = shadow_math::ReceiverCascades(cameraSnapshot, shadowDirection,
                shadowPass ? shadowPass->GetShadowDistance() : shadow_math::kDefaultDistance,
                shadowPass ? shadowPass->GetCascadeBlendBand() : .15f);
            const bool gpuVisibility = backend == EnhancedLiveBackend::DX12
                ? dx12.Resources().GetIndirectDrawCapabilities().indexedDraw
                : (vulkanPipeline && vulkanPipeline->resources.GetIndirectDrawCapabilities().indexedDraw);
            std::vector<size_t> graphShadowIndices;
            uint32_t culled = 0;
            const material_graph::SceneInputBudget sceneInputBudget;
            uint32_t optionalGpuCandidates = 0;
            bool hasOptionalGraphCandidates = false;
            for (const PooledDraw& pooled : drawPool)
            {
                // Select the visible/caster union before the bounded Graph seal.
                // Binding shape does not prove custom vertex position semantics.
                // Resolve the exact already-sealed generation before CPU rejection,
                // including on devices without indirect support and before the
                // optional offscreen candidate budget can discard a custom draw.
                bool knownPositionContract = bool(pooled.graphMaterialSource);
                const bool conservative = !knownPositionContract || !shadow_math::FinitePose(pooled.item);
                const bool sourceVisible = conservative || !cullDraws || !pooled.hasBounds || pooled.worldBounds.is_empty()
                    || math::intersects(frustum, pooled.worldBounds);
                // Skin weights/current uploaded pose are validated later. Admit
                // every skinned CAMERA candidate before CPU rejection/budgeting;
                // a bind-pose/proxy box is not a proof for arbitrary current skin.
                const bool visible = pooled.item.boneCount != 0 || sourceVisible;
                if (!pooled.graphMaterialSource || !pooled.item.materialGraphInstance)
                {
                    lastError = "Live scene draw requires a prepared material graph source.";
                    ++frameFailures;
                    return false;
                }
                // Shadow relevance is independent from camera admission. Weight
                // normalization is not proven here, so skin geometry cannot use
                // the convex-hull pose bound as an upstream rejection proof.
                // Unknown bounds conservatively retain eligible shadow casters.
                const auto casterBounds = pooled.item.boneCount != 0
                    ? shadow_math::Sphere{} : shadow_math::WorldBounds(pooled.item);
                const bool relevantToShadow = shadow_math::RelevantToView(sourceVisible,
                    casterBounds, receivers, shadowDirection, hasShadowLight);
                // Static and skinned candidates reach the GPU even outside the camera.
                // Shadow selection remains independent; it must not expand with
                // the geometry visibility working set.
                // Material effects and ordering do not choose the submission
                // mechanism. Native/Graph geometry retains its original command
                // order while GPU-generated counts gate accepted candidates.
                // Unknown position semantics above are always conservative.
                const bool gpuCandidate = gpuVisibility;
                if (!visible && !relevantToShadow && !gpuCandidate)
                {
                    ++culled;
                    continue;
                }
                // Optional offscreen work must not grow CPU staging without a
                // bound or displace the original visible/caster working set.
                if (!visible && !relevantToShadow)
                {
                    if (optionalGpuCandidates >= sceneInputBudget.draws)
                    {
                        ++culled;
                        continue;
                    }
                    ++optionalGpuCandidates;
                    hasOptionalGraphCandidates |= bool(pooled.graphMaterialSource);
                }
                size_t shadowIndex = static_cast<size_t>(-1);
                if (relevantToShadow)
                {
                    shadowIndex = shadowDraws.size();
                    shadowDraws.push_back(pooled.item);
                }
                auto graphDraw = pooled.item;
                graphDraw.materialSnapshot.reset();
                graphDraw.forwardMaterialSnapshot.reset();
                graphDraws.push_back(std::move(graphDraw));
                graphShadowIndices.push_back(shadowIndex);
                graphShadowEligible.push_back(relevantToShadow);
                graphViewRequired.push_back(visible);
            }

            lastPoolDraws = static_cast<uint32_t>(drawPool.size());
            lastCulledDraws = culled;

            if (pbrCapture && pbrCapture->controlled && pbrCapture->replayExtensions
                && pbrCapture->result.state == EnhancedPbrCaptureState::Pending
                && pbrCapture->target == viewPacket.displayTarget && frame.frameId > pbrCapture->afterFrameId)
            {
                std::string replayError;
                EnhancedDrawReplayInput selected;
                EnhancedLatticeReplayInput selectedMaterials;
                // Stage both slices: a material rejection must not leave an
                // otherwise valid world/pose replay partly applied to live draws.
                auto stagedGraph=graphDraws;
                bool passed = pbrCapture->drawReplay
                    ? pbrCapture->drawReplay->Apply({}, {}, stagedGraph, replayError)
                    : EnhancedDrawReplayInput::Seal({}, {}, graphDraws, selected, replayError);
                if (passed && pbrCapture->latticeReplayExtension) passed = pbrCapture->latticeReplay
                    ? pbrCapture->latticeReplay->Apply(stagedGraph, replayError)
                    : EnhancedLatticeReplayInput::Seal(graphDraws, selectedMaterials, replayError);
                if (!passed)
                {
                    // Apply validates all identities first: the normal live view
                    // remains intact even when only this diagnostic request fails.
                    pbrCapture->Fail(replayError);
                }
                else
                {
                    graphDraws.swap(stagedGraph);
                    for (size_t i = 0; i < graphDraws.size(); ++i)
                    {
                        if (graphShadowIndices[i] < shadowDraws.size())
                        {
                            shadowDraws[graphShadowIndices[i]] = graphDraws[i];
                        }
                    }
                    pbrCapture->drawInputBytes = pbrCapture->drawReplay
                        ? pbrCapture->drawReplay->Encode() : selected.Encode();
                    if (pbrCapture->latticeReplayExtension)
                        pbrCapture->latticeInputBytes = pbrCapture->latticeReplay
                            ? pbrCapture->latticeReplay->Encode() : selectedMaterials.Encode();
                }
            }

            {
                material_graph::SceneInputView inputView;
                inputView.frameId = frame.frameId;
                inputView.sceneEpoch = frame.sceneEpoch;
                inputView.viewId = viewPacket.key.viewId;
                inputView.historyRevision = viewPacket.key.historyRevision;
                inputView.width = frame.width;
                inputView.height = frame.height;
                inputView.camera = cameraSnapshot;
                std::string inputError;
                bool sealed = material_graph::SceneViewInput::Seal(inputView, graphDraws,
                    sceneInputBudget, graphViewInput, inputError);
                if (!sealed && hasOptionalGraphCandidates)
                {
                    // Aggregate geometry budgets can be tighter than the draw
                    // limit. Retry the original selection, keeping source-index
                    // aligned view and shadow eligibility together.
                    size_t retained = 0;
                    for (size_t i = 0; i < graphDraws.size(); ++i)
                    {
                        if (!graphShadowEligible[i] && !graphViewRequired[i])
                        {
                            ++lastCulledDraws;
                            continue;
                        }
                        if (retained != i)
                        {
                            graphDraws[retained] = std::move(graphDraws[i]);
                            graphShadowEligible[retained] = graphShadowEligible[i];
                            graphViewRequired[retained] = graphViewRequired[i];
                        }
                        ++retained;
                    }
                    graphDraws.resize(retained);
                    graphShadowEligible.resize(retained);
                    graphViewRequired.resize(retained);
                    sealed = material_graph::SceneViewInput::Seal(inputView, graphDraws,
                        sceneInputBudget, graphViewInput, inputError);
                }
                if (!sealed)
                {
                    lastError = "LX Scene input sealing failed: " + inputError;
                    ++frameFailures;
                    return false;
                }
                // The sealed view owns geometry and pose bytes. No proxy palette
                // pointers need to survive into GPU packet preparation.
                graphDraws.clear();
            }

            // 광원도 프리미티브와 같은 규약으로 밀봉한다 — 맵을 직접 훑지
            // 않고 스냅샷(shared_ptr 복사)을 받아 값만 옮겨 담는다.
            //
            // 여기서 뷰가 고른다(RenderSceneViewPlan ②): 절두체에 닿지 않는
            // 지역 광원을 빼고, 남은 것을 기여도 순으로 세운다. 패스마다
            // 다른 셰이더 배열 한도는 그대로지만, 목록이 정렬돼 있으므로
            // "앞의 N개"가 "가장 중요한 N개"가 된다 — 예전에는 등록 순서로
            // 잘렸다.


            return true;
        }

        /// 두 백엔드가 같은 scene pass 입력을 준비한다. 호출 시점에는 해당
        /// 백엔드의 프레임이 열려 있어야 한다 — IBL과 자산 캐시 업로드가
        /// immediate encoder를 사용하고, 그 뒤 병렬 그래프 제출이 이어진다.
        template <typename PipelineT>
        bool PreparePipelineFrame(PipelineT& p, uint32_t viewIndex, RHIShaderBinary output,
            std::string& outError)
        {
            RHIShaderCompiler::ScopedOutput environmentOutput(output);
            p.frameContext.shadowDraws = &shadowDraws;
            p.graphInput = graphViewInput;
            p.frameContext.viewFlags = HasViewFlag(p.views[viewIndex].viewFlags,
                EnhancedLiveViewFlags::SceneOverlay) ? LiveViewFlags::kSceneOverlay : LiveViewFlags::kScreenSpaceUI;
            {
                RHIShaderCompiler::ScopedOutput outputScope(output);
                if (!p.graphMaterials.SelectReadyInput(p.frameContext, p.graphInput, p.graphInput, outError))
                {
                    return false;
                }
            }
            p.frameContext.graphSceneInput = p.graphInput;
            p.frameContext.forwardLightingConsumer = p.graphInput && std::ranges::any_of(p.graphInput->Draws(), [](const auto& draw)
            {
                const auto& program = draw.material->generation->cooked.product.program;
                return program.surface && (draw.queue == material_graph::SceneCoverage::Blended || (program.features & 0x0800u));
            });
            if (!p.iblGenerated || skyBoxDirty)
            {
                const auto prepareIbl = [&]() -> bool
                {
                    // Startup and pipeline rebuilds retain their existing
                    // synchronous path. Explicit selections arrive prepared.
                    if (!skyCooked && !skyEquirect)
                    {
                        assets::CookedEnvironment cached;
                        if (file::path(skyBoxPath).extension() == ".ceibl")
                        {
                            if (!assets::ReadCookedEnvironment(skyBoxPath, cached, outError))
                            {
                                return false;
                            }
                            Hash::Sha256Digest recipe;
                            if (!assets::EnvironmentRecipeIdentity(PathFinder::ShaderPath() / "DefaultPassShader",
                                    cached.cubeSize, cached.brdfSize, recipe, outError))
                            {
                                return false;
                            }
                            if (cached.identity.recipe != recipe)
                            {
                                outError = "Cooked environment recipe changed; recook the selected environment";
                                return false;
                            }
                            skyCooked = std::move(cached);
                        }
                        else
                        {
                            if (!skyCookIdentity)
                            {
                                assets::EnvironmentIdentity identity;
                                if (!assets::EnvironmentSourceIdentity(skyBoxPath, identity.source, outError)
                                    || !assets::EnvironmentRecipeIdentity(PathFinder::ShaderPath() / "DefaultPassShader",
                                        512, 512, identity.recipe, outError))
                                {
                                    return false;
                                }
                                skyCookIdentity = identity;
                                skyCookCachePath = PathFinder::CachePath() / "Environment" / assets::EnvironmentCacheName(identity);
                            }
                            std::string cacheError;
                            if (assets::ReadCookedEnvironment(skyCookCachePath, cached, cacheError, &*skyCookIdentity))
                            {
                                skyCooked = std::move(cached);
                                Debug::PrintLog(spdlog::level::info, "[EnvironmentCache] hit " + skyCookCachePath.string());
                            }
                        }
                    }
                    if (skyCooked)
                    {
                        if (!p.ibl.InstallCooked(p.frameContext, std::move(*skyCooked), outError))
                        {
                            return false;
                        }
                        skyCooked.reset();
                        Debug::PrintLog(spdlog::level::info, "[EnvironmentCache] uploaded cooked maps " + skyBoxPath);
                    }
                    else
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
                        const RHITextureEntry skyEntry = p.frameContext.textureCache->GetOrUpload(
                            skyEquirect.get(), skyUploadError);
                        if (!skyEntry.IsValid() || skyEntry.isCube || !skyUploadError.empty())
                        {
                            outError = "equirect HDR 운반 실패";
                            if (!skyUploadError.empty())
                            {
                                outError += ": " + skyUploadError;
                            }
                            return false;
                        }

                        if (!p.ibl.Generate(p.frameContext, skyEntry.handle, skyEntry.format, 512, 512, outError))
                        {
                            outError = "HDR→Cube/IBL 생성 실패: " + outError;
                            return false;
                        }
                        if (!p.ibl.QueueCookedCapture(skyCookCachePath, *skyCookIdentity, outError))
                        {
                            return false;
                        }
                        Debug::PrintLog(spdlog::level::info, "[EnvironmentCache] generated; queued " + skyCookCachePath.string());
                    }

                    const bool previouslyGenerated = p.iblGenerated;
                    const bool previouslyDirty = skyBoxDirty;
                    p.ibl.WatchPreparationRejected([this, &p, previouslyGenerated, previouslyDirty]
                    {
                        p.iblGenerated = previouslyGenerated;
                        skyBoxDirty = previouslyDirty;
                    });
                    p.iblGenerated = true;
                    skyBoxDirty = false;
                    std::lock_guard<std::mutex> displayLock(displayLifetimeMutex);
                    ++displaySnapshot.iblGenerationCount;
                    return true;
                };
                if (!prepareIbl())
                {
                    FinishEnvironmentPreparation(outError);
                    return false;
                }
                FinishEnvironmentPreparation({});
            }

            if (!p.ibl.TouchCooked(p.frameContext,outError)) return false;
            // Bind the retained environment maps for this frame's consumers.
            p.skyBox.SetCubeMap(p.ibl.GetCubeMap(), p.frameContext.resources->DescribeTexture(p.ibl.GetCubeMap()).format, 1);
            p.deferred.SetIBL(p.ibl.GetIrradianceMap(), p.ibl.GetPrefilteredMap(),
                EnhancedIBLGenerator::kPrefilterMips, p.ibl.GetBrdfLut());
            p.forward.SetIBL(p.ibl.GetIrradianceMap(), p.ibl.GetPrefilteredMap(),
                EnhancedIBLGenerator::kPrefilterMips, p.ibl.GetBrdfLut());

            // 기즈모 데이터(gizmoData)의 프레임별 feed는 기여 노드의 prepare가
            // 한다 — RenderFeatureContext가 안정 주소를 넘겼다(E4-2).
            p.sprite.SetItems(&worldSprites);
            p.ui.SetRects(&uiRects);

            if (!p.animationPalettes.Prepare(*p.frameContext.resources,
                    p.frameContext.shadowDraws, p.frameContext.forwardDraws, p.frameContext.draws))
            {
                outError = "공용 애니메이션 팔레트 업로드 실패 또는 동일 animator key의 pose 불일치";
                return false;
            }
            return p.desc.PrepareAll(p.frameContext, viewIndex, outError);
        }

        // ── 한 프레임 ──
        //
        // 배선은 dx12.scene의 renderAndCount와 같다(리드백 프로브만 없다).
        // 마지막의 live_present가 이 러너의 고유 조각이다 — 포스트 체인 결과를
        // 공유 텍스처로 복사한다. 그 소비 선언이 있어야 그래프가 체인을
        // 걷어내지 않는다(post_probe가 하던 역할을 실전에서는 이 복사가 맡는다).
        bool RenderOnce(LivePipeline::CameraView& view, int slotIndex,
            uint64_t sourceFrameId, uint64_t sourceCaptureNanoseconds,
            std::string& outError, EnhancedPbrCapture* capture,
            LiveGraphSnapshot* diagnosticOutput, bool& preparationDeferred)
        {
            preparationDeferred = false;
            LivePipeline& p = *pipeline;
            LivePipeline::DisplaySlot& slot = view.slots[slotIndex];

            std::string environmentCacheError;
            if (!p.ibl.FinishCookedCapture(dx12.GetCompletedFenceValue(),environmentCacheError))
                Debug::PrintLog(spdlog::level::warn,"[EnvironmentCache] " + environmentCacheError);

            {
                RenderThreadPhaseScope begin(RenderPhase::begin_frame);
                if (!dx12.BeginFrame(outError))
                {
                    if (capture) capture->Fail(outError);
                    return false;
                }
            }

            // 여기서부터는 커맨드 리스트가 열려 있다. 아래 어느 지점에서
            // 실패로 빠져나가든 닫고 나가야 한다 — 열린 채 두면 다음
            // BeginFrame의 얼로케이터 Reset이 E_FAIL로 죽고, 원래 사유가
            // 그 2차 오류로 덮인다. 반환 지점이 스무 곳이 넘어 가드로 건다.
            bool frameCommitted = false;
            GpuFrameToken profilerToken{};
            struct FrameGuard
            {
                EnhancedSceneRendererLiveDX12Adapter& backend;
                const bool& committed;
                GpuFrameToken& profilerToken;
                EnhancedPbrCapture* capture;
                std::string& error;
                ~FrameGuard()
                {
                    if (!committed)
                    {
                        backend.AbortFrame();
                    }
                    // A committed frame still owns admission until it transfers
                    // the token into its retained display slot.
                    finish_gpu_capture(profilerToken, false,
                        "DX12 frame ended before GPU query collection ownership was retained");
                    if (capture)
                    {
                        std::string releaseError;
                        bool safe = backend.DrainForLifecycle(
                            RHILifecycleCommand::OfflineReadbackCapture, releaseError);
                        if (!safe && backend.HasDeviceLossProof())
                        {
                            safe = backend.DrainForLifecycle(
                                RHILifecycleCommand::UnrecoverableDeviceError, releaseError);
                        }
                        if (safe)
                        {
                            capture->Release(backend.Resources());
                        }
                        else
                        {
                            capture->Fail("Capture resources retained until GPU idle: " + releaseError);
                        }
                        if (capture->result.state == EnhancedPbrCaptureState::Recording)
                            capture->Fail(error);
                    }
                }
            } frameGuard{ dx12, frameCommitted, profilerToken, capture, outError };

            const EnhancedLiveGpuSpanSink& captureSink = GpuSpanSink();
            profilerToken.engineFrameId = sourceFrameId;
            if (captureSink.on_begin_capture && captureSink.on_finish_capture)
            {
                profilerToken.captureGeneration = captureSink.on_begin_capture(static_cast<uint32_t>(sourceFrameId));
            }
            profilerToken = dx12.BeginProfilerFrame(sourceFrameId, frameCounter++,
                view.key.viewId, profilerToken.captureGeneration);
            const uint32_t viewIndex = static_cast<uint32_t>(&view - &p.views[0]);
            // Restart only the captured view. Also discard this diagnostic history
            // afterward, so the next interactive frame cannot blend with time zero.
            struct CaptureHistoryGuard
            {
                LivePipeline::CameraView& view;
                bool active;
                void Reset() { view.ssgi.ResetHistory(); view.fog.ResetHistory(); }
                ~CaptureHistoryGuard() { if (active) Reset(); }
            } historyGuard{ view, capture && capture->controlled };
            if (historyGuard.active) historyGuard.Reset();
            {
                RenderThreadPhaseScope prepare(RenderPhase::resource_prepare);
                if (!PreparePipelineFrame(p, viewIndex, RHIShaderBinary::Dxil, outError))
                {
                    preparationDeferred = p.graphMaterials.SelectionDeferred();
                    return false;
                }
            }

            lastDrawCount = p.gbuffer.GetLastDrawCount();
            lastBatchCount = p.gbuffer.GetLastBatchCount();
			profileFrameDrawCount += lastDrawCount;
			profileFrameBatchCount += lastBatchCount;
            lastDecalCount = p.decal.GetLastDecalCount();
            lastDecalBatchCount = p.decal.GetLastBatchCount();
            lastSpriteCount = p.sprite.GetLastItemCount();
            lastSpriteBatchCount = p.sprite.GetLastBatchCount();
            lastUIRectCount = p.ui.GetLastRectCount();
            lastUIBatchCount = p.ui.GetLastBatchCount();
            const uint32_t targetIndex = DisplayTargetIndex(view.displayTarget);
            viewSpriteCounts[targetIndex] = lastSpriteCount;
            viewUICounts[targetIndex] = lastUIRectCount;

            slot.graph = std::make_shared<EnhancedRenderGraph>(dx12.Resources(),
                kLiveGraphScheduling, RGOrderPolicy::DependencyOrder);
            EnhancedRenderGraph& graph = *slot.graph;
            graph.SetProfiler(dx12.Profiler());
            graph.SetTransientPool(&p.transientPool);
            graph.SetTransientAliasing(ReadLivePostFlag("CREATOR_RENDERGRAPH_ALIASING", false),
                ReadLivePostFlag("CREATOR_RENDERGRAPH_EXTEND_LIFETIMES", false));
            if (!PrepareSceneRecording(p, graph, dx12.CommandPool(), RHIShaderBinary::Dxil,
                    preparationDeferred, capture, outError))
            {
                return false;
            }
            // ── 조립은 노드 목록이 정한다(PHASE 3-10 슬라이스 1) ──
            //
            // 예전에는 여기 200줄이 "무엇을 어떤 순서로 잇는가"를 직접 적었다.
            // 지금은 BuildPipelineDesc가 짜 둔 노드 목록을 순서대로 돌 뿐이고,
            // 패스를 하나 잇는 일은 그 목록에 노드 하나를 넣는 일이 됐다.
            //
            // litColor 폴백 체인(`X.GetOutput().IsValid() ? X : 이전값`)이 통째로
            // 사라진 것에 주목할 것. 그 일은 이제 블랙보드 슬롯이 한다 —
            // 수정 노드가 꺼지면 슬롯 값이 그대로 남는다.
            double compileMs = 0.0;
            LiveStopwatch compileWatch;
            {
                RenderThreadPhaseScope build(RenderPhase::graph_build);
                p.blackboard.Reset();

                LiveFrameBinding binding{};
                binding.viewIndex = viewIndex;
                binding.sharedTarget = slot.rhiTexture;
                binding.viewFlags = HasViewFlag(view.viewFlags,
                    EnhancedLiveViewFlags::SceneOverlay)
                    ? LiveViewFlags::kSceneOverlay : LiveViewFlags::kScreenSpaceUI;

                if (HasViewFlag(view.viewFlags, EnhancedLiveViewFlags::HideSkyBox))
                    binding.viewFlags |= LiveViewFlags::kHideSkyBox;
                bool stageCaptureOk = true;
                p.desc.DeclareAll(p.blackboard, graph, p.frameContext, binding,
                    [&](const LivePassNode& node, const LiveBlackboard& board) {
                        if (capture && stageCaptureOk)
                            stageCaptureOk = capture->DeclareStage(dx12.Resources(), graph, board, node,
                                p.width, p.height, outError);
                    });
                if (!stageCaptureOk) return false;
                if (capture && !capture->Declare(dx12.Resources(), graph, p.blackboard,
                        p.width, p.height, outError)) return false;

                if (!p.blackboard.Get(LiveSlots::kDisplayLdr).IsValid())
                {
                    outError = "포스트 체인 출력이 없다";
                    return false;
                }

                compileWatch.Start();
                if (!graph.Compile(outError)) return false;
                compileMs = compileWatch.ElapsedMs();
            }

            if (diagnosticOutput)
            {
                *diagnosticOutput = CaptureLiveGraphSnapshot(graph, view.key.viewId,
                    view.key.historyRevision, sourceFrameId, p.frameContext.sceneEpoch, p.width, p.height);
            }

            RHIRecordedBatchDesc batchDesc{};
            batchDesc.frameId = sourceFrameId;
            batchDesc.backendGeneration = dx12.GetBackendGeneration();
            batchDesc.displayToken = slot.interopToken;
            batchDesc.lifetimeToken = slot.graph;
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
            batchDesc.captureContext = profilerToken.IsValid()
                ? ce::dx_capture::submission_context{ profilerToken.engineFrameId,
                    profilerToken.submissionId, profilerToken.renderViewId, true }
                : ce::dx_capture::submission_context{ sourceFrameId, 0, view.key.viewId, false };
#endif
            RHIRecordedBatch batch;
            RHISubmissionTicket batchTicket;
            LiveStopwatch recordWatch;
            recordWatch.Start();
            {
                RenderThreadPhaseScope record(RenderPhase::command_record);
                if (!graph.RecordParallel(dx12.CommandPool(), 4, batchDesc,
                    batch, outError)) return false;
            }
            viewShadowStats[targetIndex] = CaptureShadowStats(p);
            p.lastNativeRecordMs = recordWatch.ElapsedMs();
            if (capture && !capture->RecordCompiledGraph(graph, p.lastNativeRecordMs, compileMs))
            { outError = "capture graph is not compiled"; return false; }
            p.lastGraphStats = graph.GetStats();
            {
                RenderThreadPhaseScope submit(RenderPhase::submit);
                if (!dx12.EnqueueRecordedBatch(std::move(batch), batchTicket,
                    outError))
                {
                    return false;
                }
                const auto graphCompletion = batchTicket.GetRecordedBatch()->GetCompletionPoint();
                if (!p.graphMaterials.PublishSubmittedCache(sourceFrameId,
                        graphCompletion, outError, batchTicket))
                {
                    return false;
                }

                dx12.ResolveProfilerFrame(profilerToken);

                if (!dx12.EndFrame(outError))
                {
                    return false;
                }
            }
            frameCommitted = true;   // 제출됐다 — 가드가 닫을 것이 없다

            // 여기서 기다리지 않는다 — 이것이 이 슬라이스의 전부다.
            // EndFrame이 서명한 펜스 값을 슬롯에 적어 두고, TickLive가 다음
            // 프레임들에서 완료를 논블로킹으로 확인해 표시로 승격한다.
            // 1차 실측에서 게임 스레드의 동기 WaitForGpu가 프레임당 33ms를
            // 먹어 총 대기를 58ms로 만들었다(실제 GPU는 3.7ms) — 그 벽을
            // 여기서 없앤다.
            slot.fenceValue = dx12.GetLastSignaledFenceValue();
            slot.frameId = sourceFrameId;
            slot.sourceCaptureNanoseconds = sourceCaptureNanoseconds;
            slot.sceneEpoch = p.frameContext.sceneEpoch;
            // cameraSnapshot은 다음 뷰에서 덮이므로 제출 슬롯에 값으로 봉인한다.
            slot.camera = *p.frameContext.camera;
            slot.previewComplete = p.graphInput && !p.graphInput->Draws().empty() &&
                (!materialPreviewView || (graphViewInput && !graphViewInput->Draws().empty() &&
                    p.graphInput->Draws()[0].material == graphViewInput->Draws()[0].material));
            slot.key = view.key;
            finish_gpu_capture(slot.profilerToken, false,
                "DX12 display slot reused before GPU query collection");
            slot.profilerToken = profilerToken;
            profilerToken.captureGeneration = 0;

            // W8: 기록이 끝난 자리에서 인코더가 버린 명령을 비우며 모은다.
            // Vulkan 경로와 같은 뜻이고 같은 수를 센다.
            {
                std::string lastDrop;
                const uint32_t drops = dx12.CommandPool().DrainEncoderDrops(lastDrop);
                if (0 != drops)
                {
                    encoderDrops += drops;
                    if (!lastDrop.empty()) lastEncoderDrop = lastDrop;
                }
            }

            view.pendingQueue.push_back(slotIndex);
            if (capture)
            {
                if (!dx12.DrainForLifecycle(RHILifecycleCommand::OfflineReadbackCapture, outError)
                    || dx12.HasDeviceLossProof())
                {
                    capture->Fail(outError.empty() ? "DX12 capture cannot read data after device loss." : outError);
                    return false;
                }
                std::string validation;
                const uint32_t validationCount = dx12.DrainDebugMessages(validation);
                // W8: Vulkan 경로와 같은 자리에 같은 기록을 남긴다.
                capture->RecordSealLedger(p.gbuffer.GetSealLedger(),
                    p.gbuffer.GetSamplerIdentity(), p.forward.GetSealLedger(),
                    p.forward.GetSamplerIdentity(), encoderDrops, lastEncoderDrop,
                    dx12.TextureCache().GetUploadFailureCount());
                capture->RecordIblContract(EnhancedIBLGenerator::kImportanceSampleCount,
                    EnhancedIBLGenerator::kSceneReflectionSampleCount,
                    dx12.Resources().DescribeTexture(p.ibl.GetImportanceMaps()[2]));
                uint64_t usedMB = 0, budgetMB = 0;
                const bool memoryAvailable = dx12.QueryVideoMemory(usedMB, budgetMB);
                capture->RecordMemory(usedMB, budgetMB, memoryAvailable);
                std::vector<EnhancedLivePassTiming> captureTimings;
                std::vector<EnhancedLiveGpuSlice> captureSlices;
                EnhancedLiveGpuSpan captureSpan;
                double captureTotalMs = 0.0;
                std::string timingError;
                if (!dx12.CollectProfiler(profilerToken, captureTimings, captureSlices,
                        captureSpan, captureTotalMs, timingError) && timingError.empty())
                    timingError = "capture submission timestamps unavailable";
                capture->RecordGpuTiming(captureTimings, captureSpan, captureTotalMs, timingError);
                if (!capture->Save(dx12.Resources(), graph.GetStats(), outError,
                        validationCount, validation)) return false;
            }
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
                // 그림자 패스는 뷰들이 나눠 쓰는 인스턴스 하나다. 거리·섞음 폭은
                // 장면 쪽 캐스터 선택이 다음 프레임에 같은 값을 읽는다.
                const auto& shadow = pendingTuning.shadow;
                p.shadow.SetBiasTexels(shadow.biasTexels);
                p.shadow.SetSlopeScale((std::max)(0.f, shadow.slopeScale));
                p.shadow.SetCascadeBlendBand(std::clamp(shadow.cascadeBlendBand, 0.f, 0.9f));
                p.shadow.SetShadowDistance(shadow.shadowDistance);
                const int lastView = static_cast<int>(EnhancedShadowDebugView::Count) - 1;
                p.shadow.SetDebugView(static_cast<EnhancedShadowDebugView>(
                    std::clamp(shadow.debugView, 0, lastView)));
                const int lastFilter = static_cast<int>(EnhancedShadowFilter::Count) - 1;
                p.shadow.SetFilter(static_cast<EnhancedShadowFilter>(
                    std::clamp(shadow.filter, 0, lastFilter)));
            }
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
            tuningMirror.shadow.biasTexels = p.shadow.GetBiasTexels();
            tuningMirror.shadow.slopeScale = p.shadow.GetSlopeScale();
            tuningMirror.shadow.cascadeBlendBand = p.shadow.GetCascadeBlendBand();
            tuningMirror.shadow.shadowDistance = p.shadow.GetShadowDistance();
            tuningMirror.shadow.debugView = static_cast<int>(p.shadow.GetDebugView());
            tuningMirror.shadow.filter = static_cast<int>(p.shadow.GetFilter());
        }
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
        compacted.take_palettes_from(batch);
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

    void LiveState::FailPendingGpuCaptures(const char* reason)
    {
        if (!pipeline)
        {
            return;
        }
        for (LivePipeline::CameraView& view : pipeline->views)
        {
            for (LivePipeline::DisplaySlot& slot : view.slots)
            {
                finish_gpu_capture(slot.profilerToken, false, reason);
            }
        }
    }

    void LiveState::CollectCompletedDisplays()
    {
        LiveState& state = *this;
        const uint64_t previousRendered = framesRendered;
        bool displayPromoted = false;
        if (EnhancedLiveBackend::Vulkan == backend)
        {
            if (state.vulkanPipeline)
            {
                RenderThreadPhaseScope collect(RenderPhase::gpu_collect);
                uint64_t promoted = 0;
                std::string validation;
                state.vulkanPipeline->PromoteCompleted(
                    state.CopyPresentationSink(), promoted, validation);
                displayPromoted = state.PublishVulkanDisplayResults();
                state.framesRendered += promoted;
                if (0 != promoted && !state.vulkanFirstFrameReported)
                {
                    state.vulkanFirstFrameReported = true;
                    const VulkanMeshCache::Stats meshStats =
                        state.vulkanPipeline->meshCache.GetStats();
                    Debug::PrintLog(spdlog::level::warn, "[vulkan.live] editor TickLive 첫 프레임 완성"
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
                    Debug::PrintLog(spdlog::level::err, "[vulkan.live 검증] " + validation);
                }
            }
        }
        else
        {
            // 인플라이트 제출분의 완료 확인(논블로킹) — 뷰마다. 제출 순서대로,
            // 완료된 것을 전부 표시로 승격한다 — DX11이 읽는 슬롯은 항상 '펜스가
            // 끝난' 슬롯뿐이라는 것이 비동기의 계약이다.
            if (nullptr != state.pipeline)
            {
                if (state.dx12.HasDeviceLossProof())
                {
                    state.FailPendingGpuCaptures("DX12 device lost before GPU query collection");
                    return;
                }
                RenderThreadPhaseScope collect(RenderPhase::gpu_collect);
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
                {
                    std::lock_guard displayLock(state.displayLifetimeMutex);
                    state.dx12.CollectRetiredDisplays();
                }

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
                            view.slots[slotIndex].completedAgeMs = capture_age_milliseconds(
                                view.slots[slotIndex].sourceCaptureNanoseconds, capture_steady_nanoseconds());
                            displayPromoted |= state.PublishDisplayResultLocked(view.displayTarget, view.key,
                                view.slots[slotIndex].interopToken,
                                view.slots[slotIndex].frameId, view.promotionCount,
                                view.promotedSlotMask, p.width, p.height,
                                view.slots[slotIndex].sceneEpoch, view.slots[slotIndex].camera,
                                p.resizeGeneration, view.slots[slotIndex].previewComplete,
                                view.slots[slotIndex].sourceCaptureNanoseconds,
                                view.slots[slotIndex].completedAgeMs);
                        }
                        view.pendingQueue.erase(view.pendingQueue.begin());
                        view.slots[slotIndex].graph.reset();   // GPU가 끝났다 — transient가 풀로 돌아간다
                        view.slots[slotIndex].key = {};

                        // ★ 검증 레이어를 읽는다. 배선 오류(포맷·상태·디스크립터)는 여기에만
                        //   남는데 아무도 안 읽으면 증상만 보고 추측하게 된다 — 실제로 그
                        //   상태로 며칠을 쫓았다.
                        //
                        //   W8-3: 그 가드가 `_DEBUG` 였다. 그래서 출하 구성에서는 레이어를
                        //   켜도 이 자리가 닫혀 있었고, 계획서 W8 이 판정하라는 "검증 오류 0"
                        //   을 잴 수단이 없었다. 꺼진 실행의 비용은 포인터 하나 검사다.
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

                        // ★ 수집은 **그 제출의 표로** 한다.
                        //
                        //   예전에는 Collect() 가 token 을 받지 않아 "지금 기록 중인 슬롯" 을
                        //   읽었다. BeginProfilerFrame 은 **뷰마다** 불리고 제출은 인플라이트로
                        //   겈리므로, 펜스가 끝난 제출의 기록을 뒤에 온 제출이 이미 덮어썼을 수
                        //   있었고, 실측에서 수집의 83% 가 그러고 있었다(§0.5.10).
                        //
                        //   이제는 표가 낡았으면 Collect 가 **실패한다.** 그럴듯한 숫자를 내는
                        //   대신 세서 드러낸다 — mismatches 가 0 이 아니면 링이 모자란다는 뜻이고,
                        //   그것은 숫자가 틀렸다는 것보다 훨씬 고치기 쉬운 신호다.
                        ++state.gpuCollects;

                        std::vector<EnhancedLiveGpuSlice> slices;
                        std::vector<EnhancedLivePassTiming> timings;
                        EnhancedLiveGpuSpan span{};
                        std::string collectError;
                        GpuCaptureCompletion captureCompletion{ view.slots[slotIndex].profilerToken };
                        double totalMilliseconds = 0.0;
                        if (state.dx12.CollectProfiler(view.slots[slotIndex].profilerToken,
                            timings, slices, span, totalMilliseconds, collectError))
                        {
                            state.lastGpuMs = totalMilliseconds;
                            // 패스별 시간은 예전에는 여기서 버려졌다 — 합계만 남기면
                            // "느려졌다"까지만 알 수 있고 어느 패스인지는 알 수 없다.
                            // 렌더 디버그 창이 읽도록 마지막 성공분을 보관한다.
                            state.lastPassTimings = std::move(timings);
                            state.lastGpuFrameId = view.slots[slotIndex].profilerToken.engineFrameId;
                            state.lastGpuSubmissionId = view.slots[slotIndex].profilerToken.submissionId;
                            state.lastGpuViewId = view.slots[slotIndex].profilerToken.renderViewId;
                            state.lastGpuSpan = span;
                            state.gpuQueryOverflowPasses += span.queryOverflowPasses;
                            if (span.queryOverflowPasses > 0)
                            {
                                const EnhancedLiveGpuSpanSink& sink = GpuSpanSink();
                                if (sink.on_issue && view.slots[slotIndex].profilerToken.captureGeneration != 0)
                                {
                                    sink.on_issue(static_cast<uint32_t>(
                                        view.slots[slotIndex].profilerToken.engineFrameId),
                                        span.queryOverflowPasses, false, "GPU query slots exhausted",
                                        view.slots[slotIndex].profilerToken.captureGeneration);
                                }
                            }

                            // 길이 셋의 관계를 여기서 묻는다. 둘 다 손에 있는 자리가
                            // 여기뿐이고, 여기서 세야 모든 뷰의 모든 수집이 검사를 받는다.
                            state.gpuDroppedSlices += span.droppedSlices;
                            state.gpuZeroLengthSlices += span.zeroLengthSlices;
                            if (span.busyMs > span.queueSpanMs + 1e-9)
                            {
                                ++state.gpuSpanViolations;
                            }
                            // ── 통합 축의 검산(§5.1) ─────────────────────────────────
                            //
                            // 변환한 GPU 구간은 **제출을 연 뒤에 시작해서 수집하기 전에**
                            // 끝나야 한다. 그 바깥으로 나가면 두 시계가 맞지 않는 것이고,
                            // 그때 GPU 트랙은 그럴듯한 거짓말이 된다. 임계값이 아니라
                            // 인과라서 하드웨어가 달라도 그대로 선다.
                            if (!span.cpuAligned)
                            {
                                ++state.gpuUnalignedCollects;
                            }
                            else
                            {
                                if (span.submitToGpuBeginMs < 0.0 || span.gpuEndToCollectMs < 0.0)
                                {
                                    ++state.gpuAlignmentViolations;
                                }
                                // 여유의 **최솟값**을 든다. 평균은 한 번의 큰 어긋남을 가린다.
                                if (0 == state.gpuAlignedCollects)
                                {
                                    state.gpuMinSubmitToBeginMs = span.submitToGpuBeginMs;
                                    state.gpuMinEndToCollectMs = span.gpuEndToCollectMs;
                                }
                                else
                                {
                                    state.gpuMinSubmitToBeginMs = (std::min)(
                                        state.gpuMinSubmitToBeginMs, span.submitToGpuBeginMs);
                                    state.gpuMinEndToCollectMs = (std::min)(
                                        state.gpuMinEndToCollectMs, span.gpuEndToCollectMs);
                                }
                                state.gpuMaxSubmitToCollectMs = (std::max)(
                                    state.gpuMaxSubmitToCollectMs, span.submitToCollectMs);
                                ++state.gpuAlignedCollects;

                                // ── EngineDiagnostics 로 귀속(§7.3) ──────────
                                //
                                // ★ 여기서만 흘린다. 통합 축이 살아 있고 정렬도
                                //   맞은 수집만 내보낸다 — 맞지 않는 구간을 레인에
                                //   얹으면 그럴듯한 자리에 거짓이 그려진다.
                                const EnhancedLiveGpuSpanSink& sink = GpuSpanSink();
                                if (sink.on_span && view.slots[slotIndex].profilerToken.captureGeneration != 0 &&
                                    span.submitToGpuBeginMs >= 0.0 && span.gpuEndToCollectMs >= 0.0)
                                {
                                    const uint32_t frameLabel = static_cast<uint32_t>(
                                        view.slots[slotIndex].profilerToken.engineFrameId);

                                    // 귀속은 **그 제출의 표**에서 뽑는다. "지금 기록 중인
                                    // 슬롯" 을 읽으면 §0.5.10 의 83% 가 그대로 돌아온다.
                                    EnhancedLiveGpuSpanOrigin origin{};
                                    origin.submissionId = static_cast<uint32_t>(
                                        view.slots[slotIndex].profilerToken.submissionId);
                                    origin.renderViewId = static_cast<uint16_t>(
                                        view.slots[slotIndex].profilerToken.renderViewId);
                                    origin.queueId = view.slots[slotIndex].profilerToken.queueId;
                                    origin.captureGeneration = view.slots[slotIndex].profilerToken.captureGeneration;

                                    for (const EnhancedLiveGpuSlice& slice : slices)
                                    {
                                        sink.on_span(slice.name.c_str(), slice.beginCpuTick,
                                                     slice.endCpuTick, frameLabel, origin);
                                        ++state.gpuSpansEmitted;
                                    }
                                }
                            }

                            if (span.sliceCount < state.lastPassTimings.size())
                            {
                                ++state.gpuSliceUnderflows;
                            }

                            // Completion follows every span and the owner-side
                            // flush, so Stop cannot freeze before the final chunk.
                            const EnhancedLiveGpuSpanSink& sink = GpuSpanSink();
                            const bool timestampsPublished = span.sliceCount == 0 ||
                                (span.cpuAligned && span.submitToGpuBeginMs >= 0.0 &&
                                    span.gpuEndToCollectMs >= 0.0 && sink.on_span && sink.on_flush &&
                                    slices.size() == span.sliceCount);
                            captureCompletion.complete = timestampsPublished &&
                                span.queryOverflowPasses == 0 && span.droppedSlices == 0;
                            if (!captureCompletion.complete)
                            {
                                captureCompletion.reason = timestampsPublished
                                    ? "DX12 GPU query coverage was incomplete"
                                    : "DX12 GPU timestamps could not be published on the CPU timeline";
                            }
                        }
                        else
                        {
                            ++state.gpuCollectMismatches;
                            state.lastGpuCollectError = collectError;
                            captureCompletion.reason = collectError.c_str();
                            const EnhancedLiveGpuSpanSink& sink = GpuSpanSink();
                            if (sink.on_issue && view.slots[slotIndex].profilerToken.captureGeneration != 0)
                            {
                                sink.on_issue(static_cast<uint32_t>(
                                    view.slots[slotIndex].profilerToken.engineFrameId),
                                    0, true, collectError.c_str(),
                                    view.slots[slotIndex].profilerToken.captureGeneration);
                            }
                        }
                        ++state.framesRendered;
                    }
                }
            }
        }
        if (framesRendered != previousRendered)
        {
            PublishDebugSnapshot();
            // 장면 생산자 GPU의 완료 알림이다. sink는 표시 작업만 깨우며,
            // Host CPU 제출이나 실제 화면 출력의 완료로 해석하지 않는다.
            const auto sink = CopyPresentationSink();
            if (sink && displayPromoted)
            {
                sink->NotifyDisplayAvailable();
            }
        }
    }

    uint32_t LiveState::PendingGpuSubmissions() const
    {
        if (EnhancedLiveBackend::Vulkan == backend)
        {
            return vulkanPipeline ? vulkanPipeline->PendingCount() : 0;
        }
        uint32_t pending = 0;
        if (pipeline)
        {
            for (const LivePipeline::CameraView& view : pipeline->views)
            {
                pending += static_cast<uint32_t>(view.pendingQueue.size());
            }
        }
        return pending;
    }

    void LiveState::ArmGpuCompletionEvent(uint32_t pendingGpu)
    {
        // Vulkan 은 이벤트를 걸지 않는다. 걸지 못하면 armedGpuFenceValue 가 0 이고,
        // 대기 쪽이 kSceneCompletionPollMs 조회로 물러선다.
        if (0 == pendingGpu || EnhancedLiveBackend::DX12 != backend || !pipeline)
        {
            armedGpuFenceValue = 0;
            return;
        }
        uint64_t oldest = UINT64_MAX;
        for (const LivePipeline::CameraView& view : pipeline->views)
        {
            if (!view.pendingQueue.empty())
                oldest = (std::min)(oldest, view.slots[view.pendingQueue.front()].fenceValue);
        }
        // 같은 값에 다시 걸면 펜스의 대기 목록만 쌓인다.
        if (UINT64_MAX == oldest || oldest == armedGpuFenceValue) return;
        armedGpuFenceValue = dx12.SignalEventOnFenceValue(oldest, gpuCompletionEvent.Get()) ? oldest : 0;
    }

    bool LiveState::PacingAllowsAdmission() const
    {
        const EnhancedLivePacing pacing = livePacing.load(std::memory_order_relaxed);
        const uint64_t elapsed = capture_steady_nanoseconds() - pacedAdmissionNanoseconds;
        switch (pacing.mode)
        {
        case EnhancedLivePacingMode::Unlimited:
            return true;
        case EnhancedLivePacingMode::FixedRate:
            return 0 == pacing.framesPerSecond || elapsed >= 1000000000ull / pacing.framesPerSecond;
        case EnhancedLivePacingMode::Display:
            break;
        }
        if (!compositorWait || compositorTickSinceAdmission) return true;
        // 시계를 대기 밖에서 놓쳤거나 시계가 멈췄다. 주기는 모니터를 옮기면
        // 바뀌므로 그때그때 묻는다(1 us 남짓).
        DWM_TIMING_INFO timing{};
        timing.cbSize = sizeof(timing);
        LARGE_INTEGER frequency{};
        if (FAILED(DwmGetCompositionTimingInfo(nullptr, &timing)) || 0 == timing.qpcRefreshPeriod ||
            !QueryPerformanceFrequency(&frequency) || 0 == frequency.QuadPart)
        {
            return true;
        }
        const uint64_t periodNanoseconds =
            timing.qpcRefreshPeriod * 1000000000ull / static_cast<uint64_t>(frequency.QuadPart);
        return elapsed + kMissedTickSlackNanoseconds >= periodNanoseconds;
    }

    void LiveState::MarkPacedAdmission()
    {
        compositorTickSinceAdmission = false;
        const uint64_t now = capture_steady_nanoseconds();
        const EnhancedLivePacing pacing = livePacing.load(std::memory_order_relaxed);
        if (EnhancedLivePacingMode::FixedRate == pacing.mode && 0 != pacing.framesPerSecond)
        {
            // 타이머 깨어남 지연(~0.5 ms)이 주기마다 쌓이면 초당 144 가 133 이 된다.
            // 이상적인 격자에 붙이고, 두 주기 넘게 밀렸으면 격자를 지금으로 옮긴다.
            const uint64_t interval = 1000000000ull / pacing.framesPerSecond;
            pacedAdmissionNanoseconds = now - pacedAdmissionNanoseconds < 2 * interval
                ? pacedAdmissionNanoseconds + interval : now;
            return;
        }
        pacedAdmissionNanoseconds = now;
    }

    void LiveState::WaitForRenderWork(bool untilPacingSlot, DWORD timeoutMs)
    {
        // 진입 시점을 기다리는 동안에도 같은 두 이벤트로 깬다. 끝난 GPU 제출을
        // 바로 회수해야 표시가 늦지 않는다.
        const HANDLE handles[] = { renderWakeEvent.Get(), gpuCompletionEvent.Get(), pacingTimer.Get() };
        const EnhancedLivePacing pacing = livePacing.load(std::memory_order_relaxed);
        if (untilPacingSlot && EnhancedLivePacingMode::Display == pacing.mode && compositorWait)
        {
            // 시계가 울려 돌아오면 WAIT_OBJECT_0 + 핸들 수를 돌려준다.
            if (WAIT_OBJECT_0 + 2 == compositorWait(2, handles, timeoutMs))
                compositorTickSinceAdmission = true;
            return;
        }
        if (untilPacingSlot && EnhancedLivePacingMode::FixedRate == pacing.mode &&
            0 != pacing.framesPerSecond && pacingTimer.IsValid())
        {
            const uint64_t interval = 1000000000ull / pacing.framesPerSecond;
            const uint64_t elapsed = capture_steady_nanoseconds() - pacedAdmissionNanoseconds;
            // 음수는 상대 시각이고 단위는 100 ns 다.
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(elapsed < interval ? (interval - elapsed) / 100 + 1 : 1);
            if (SetWaitableTimer(pacingTimer.Get(), &due, 0, nullptr, nullptr, FALSE))
            {
                WaitForMultipleObjects(3, handles, FALSE, timeoutMs);
                return;
            }
        }
        // 기다릴 진입 시점이 없으면(Unlimited, 시계 없음) 짧게 조회로 물러선다.
        WaitForMultipleObjects(2, handles, FALSE,
            untilPacingSlot ? (std::min)(timeoutMs, DWORD{ kSceneCompletionPollMs }) : timeoutMs);
    }

    bool LiveState::ShouldSkipScenePixels(const EnhancedLiveFramePacket& frame)
    {
        if (controlledCaptureFrame)
        {
            return false;
        }
        const uint64_t now = capture_steady_nanoseconds();
        // 고정 속도로 일부러 늦출 때는 나이가 늘 기준을 넘는다 — GT 가 찍은 뒤
        // 생산자 대기(최대 kProducerPacingMs)를 치르고, RT 가 다시 한 주기를 기다려
        // 집기 때문이다. 그 몫을 빼지 않으면 늦춘 그림을 "밀렸다"며 버린다
        // (초당 10 에서 그림 3~4장으로 실측).
        double budgetMs = kSceneSoftAgeBudgetMs;
        const EnhancedLivePacing pacing = livePacing.load(std::memory_order_relaxed);
        if (EnhancedLivePacingMode::FixedRate == pacing.mode && 0 != pacing.framesPerSecond)
        {
            budgetMs += 2.0 * (std::min)(1000.0 / pacing.framesPerSecond, double{ kProducerPacingMs });
        }
        if (capture_age_milliseconds(frame.sourceCaptureNanoseconds, now) <= budgetMs)
        {
            return false;
        }
        std::lock_guard<std::mutex> queueLock(renderQueueMutex);
        // 마지막 입력은 나이가 많다는 이유로 버리지 않는다. 다만 뷰별 credit/lease
        // 부족으로 일부 뷰가 생략될 수 있고, TickLive를 재실행하지는 않는다.
        // CPU 준비가 목표 시간을 넘겨도 주기적인 진행 허용으로 기아를 막는다.
        // 이미 제출한 GPU 작업의 취소나 절대적인 표시 지연 상한은 보장하지 않는다.
        if (renderQueue.empty() || renderThreadStopRequested || renderLastAdmissionNanoseconds == 0 ||
            now - renderLastAdmissionNanoseconds >= kSceneProgressNanoseconds)
        {
            return false;
        }
        ++renderStalePixelSkips;
        return true;
    }

    void LiveState::RecordSceneAdmission(const EnhancedLiveFramePacket& frame)
    {
        std::lock_guard<std::mutex> queueLock(renderQueueMutex);
        if (renderAdmittedFrameId == frame.frameId)
        {
            return;
        }
        renderAdmittedFrameId = frame.frameId;
        renderLastAdmissionNanoseconds = capture_steady_nanoseconds();
        renderLastAdmissionAgeMs = capture_age_milliseconds(
            frame.sourceCaptureNanoseconds, renderLastAdmissionNanoseconds);
        renderMaxAdmissionAgeMs = (std::max)(renderMaxAdmissionAgeMs, renderLastAdmissionAgeMs);
        if (renderLastAdmissionAgeMs > kSceneSoftAgeBudgetMs)
        {
            ++renderOverBudgetAdmissions;
        }
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

        // 윈도우 11 합성기 시계. 정적 연결하면 윈도우 10 에서 엔진 DLL 적재가 실패한다.
        if (!compositorWait)
        {
            if (const HMODULE dcomp = LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                compositorWait = reinterpret_cast<CompositorWaitFn>(
                    GetProcAddress(dcomp, "DCompositionWaitForCompositorClock"));
            }
            if (!compositorWait)
            {
                Debug::PrintLog(spdlog::level::warn,
                    "[RenderThread] 합성기 시계가 없어 화면 주기 맞춤을 건너뛴다");
            }
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

                // PHASE 14 P2 — 이 스레드를 관측 도구에 알린다. COM 초기화가
                // 실패하면 위에서 돌아가므로, 등록과 해제는 여기부터 짝이다.
                {
                    const EnhancedSceneRenderer::RenderThreadHooks& hooks =
                        EnhancedSceneRenderer::MutableRenderThreadHooks();
                    if (hooks.OnStart) hooks.OnStart();
                }

                auto nextVideoMemorySample = std::chrono::steady_clock::now();
                // 펜스 이벤트를 걸지 못했으면(Vulkan) 예전처럼 완료를 조회한다.
                const auto wakeTimeout = [this](uint32_t pendingGpu) -> DWORD
                {
                    return pendingGpu != 0 && 0 == armedGpuFenceValue
                        ? kSceneCompletionPollMs : kRenderWakeBackstopMs;
                };
                for (;;)
                {
                    // GT 발행이 멈춰도 마지막 생산자 GPU 결과를 수집한다.
                    // 전용 RT만 기다리며 작업 스케줄러의 worker는 점유하지 않는다.
                    uint32_t pendingGpu = 0;
                    std::unique_ptr<PreparedEnvironment> failedEnvironment;
                    {
                        std::lock_guard<std::mutex> stateLock(renderStateMutex);
                        try
                        {
                            std::string submissionError;
                            const bool failed = enabled && (backend == EnhancedLiveBackend::DX12
                                ? (pipeline && dx12.ConsumeSubmissionFailure(submissionError))
                                : (vulkanPipeline &&
                                    vulkanPipeline->resources.ConsumeSubmissionFailure(submissionError)));
                            if (failed)
                            {
                                lastError = "Scene submission failed: " + submissionError;
                                FailPendingGpuCaptures(lastError.c_str());
                                ++frameFailures;
                                enabled = false;
                                Debug::PrintLog(spdlog::level::err, lastError);
                            }
                            CollectCompletedDisplays();
                            pendingGpu = enabled ? PendingGpuSubmissions() : 0;
                            ArmGpuCompletionEvent(pendingGpu);
                        }
                        catch (const std::exception& exception)
                        {
                            lastError = std::string("RenderThread completion exception: ") + exception.what();
                            FailPendingGpuCaptures(lastError.c_str());
                            ++frameFailures;
                            enabled = false;
                            Debug::PrintLog(spdlog::level::err, lastError);
                        }
                        catch (...)
                        {
                            lastError = "RenderThread completion unknown exception";
                            FailPendingGpuCaptures(lastError.c_str());
                            ++frameFailures;
                            enabled = false;
                            Debug::PrintLog(spdlog::level::err, lastError);
                        }
                        if (!enabled)
                        {
                            // Fail pending/applying work even if no more frames
                            // are admitted; release its pixels outside this lock.
                            failedEnvironment = FailEnvironmentPreparation(*environmentPreparation, lastError);
                        }
                    }

                    FrameSubmission submission;
                    {
                        std::unique_lock<std::mutex> queueLock(renderQueueMutex);
                        if (renderQueue.empty())
                        {
                            if (renderThreadStopRequested)
                            {
                                break;
                            }
                            // 새 발행·정지·GPU 완료 중 하나로 깬다. 깨어난 뒤 완료를
                            // 다시 수집하고 최신 입력을 고른다.
                            queueLock.unlock();
                            EnhancedSceneRenderer::RenderThreadPhaseScope idle(
                                EnhancedSceneRenderer::RenderPhase::queue_idle);
                            WaitForRenderWork(false, wakeTimeout(pendingGpu));
                            continue;
                        }
                        if (renderThreadTestDelayMs != 0 && !renderThreadStopRequested)
                        {
                            EnhancedSceneRenderer::RenderThreadPhaseScope delay(
                                EnhancedSceneRenderer::RenderPhase::test_delay);
                            renderQueueWake.wait_for(queueLock,
                                std::chrono::milliseconds(renderThreadTestDelayMs), [this]
                                {
                                    return renderThreadStopRequested;
                                });
                        }
                        // 보통의 Editor + Game 두 뷰가 쓸 공용 credit을 먼저 확보한 뒤
                        // 같은 입력을 확정한다. 이 보수적인 묶음 승인은 GPU 작업의
                        // 겹침을 줄일 수 있고, 모든 뷰의 표시 슬롯 확보를 보장하지 않는다.
                        const auto& waitingFrame = renderQueue.front().frame;
                        uint32_t requiredCredits = 0;
                        if (!waitingFrame.sceneLoading && waitingFrame.width != 0 && waitingFrame.height != 0)
                        {
                            const uint32_t viewCount = (std::min)(waitingFrame.viewCount, kEnhancedMaxLiveCameraViews);
                            for (uint32_t i = 0; i < viewCount && requiredCredits < 2; ++i)
                            {
                                if (waitingFrame.views[i].key.IsValid())
                                {
                                    ++requiredCredits;
                                }
                            }
                        }
                        if (requiredCredits != 0 && pendingGpu + requiredCredits > 2 && !renderThreadStopRequested)
                        {
                            ++renderGpuAdmissionWaits;
                            // 가장 오래된 제출의 펜스 완료가 깨운다. 새 발행이 깨워도
                            // credit 이 그대로면 다시 여기로 와서 잔다.
                            queueLock.unlock();
                            EnhancedSceneRenderer::RenderThreadPhaseScope idle(
                                EnhancedSceneRenderer::RenderPhase::queue_idle);
                            WaitForRenderWork(false, wakeTimeout(pendingGpu));
                            continue;
                        }
                        if (renderDisplayLeaseBlocked && !renderThreadStopRequested)
                        {
                            // 표시 슬롯이 하나도 비지 않았는데 바로 다음 packet 을 집으면
                            // 그릴 것 없이 packet 만 소비하며 돈다. 슬롯을 놓는 표시 쪽은
                            // 알림이 없으므로 다음 합성 주기에 다시 본다.
                            renderDisplayLeaseBlocked = false;
                            ++renderDisplayLeaseWaits;
                            MarkPacedAdmission();
                            queueLock.unlock();
                            EnhancedSceneRenderer::RenderThreadPhaseScope idle(
                                EnhancedSceneRenderer::RenderPhase::queue_idle);
                            WaitForRenderWork(true, kCompositorStallMs);
                            continue;
                        }
                        // 진입 속도 맞춤(livePacing). 종료 중에는 delta 를 순서대로
                        // 흘려야 하므로 기다리지 않는다.
                        if (!renderThreadStopRequested && !PacingAllowsAdmission())
                        {
                            ++renderDisplayPacingWaits;
                            queueLock.unlock();
                            EnhancedSceneRenderer::RenderThreadPhaseScope idle(
                                EnhancedSceneRenderer::RenderPhase::queue_idle);
                            WaitForRenderWork(true, kCompositorStallMs);
                            continue;
                        }
                        // 이 시점에만 입력을 확정한다. 기다리는 동안에도 대기 입력은
                        // 교체 가능했고 lifecycle delta의 순서는 보존되었다.
                        submission = std::move(renderQueue.front());
                        renderQueue.pop_front();
                        renderInProgress = 1;
                        activeFrameDrainOnly = renderThreadStopRequested;
                        MarkPacedAdmission();
                    }
                    renderQueueWake.notify_all();

                    activeDeltaBatch = std::move(submission.deltas);
                    {
                        // 프레임 하나를 소비하는 구간. try/catch 바깥에 세워
                        // 예외로 빠져나가도 닫히게 한다.
                        EnhancedSceneRenderer::RenderThreadFrameScope frameScope;
                        try
                        {
							const auto& hooks = EnhancedSceneRenderer::MutableRenderThreadHooks();
							const bool sampleCounters = hooks.OnCounters && hooks.ShouldSampleCounters &&
								hooks.ShouldSampleCounters();
							std::chrono::steady_clock::duration counterQueryTime{};
							const auto beforeQueryStarted = std::chrono::steady_clock::now();
							std::uint64_t beforeUploadBytes = 0, beforeUploadOverflows = 0;
							std::uint64_t beforeDescriptors = 0, beforeDescriptorOverflows = 0;
							if (sampleCounters && backend == EnhancedLiveBackend::DX12 && pipeline)
							{
								const auto before = dx12.GetCounterSnapshot();
								beforeUploadBytes = before.uploadBytes;
								beforeUploadOverflows = before.uploadOverflows;
								beforeDescriptors = before.descriptorAllocations;
								beforeDescriptorOverflows = before.descriptorOverflows;
							}
							else if (sampleCounters && backend == EnhancedLiveBackend::Vulkan && vulkanPipeline)
							{
								const auto upload = vulkanPipeline->resources.GetUploadStats();
								beforeUploadBytes = upload.bytesAllocated;
								beforeUploadOverflows = upload.batchRollbacks;
								const auto descriptor = vulkanPipeline->resources.GetDescriptorRecyclerStats();
								beforeDescriptors = descriptor.allocations;
								beforeDescriptorOverflows = descriptor.allocationFailures;
							}
							if (sampleCounters) counterQueryTime += std::chrono::steady_clock::now() - beforeQueryStarted;
                            EnhancedSceneRenderer::TickLive(submission.frame);
							if (sampleCounters)
							{
								const auto afterQueryStarted = std::chrono::steady_clock::now();
								std::uint64_t afterUploadBytes = 0, afterUploadOverflows = 0;
							std::uint64_t afterDescriptors = 0, afterDescriptorOverflows = 0;
							if (backend == EnhancedLiveBackend::DX12 && pipeline)
							{
									const auto after = dx12.GetCounterSnapshot();
									afterUploadBytes = after.uploadBytes;
									afterUploadOverflows = after.uploadOverflows;
									afterDescriptors = after.descriptorAllocations;
									afterDescriptorOverflows = after.descriptorOverflows;
								}
								else if (backend == EnhancedLiveBackend::Vulkan && vulkanPipeline)
								{
									const auto upload = vulkanPipeline->resources.GetUploadStats();
									afterUploadBytes = upload.bytesAllocated;
									afterUploadOverflows = upload.batchRollbacks;
									const auto descriptor = vulkanPipeline->resources.GetDescriptorRecyclerStats();
									afterDescriptors = descriptor.allocations;
									afterDescriptorOverflows = descriptor.allocationFailures;
								}
								counterQueryTime += std::chrono::steady_clock::now() - afterQueryStarted;
								const EnhancedSceneRenderer::RenderThreadHooks::Counters sample{
									afterUploadBytes >= beforeUploadBytes ? afterUploadBytes - beforeUploadBytes : 0,
									afterUploadOverflows >= beforeUploadOverflows ? afterUploadOverflows - beforeUploadOverflows : 0,
									afterDescriptors >= beforeDescriptors ? afterDescriptors - beforeDescriptors : 0,
									afterDescriptorOverflows >= beforeDescriptorOverflows ? afterDescriptorOverflows - beforeDescriptorOverflows : 0,
									profileFrameDrawCount, profileFrameBatchCount,
									std::chrono::duration<double, std::micro>(counterQueryTime).count() };
								hooks.OnCounters(static_cast<std::uint32_t>(submission.frame.frameId), sample);
							}
							// Query on the resource-owning thread at 4 Hz. No Editor/UI
							// path touches the renderer's state or its lock for this graph.
							const auto sampleTime = std::chrono::steady_clock::now();
							if (hooks.OnVideoMemory && sampleTime >= nextVideoMemorySample)
							{
								nextVideoMemorySample = sampleTime + std::chrono::milliseconds(250);
								std::uint64_t usedMB = 0;
								std::uint64_t budgetMB = 0;
								bool available = false;
								if (backend == EnhancedLiveBackend::Vulkan && vulkanPipeline)
								{
									const RHIVideoMemoryInfo memory = vulkanPipeline->resources.QueryVideoMemory();
									available = memory.budgetMB > 0;
									usedMB = memory.usedMB;
									budgetMB = memory.budgetMB;
								}
								else if (backend == EnhancedLiveBackend::DX12 && pipeline)
									available = dx12.QueryVideoMemory(usedMB, budgetMB);
								if (available)
									hooks.OnVideoMemory(static_cast<std::uint32_t>(submission.frame.frameId), usedMB, budgetMB);
							}
                        }
                        catch (const std::exception& exception)
                        {
                            std::lock_guard<std::mutex> stateLock(renderStateMutex);
                            lastError = std::string("RenderThread frame exception: ") +
                                exception.what();
                            FailPendingGpuCaptures(lastError.c_str());
                            FinishEnvironmentPreparation(lastError);
                            ++frameFailures;
                            Debug::PrintLog(spdlog::level::err, lastError);
                        }
                        catch (...)
                        {
                            std::lock_guard<std::mutex> stateLock(renderStateMutex);
                            lastError = "RenderThread frame unknown exception";
                            FailPendingGpuCaptures(lastError.c_str());
                            FinishEnvironmentPreparation(lastError);
                            ++frameFailures;
                            Debug::PrintLog(spdlog::level::err, lastError);
                        }
                        if (activeDeltaBatch.size() != 0)
                        {
                            ProxyCommandQueue->DeferBatch(std::move(activeDeltaBatch));
                        }
                    }

                    {
                        EnhancedSceneRenderer::RenderThreadPhaseScope complete(
                            EnhancedSceneRenderer::RenderPhase::completion);
                        std::lock_guard<std::mutex> queueLock(renderQueueMutex);
                        renderInProgress = 0;
                        ++renderConsumed;
                        renderCompletedFrameId = submission.frame.frameId;
                        // 예열 장부: 라이브 프레임 하나가 **끝난** 때.
                        //
                        // ★ 이 자리라야 한다. 처음에는 CPU 톤맵 경로의
                        //   `view.completedFrameId` 옆에 찍었는데 DX12 는 그 길을
                        //   지나지 않아(텍스처를 직접 공유한다) 190 초를 돌려도
                        //   끝내 미도달이었다. `render.live.wait` 의 판정이 읽는
                        //   값이 여기서 서므로, 예열의 "한 프레임 끝" 도 같은
                        //   자리를 써야 둘이 같은 사건을 말한다.
                        engine::warmup::mark(engine::warmup::stage::first_live_frame);
                    }
                    renderQueueWake.notify_all();
                }

                {
                    std::lock_guard<std::mutex> stateLock(renderStateMutex);
                    FailPendingGpuCaptures("DX12 collection owner stopped before GPU query completion");
                }
                {
                    std::lock_guard<std::mutex> queueLock(renderQueueMutex);
                    renderThreadRunning = false;
                }
                renderQueueWake.notify_all();
                // 스레드가 죽기 전에 끊는다. 이것이 없으면 수집기가 죽은
                // 저장소를 가리킨 채 남는다.
                {
                    const EnhancedSceneRenderer::RenderThreadHooks& hooks =
                        EnhancedSceneRenderer::MutableRenderThreadHooks();
                    if (hooks.OnStop) hooks.OnStop();
                }
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

        // 씬 잠금 밖의 GT 에는 다른 속도 제한이 없다. 앞 packet 이 아직 대기 중이면
        // RT 가 가져갈 때까지 기다려 GT 를 RT 소비 속도에 맞춘다(최신 입력 정책은 그대로).
        if (renderQueue.size() >= kRenderQueueCapacity)
        {
            ++renderProducerPacingWaits;
            renderQueueWake.wait_for(lock, std::chrono::milliseconds(kProducerPacingMs), [this]
            {
                return !renderThreadAccepting || renderQueue.size() < kRenderQueueCapacity;
            });
            if (!renderThreadAccepting)
            {
                renderShutdownDiscardedDeltas += submission.deltas.size();
                ProxyCommandQueue->MarkShutdownDiscarded(
                    static_cast<uint64_t>(submission.deltas.size()));
                return false;
            }
        }

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
                if (!renderThreadAccepting)
                {
                    renderShutdownDiscardedDeltas += submission.deltas.size();
                    ProxyCommandQueue->MarkShutdownDiscarded(
                        static_cast<uint64_t>(submission.deltas.size()));
                    return false;
                }
                renderQueue.push_back(std::move(submission));
            }
            else
            {
                ProxyCommandQueueController::Batch merged;
                merged.reserve(mergedDeltaCount);
                merged.append(std::move(newest.deltas));
                merged.append(std::move(submission.deltas));

                const uint64_t mergedSuperseded = CompactProxyUpdates(merged);
                renderCoalescedDeltas += mergedSuperseded;
                ProxyCommandQueue->MarkSuperseded(mergedSuperseded);
                submission.deltas = std::move(merged);
                newest = std::move(submission);
                ++renderCoalescedFrames;
            }
        }

        ++renderPublished;
        renderQueueHighWatermark = (std::max)(renderQueueHighWatermark,
            static_cast<uint32_t>(renderQueue.size()));
        lock.unlock();
        renderQueueWake.notify_one();
        SetEvent(renderWakeEvent.Get());
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
        SetEvent(renderWakeEvent.Get());
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
        stats.gpuAdmissionWaits = renderGpuAdmissionWaits;
        stats.stalePixelSkips = renderStalePixelSkips;
        stats.overBudgetAdmissions = renderOverBudgetAdmissions;
        stats.displayLeaseSkips = renderDisplayLeaseSkips;
        stats.producerPacingWaits = renderProducerPacingWaits;
        stats.displayLeaseWaits = renderDisplayLeaseWaits;
        stats.displayPacingWaits = renderDisplayPacingWaits;
        stats.admittedFrameId = renderAdmittedFrameId;
        stats.lastAdmissionAgeMs = renderLastAdmissionAgeMs;
        stats.maxAdmissionAgeMs = renderMaxAdmissionAgeMs;
        stats.pendingAgeMs = renderQueue.empty() ? 0.0 : capture_age_milliseconds(
            renderQueue.front().frame.sourceCaptureNanoseconds, capture_steady_nanoseconds());
        stats.softAgeBudgetMs = kSceneSoftAgeBudgetMs;
        stats.pending = static_cast<uint32_t>(renderQueue.size());
        stats.inProgress = renderInProgress;
        stats.highWatermark = renderQueueHighWatermark;
        stats.capacity = kRenderQueueCapacity;
        stats.publishedFrameId = publishedFrameId.load();
        stats.completedFrameId = renderCompletedFrameId;
        stats.running = renderThreadRunning;
        stats.accepting = renderThreadAccepting;
        stats.producerConsumerSeparated =
            frameProducerThread != std::thread::id{} &&
            frameConsumerThread != std::thread::id{} &&
            frameProducerThread != frameConsumerThread;
        return stats;
    }

    bool LiveState::WaitForRenderThreadIdle(uint32_t timeoutMilliseconds)
    {
        std::unique_lock<std::mutex> lock(renderQueueMutex);
        if (!renderThreadRunning) return false;

        return renderQueueWake.wait_for(lock,
            std::chrono::milliseconds(timeoutMilliseconds), [this]
            {
                return renderQueue.empty() && 0 == renderInProgress;
            });
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
        // 에디터 재질 저작은 이 백엔드 하나만 컴파일한다.
        material_graph::SetAuthoringSceneBackend(backend == EnhancedLiveBackend::Vulkan
            ? RHIShaderBinary::SpirV : RHIShaderBinary::Dxil);
        state.ResetDisplaySnapshot();
        state.renderScene = std::make_shared<RenderScene>();
        state.renderScene->Initialize();

        // 에디터 카메라는 여기서 만들지 않는다(E4-5) — Editor 세션이 소유하고
        // Host가 뷰 요청에 실어 넘긴다. Core는 씬 오버레이 뷰 판정도 하지 않는다.

        state.skyBoxPath =
            PathFinder::EngineResourcePath("Environment/forest.ceibl").string();
        assets::CookedEnvironment defaultEnvironment;
        if (!assets::ReadCookedEnvironment(state.skyBoxPath,defaultEnvironment,outError)) return false;
        Hash::Sha256Digest recipe;
        if (!assets::EnvironmentRecipeIdentity(PathFinder::ShaderPath()/"DefaultPassShader",
                defaultEnvironment.cubeSize,defaultEnvironment.brdfSize,recipe,outError)) return false;
        if (recipe != defaultEnvironment.identity.recipe)
        {
            outError="Default forest environment cook recipe is stale; recook engine resources";
            return false;
        }
        state.skyCooked=std::move(defaultEnvironment);
        state.skyEquirect.reset();
        state.skyBoxDirty = true;
        // Bootstrap actually installs the bundled forest, not the legacy saved path.
        // Keep the reported selection consistent and hide only its background.
        if (auto* settings = RuntimeSettings::TryGet())
            settings->SetEnvironmentSelection("forest.ceibl", false);

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

        {
            std::lock_guard<std::mutex> lock(state.environmentPreparation->mutex);
            state.environmentPreparation->accepting = true;
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
        if (state.renderScene->GetScene() != scene)
        {
            InvalidateEnvironmentPreparation(*state.environmentPreparation, false);
            ++state.sceneEpoch;
        }
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
    if (!state.enabled)
    {
        outError = "EnhancedRenderer is not enabled";
        return false;
    }
    if (path.empty())
    {
        outError = "HDR 경로가 비어 있다";
        return false;
    }
    const file::path source(path);
    if (source.extension() != ".hdr" && source.extension() != ".ceibl")
    {
        outError = "Select an HDR or cooked .ceibl environment";
        return false;
    }
    // Capture every path on the caller. Worker admission and progress never
    // acquire renderStateMutex, even while the first frame compiles shaders.
    EnvironmentPreparationRequest request;
    request.selection = path;
    request.source = file::absolute(source);
    request.shaders = PathFinder::ShaderPath() / "DefaultPassShader";
    request.cache = PathFinder::CachePath() / "Environment";
    request.project = PathFinder::BaseProjectPath();
    EnvironmentPreparationProgress progress;
    progress.activeRequests = 1;
    progress.phase = "Queued";
    progress.name = source.filename().string();
    const auto preparation = state.environmentPreparation;
    bool launch = false;
    std::unique_ptr<PreparedEnvironment> retired;
    {
        std::lock_guard<std::mutex> lock(preparation->mutex);
        if (!preparation->accepting)
        {
            outError = "EnhancedRenderer environment preparation is not running";
            return false;
        }
        request.id = ++preparation->nextRequest;
        request.generation = preparation->generation;
        progress.requestId = request.id;
        progress.appliedRequestId = preparation->progress.appliedRequestId;
        preparation->pending = std::move(request);
        retired = std::move(preparation->ready);
        preparation->progress = std::move(progress);
        launch = !preparation->running;
        preparation->running = true;
    }
    if (launch)
    {
        try
        {
            const auto job = ce::get_job_scheduler().submit([preparation]
            {
                try
                {
                    RunEnvironmentPreparation(preparation);
                }
                catch (...)
                {
                    // Includes allocation failure outside the decoder. Clear
                    // admission state without allocating another error string.
                    std::unique_ptr<PreparedEnvironment> retired;
                    std::lock_guard<std::mutex> lock(preparation->mutex);
                    preparation->running = false;
                    preparation->pending.reset();
                    retired = std::move(preparation->ready);
                    preparation->progress.activeRequests = 0;
                    preparation->progress.phase = "Failed";
                }
            });
            // Dependency-free dispatch failures are completed before submit
            // returns. Observe them without ever waiting for running work.
            if (job.is_complete())
            {
                job.wait();
            }
        }
        catch (const std::exception& exception)
        {
            std::lock_guard<std::mutex> lock(preparation->mutex);
            preparation->running = false;
            preparation->pending.reset();
            preparation->progress.activeRequests = 0;
            preparation->progress.phase = "Failed";
            preparation->progress.error = exception.what();
            outError = preparation->progress.error;
            return false;
        }
    }
    outError.clear();
    return true;
}

EnhancedSceneRenderer::EnvironmentPreparationProgress EnhancedSceneRenderer::GetEnvironmentPreparationProgress()
{
    const auto preparation = GetLiveState().environmentPreparation;
    std::lock_guard<std::mutex> lock(preparation->mutex);
    return preparation->progress;
}

void EnhancedSceneRenderer::CancelEnvironmentPreparation()
{
    InvalidateEnvironmentPreparation(*GetLiveState().environmentPreparation, false);
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

EnhancedRequiredAssetPacket EnhancedSceneRenderer::BuildRequiredAssetPacket(
    std::span<const std::shared_ptr<Material>> materials)
{
    EnhancedRequiredAssetPacket packet{};
    for (const std::shared_ptr<Material>& material : materials)
    {
        if (!material) continue;
        const EnhancedShaderMetaDomain domain =
            MaterialRenderingMode::Transparent == material->m_renderingMode
            ? EnhancedShaderMetaDomain::Forward
            : EnhancedShaderMetaDomain::GBuffer;
        packet.RequireShaderMeta(domain, material->m_shaderMetaGuid);
    }
    packet.Canonicalize();
    return packet;
}

EnhancedLiveFramePacket EnhancedSceneRenderer::BuildLiveFramePacket(
    float deltaSeconds, const EnhancedLiveViewRequest* views, uint32_t viewCount,
    bool sceneLoading, const EnhancedRequiredAssetPacket& requiredAssets)
{
    LiveState& state = GetLiveState();
    const std::thread::id currentThread = std::this_thread::get_id();
    if (state.frameProducerThread == std::thread::id{})
        state.frameProducerThread = currentThread;
    assert(state.frameProducerThread == currentThread &&
        "BuildLiveFramePacket must stay on the game-thread producer");

    EnhancedLiveFramePacket frame{};
    frame.sourceCaptureNanoseconds = capture_steady_nanoseconds();
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
    frame.skyBoxEnabled = RuntimeSettings::Get().GetRenderPassSettings().m_isSkyboxEnabled;
    const auto screenSize = ScreenResizeBus::Get().GetSizeSnapshot();
    frame.width = screenSize.width;
    frame.height = screenSize.height;
    frame.requiredAssets = requiredAssets;
    frame.requiredAssets.Canonicalize();

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
        if (!views[i].key.IsValid()) continue;

        EnhancedLiveViewPacket& view = frame.views[frame.viewCount++];
        view.key = views[i].key;
        view.camera = views[i].camera;
        view.displayTarget = views[i].displayTarget;
        view.viewFlags = views[i].viewFlags;
        view.materialPreview = views[i].materialPreview;
        view.materialPreviewFloor = views[i].materialPreviewFloor;
        if (view.displayTarget == EnhancedLiveDisplayTarget::MaterialPreview)
        {
            view.camera = MaterialPreviewCamera(frame.width, frame.height);
            continue;
        }

        if (!HasViewFlag(view.viewFlags, EnhancedLiveViewFlags::SceneOverlay))
        {
            continue;
        }
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

void EnhancedSceneRenderer::SetLivePacing(EnhancedLivePacing pacing)
{
    LiveState& state = GetLiveState();
    // 호스트는 매 프레임 부른다. 바뀐 때만 RT 를 깨워 느린 주기의 대기를 끊는다.
    if (state.livePacing.exchange(pacing, std::memory_order_relaxed) != pacing)
        SetEvent(state.renderWakeEvent.Get());
}

EnhancedLivePacing EnhancedSceneRenderer::GetLivePacing()
{
    return GetLiveState().livePacing.load(std::memory_order_relaxed);
}

void EnhancedSceneRenderer::SetRenderThreadHooks(const RenderThreadHooks& hooks)
{
    MutableRenderThreadHooks() = hooks;
}

void EnhancedSceneRenderer::StopLiveRenderThread()
{
    LiveState& state = GetLiveState();
    InvalidateEnvironmentPreparation(*state.environmentPreparation, true);
    state.StopRenderThread();
}

EnhancedRenderThreadStats EnhancedSceneRenderer::GetLiveRenderThreadStats()
{
    return GetLiveState().GetRenderThreadStats();
}

bool EnhancedSceneRenderer::RequestLivePbrCapture(const std::string& directory,
    EnhancedLiveDisplayTarget target, std::string& outError, bool controlled,
    const std::string& cameraReplayPath, const std::string& drawReplayPath, const std::string& latticeReplayPath,
    bool replayExtensions, bool latticeReplayExtension)
{
    std::optional<EnhancedCameraReplayInput> replay;
    if (!cameraReplayPath.empty())
    {
        EnhancedCameraReplayInput candidate;
        if (!controlled || !EnhancedCameraReplayInput::Load(cameraReplayPath, candidate, outError))
        {
            if (!controlled) outError = "camera replay requires controlled capture";
            return false;
        }
        if (candidate.target != static_cast<uint32_t>(target))
        { outError = "camera replay display target mismatch"; return false; }
        replay = candidate;
    }
    std::optional<EnhancedDrawReplayInput> drawReplay;
    if (!drawReplayPath.empty())
    {
        EnhancedDrawReplayInput candidate;
        if (!controlled || !replay || target == EnhancedLiveDisplayTarget::MaterialPreview
            || !EnhancedDrawReplayInput::Load(drawReplayPath, candidate, outError))
        {
            if (!controlled || !replay || target == EnhancedLiveDisplayTarget::MaterialPreview)
                outError = "draw replay requires controlled camera replay for an editor/game view";
            return false;
        }
        drawReplay = std::move(candidate);
    }
    std::optional<EnhancedLatticeReplayInput> latticeReplay;
    if (!latticeReplayPath.empty())
    {
        EnhancedLatticeReplayInput candidate;
        if (!drawReplay || !EnhancedLatticeReplayInput::Load(latticeReplayPath, candidate, outError))
        {
            if (!drawReplay) outError = "Lattice replay requires controlled camera and draw replay";
            return false;
        }
        latticeReplay = std::move(candidate);
    }
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.renderStateMutex);
    if (target >= EnhancedLiveDisplayTarget::Count || !state.enabled)
    { outError = "capture requires an enabled live renderer and valid display target"; return false; }
    if (state.pbrCapture && (state.pbrCapture->result.state == EnhancedPbrCaptureState::Pending
        || state.pbrCapture->result.state == EnhancedPbrCaptureState::Recording))
    { outError = "a PBR capture is already pending"; return false; }
    if (state.pbrCapture && state.pbrCapture->HasResources())
    {
        outError = "Previous capture resources are retained until GPU idle or device-loss cleanup.";
        return false;
    }
    std::error_code error;
    const std::filesystem::path path(directory);
    if (!path.is_absolute() || !std::filesystem::create_directories(path, error) || error)
    { outError = "capture requires a new absolute directory: " + directory; return false; }
    state.pbrCapture = std::make_unique<EnhancedPbrCapture>();
    state.pbrCapture->target = target;
    state.pbrCapture->controlled = controlled;
    state.pbrCapture->replayExtensions = replayExtensions || latticeReplayExtension
        || !drawReplayPath.empty() || !latticeReplayPath.empty();
    state.pbrCapture->latticeReplayExtension = latticeReplayExtension || !latticeReplayPath.empty();
    state.pbrCapture->cameraReplay = std::move(replay);
    state.pbrCapture->drawReplay = std::move(drawReplay);
    state.pbrCapture->latticeReplay = std::move(latticeReplay);
    state.pbrCapture->afterFrameId = state.publishedFrameId.load();
    state.pbrCapture->result.directory = directory;
    state.pbrCapture->result.state = EnhancedPbrCaptureState::Pending;
    outError.clear();
    return true;
}

EnhancedLivePbrCaptureStatus EnhancedSceneRenderer::GetLivePbrCaptureStatus()
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.renderStateMutex);
    return state.pbrCapture ? state.pbrCapture->result : EnhancedLivePbrCaptureStatus{};
}

void EnhancedSceneRenderer::CancelLivePbrCapture()
{
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> lock(state.renderStateMutex);
    if (state.pbrCapture && state.pbrCapture->result.state == EnhancedPbrCaptureState::Pending)
        state.pbrCapture->Fail("capture timed out before a matching live view was rendered");
}

bool EnhancedSceneRenderer::WaitForLiveRenderThreadIdle(uint32_t timeoutMilliseconds)
{
    return GetLiveState().WaitForRenderThreadIdle(timeoutMilliseconds);
}

void EnhancedSceneRenderer::TickLive(const EnhancedLiveFramePacket& inputFrame)
{
    LiveState& state = GetLiveState();
    // Declared before the lock so old CPU pixels are freed after it is released.
    std::unique_ptr<PreparedEnvironment> retiredEnvironment;
    std::unique_lock<std::mutex> stateLock(state.renderStateMutex, std::defer_lock);
    {
        RenderThreadPhaseScope lockWait(RenderPhase::state_lock_wait);
        stateLock.lock();
    }
    state.ApplyPreparedEnvironment(retiredEnvironment);
	state.profileFrameDrawCount = 0;
	state.profileFrameBatchCount = 0;
    state.controlledCaptureFrame = state.pbrCapture && state.pbrCapture->controlled
        && state.pbrCapture->result.state == EnhancedPbrCaptureState::Pending
        && inputFrame.frameId > state.pbrCapture->afterFrameId;
    // Copy only for diagnostic captures. Publication IDs, resource retirement and
    // simulation state remain monotonic; only render clock consumers see zero.
    std::optional<EnhancedLiveFramePacket> controlledFrame;
    if (state.controlledCaptureFrame)
    {
        controlledFrame = inputFrame;
        controlledFrame->totalSeconds = 0.f;
        controlledFrame->deltaSeconds = 0.f;
        if (state.pbrCapture->cameraReplay)
        {
            const auto& replay = *state.pbrCapture->cameraReplay;
            bool applied = false;
            if (replay.width == inputFrame.width && replay.height == inputFrame.height)
            {
                for (uint32_t i = 0; i < (std::min)(controlledFrame->viewCount, kMaxLiveCameraViews); ++i)
                {
                    auto& view = controlledFrame->views[i];
                    if (view.displayTarget != state.pbrCapture->target) continue;
                    // This slice restores camera/clock only. Other live view input
                    // must still agree; silently switching overlay/sky state would
                    // claim an archive of data that this file does not own.
                    if (static_cast<uint32_t>(view.viewFlags) != replay.viewFlags
                        || inputFrame.skyBoxEnabled != replay.skyBoxEnabled) break;
                    view.camera = replay.camera;
                    controlledFrame->totalSeconds = replay.totalSeconds;
                    controlledFrame->deltaSeconds = replay.deltaSeconds;
                    applied = true;
                    break;
                }
            }
            if (!applied)
            {
                state.pbrCapture->Fail("camera replay dimensions, target or view state mismatch");
                controlledFrame.reset();
                state.controlledCaptureFrame = false;
            }
        }
    }
    const EnhancedLiveFramePacket& frame = controlledFrame ? *controlledFrame : inputFrame;
    const std::thread::id currentThread = std::this_thread::get_id();
    if (state.frameConsumerThread == std::thread::id{})
        state.frameConsumerThread = currentThread;
    assert(state.frameConsumerThread == currentThread &&
        "TickLive consumer changed threads without an ownership handoff");
    assert(frame.frameId > state.consumedFrameId &&
        "RenderFramePacket must be consumed exactly once in publication order");
    const auto traceProgress = [&](const char* phase) {
        static const bool enabled = [] { const char* value = std::getenv("CE_RENDER_PROGRESS_TRACE"); return value && std::string_view(value) == "1"; }();
        if (enabled && frame.frameId <= 3)
        {
            std::printf("[render.progress] frame=%llu phase=%s\n", static_cast<unsigned long long>(frame.frameId), phase);
            std::fflush(stdout);
        }
    };
    traceProgress("consume");

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
    traceProgress("tuning.begin");
    state.ApplyAndPublishTuning();
    traceProgress("tuning.end");

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
    state.skyBoxEnabled = frame.skyBoxEnabled;

    // ── [프레임당 1회] 씬·프록시 갱신 ──
    //
    // SceneRenderer::CreateCommandListPass/EndOfFrame에서 이관한 프레임 입력
    // 단계. App이 두 렌더 배리어를 모두 지난 뒤 호출하므로 씬 구조 변경과
    // 에디터 카메라 조작이 끝난 안정된 프레임 경계다. 카메라 수와 무관하게
    // 프록시는 한 번만 민다 — 카메라 값은 이미 frame packet에 밀봉돼 있다.
    {
        RenderThreadPhaseScope proxy(RenderPhase::proxy_sync);
        if (state.runtimeInitialized && state.renderScene && !sceneLoading)
        {
            if (state.renderScene->BeginProxyFrame(frame.sceneEpoch))
            {
                ProxyCommandQueue->ExecuteBatch(*state.renderScene, frame.sceneEpoch,
                    std::move(state.activeDeltaBatch));
            }
            else
            {
                ProxyCommandQueue->DeferBatch(std::move(state.activeDeltaBatch));
            }
        }
        else
        {
            // 로딩 중에는 frame은 건너뛰어도 delta를 잃으면 안 된다. consumer 보류
            // 큐에 두었다가 다음 renderable packet에서 epoch 규칙과 함께 적용한다.
            ProxyCommandQueue->DeferBatch(std::move(state.activeDeltaBatch));
            // 이번 프레임에 갱신하지 않았다면 풀을 비운다.
            //
            // ★ 풀은 값 snapshot과 shared owner를 함께 들지만, 갱신을 건너뛴
            //   프레임에 이전 씬의 draw를 재사용하면 scene epoch가 달라진 뒤에도
            //   낡은 화면이 남는다. "이번 프레임에 모은 것만 그린다"로 둔다.
            state.drawPool.clear();
            state.graphDraws.clear();
            state.graphShadowEligible.clear();
            state.graphViewRequired.clear();
            state.graphViewInput.reset();
            state.decals.clear();
        }
    }

    traceProgress("proxy.end");
    traceProgress("collect.begin");
    state.CollectCompletedDisplays();
    if (state.activeFrameDrainOnly || state.ShouldSkipScenePixels(frame))
    {
        return;
    }
    if (state.runtimeInitialized && state.renderScene && !sceneLoading)
    {
        RenderThreadPhaseScope proxy(RenderPhase::proxy_sync);
        state.BuildDrawPool();
    }

    // ── Vulkan editor TickLive 공통 scene graph 경로 ──
    //
    // 씬/프록시 수집과 카메라별 밀봉은 위의 백엔드 중립 경로를 그대로 쓴다.
    // 갈리는 것은 backend 자원과 표시 슬롯뿐이다. DX12와 같은 LivePipelineDesc가
    // Vulkan services/encoder/cache를 받고, 완료된 timeline 슬롯만 CPU 표시
    // 브리지로 승격한다.
    if (EnhancedLiveBackend::Vulkan == state.backend)
    {
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
            if (!state.TeardownVulkanPipeline())
            {
                return;
            }
        }
        if (!state.vulkanPipeline)
        {
            std::string error;
            if (!state.BuildVulkanPipeline(rtWidth, rtHeight, error))
            {
                state.lastError = "Vulkan 파이프라인 구축 실패: " + error;
                state.enabled = false;
                Debug::PrintLog(spdlog::level::err, "[EnhancedRenderer] " + state.lastError);
                return;
            }
        }

        VulkanLivePipeline& p = *state.vulkanPipeline;
        p.frameContext.frameId = frame.frameId;
        p.frameContext.sceneEpoch = frame.sceneEpoch;
        if (state.ShouldSkipScenePixels(frame))
        {
            return;
        }
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
            if (state.controlledCaptureFrame && viewPacket.displayTarget != state.pbrCapture->target)
                continue;
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
            if (viewPacket.displayTarget == EnhancedLiveDisplayTarget::MaterialPreview &&
                p.views[viewIndex].ready && p.views[viewIndex].previewComplete &&
                p.views[viewIndex].completedSceneEpoch == frame.sceneEpoch && !state.controlledCaptureFrame &&
                !state.HasGraphSnapshotRequest(viewPacket.displayTarget))
            {
                continue;
            }
            bool captured = false;
            {
                RenderThreadPhaseScope capture(RenderPhase::view_capture);
                captured = state.CaptureFromView(frame, viewPacket);
            }
            if (!captured)
            {
                ++state.framesIdle;
                continue;
            }

            if (state.ShouldSkipScenePixels(frame))
            {
                return;
            }
            std::string error;
            const auto prepareFrame = [&state, &p, viewIndex](std::string& prepareError)
            {
                return state.PreparePipelineFrame(
                    p, static_cast<uint32_t>(viewIndex), RHIShaderBinary::SpirV, prepareError);
            };
            bool rendered = false;
            bool preparationDeferred = false;
            LiveGraphSnapshot diagnosticSnapshot;
            const bool captureDiagnostics = state.ConsumeGraphSnapshotRequest(viewPacket.displayTarget);
            {
                RenderThreadPhaseScope renderView(RenderPhase::view_render);
                rendered = p.Render(static_cast<uint32_t>(viewIndex), viewPacket,
                    frame.frameId, frame.sourceCaptureNanoseconds, frame.resizeGeneration,
                    GetRHISubmissionThread().GetOwnerGeneration(&p.resources),
                    prepareFrame, error, state.BeginPbrCapture(frame, viewPacket),
                    captureDiagnostics ? &diagnosticSnapshot : nullptr, preparationDeferred);
            }
            if (captureDiagnostics || !rendered)
            {
                state.PublishGraphSnapshot(viewPacket.displayTarget,
                    rendered ? std::move(diagnosticSnapshot) : LiveGraphSnapshot{});
            }
            if (!rendered)
            {
                if (state.pbrCapture && state.pbrCapture->result.state == EnhancedPbrCaptureState::Recording)
                    state.pbrCapture->Fail(error);
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
                if (!preparationDeferred || !validation.empty() || 0 != unimplemented)
                {
                    ++state.consecutiveFrameFailures;
                }
                else
                {
                    // Keep the last completed display while this feature retries
                    // bounded admission; budget pressure is not renderer corruption.
                    state.consecutiveFrameFailures = 0;
                }
                if (state.reportedValidation.insert(state.lastError).second)
                    Debug::PrintLog(spdlog::level::err, "[EnhancedRenderer] " + state.lastError);
                if (LiveState::kMaxConsecutiveFrameFailures <= state.consecutiveFrameFailures)
                {
                    state.TeardownVulkanPipeline();
                    state.enabled = false;
                    return;
                }
                continue;
            }

            state.consecutiveFrameFailures = 0;
            state.RecordSceneAdmission(frame);
            // W8: 인코더가 버린 명령을 프레임마다 비우며 모은다. 성공한 프레임에
            // 쌓이는 것이 특히 중요하다 — 실패 경로는 이미 소리를 내지만 이쪽은
            // "그려졌다"고 보고되면서 물체가 빠진 경우다.
            {
                std::string lastDrop;
                uint64_t drops = p.commandPool.DrainEncoderDrops(lastDrop);
                // capture 프레임이 먼저 비워 맡겨 둔 몫을 합친다.
                drops += p.stashedEncoderDrops;
                if (lastDrop.empty()) lastDrop = p.stashedLastEncoderDrop;
                p.stashedEncoderDrops = 0;
                p.stashedLastEncoderDrop.clear();
                if (0 != drops)
                {
                    state.encoderDrops += drops;
                    if (!lastDrop.empty()) state.lastEncoderDrop = lastDrop;
                }
            }
            state.lastDrawCount = p.gbuffer.GetLastDrawCount();
            state.lastBatchCount = p.gbuffer.GetLastBatchCount();
			state.profileFrameDrawCount += state.lastDrawCount;
			state.profileFrameBatchCount += state.lastBatchCount;
            state.lastDecalCount = p.decal.GetLastDecalCount();
            state.lastDecalBatchCount = p.decal.GetLastBatchCount();
            state.lastSpriteCount = p.sprite.GetLastItemCount();
            state.lastSpriteBatchCount = p.sprite.GetLastBatchCount();
            state.lastUIRectCount = p.ui.GetLastRectCount();
            state.lastUIBatchCount = p.ui.GetLastBatchCount();
            const uint32_t targetIndex = LiveState::DisplayTargetIndex(
                viewPacket.displayTarget);
            state.viewSpriteCounts[targetIndex] = state.lastSpriteCount;
            state.viewUICounts[targetIndex] = state.lastUIRectCount;
            state.viewShadowStats[targetIndex] = CaptureShadowStats(p);
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


    if (sceneLoading || 0 == cameraCount)
    {
        ++state.framesIdle;
        return;
    }

    // 해상도는 밀봉이 카메라가 아니라 화면 크기 버스에서 가져오므로
    // 모든 뷰가 공유한다 — 재구축 판정도 프레임당 한 번이면 된다.
    // 패스는 PrepareFrame/Declare에서 새 크기를 반영한다. 전체 재초기화는
    // 셰이더 소스 준비와 ShaderMeta/IBL 생성까지 반복하므로 표시 슬롯과
    // transient 풀만 GPU 완료 뒤 교체한다.
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
        if (!state.ResizePipeline(rtWidth, rtHeight, resizeError))
        {
            state.lastError = "DX12 resize lifecycle 실패: " + resizeError;
            Debug::PrintLog(spdlog::level::err, "[EnhancedRenderer] " + state.lastError);
            state.TeardownPipeline();
            state.enabled = false;
            return;
        }
    }

    traceProgress("pipeline.begin");
    if (nullptr == state.pipeline)
    {
        std::string error;
        if (!state.BuildPipeline(rtWidth, rtHeight, error))
        {
            state.lastError = "파이프라인 구축 실패: " + error;
            Debug::PrintLog(spdlog::level::err, "[EnhancedRenderer] " + state.lastError);
            state.TeardownPipeline();
            state.enabled = false;   // 실패를 조용히 반복하지 않는다
            return;
        }
    }

    traceProgress("pipeline.end");
    LivePipeline& p = *state.pipeline;
    {
        traceProgress("seal.begin");
        // W8: 이 프레임의 신원을 패스가 볼 수 있는 자리에 둔다. sealing이 도장을
        // 찍고 패스가 대조하는데, 둘 다 frameContext만 받으므로 여기가 유일한
        // 공통 자리다. 초기화/리사이즈에서 한 번 채우면 늘 같은 수가 되어
        // staleness를 못 잰다.
        p.frameContext.frameId = frame.frameId;
        p.frameContext.sceneEpoch = frame.sceneEpoch;
    }

    traceProgress("seal.end");
    if (state.ShouldSkipScenePixels(frame))
    {
        return;
    }

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
    uint32_t consideredViews = 0;
    uint32_t leaseSkippedViews = 0;
    const uint32_t startIndex = state.viewRotation++ % cameraCount;
    for (uint32_t step = 0; step < cameraCount; ++step)
    {
        const uint32_t packetViewIndex = (startIndex + step) % cameraCount;
        const EnhancedLiveViewPacket& viewPacket = frame.views[packetViewIndex];
        if (!viewPacket.key.IsValid()) continue;
        // The diagnostic clock must not enter another camera's temporal history.
        if (state.controlledCaptureFrame && viewPacket.displayTarget != state.pbrCapture->target)
            continue;
        ++consideredViews;

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
            view->displayTarget = viewPacket.displayTarget;
            view->viewFlags = viewPacket.viewFlags;
            view->displaySlot = -1;
            view->promotionCount = 0;
            view->promotedSlotMask = 0;
        }

        // View policies may change while the logical camera/history identity stays the same.
        view->viewFlags = viewPacket.viewFlags;

        // 표시 중도 인플라이트도 아닌 슬롯에 그린다.
        if (viewPacket.displayTarget == EnhancedLiveDisplayTarget::MaterialPreview &&
            view->displaySlot >= 0 && view->slots[view->displaySlot].previewComplete &&
            view->slots[view->displaySlot].sceneEpoch == frame.sceneEpoch && !state.controlledCaptureFrame &&
            !state.HasGraphSnapshotRequest(viewPacket.displayTarget))
        {
            // Reopening a hidden preview reuses its completed image. The public
            // demand snapshot was cleared while hidden, so publish it again.
            std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
            const auto& cached = view->slots[view->displaySlot];
            state.PublishDisplayResultLocked(view->displayTarget, view->key, cached.interopToken,
                cached.frameId, view->promotionCount, view->promotedSlotMask, p.width, p.height,
                cached.sceneEpoch, cached.camera, p.resizeGeneration, true,
                cached.sourceCaptureNanoseconds, cached.completedAgeMs);
            continue;
        }
        int renderSlot = -1;
        {
            // OpenDisplayTexture도 같은 잠금 아래 소비자 lease를 등록한다.
            // 선택할 token은 게시 목록에서도 빠져 있어야 잠금을 푼 뒤 새 Host
            // 조회와 이 슬롯의 기록이 경합하지 않는다.
            std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
            for (int i = 0; i < LivePipeline::kSlotsPerView; ++i)
            {
                const auto token = view->slots[i].interopToken;
                if (i == view->displaySlot ||
                    std::find(view->pendingQueue.begin(), view->pendingQueue.end(), i) != view->pendingQueue.end() ||
                    std::find(state.displayPresentationKeys.begin(), state.displayPresentationKeys.end(), token) !=
                        state.displayPresentationKeys.end())
                {
                    continue;
                }
                if (state.dx12.CanReuseDisplayTexture(token))
                {
                    renderSlot = i;
                    break;
                }
            }
        }
        if (renderSlot < 0)
        {
            ++state.framesInFlight;
            std::lock_guard<std::mutex> queueLock(state.renderQueueMutex);
            ++state.renderDisplayLeaseSkips;
            ++leaseSkippedViews;
            continue;
        }

        bool captured = false;
        {
            RenderThreadPhaseScope capture(RenderPhase::view_capture);
            captured = state.CaptureFromView(frame, viewPacket);
        }
        if (!captured)
        {
            ++state.framesIdle;
            continue;
        }

        if (state.ShouldSkipScenePixels(frame))
        {
            return;
        }
        std::string error;
        bool rendered = false;
        bool preparationDeferred = false;
        LiveGraphSnapshot diagnosticSnapshot;
        const bool captureDiagnostics = state.ConsumeGraphSnapshotRequest(viewPacket.displayTarget);
        {
            RenderThreadPhaseScope renderView(RenderPhase::view_render);
            rendered = state.RenderOnce(*view, renderSlot, frame.frameId, frame.sourceCaptureNanoseconds, error,
                state.BeginPbrCapture(frame, viewPacket),
                captureDiagnostics ? &diagnosticSnapshot : nullptr, preparationDeferred);
        }
        if (captureDiagnostics || !rendered)
        {
            state.PublishGraphSnapshot(viewPacket.displayTarget,
                rendered ? std::move(diagnosticSnapshot) : LiveGraphSnapshot{});
        }
        if (!rendered)
        {
            if (state.pbrCapture && state.pbrCapture->result.state == EnhancedPbrCaptureState::Recording)
                state.pbrCapture->Fail(error);
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
            if (preparationDeferred)
            {
                state.consecutiveFrameFailures = 0;
            }
            else
            {
                ++state.consecutiveFrameFailures;
            }

            // 같은 문장이 매 프레임 반복되므로 처음 본 것만 찍는다
            // (검증 레이어 메시지와 같은 규약 — 안 그러면 콘솔이 도배돼
            // 정작 첫 원인을 못 본다).
            if (state.reportedValidation.insert(state.lastError).second)
            {
                Debug::PrintLog(spdlog::level::err, "[EnhancedRenderer] " + state.lastError);
            }

            if (LiveState::kMaxConsecutiveFrameFailures <= state.consecutiveFrameFailures)
            {
                Debug::PrintLog(spdlog::level::err, "[EnhancedRenderer] 프레임 실패가 "
                    + std::to_string(state.consecutiveFrameFailures)
                    + "회 연속이라 파이프라인을 접는다. 마지막 사유: " + error);
                state.TeardownPipeline();
                state.enabled = false;
                return;
            }
            continue;
        }
        state.consecutiveFrameFailures = 0;
        state.RecordSceneAdmission(frame);
        ++totalPending;
        state.gpuMaxPendingSubmissions = (std::max)(
            state.gpuMaxPendingSubmissions, static_cast<uint32_t>(totalPending));
        state.AddNativeRecordSample(p.lastNativeRecordMs);
        renderedAny = true;
    }

    if (renderedAny) state.lastCpuMs = watch.ElapsedMs();
    state.renderDisplayLeaseBlocked = consideredViews != 0 && leaseSkippedViews == consideredViews;
}


EnhancedLiveDisplaySnapshot EnhancedSceneRenderer::GetLiveDisplaySnapshot()
{
    const LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
    return state.displaySnapshot;
}

EnhancedLiveDisplayTexture EnhancedSceneRenderer::GetLiveDisplayTexture(
    EnhancedLiveDisplayTarget target)
{
    // CE는 Camera/backend view를 조회하지 않는다. RT가 발행한 논리 대상의
    // 불투명 key만 열고, resize/teardown과 공유 핸들 수명은 같은 락으로 막는다.
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> displayLock(state.displayLifetimeMutex);
    const uint32_t targetIndex = static_cast<uint32_t>(target);
    if (targetIndex >= kEnhancedLiveDisplayTargetCount)
    {
        return {};
    }
    auto& entry = state.displaySnapshot.targets[targetIndex];
    auto acquiredFrame = entry;
    const auto observe = [&](uint64_t textureId, const char* reason)
    {
        ++entry.textureQueries;
        entry.lastTextureAvailable = textureId != 0;
        auto& missingSince = state.displayMissingSince[targetIndex];
        const auto now = std::chrono::steady_clock::now();
        if (!textureId)
        {
            ++entry.missingTextureQueries;
            if (missingSince == std::chrono::steady_clock::time_point{})
            {
                missingSince = now;
                entry.lastMissingResizeGeneration = state.displaySnapshot.resizeGeneration;
                std::printf("[LiveDisplay] target=%u resize=%llu unavailable=%s\n",
                    targetIndex, static_cast<unsigned long long>(
                        state.displaySnapshot.resizeGeneration), reason);
            }
        }
        if (missingSince != std::chrono::steady_clock::time_point{})
        {
            const double elapsed = std::chrono::duration<double, std::milli>(
                now - missingSince).count();
            entry.maxMissingTextureMs = (std::max)(entry.maxMissingTextureMs, elapsed);
            if (textureId)
            {
                entry.lastMissingTextureMs = elapsed;
                std::printf("[LiveDisplay] target=%u resize=%llu recovered=%.3fms frame=%llu\n",
                    targetIndex, static_cast<unsigned long long>(
                        state.displaySnapshot.resizeGeneration), elapsed,
                    static_cast<unsigned long long>(acquiredFrame.completedFrameId));
                missingSince = {};
            }
        }
        if (textureId)
        {
            entry.lastTextureFrameId = acquiredFrame.completedFrameId;
            entry.lastTextureResizeGeneration = acquiredFrame.completedResizeGeneration;
            entry.lastTextureAgeMs = capture_age_milliseconds(
                acquiredFrame.completedCaptureNanoseconds, capture_steady_nanoseconds());
            acquiredFrame.lastTextureAgeMs = entry.lastTextureAgeMs;
        }
        acquiredFrame.ready = textureId != 0;
        return EnhancedLiveDisplayTexture{textureId, textureId ? acquiredFrame.completedWidth : 0,
            textureId ? acquiredFrame.completedHeight : 0, acquiredFrame, state.displaySnapshot.backend,
            entry.completedFrameId, entry.completedCamera.editorInputSequence,
            entry.completedCamera.editorCameraRevision};
    };
    // Host가 설치한 표시 sink가 ID를 해석한다(E4-6a). Core는 표시 수명 락만
    // 소유하고 ImGui 셸을 모른다 — 미설치·비활성이면 표시할 수단이 없다.
    const std::shared_ptr<IDisplayPresentationSink> sink =
        state.CopyPresentationSink();
    if (!sink || !sink->IsActive())
    {
        return observe(0, "sink_inactive");
    }
    if (!state.enabled)
    {
        return observe(0, "renderer_disabled");
    }
    const uint64_t presentationKey = state.displayPresentationKeys[targetIndex];
    if (!entry.active)
    {
        return observe(0, "view_inactive");
    }
    if (!entry.ready)
    {
        return observe(0, "result_pending");
    }
    if (0 == presentationKey)
    {
        return observe(0, "key_missing");
    }

    if (entry.completedSceneEpoch != state.sceneEpoch.load())
    {
        return observe(0, "scene_changed");
    }
    if (entry.completedResizeGeneration != state.displaySnapshot.resizeGeneration)
    {
        return observe(0, "resize_changed");
    }
    if (EnhancedLiveBackend::Vulkan == state.displaySnapshot.backend)
    {
        const RHIDisplayTexture uploaded = sink->GetCpuFrameTexture(presentationKey);
        if (0 == uploaded.m_textureId)
        {
            return observe(0, "cpu_frame_missing");
        }
        if (uploaded.m_frame.m_viewId != entry.key.viewId ||
            uploaded.m_frame.m_historyRevision != entry.key.historyRevision ||
            uploaded.m_frame.m_sceneEpoch != state.sceneEpoch.load() ||
            uploaded.m_frame.m_resizeGeneration != state.displaySnapshot.resizeGeneration)
        {
            return observe(0, "cpu_frame_stale");
        }
        // RT 완료와 Host 업로드는 다른 시점이다. 실제 업로드한 픽셀의 카메라만 돌려준다.
        if (acquiredFrame.completedFrameId != uploaded.m_frame.m_frameId)
        {
            // 업로드 메타데이터에는 원본 캡처 시각만 있고 이전 생산자 완료
            // 관측 시각은 없다. 새 결과의 완료 시간을 옛 픽셀에 붙이지 않는다.
            acquiredFrame.completedAgeMs = 0.0;
        }
        acquiredFrame.completedFrameId = uploaded.m_frame.m_frameId;
        acquiredFrame.completedSceneEpoch = uploaded.m_frame.m_sceneEpoch;
        acquiredFrame.completedResizeGeneration = uploaded.m_frame.m_resizeGeneration;
        acquiredFrame.completedCamera = uploaded.m_frame.m_camera;
        acquiredFrame.completedCaptureNanoseconds = uploaded.m_frame.m_sourceCaptureNanoseconds;
        acquiredFrame.completedWidth = uploaded.m_width;
        acquiredFrame.completedHeight = uploaded.m_height;
        return observe(uploaded.m_textureId, "cpu_frame_missing");
    }
    return observe(state.dx12.OpenDisplayTexture(*sink, presentationKey), "display_token_missing");
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

EnhancedSceneRenderer::LiveSealDiagnostics
EnhancedSceneRenderer::GetLiveSealDiagnostics()
{
    // ★ const 로 잡으면 컴파일되지 않는다 — `TextureCache()` 접근자가 const
    //   한정되어 있지 않다(어댑터 쪽 모양이고 이 슬라이스가 고칠 것이 아니다).
    //   읽기만 하는 함수지만 참조는 비const 로 든다.
    LiveState& state = GetLiveState();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);

    LiveSealDiagnostics out;
    out.enabled = state.enabled;
    out.framesRendered = state.framesRendered;
    out.encoderDrops = state.encoderDrops;
    out.lastDrawCount = state.lastDrawCount;
    out.lastBatchCount = state.lastBatchCount;
    out.modelGenerationPairs = state.lastModelGenerationPairs;
    out.mixedGenerationModels = state.lastMixedGenerationModels;
    out.mixedNewestGeneration = state.lastMixedNewestGeneration;

    const auto read = [&out](const EnhancedGBufferPass& gbuffer,
        const EnhancedForwardPass& forward, uint32_t textureFailures)
    {
        const EnhancedDrawSealLedger& gb = gbuffer.GetSealLedger();
        const EnhancedDrawSealLedger& fw = forward.GetSealLedger();
        out.frameId = gb.frameId;
        out.gbufferStamped = gb.counters.stamped;
        out.gbufferUnstamped = gb.counters.unstamped;
        out.gbufferViolations = gb.Violations();
        out.forwardStamped = fw.counters.stamped;
        out.forwardUnstamped = fw.counters.unstamped;
        out.forwardViolations = fw.Violations();
        out.textureUploadFailures = textureFailures;
    };
    if (EnhancedLiveBackend::Vulkan == state.backend)
    {
        if (const VulkanLivePipeline* pipeline = state.vulkanPipeline.get())
            read(pipeline->gbuffer, pipeline->forward,
                pipeline->textureCache.GetUploadFailureCount());
    }
    else if (const LivePipeline* pipeline = state.pipeline.get())
    {
        read(pipeline->gbuffer, pipeline->forward,
            state.dx12.TextureCache().GetUploadFailureCount());
    }
    return out;
}

EnhancedSceneRenderer::WarmupStatus EnhancedSceneRenderer::GetWarmupStatus()
{
    LiveState& state = GetLiveState();
    std::unique_lock<std::mutex> render(state.renderStateMutex, std::defer_lock);
    std::unique_lock<std::mutex> display(state.displayLifetimeMutex, std::defer_lock);
    WarmupStatus status;
    if (std::try_lock(render, display) != -1)
    {
        status.busy = true;
        return status;
    }
    status.failed = !state.enabled && !state.lastError.empty();
    if (status.failed) status.error = state.lastError;
    const auto index = static_cast<std::uint32_t>(EnhancedLiveDisplayTarget::Editor);
    const auto& entry = state.displaySnapshot.targets[index];
    status.ready = state.enabled && entry.active && entry.ready &&
        entry.completedFrameId != 0 && state.displayPresentationKeys[index] != 0;
    status.completedFrame = entry.completedFrameId;
    return status;
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
            " · CPU %.2f ms · 메시 캐시 %u개/업로드 %u(generation %u)/실패 %u",
            state.enabled ? "켜짐" : "꺼짐",
            pipeline ? "준비됨" : "없음",
            pipeline ? pipeline->width : 0u,
            pipeline ? pipeline->height : 0u,
            static_cast<unsigned long long>(state.framesRendered),
            static_cast<unsigned long long>(state.framesIdle),
            pipeline ? pipeline->PendingCount() : 0u,
            state.lastDrawCount, state.lastBatchCount, state.lastCpuMs,
            meshStats.residentCount, meshStats.uploads,
            meshStats.modelGenerationUploads, meshStats.failures);

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
        state.AppendGBufferMaterialStatus(status);
        state.AppendForwardMaterialStatus(status);
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
    // MBC9: 모델 지오메트리 업로드는 typed generation 진입점 하나다.
    status += "\n  메시 캐시 — generation 업로드 "
        + std::to_string(const_cast<LiveState&>(state)
            .dx12.MeshCache().GetModelGenerationUploadCount());
    char recordLine[192]{};
    std::snprintf(recordLine, sizeof(recordLine),
        "\n  Native record CPU — last %.3f ms · avg %.3f ms · max %.3f ms · samples %llu",
        state.lastNativeRecordMs, state.AverageNativeRecordMs(),
        state.maxNativeRecordMs,
        static_cast<unsigned long long>(state.nativeRecordSamples));
    status += recordLine;
    if (state.pipeline)
    {
        const EnhancedRenderGraph::Stats& graphStats =
            state.pipeline->lastGraphStats;
        status += "\n  병렬 기록 — worker " +
            std::to_string(graphStats.recordWorkers) + " · batch " +
            std::to_string(graphStats.recordedLists) + " · unit " +
            std::to_string(graphStats.recordUnits) +
            (graphStats.parallelDeclined ? " · 순차 전환" : "");
    }

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
        state.AppendGBufferMaterialStatus(status);
        state.AppendForwardMaterialStatus(status);
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

std::shared_ptr<const EnhancedRenderGraph::DiagnosticSnapshot>
EnhancedSceneRenderer::GetLiveGraphSnapshot(EnhancedLiveDisplayTarget target)
{
    LiveState& state = GetLiveState();
    const uint32_t index = static_cast<uint32_t>(target);
    if (index >= kEnhancedLiveDisplayTargetCount)
    {
        return {};
    }
    const auto display = GetLiveDisplaySnapshot();
    std::lock_guard<std::mutex> lock(state.debugMutex);
    // Completed previews reuse their image. Sample once when the retained graph
    // is absent or stale, then preserve both the image and its CPU snapshot.
    if (target != EnhancedLiveDisplayTarget::MaterialPreview || !state.graphSnapshots[index] ||
        !EnhancedGraphSnapshotMatchesView(*state.graphSnapshots[index], display.Get(target)))
    {
        state.graphSnapshotRequests[index] = true;
    }
    return state.graphSnapshots[index];
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
    InvalidateEnvironmentPreparation(*state.environmentPreparation, true);
    state.StopRenderThread();
    std::lock_guard<std::mutex> stateLock(state.renderStateMutex);
    state.enabled = false;
    const bool dx12Released = state.TeardownPipeline();
    const bool vulkanReleased = state.TeardownVulkanPipeline();
    if (!dx12Released || !vulkanReleased)
    {
        // Keep scene/interop owners alive for a later proven-idle recovery.
        return;
    }
    state.ResetDisplaySnapshot();

    state.dx12.ShutdownInterop();

    if (state.runtimeInitialized)
    {
        state.skyEquirect.reset();
        state.skyCooked.reset();
        state.skyCookIdentity.reset();
        state.skyCookCachePath.clear();
        if (state.renderScene)
        {
            // SceneManager::Decommissioning이 활성 RenderScene을 먼저 Finalize한다.
            // 여기서 다시 부르면 프록시 맵/컨테이너를 이중 정리한다.
            state.renderScene.reset();
        }
        state.runtimeInitialized = false;
    }
}
