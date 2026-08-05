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
#include "EnhancedGBufferPass.h"
#include "EnhancedShadowPass.h"
#include "EnhancedDeferredPass.h"
#include "EnhancedSSGIPass.h"
#include "EnhancedForwardPass.h"
#include "EnhancedSSAOPass.h"
#include "EnhancedPostChainPass.h"

#include "../../Camera.h"
#include "../../Material.h"
#include "../../RenderScene.h"
#include "../../LightController.h"
#include "../../Texture.h"
#include "../../RenderPassData.h"
#include "../../MeshRendererProxy.h"
#include "../../Skeleton.h"
#include "../../Mesh.h"

#include <d3d11_1.h>
#include <wrl/client.h>
#include <cstdio>
#include <vector>

// EnhancedSceneRenderer의 상시 러너(렌더러 스위치, 병존기) 구현.
//
// 공개 표면은 EnhancedSceneRenderer의 정적 Live API다(헤더의 규약 주석 참조).
// 상태를 이 파일 안의 싱글턴에 숨긴 이유: EnhancedSceneRenderer 인스턴스는
// 콘솔 명령마다 스택에 만들어지는 검증용이라 상시 상태를 들 수 없고, 그렇다고
// 별도 공개 클래스를 두면 "DX12 렌더러 = EnhancedSceneRenderer"라는 로드맵의
// 명칭 체계가 흐려진다(실제로 그렇게 만들었다가 물렸다).
namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.

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

        EnhancedGBufferPass   gbuffer;
        EnhancedShadowPass    shadow;
        EnhancedDeferredPass  deferred;
        EnhancedSSGIPass      ssgi;
        EnhancedForwardPass   forward;
        EnhancedSSAOPass      ssao;
        EnhancedPostChainPass postChain;

        EnhancedFrameContext frameContext{};

        // DX12가 그리는 공유 텍스처와, DX11이 그것을 연 것.
        ComPtr<ID3D12Resource>           sharedTexture;
        HANDLE                           sharedHandle{ nullptr };
        ComPtr<ID3D11Texture2D>          openedTexture;
        ComPtr<ID3D11ShaderResourceView> openedSrv;
    };

    struct LiveState
    {
        template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

        bool enabled{ false };

        // 지금 표시 중인 그림의 원천 카메라. GetLiveDisplaySrv가 이 포인터와
        // 대조해 '그 카메라의 창'만 교체 표시한다 — 다른 카메라의 창이 엉뚱한
        // 그림을 받는 것을 막는다.
        const Camera* boundCamera{ nullptr };

        std::unique_ptr<LivePipeline> pipeline;

        // 프레임 입력. frameContext가 이들의 주소를 들므로 파이프라인과 무관한
        // 여기 멤버로 둔다 — 주소가 흔들리면 패스가 든 포인터가 전부 무효가 된다.
        FrameCameraSnapshot           cameraSnapshot{};
        std::vector<EnhancedDrawItem> draws;
        std::vector<EnhancedDrawItem> forwardDraws;
        std::vector<EnhancedLight>    lights;

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
        uint64_t framesRendered{ 0 };
        uint64_t framesIdle{ 0 };
        double   lastGpuMs{ 0.0 };
        double   lastCpuMs{ 0.0 };
        std::string lastError;

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
                !p.textureCache.Initialize(&p.resources, DirectX11::DeviceStates->g_pDevice,
                    DirectX11::DeviceStates->g_pDeviceContext, outError) ||
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
            if (!p.gbuffer.Initialize(p.frameContext, outError)) return false;
            p.gbuffer.SetKeepAlive(false);
            if (!p.shadow.Initialize(p.frameContext, outError)) return false;
            if (!p.deferred.Initialize(p.frameContext, outError)) return false;
            if (!p.ssgi.Initialize(p.frameContext, outError)) return false;
            if (!p.forward.Initialize(p.frameContext, outError)) return false;
            if (!p.ssao.Initialize(p.frameContext, outError)) return false;
            if (!p.postChain.Initialize(p.frameContext, outError)) return false;

            // ── 공유 텍스처 + DX11 SRV ──
            //
            // 포스트 체인의 LDR 출력과 같은 RGBA8이라야 CopyTextureRegion이
            // 성립한다.
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
                    nullptr, IID_PPV_ARGS(&p.sharedTexture))))
                {
                    outError = "공유 텍스처 생성 실패";
                    return false;
                }

                if (FAILED(p.resources.GetDevice()->CreateSharedHandle(p.sharedTexture.Get(),
                        nullptr, GENERIC_ALL, nullptr, &p.sharedHandle)) ||
                    nullptr == p.sharedHandle)
                {
                    outError = "공유 핸들 생성 실패";
                    return false;
                }

                ComPtr<ID3D11Device1> device1;
                if (FAILED(dx11Device->QueryInterface(IID_PPV_ARGS(&device1))) ||
                    FAILED(device1->OpenSharedResource1(p.sharedHandle,
                        IID_PPV_ARGS(&p.openedTexture))))
                {
                    outError = "DX11에서 공유 텍스처 열기 실패";
                    return false;
                }

                if (FAILED(dx11Device->CreateShaderResourceView(p.openedTexture.Get(),
                    nullptr, &p.openedSrv)))
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

            if (p.sharedTexture || p.openedSrv)
            {
                Grave grave;
                grave.sharedTexture = std::move(p.sharedTexture);
                grave.sharedHandle = p.sharedHandle;
                grave.openedTexture = std::move(p.openedTexture);
                grave.openedSrv = std::move(p.openedSrv);
                graveyard.push_back(std::move(grave));
                p.sharedHandle = nullptr;
            }

            p.postChain.Shutdown();
            p.ssao.Shutdown();
            p.forward.Shutdown();
            p.ssgi.Shutdown();
            p.deferred.Shutdown();
            p.shadow.Shutdown();
            p.gbuffer.Shutdown();

            p.profiler.Shutdown();
            p.textureCache.Shutdown();
            p.meshCache.Shutdown();
            p.rootSignatures.Shutdown();
            p.psoManager.Shutdown();
            p.resources.Shutdown();

            pipeline.reset();
            boundCamera = nullptr;
        }

        // ── 씬 밀봉 복사 ──
        //
        // 규칙은 dx12.scene(RunSceneBindingTest [1/4])과 같다: 프록시·재질을
        // 들지 않고 필요한 것만 복사한다. 거기와 여기가 갈리면 dx12.scene은
        // 통과하는데 라이브만 틀리는 상태가 되므로, 규칙을 바꿀 때는 두 곳을
        // 함께 바꿀 것.
        bool CaptureScene(const Camera*& outCamera,
            uint32_t& outRtWidth, uint32_t& outRtHeight)
        {
            Camera* sceneCamera = nullptr;
            for (auto& camera : CameraManagement->GetCameras())
            {
                if (camera && RenderPassData::VaildCheck(camera.get()))
                {
                    sceneCamera = camera.get();
                    break;
                }
            }
            if (nullptr == sceneCamera) return false;

            const RenderPassData* renderData = RenderPassData::GetData(sceneCamera);
            if (nullptr == renderData || nullptr == renderData->m_renderTarget.get())
            {
                return false;
            }

            outCamera = sceneCamera;
            outRtWidth = static_cast<uint32_t>(renderData->m_renderTarget->GetWidth());
            outRtHeight = static_cast<uint32_t>(renderData->m_renderTarget->GetHeight());
            if (0 == outRtWidth || 0 == outRtHeight) return false;

            cameraSnapshot = renderData->GetFrameSnapshot();

            draws.clear();
            forwardDraws.clear();
            lights.clear();

            const auto copyQueue = [](const auto& queue, std::vector<EnhancedDrawItem>& out)
            {
                for (auto* proxy : queue)
                {
                    if (nullptr == proxy || nullptr == proxy->m_Mesh) continue;

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
                }
            };
            copyQueue(renderData->m_deferredQueue, draws);
            copyQueue(renderData->m_forwardQueue, forwardDraws);

            if (auto* renderScene = RenderPassData::GetActiveRenderScene())
            {
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
            }
            return true;
        }

        // ── 한 프레임 ──
        //
        // 배선은 dx12.scene의 renderAndCount와 같다(리드백 프로브만 없다).
        // 마지막의 live_present가 이 러너의 고유 조각이다 — 포스트 체인 결과를
        // 공유 텍스처로 복사한다. 그 소비 선언이 있어야 그래프가 체인을
        // 걷어내지 않는다(post_probe가 하던 역할을 실전에서는 이 복사가 맡는다).
        bool RenderOnce(std::string& outError)
        {
            LivePipeline& p = *pipeline;

            if (!p.resources.BeginFrame(outError)) return false;
            p.profiler.BeginFrame(frameCounter % DX12DeviceResources::kFrameCount);
            ++frameCounter;

            if (!p.shadow.PrepareFrame(p.frameContext, outError)) return false;
            if (!p.gbuffer.PrepareFrame(p.frameContext, outError)) return false;
            if (!p.deferred.PrepareFrame(p.frameContext, outError)) return false;
            if (!p.ssgi.PrepareFrame(p.frameContext, outError)) return false;
            if (!p.forward.PrepareFrame(p.frameContext, outError)) return false;
            if (!p.ssao.PrepareFrame(p.frameContext, outError)) return false;
            if (!p.postChain.PrepareFrame(p.frameContext, outError)) return false;

            EnhancedRenderGraph graph;
            graph.SetProfiler(&p.profiler);

            p.shadow.Declare(graph, p.frameContext);

            p.gbuffer.Declare(graph, p.frameContext);
            const auto outputs = p.gbuffer.GetOutputs();

            p.deferred.SetInputs(outputs);
            p.deferred.SetShadow(p.shadow.GetShadowMap(), p.shadow.GetShadowData());

            {
                EnhancedSSAOPass::Inputs ssaoInputs{};
                ssaoInputs.depth = outputs.depth;
                ssaoInputs.normal = outputs.normal;
                p.ssao.SetInputs(ssaoInputs);
            }
            p.ssao.SetFrameIndex(ssaoFrameIndex++);
            p.ssao.Declare(graph, p.frameContext);

            p.deferred.Declare(graph, p.frameContext);

            {
                EnhancedSSGIPass::Inputs ssgiInputs{};
                ssgiInputs.depth = outputs.depth;
                ssgiInputs.normal = outputs.normal;
                ssgiInputs.diffuse = outputs.diffuse;
                ssgiInputs.metalRough = outputs.metalRough;
                ssgiInputs.lighting = p.deferred.GetOutput();
                ssgiInputs.ambientOcclusion = p.ssao.GetOutput();
                p.ssgi.SetInputs(ssgiInputs);
            }
            p.ssgi.Declare(graph, p.frameContext);

            {
                EnhancedForwardPass::Inputs forwardInputs{};
                forwardInputs.depth = outputs.depth;
                forwardInputs.lighting = p.ssgi.GetOutput().IsValid()
                    ? p.ssgi.GetOutput() : p.deferred.GetOutput();
                p.forward.SetInputs(forwardInputs);
            }
            p.forward.Declare(graph, p.frameContext);

            {
                EnhancedPostChainPass::Inputs postInputs{};
                postInputs.color = p.ssgi.GetOutput().IsValid()
                    ? p.ssgi.GetOutput() : p.deferred.GetOutput();
                p.postChain.SetInputs(postInputs);
            }
            p.postChain.Declare(graph, p.frameContext);

            // ── live_present: LDR 결과 → 공유 텍스처 ──
            if (!p.postChain.GetOutput().IsValid())
            {
                outError = "포스트 체인 출력이 없다";
                return false;
            }
            {
                const RGHandle finalHandle = p.postChain.GetOutput();
                const RGHandle sharedHandleRG = graph.ImportTexture(p.sharedTexture.Get(),
                    RGResourceState::CopyDest, "Live.Shared");

                ID3D12Resource* const sharedResource = p.sharedTexture.Get();
                graph.AddPass("live_present",
                    { { finalHandle, RGResourceState::CopySource },
                      { sharedHandleRG, RGResourceState::CopyDest } },
                    [sharedResource, finalHandle](const EnhancedRenderGraph::ExecuteContext& executeContext)
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
            }

            if (!graph.Compile(p.resources.GetDevice(), outError)) return false;
            if (!graph.Execute(p.resources.GetCommandList(), outError)) return false;

            p.profiler.ResolveFrame(p.resources.GetCommandList());

            if (!p.resources.EndFrame(outError)) return false;

            // 프레임마다 끝까지 기다린다. 공유 텍스처에 펜스 공유·키드 뮤텍스를
            // 아직 안 붙였으므로, DX11이 읽기 전에 DX12 쓰기가 끝났음을 보장하는
            // 수단이 이 동기 대기뿐이다. 병존기의 브링업 품질이고, 3-9에서
            // 스왑체인과 함께 비동기화한다.
            p.resources.WaitForGpu();

            std::vector<DX12GpuProfiler::PassTiming> timings;
            std::string collectError;
            if (p.profiler.Collect(timings, collectError))
            {
                lastGpuMs = p.profiler.GetLastTotalMilliseconds();
            }
            return true;
        }
    };

    LiveState& GetLiveState()
    {
        static LiveState state;
        return state;
    }
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
    state.enabled = false;
    state.TeardownPipeline();
}

bool EnhancedSceneRenderer::IsLiveEnabled()
{
    return GetLiveState().enabled;
}

void EnhancedSceneRenderer::TickLive()
{
    LiveState& state = GetLiveState();
    if (!state.enabled) return;

    const Camera* sceneCamera = nullptr;
    uint32_t rtWidth = 0;
    uint32_t rtHeight = 0;
    if (!state.CaptureScene(sceneCamera, rtWidth, rtHeight))
    {
        ++state.framesIdle;
        return;
    }

    // 크기가 바뀌면 통째로 다시 세운다. 패스들이 화면 크기 리소스를
    // Initialize에서 잡으므로 부분 리사이즈보다 재구축이 단순하고,
    // 에디터 뷰 리사이즈는 드문 사건이라 비용이 문제되지 않는다.
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

    LiveStopwatch watch;
    watch.Start();

    std::string error;
    if (!state.RenderOnce(error))
    {
        state.lastError = "프레임 실패: " + error;
        state.TeardownPipeline();
        state.enabled = false;
        return;
    }

    state.lastCpuMs = watch.ElapsedMs();
    state.boundCamera = sceneCamera;
    ++state.framesRendered;
}

ID3D11ShaderResourceView* EnhancedSceneRenderer::GetLiveDisplaySrv(const Camera* camera)
{
    const LiveState& state = GetLiveState();
    if (!state.enabled || nullptr == state.pipeline) return nullptr;
    if (nullptr == camera || camera != state.boundCamera) return nullptr;
    if (0 == state.framesRendered) return nullptr;
    return state.pipeline->openedSrv.Get();
}

std::string EnhancedSceneRenderer::GetLiveStatus()
{
    const LiveState& state = GetLiveState();

    char line[384]{};
    std::snprintf(line, sizeof(line),
        "dx12.live — %s · 파이프라인 %s · %ux%u · 렌더 %llu프레임(대기 %llu)"
        " · CPU %.2f ms · GPU %.2f ms · 묘지 %zu",
        state.enabled ? "켜짐" : "꺼짐",
        state.pipeline ? "준비됨" : "없음",
        state.pipeline ? state.pipeline->width : 0u,
        state.pipeline ? state.pipeline->height : 0u,
        static_cast<unsigned long long>(state.framesRendered),
        static_cast<unsigned long long>(state.framesIdle),
        state.lastCpuMs, state.lastGpuMs,
        state.graveyard.size());

    std::string status = line;
    if (!state.lastError.empty()) status += "\n  마지막 오류: " + state.lastError;
    return status;
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
}

#endif
