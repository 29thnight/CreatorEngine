#include "SceneRenderer.h"
#include "DeviceState.h"
#include "RHI/DX11RHI.h"
#include "EngineSetting.h"
#include "ShaderSystem.h"
#include "ImGuiRegister.h"
#include "Benchmark.hpp"
#include "RenderScene.h"
#include "SceneManager.h"
#include "Scene.h"
#include "RenderableComponents.h"
#include "ImageComponent.h"
#include "SpriteSheetComponent.h"
#include "UIManager.h"
#include "UIButton.h"
#include "TextComponent.h"
#include "DataSystem.h"
#include "RenderState.h"
#include "TimeSystem.h"
#include "InputManager.h"
#include "LightComponent.h"
#include "CameraComponent.h"
#include "Terrain.h"
#include "CullingManager.h"
#include "IconsFontAwesome6.h"
#include "FoliageComponent.h"
#include "DecalComponent.h"
#include "fa.h"
#include "Trim.h"
#include "Profiler.h"
#include "SwapEvent.h"
#include "RenderDebugManager.h"

#include <iostream>
#include <string>
#include <regex>

#include "Animator.h"
#include "EffectComponent.h"
#include "EffectProxyController.h"

using namespace lm;

SceneRenderer* SceneRenderer::s_active = nullptr;

SceneRenderer::SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
{
	s_active = this;

    InitializeDeviceState();
    InitializeShadowMapDesc();

	m_threadPool = new ThreadPool;
	m_commandThreadPool = std::make_unique<RenderThreadPool>(DirectX11::DeviceStates->g_pDevice);
	m_renderScene = std::make_shared<RenderScene>();
	SceneManagers->SetRenderScene(m_renderScene.get());
	
	//sampler 생성
	//m_linearSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	//m_pointSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	InitializeTextures();

	ShaderSystem->Initialize();

	auto ao = Texture::CreateSharedScreenSized(
		"AmbientOcclusion",
		//DXGI_FORMAT_R16_UNORM,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	);
	ao->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	ao->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_ambientOcclusionTexture = ao;

	//Buffer 생성
	XMMATRIX identity = XMMatrixIdentity();

	m_ModelBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &Mathf::xMatrixIdentity);
	DirectX::SetName(m_ModelBuffer.Get(), "ModelBuffer");

#ifndef BUILD_FLAG
	m_pEditorCamera = std::make_shared<Camera>();
	m_pEditorCamera->RegisterContainer();
	m_pEditorCamera->m_avoidRenderPass.Set((flag)RenderPipelinePass::BlitPass);
	m_pEditorCamera->m_avoidRenderPass.Set((flag)RenderPipelinePass::AutoExposurePass);
#endif // !BUILD_FLAG

    //pass 생성
    //shadowMapPass 는 RenderScene의 맴버
    //gBufferPass
	m_threadPool->Enqueue([this]()
	{
		m_pGBufferPass = std::make_unique<GBufferPass>();
		ID3D11RenderTargetView* views[]{
			m_diffuseTexture->GetRTV(),
			m_metalRoughTexture->GetRTV(),
			m_normalTexture->GetRTV(),
			m_emissiveTexture->GetRTV(),
			m_bitmaskTexture->GetRTV()
		};
		m_pGBufferPass->SetRenderTargetViews(views, ARRAYSIZE(views));
		m_pGBufferPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings());
	});

    //ssaoPass
	m_threadPool->Enqueue([this, ao]()
	{
		m_pSSAOPass = std::make_unique<SSAOPass>();
		m_pSSAOPass->Initialize(
			ao,
			m_deviceResources->GetDepthStencilViewSRV(),
			m_normalTexture,
			m_diffuseTexture
		);
		m_pSSAOPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().ssao);
	});

    //deferredPass
	m_threadPool->Enqueue([this]()
	{
		m_pDeferredPass = std::make_unique<DeferredPass>();
		m_pDeferredPass->Initialize(
			m_diffuseTexture,
			m_metalRoughTexture,
			m_normalTexture,
			m_emissiveTexture,
			m_bitmaskTexture
		);
		m_pDeferredPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().deferred);
	});

	//forwardPass
	m_threadPool->Enqueue([this]()
	{
		m_pForwardPass = std::make_unique<ForwardPass>();
		m_pForwardPass->SetTexture(m_normalTexture.get());
		m_pForwardPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings());
	});

	//skyBoxPass
	m_threadPool->Enqueue([this]()
	{
		m_pSkyBoxPass = std::make_unique<SkyBoxPass>();
		m_currentSkyTextureName = PathFinder::Relative("HDR\\rosendal_park_sunset_puresky_4k.hdr").string();
		m_pSkyBoxPass->Initialize(m_currentSkyTextureName);
	});
	
	//toneMapPass
	m_threadPool->Enqueue([this]()
	{
		m_pToneMapPass = std::make_unique<ToneMapPass>();
		m_pToneMapPass->Initialize(
			m_toneMappedColourTexture
		);

		m_pToneMapPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().toneMap);
	});
	//spritePass
	m_threadPool->Enqueue([this]()
	{
		m_pSpritePass = std::make_unique<SpritePass>();
	});
	//m_pSpritePass->Initialize(m_toneMappedColourTexture.get());

	//blitPass
	m_threadPool->Enqueue([this]()
	{
		m_pBlitPass = std::make_unique<BlitPass>();
		m_pBlitPass->Initialize(m_deviceResources->GetBackBufferRenderTargetView());
	});

	m_threadPool->Enqueue([this]()
	{
		//PositionMapPass
		m_pPositionMapPass = std::make_unique<PositionMapPass>();
		//LightMap
		lightMap.Initialize();
	});


	//SSR
	m_threadPool->Enqueue([this]()
	{
		m_pScreenSpaceReflectionPass = std::make_unique<ScreenSpaceReflectionPass>();
		m_pScreenSpaceReflectionPass->Initialize(m_diffuseTexture.get(),
			m_metalRoughTexture.get(),
			m_normalTexture.get(),
			m_emissiveTexture.get(),
			m_bitmaskTexture.get()
		);
	});

	//SSS
	m_threadPool->Enqueue([this]()
	{
		m_pSubsurfaceScatteringPass = std::make_unique<SubsurfaceScatteringPass>();
		m_pSubsurfaceScatteringPass->Initialize(m_diffuseTexture.get(),
			m_metalRoughTexture.get()
		);
	});

	//Vignette
	m_threadPool->Enqueue([this]()
	{
		m_pVignettePass = std::make_unique<VignettePass>();
		m_pVignettePass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().vignette);
	});

	//ColorGrading
	m_threadPool->Enqueue([this]()
	{
		m_pColorGradingPass = std::make_unique<ColorGradingPass>();
		m_pColorGradingPass->Initialize();
		m_pColorGradingPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().colorGrading);
	});
	//m_pColorGradingPass->Initialize(PathFinder::Relative("ColorGrading\\LUT_3.png").string());

	//VolumetricFog
	m_threadPool->Enqueue([this]()
	{
		m_pVolumetricFogPass = std::make_unique<VolumetricFogPass>();
		m_pVolumetricFogPass->Initialize(PathFinder::Relative("VolumetricFog\\blueNoise.dds").string());
		m_pVolumetricFogPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().volumetricFog);
	});

	m_threadPool->Enqueue([this]()
	{
		m_pUIPass = std::make_unique<UIPass>();
		m_pUIPass->Initialize(m_toneMappedColourTexture.get());
	});

	//AAPass
	m_threadPool->Enqueue([this]()
	{
		m_pAAPass = std::make_unique<AAPass>();
		m_pAAPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().aa);
	});

	m_threadPool->Enqueue([this]()
	{
		m_pPostProcessingPass = std::make_unique<PostProcessingPass>();
		m_pPostProcessingPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().bloom);
	});

	//lightmapPass
	m_threadPool->Enqueue([this]()
	{
		m_pLightMapPass = std::make_unique<LightMapPass>();
	});


	//SSGIPass
	m_threadPool->Enqueue([this, ao]()
	{
		m_pSSGIPass = std::make_unique<SSGIPass>();
		m_pSSGIPass->Initialize(m_diffuseTexture.get(), m_normalTexture.get(), m_lightingTexture.get(), m_metalRoughTexture.get(), ao.get());
		m_pSSGIPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().ssgi);
	});

	//BitmaskPass
	m_threadPool->Enqueue([this]()
	{
		m_pBitMaskPass = std::make_unique<BitMaskPass>();
		m_pBitMaskPass->Initialize(m_bitmaskTexture.get());
		m_pBitMaskPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().bitMask);
	});

	//DecalPass
	m_threadPool->Enqueue([this]()
	{
		m_pDecalPass = std::make_unique<DecalPass>();
		m_pDecalPass->Initialize(m_diffuseTexture.get(), m_normalTexture.get(), m_metalRoughTexture.get());
	});

	m_threadPool->NotifyAllAndWait();

	SceneManagers->sceneLoadedEvent.AddLambda([&]() 
	{
		auto scene = SceneManagers->GetActiveScene();
		auto sceneName = scene->GetSceneName();
		
		int i = 0;
		while (true) {
			file::path fileName = sceneName.ToString();
			fileName += std::to_wstring(i) + L".hdr";
			i++;

			Texture* texture = Texture::LoadFormPath(fileName);
			if (texture == nullptr) break;
			scene->m_lightmapTextures.push_back(texture);
		}
			
		i = 0;
		while (true) {
			file::path fileName = L"Dir_";
			fileName += sceneName.ToString();
			fileName += std::to_wstring(i) + L".hdr";
			i++;

			Texture* texture = Texture::LoadFormPath(fileName);
			if (texture == nullptr) break;
			scene->m_directionalmapTextures.push_back(texture);
		}

		m_pLightMapPass->Initialize(scene->m_lightmapTextures, scene->m_directionalmapTextures);
	}
	);

	m_pTerrainGizmoPass = std::make_unique<TerrainGizmoPass>();

	m_renderScene->Initialize();
	m_renderScene->SetBuffers(m_ModelBuffer.Get());

	EffectManagers->Initialize();
	m_EffectEditor = std::make_unique<EffectEditor>();

	//m_pEffectPass->MakeEffects(Effect::Sparkle, "asd", float3(0, 0, 0));
    m_newSceneCreatedEventHandle	= newSceneCreatedEvent.AddRaw(this, &SceneRenderer::NewCreateSceneInitialize);
	m_trimEventHandle				= resourceTrimEvent.AddRaw(this, &SceneRenderer::ResourceTrim);
	m_activeSceneChangedEventHandle = activeSceneChangedEvent.AddLambda([&] 
	{
		m_renderScene->Update(0.f);
	});
}

SceneRenderer::~SceneRenderer()
{
	// 자기 자신일 때만 지운다. 다른 인스턴스가 이미 자리를 가져갔다면
	// 그것을 지워서는 안 된다.
	if (this == s_active) s_active = nullptr;

	m_deviceResources.reset();
}

void SceneRenderer::CaptureDrawSnapshot(RenderPassData* data)
{
	// 큐가 지워지기 직전에만 불린다. 요청이 없으면 아무 일도 하지 않는다.
	if (!m_captureRequested.load(std::memory_order_acquire)) return;
	if (nullptr == data) return;

	// 다른 큐도 함께 센다.
	//
	// DX11 GBufferPass는 deferredQueue만 그리지 않는다 — TerrainRenderCommandList가
	// 같은 렌더 타깃에 지형을 그리고, foliage도 있다. 대조에서 'DX11만 그린
	// 픽셀'이 화면 전체 폭의 가로 띠로 나왔는데, 그것이 이 큐들 때문인지
	// 확인할 방법이 없었다.
	Debug->LogWarning("[캡처] deferred " + std::to_string(data->m_deferredQueue.size())
		+ " · terrain " + std::to_string(data->m_terrainQueue.size())
		+ " · foliage " + std::to_string(data->m_foliageQueue.size())
		+ " · forward " + std::to_string(data->m_forwardQueue.size()));

	m_captureDraws.clear();
	m_captureDraws.reserve(data->m_deferredQueue.size());

	for (auto* proxy : data->m_deferredQueue)
	{
		if (nullptr == proxy || nullptr == proxy->m_Mesh) continue;

		GBufferCaptureDraw draw{};
		draw.mesh = proxy->m_Mesh.get();
		draw.worldMatrix = proxy->m_worldMatrix;

		if (auto* material = proxy->m_Material.get())
		{
			draw.baseColor = material->m_pBaseColor;
			draw.normalMap = material->m_pNormal;
			draw.occRoughMetal = material->m_pOccRoughMetal;
			draw.emissive = material->m_pEmissive;
		}

		m_captureDraws.push_back(draw);
	}
}

void SceneRenderer::ProcessGBufferCapture()
{
	// 렌더 스레드에서만 불린다. 요청이 없으면 아무 일도 하지 않는다 —
	// 상시 비용이 붙으면 진단 장치가 측정 대상을 바꾼다.
	if (!m_captureRequested.load(std::memory_order_acquire)) return;
	if (nullptr == m_bitmaskTexture || nullptr == m_bitmaskTexture->m_pTexture) return;

	auto* device = DirectX11::DeviceStates->g_pDevice;
	auto* context = DirectX11::DeviceStates->g_pDeviceContext;
	if (nullptr == device || nullptr == context) return;

	D3D11_TEXTURE2D_DESC desc{};
	m_bitmaskTexture->m_pTexture->GetDesc(&desc);

	D3D11_TEXTURE2D_DESC stagingDesc = desc;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;
	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;

	ComPtr<ID3D11Texture2D> staging;
	if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)))
	{
		// 요청은 내린다. 실패한 채로 남겨 두면 호출부가 영원히 기다린다.
		m_captureRequested.store(false, std::memory_order_release);
		return;
	}

	context->CopyResource(staging.Get(), m_bitmaskTexture->m_pTexture);

	D3D11_MAPPED_SUBRESOURCE mapped{};
	if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
	{
		m_captureRequested.store(false, std::memory_order_release);
		return;
	}

	{
		std::lock_guard<std::mutex> guard(m_captureMutex);
		m_captureWidth = desc.Width;
		m_captureHeight = desc.Height;
		m_captureRowPitch = mapped.RowPitch;
		m_captureFormat = desc.Format;
		m_captureBytes.assign(static_cast<size_t>(mapped.RowPitch) * desc.Height, 0);
		memcpy(m_captureBytes.data(), mapped.pData, m_captureBytes.size());
	}

	context->Unmap(staging.Get(), 0);

	m_captureRequested.store(false, std::memory_order_release);
	m_captureReady.store(true, std::memory_order_release);
}

bool SceneRenderer::ConsumeGBufferCapture(std::vector<uint8_t>& outBytes, uint32_t& outWidth,
	uint32_t& outHeight, uint32_t& outRowPitch, DXGI_FORMAT& outFormat)
{
	if (!m_captureReady.load(std::memory_order_acquire)) return false;

	std::lock_guard<std::mutex> guard(m_captureMutex);
	outBytes = std::move(m_captureBytes);
	outWidth = m_captureWidth;
	outHeight = m_captureHeight;
	outRowPitch = m_captureRowPitch;
	outFormat = m_captureFormat;

	m_captureBytes.clear();
	m_captureReady.store(false, std::memory_order_release);
	return true;
}

void SceneRenderer::Finalize()
{
	// 등록의 역순 — 패스들이 아직 살아 있는 동안에는 RHI가 유효해야 한다.
	RHI::Shutdown();

	m_diffuseTexture.reset();
	m_metalRoughTexture.reset();
	m_normalTexture.reset();
	m_emissiveTexture.reset();
	m_toneMappedColourTexture.reset();
	m_bitmaskTexture.reset();
	m_ambientOcclusionTexture.reset();
	m_lightingTexture.reset();

	m_pSSAOPass.reset();
	m_pGBufferPass.reset();
	m_pDeferredPass.reset();
	m_pForwardPass.reset();
	m_pSkyBoxPass.reset();
	m_pToneMapPass.reset();
	m_pSpritePass.reset();
	m_pBlitPass.reset();
	m_pAAPass.reset();
	m_pPostProcessingPass.reset();
	m_pPositionMapPass.reset();
	m_pLightMapPass.reset();
	m_pScreenSpaceReflectionPass.reset();
	m_pSubsurfaceScatteringPass.reset();
	m_pVignettePass.reset();
	m_pColorGradingPass.reset();
	m_pVolumetricFogPass.reset();
	m_pUIPass.reset();
	m_pSSGIPass.reset();
	m_pBitMaskPass.reset();
	m_pTerrainGizmoPass.reset();
	m_EffectEditor.reset();
	m_pDecalPass.reset();

	m_commandThreadPool.reset();

	delete m_threadPool;

	OnResizeEvent -= m_resizeEventHandle;

	DirectX11::DeviceStates->g_pDevice				= nullptr;
	DirectX11::DeviceStates->g_pDeviceContext		= nullptr;
	DirectX11::DeviceStates->g_pDepthStencilView	= nullptr;
	DirectX11::DeviceStates->g_pDepthStencilState	= nullptr;
	DirectX11::DeviceStates->g_pRasterizerState		= nullptr;
	DirectX11::DeviceStates->g_pBlendState			= nullptr;
	DirectX11::DeviceStates->g_backBufferRTV		= nullptr;
	DirectX11::DeviceStates->g_depthStancilSRV		= nullptr;
	DirectX11::DeviceStates->g_annotation			= nullptr;

#ifndef BUILD_FLAG
	CameraManagement->DeleteCamera(m_pEditorCamera->m_cameraIndex);
	m_pEditorCamera.reset();
#endif
	CameraManagement->Finalize();
	m_renderScene.reset();
	m_deviceResources.reset();
}

void SceneRenderer::InitializeDeviceState()
{
    // RHI 등록(PHASE 3-1). DeviceStates가 채워지는 곳이 곧 백엔드가 확정되는
    // 곳이므로 여기서 한다. 교체 스위치(3-9)가 생기면 여기가 분기점이 된다.
    if (!RHI::IsInitialized())
    {
        RHI::Initialize(std::make_unique<DX11RHIDevice>());
    }

    DirectX11::DeviceStates->g_pDevice				= m_deviceResources->GetD3DDevice();
    DirectX11::DeviceStates->g_pDeviceContext		= m_deviceResources->GetD3DDeviceContext();
    DirectX11::DeviceStates->g_pDepthStencilView	= m_deviceResources->GetDepthStencilView();
    DirectX11::DeviceStates->g_pDepthStencilState	= m_deviceResources->GetDepthStencilState();
    DirectX11::DeviceStates->g_pRasterizerState		= m_deviceResources->GetRasterizerState();
    DirectX11::DeviceStates->g_pBlendState			= m_deviceResources->GetBlendState();
    DirectX11::DeviceStates->g_Viewport				= m_deviceResources->GetScreenViewport();
    DirectX11::DeviceStates->g_fullsizeViewport		= m_deviceResources->GetScreenViewport();
    DirectX11::DeviceStates->g_backBufferRTV		= m_deviceResources->GetBackBufferRenderTargetView();
    DirectX11::DeviceStates->g_depthStancilSRV		= m_deviceResources->GetDepthStencilViewSRV();
    // 뷰포트(GetScreenViewport)가 렌더 타깃 크기 = 출력 크기에서 만들어지므로
    // g_ClientRect도 같은 출처를 써야 한다. 예전에는 초기화가 GetOutputSize를,
    // 리사이즈가 GetLogicalSize를 써서 DPI 배율이 100%가 아니면 두 값이 갈렸다 —
    // 첫 프레임에 만들어진 카메라만 다른 해상도의 렌더 타깃을 갖는 상태였다.
    DirectX11::DeviceStates->g_ClientRect			= m_deviceResources->GetOutputSize();
    DirectX11::DeviceStates->g_aspectRatio			= m_deviceResources->GetAspectRatio();
	DirectX11::DeviceStates->g_annotation			= m_deviceResources->GetAnnotation();

	// 화면 크기를 버스에 심는다. 화면 크기 텍스처는 여기서 크기를 읽으므로
	// 어떤 패스가 만들어지기 전에 설정돼 있어야 한다.
	ScreenResizeBus::Get().SetSize(
		static_cast<uint32_t>(DirectX11::DeviceStates->g_ClientRect.width),
		static_cast<uint32_t>(DirectX11::DeviceStates->g_ClientRect.height));

	m_resizeEventHandle = OnResizeEvent.AddLambda([&](uint32_t width, uint32_t height)
	{
		DirectX11::DeviceStates->g_pDevice				= m_deviceResources->GetD3DDevice();
		DirectX11::DeviceStates->g_pDeviceContext		= m_deviceResources->GetD3DDeviceContext();
		DirectX11::DeviceStates->g_pDepthStencilView	= m_deviceResources->GetDepthStencilView();
		DirectX11::DeviceStates->g_pDepthStencilState	= m_deviceResources->GetDepthStencilState();
		DirectX11::DeviceStates->g_pRasterizerState		= m_deviceResources->GetRasterizerState();
		DirectX11::DeviceStates->g_pBlendState			= m_deviceResources->GetBlendState();
		// g_Viewport는 초기화 때만 기록돼 있어서 창 크기를 바꿔도 저작 해상도에 묶여 있었다.
		// 모든 렌더 패스가 RSSetViewports에 이 값을 그대로 넘기고, Camera::GetScreenSize도
		// 여기서 화면 크기를 읽는다. 갱신을 빠뜨리면 리사이즈 후에도 파이프라인과 스크립트가
		// 옛 해상도를 본다(PHASE 7).
		DirectX11::DeviceStates->g_Viewport				= m_deviceResources->GetScreenViewport();
		DirectX11::DeviceStates->g_fullsizeViewport		= m_deviceResources->GetScreenViewport();
		DirectX11::DeviceStates->g_backBufferRTV		= m_deviceResources->GetBackBufferRenderTargetView();
		DirectX11::DeviceStates->g_depthStancilSRV		= m_deviceResources->GetDepthStencilViewSRV();
		// 초기화와 같은 출처를 쓴다(위 주석 참조).
		DirectX11::DeviceStates->g_ClientRect			= m_deviceResources->GetOutputSize();
		DirectX11::DeviceStates->g_aspectRatio			= m_deviceResources->GetAspectRatio();
		DirectX11::DeviceStates->g_annotation			= m_deviceResources->GetAnnotation();

		m_pSSAOPass->ReloadDSV(m_deviceResources->GetDepthStencilViewSRV());

		m_pBlitPass->Initialize(m_deviceResources->GetBackBufferRenderTargetView());
	});
}

void SceneRenderer::InitializeShadowMapDesc()
{
	ShadowMapRenderDesc& desc = RenderScene::g_shadowMapDesc;
	desc.m_lookAt = XMVectorSet(0, 0, 0, 1);
	desc.m_eyePosition = Mathf::Vector4{ -1, -1, 1, 0 } *-50.f;
	desc.m_viewWidth = 100;
	desc.m_viewHeight = 100;
	desc.m_nearPlane = 0.1f;
	desc.m_farPlane = 1000.0f;
	desc.m_textureWidth = 2048;
	desc.m_textureHeight = 2048;
}

void SceneRenderer::InitializeTextures()
{
	m_threadPool->Enqueue([this]()
	{
		auto diffuseTexture = TextureHelper::CreateSharedScreenRenderTexture(
			"DiffuseRTV",
			DXGI_FORMAT_R16G16B16A16_FLOAT
		);
		m_diffuseTexture.swap(diffuseTexture);
	});

	m_threadPool->Enqueue([this]()
	{
		auto metalRoughTexture = TextureHelper::CreateSharedScreenRenderTexture(
			"MetalRoughRTV",
			DXGI_FORMAT_R16G16B16A16_FLOAT
		);
		m_metalRoughTexture.swap(metalRoughTexture);
	});

	m_threadPool->Enqueue([this]()
	{
		auto normalTexture = TextureHelper::CreateSharedScreenRenderTexture(
			"NormalRTV",
			DXGI_FORMAT_R16G16B16A16_FLOAT
		);
		m_normalTexture.swap(normalTexture);
	});

	m_threadPool->Enqueue([this]()
	{
		auto emissiveTexture = TextureHelper::CreateSharedScreenRenderTexture(
			"EmissiveRTV",
			DXGI_FORMAT_R16G16B16A16_FLOAT
		);
		m_emissiveTexture.swap(emissiveTexture);
	});

	m_threadPool->Enqueue([this]()
	{
		auto bitmaskTexture = TextureHelper::CreateSharedScreenRenderTexture(
			"BitmaskRTV",
			DXGI_FORMAT_R32_UINT
		);
		m_bitmaskTexture.swap(bitmaskTexture);
	});

	m_threadPool->Enqueue([this]()
	{
		auto toneMappedColourTexture = TextureHelper::CreateSharedScreenRenderTexture(
			"ToneMappedColourRTV",
			DXGI_FORMAT_R16G16B16A16_FLOAT
		);
		m_toneMappedColourTexture.swap(toneMappedColourTexture);
	});

	m_threadPool->Enqueue([this]()
	{
		auto lightingTexture = TextureHelper::CreateSharedScreenRenderTexture(
			"LightingRTV",
			DXGI_FORMAT_R16G16B16A16_FLOAT
		);
		m_lightingTexture.swap(lightingTexture);
	});

	m_threadPool->NotifyAllAndWait();
}

void SceneRenderer::NewCreateSceneInitialize()
{
	auto scene = SceneManagers->GetActiveScene();
	m_renderScene->SetScene(scene);

	auto cameraObj = scene->CreateGameObject("Main Camera", GameObjectType::Camera);
	auto cameraComponent = cameraObj->AddComponent<CameraComponent>();

	auto lightObj1 = scene->CreateGameObject("Directional Light", GameObjectType::Light);
	lightObj1->SetTag("MainCamera");
	auto lightComponent1 = lightObj1->AddComponent<LightComponent>();
	lightComponent1->m_lightStatus = LightStatus::StaticShadows;

	ShadowMapRenderDesc& desc = RenderScene::g_shadowMapDesc;
	m_renderScene->m_LightController->Initialize();
	try
	{
		m_renderScene->m_LightController->SetLightWithShadows(0, desc);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error initializing light with shadows: " << e.what() << std::endl;
	}
	//TODO : skybox 텍스쳐 저장해야한다 씬 별로
	m_renderScene->m_LightController->UseCloudShadowMap(PathFinder::Relative("Cloud\\Cloud.png").string());

	m_pSkyBoxPass->GenerateCubeMap(*m_renderScene);
	auto envMap = m_pSkyBoxPass->GenerateEnvironmentMap(*m_renderScene);
	auto preFilter = m_pSkyBoxPass->GeneratePrefilteredMap(*m_renderScene);
	auto brdfLUT = m_pSkyBoxPass->GenerateBRDFLUT(*m_renderScene);

	m_pDeferredPass->UseEnvironmentMap(envMap, preFilter, brdfLUT);
	m_pForwardPass->UseEnvironmentMap(envMap, preFilter, brdfLUT);
	lightMap.envMap = envMap.get();
}

void SceneRenderer::OnWillRenderObject(float deltaTime)
{
}

void SceneRenderer::EndOfFrame(float deltaTime)
{
	PROFILE_CPU_BEGIN("EraseRenderPassData");
	m_renderScene->EraseRenderPassData();
	PROFILE_CPU_END();
	PROFILE_CPU_BEGIN("EoFUpdate");
	m_renderScene->Update(deltaTime);
	PROFILE_CPU_END();
	PROFILE_CPU_BEGIN("OnProxyDestroy");
	m_renderScene->OnProxyDestroy();
	PROFILE_CPU_END();
	PROFILE_CPU_BEGIN("PrepareRender");
	PrepareRender();
	PROFILE_CPU_END();
}

void SceneRenderer::SceneRendering()
{
#ifndef BUILD_FLAG
	if (ShaderSystem->IsReloading())
	{
		ReloadShaders();
	}
#endif // !BUILD_FLAG
	DirectX11::ResetCallCount();

	bool isVolumeProfileApplied = SceneManagers->IsVolumeProfileApply();
	if(isVolumeProfileApplied)
	{
		auto& Settings					= EngineSettingInstance->GetRenderPassSettings();
		std::string_view prevSkyboxName = m_currentSkyTextureName;
		std::string_view currSkyboxName = Settings.skyboxTextureName;
		if(!currSkyboxName.empty() && prevSkyboxName != currSkyboxName)
		{
			std::string fullPath = currSkyboxName.data();
			ApplyNewCubeMap(fullPath);
			m_currentSkyTextureName = currSkyboxName;
		}
	}

	float deltaTime = Time->GetElapsedSeconds();
	m_EffectEditor->Update(deltaTime);
	EffectManagers->Update(deltaTime);

	for (auto& camera : CameraManagement->GetCameras())
	{
		if (!RenderPassData::VaildCheck(camera.get())) continue;
		auto renderData = RenderPassData::GetData(camera.get());

#ifndef BUILD_FLAG
		if (camera.get() != m_pEditorCamera.get())
		{
			if (EngineSettingInstance->IsGameView())
			{
				camera->m_avoidRenderPass.Clear((flag)RenderPipelinePass::BlitPass);
			}
			else
			{
				camera->m_avoidRenderPass.Set((flag)RenderPipelinePass::BlitPass);
			}
		}
#else
		camera->m_avoidRenderPass.Clear((flag)RenderPipelinePass::BlitPass);
#endif // !BUILD_FLAG

		std::wstring w_name =  L"Camera" + std::to_wstring(camera->m_cameraIndex);
		std::string name = "Camera" + std::to_string(camera->m_cameraIndex);
		PROFILE_CPU_BEGIN(name.c_str());
		DirectX11::BeginEvent(w_name);
		//[1] ShadowMapPass
		{
			PROFILE_CPU_BEGIN("ShadowMapPass");
			DirectX11::BeginEvent(L"ShadowMapPass");
			Benchmark banch;
			//TODO : 여기 한번 정리 해보자
			renderData->ClearRenderTarget();
			m_renderScene->ShadowStage(*camera);
			Clear(DirectX::Colors::Transparent, 1.0f, 0);
			UnbindRenderTargets();
			RenderStatistics->UpdateRenderState("ShadowMapPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//[2] GBufferPass
		{
			PROFILE_CPU_BEGIN("GBufferPass");
			DirectX11::BeginEvent(L"GBufferPass");
			Benchmark banch;
			m_pGBufferPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("GBufferPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		if (useTestLightmap)
		{
			//PROFILE_CPU_BEGIN("LightMapPass");
			DirectX11::BeginEvent(L"LightMapPass");
			Benchmark banch;
			m_pLightMapPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("LightMapPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			//PROFILE_CPU_END();
		}

		// DecalPass
		{
			DirectX11::BeginEvent(L"DecalPass");
			Benchmark banch;
			m_pDecalPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("DecalPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//[3] SSAOPass
		{
			DirectX11::BeginEvent(L"SSAOPass");
			Benchmark banch;
			m_pSSAOPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SSAOPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		if (!useTestLightmap)
        {
			//[4] DeferredPass
			{
				PROFILE_CPU_BEGIN("DeferredPass");
				DirectX11::BeginEvent(L"DeferredPass");
				Benchmark banch;
				m_pDeferredPass->Execute(*m_renderScene, *camera);
				RenderStatistics->UpdateRenderState("DeferredPass", banch.GetElapsedTime());
				DirectX11::EndEvent();
				PROFILE_CPU_END();
			}
		}

		{
			//PROFILE_CPU_BEGIN("SSGIPass");
			DirectX11::BeginEvent(L"SSGIPass");
			Benchmark banch;
			m_pSSGIPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SSGIPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			//PROFILE_CPU_END();
		}

		if(camera.get() == m_pEditorCamera.get())
		{
			PROFILE_CPU_BEGIN("TerrainGizmoPass");
			DirectX11::BeginEvent(L"TerrainGizmoPass");
			Benchmark banch;
			m_pTerrainGizmoPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("TerrainGizmoPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//[5] skyBoxPass
		{
			PROFILE_CPU_BEGIN("SkyBoxPass");
			DirectX11::BeginEvent(L"SkyBoxPass");
			Benchmark banch;
			m_pSkyBoxPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SkyBoxPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		{
			PROFILE_CPU_BEGIN("ForwardPass");
			DirectX11::BeginEvent(L"ForwardPass");
			Benchmark banch;
			m_pForwardPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ForwardPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		// EffectPass
		{
			PROFILE_CPU_BEGIN("EffectPass");
			DirectX11::BeginEvent(L"EffectPass");
			Benchmark banch;
			EffectManagers->Execute(*m_renderScene, *camera);
			m_EffectEditor->Render(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("EffectPass", banch.GetElapsedTime());
			DirectX11::DeviceStates->g_pDeviceContext->Flush();
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//SSS
		{
			PROFILE_CPU_BEGIN("SubsurfaceScatteringPass");
			DirectX11::BeginEvent(L"SubsurfaceScatteringPass");
			Benchmark banch;
			m_pSubsurfaceScatteringPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SubsurfaceScatteringPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//SSR
		PROFILE_CPU_BEGIN("ScreenSpaceReflectionPass");
		DirectX11::BeginEvent(L"ScreenSpaceReflectionPass");
		{
			Benchmark banch;
			m_pScreenSpaceReflectionPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ScreenSpaceReflectionPass", banch.GetElapsedTime());
		}
		DirectX11::EndEvent();
		PROFILE_CPU_END();
		
		if (m_pEditorCamera.get() != camera.get())
		{
			//VolumetricFog or VolumetricLight
			PROFILE_CPU_BEGIN("VolumetricFogPass");
			DirectX11::BeginEvent(L"VolumetricFogPass");
			{
				Benchmark VolumetricFogBanch;
				m_pVolumetricFogPass->Execute(*m_renderScene, *camera);
				RenderStatistics->UpdateRenderState("VolumetricFogPass", VolumetricFogBanch.GetElapsedTime());
			}
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

        //[*] PostProcessPass
        {
			PROFILE_CPU_BEGIN("BloomPass");
			DirectX11::BeginEvent(L"BloomPass");
			Benchmark banch;
            m_pPostProcessingPass->Execute(*m_renderScene, *camera);
            RenderStatistics->UpdateRenderState("PostProcessPass", banch.GetElapsedTime());
            DirectX11::EndEvent();
			PROFILE_CPU_END();
        }

		{
			DirectX11::BeginEvent(L"BitMaskPass");
			Benchmark banch;
			m_pBitMaskPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("BitMaskPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//[6] AAPass
		{
			PROFILE_CPU_BEGIN("AAPass");
			DirectX11::BeginEvent(L"AAPass");
			Benchmark banch;
			m_pAAPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("AAPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//[7] ToneMapPass
		{
			PROFILE_CPU_BEGIN("ToneMapPass");
			DirectX11::BeginEvent(L"ToneMapPass");
			Benchmark banch;
			m_pToneMapPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ToneMapPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//Vignette
		if (m_pEditorCamera.get() != camera.get())
		{
			PROFILE_CPU_BEGIN("VignettePass");
			DirectX11::BeginEvent(L"VignettePass");
			Benchmark banch;
			m_pVignettePass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("VignettePass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//m_pColorGradingPass
		{
			PROFILE_CPU_BEGIN("ColorGradingPass");
			DirectX11::BeginEvent(L"ColorGradingPass");
			Benchmark banch;
			m_pColorGradingPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ColorGradingPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//[7] SpritePass
		{
			PROFILE_CPU_BEGIN("SpritePass");
			DirectX11::BeginEvent(L"SpritePass");
			Benchmark banch;
			m_pSpritePass->SetGizmoRendering(false);
			m_pSpritePass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SpritePass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//[]  UIPass
		{
			PROFILE_CPU_BEGIN("UIPass");
			DirectX11::BeginEvent(L"UIPass");
			Benchmark banch;
			m_pUIPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("UIPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		//[8] BlitPass
		{
			PROFILE_CPU_BEGIN("BlitPass");
			DirectX11::BeginEvent(L"BlitPass");
			Benchmark banch;
			m_pBlitPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("BlitPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
			PROFILE_CPU_END();
		}

		DirectX11::EndEvent();
		PROFILE_CPU_END();

	}

	// 대조 검증이 요청했으면 여기서 GBuffer를 CPU로 내린다.
	// 프레임을 다 그린 뒤이므로 읽는 것이 완성된 결과이고,
	// 이 스레드가 컨텍스트의 유일한 사용자다.
	ProcessGBufferCapture();
}

void SceneRenderer::CreateCommandListPass()
{
#ifndef BUILD_FLAG
	RenderDebugManager::GetInstance()->AddFrame();
#endif // !BUILD_FLAG

	auto renderScene = m_renderScene;

	ID3D11RenderTargetView* views[]{
		m_diffuseTexture->GetRTV(),
		m_metalRoughTexture->GetRTV(),
		m_normalTexture->GetRTV(),
		m_emissiveTexture->GetRTV(),
		m_bitmaskTexture->GetRTV()
	};
	m_pGBufferPass->SetRenderTargetViews(views, ARRAYSIZE(views));
	PROFILE_CPU_BEGIN("ProxyCommandExecute");
	ProxyCommandQueue->Execute();
	PROFILE_CPU_END();

	if (SceneManagers->IsVolumeProfileApply())
	{
		ApplyVolumeProfile();
		SceneManagers->ResetVolumeProfileApply();
	}

	for (auto& camera : CameraManagement->GetCameras())
	{
		if (!RenderPassData::VaildCheck(camera.get())) return;
		auto data = RenderPassData::GetData(camera.get());

		// 이 프레임이 쓸 카메라 스냅샷을 여기서 한 번 고정한다 (PHASE 3-2).
		//
		// 이후 모든 패스는 같은 면을 본다. 매 읽기마다 게시 인덱스를 다시 보면
		// 게임 스레드가 그 사이에 게시했을 때 패스마다 다른 카메라를 보게 된다.
		// 커맨드 빌드 스레드가 프레임의 유일한 래치 지점이다.
		data->LatchFrameSnapshot();

		PROFILE_CPU_BEGIN("PrepareCommandBuilding");
		for (auto& instanceID : data->GetShadowRenderDataBuffer())
		{
			auto proxy = renderScene->FindProxy(instanceID);
			if (nullptr != proxy)
			{
				data->PushShadowRenderQueue(proxy);
			}
		}

		for (auto& instanceID : data->GetCullDataBuffer())
		{
			auto proxy = renderScene->FindProxy(instanceID);
			if(nullptr != proxy)
			{
				data->PushRenderQueue(proxy);
			}
		}

		for (auto& instanceID : data->GetUIRenderDataBuffer())
		{
			auto proxy = renderScene->FindUIProxy(instanceID);
			if (nullptr != proxy && proxy->IsEnabled())
			{
				data->PushUIRenderQueue(proxy);
			}
		}

		data->SortRenderQueue();
		data->SortUIRenderQueue();
		data->SortShadowRenderQueue();
		data->ClearCullDataBuffer();
		data->ClearUIRenderDataBuffer();
		data->ClearShadowRenderDataBuffer();
		PROFILE_CPU_END();

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("ShadowPassCommandList");
			m_renderScene->CreateShadowCommandList(deferredContext , *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			{
				PROFILE_CPU_BEGIN("TerrainPassCommandList");
				m_pGBufferPass->TerrainRenderCommandList(rhiContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}

			{
				PROFILE_CPU_BEGIN("GBufferPassCommandList");
				m_pGBufferPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}

			if (useTestLightmap)
			{
				PROFILE_CPU_BEGIN("LightMapPassCommandList");
				m_pLightMapPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}
			else
			{
				PROFILE_CPU_BEGIN("DeferredPassCommandList");
				m_pDeferredPass->UseAmbientOcclusion(m_ambientOcclusionTexture);
				m_pDeferredPass->UseLightAndEmissiveRTV(m_lightingTexture);
				m_pDeferredPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("DecalPassCommandList");
			m_pDecalPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("SSAOPassCommandList");
			m_pSSAOPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("SSGIPassCommandList");
			m_pSSGIPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("ForwardPassCommandList");
			m_pForwardPass->CreateFoliageCommandList(rhiContext, *m_renderScene, *camera);
			m_pForwardPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("SubsurfaceScatteringPassCommandList");
			m_pSubsurfaceScatteringPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("SkyBoxPassCommandList");
			m_pSkyBoxPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("BitMaskPassCommandList");
			m_pBitMaskPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});
		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("ScreenSpaceReflectionPassCommandList");
			m_pScreenSpaceReflectionPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		if (m_pEditorCamera.get() != camera.get())
		{
			m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
			{
				DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
				PROFILE_CPU_BEGIN("VolumetricFogPassCommandList");
				m_pVolumetricFogPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			});
		}
		else
		{
			m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
			{
				DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
				PROFILE_CPU_BEGIN("TerrainGizmoPassCommandList");
				m_pTerrainGizmoPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			});
		}

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("SpritePassCommnadList");
			//m_pUIPass->SortUIObjects();
			m_pSpritePass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			DX11CommandContext rhiContext(deferredContext); // 백엔드 소유자인 SceneRenderer가 직접 만든다(PHASE 3-1, 7차)
			PROFILE_CPU_BEGIN("UIPassCommnadList");
			//m_pUIPass->SortUIObjects();
			m_pUIPass->CreateRenderCommandList(rhiContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->NotifyAllAndWait();

		// 큐가 살아 있는 마지막 지점이다. 대조 검증이 요청했으면 여기서 뜬다.
		CaptureDrawSnapshot(data);

		data->ClearRenderQueue();
		data->ClearShadowRenderQueue();
	}

	//m_pUIPass->ClearFrameQueue();

#ifndef BUILD_FLAG
	RenderDebugManager::GetInstance()->EndFrame();
#endif // !BUILD_FLAG

}

void SceneRenderer::ReApplyCurrCubeMap()
{
	ApplyNewCubeMap(m_pSkyBoxPass->CurrentSkyBoxTextureName().string());
}

void SceneRenderer::ApplyVolumeProfile()
{
	if (m_renderScene && m_renderScene->m_LightController)
	{
		m_renderScene->m_LightController->m_shadowMapPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().shadow);
	}
	if (m_pDeferredPass)
	{
		m_pDeferredPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().deferred);
	}
	if (m_pSSAOPass)
	{
		m_pSSAOPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().ssao);
	}
	if (m_pAAPass)
	{
		m_pAAPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().aa);
	}
	if (m_pPostProcessingPass)
	{
		m_pPostProcessingPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().bloom);
	}
	if (m_pVignettePass)
	{
		m_pVignettePass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().vignette);
	}
	if (m_pColorGradingPass)
	{
		m_pColorGradingPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().colorGrading);
	}
	if (m_pSSGIPass)
	{
		m_pSSGIPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().ssgi);
	}
	if (m_pToneMapPass)
	{
		m_pToneMapPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().toneMap);
	}
	if (m_pSkyBoxPass)
	{
		m_pSkyBoxPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().m_isSkyboxEnabled);
	}
	if (m_pGBufferPass)
	{
		m_pGBufferPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings());
	}
	if(m_pForwardPass)
	{
		m_pForwardPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings());
	}
	if (m_pVolumetricFogPass)
	{
		m_pVolumetricFogPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().volumetricFog);
	}
}

void SceneRenderer::PrepareRender()
{
	auto GameSceneStart = SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
	auto GameSceneEnd = !SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;

	/*auto renderScene = m_renderScene;
	auto m_currentScene = SceneManagers->GetActiveScene();
	PROFILE_CPU_BEGIN("CopySceneData");
	std::vector<MeshRenderer*> allMeshes = m_currentScene->GetMeshRenderers();
	std::vector<TerrainComponent*> terrainComponents = m_currentScene->GetTerrainComponent();
	std::vector<FoliageComponent*> foliageComponents = m_currentScene->GetFoliageComponents();
	std::vector<SpriteRenderer*> spriteRenderers = m_currentScene->GetSpriteRenderers();
	std::vector<ImageComponent*> imageComponents = UIManagers->Images;
	std::vector<TextComponent*> textComponents = UIManagers->Texts;
	std::vector<SpriteSheetComponent*> spriteComponents = UIManagers->SpriteSheets;
	std::vector<DecalComponent*> decalComponents = m_currentScene->GetDecalComponents();
	PROFILE_CPU_END();

	PROFILE_CPU_BEGIN("UpdateCommand");
	if (!textComponents.empty())
	{
		m_threadPool->Enqueue([&, renderScene, texts = std::move(textComponents)]
		{
			for (auto& text : texts)
			{
				auto owner = text->GetOwner();
				if (nullptr == owner) continue;
				auto scene = owner->GetScene();

				if (scene && scene == m_currentScene)
				{
					try
					{
						renderScene->UpdateCommand(text);
					}
					catch (const std::exception& e)
					{
						std::cerr << "Error updating text command: " << e.what() << std::endl;
					}
				}
			}
		});
	}

	if (!imageComponents.empty())
	{
		m_threadPool->Enqueue([&, renderScene, images = std::move(imageComponents)]
		{
			for (auto& image : images)
			{
				auto owner = image->GetOwner();
				if (nullptr == owner) continue;
				auto scene = owner->GetScene();
				if (scene && scene == m_currentScene)
				{
					try
					{
						renderScene->UpdateCommand(image);
					}
					catch (const std::exception& e)
					{
						std::cerr << "Error updating image command: " << e.what() << std::endl;
					}
				}
			}
		});
	}

	if (!spriteComponents.empty())
	{
		m_threadPool->Enqueue([&, renderScene, sprites = std::move(spriteComponents)]
		{
			for (auto& sprite : sprites)
			{
				auto owner = sprite->GetOwner();
				if (nullptr == owner) continue;
				auto scene = owner->GetScene();
				if (scene && scene == m_currentScene)
				{
					try
					{
						renderScene->UpdateCommand(sprite);
					}
					catch (const std::exception& e)
					{
						std::cerr << "Error updating sprite command: " << e.what() << std::endl;
					}
				}

			}
		});
	}

	if (!terrainComponents.empty())
	{
		m_threadPool->Enqueue([&, renderScene, terrains = std::move(terrainComponents)]
		{
			for (auto& terrain : terrains)
			{
				try
				{
					renderScene->UpdateCommand(terrain);
				}
				catch (const std::exception& e)
				{
					std::cerr << "Error updating terrain command: " << e.what() << std::endl;
				}
			}
		});
	}

	if (!allMeshes.empty())
	{
		m_threadPool->Enqueue([&, renderScene, meshes = std::move(allMeshes)]
		{
			for (auto& mesh : meshes)
			{
				if (!mesh) continue;
				try
				{
					renderScene->UpdateCommand(mesh);
				}
				catch (const std::exception& e)
				{
					std::cerr << "Error updating mesh command: " << e.what() << '\n';
				}
			}
		});
	}

	if (!foliageComponents.empty())
	{
		m_threadPool->Enqueue([&, renderScene, foliages = std::move(foliageComponents)]
		{
			for (auto& foliage : foliages)
			{
				try
				{
					renderScene->UpdateCommand(foliage);
				}
				catch (const std::exception& e)
				{
					std::cerr << "Error updating foliage command: " << e.what() << std::endl;
				}
			}
		});
	}

	if (!decalComponents.empty())
	{
		m_threadPool->Enqueue([&, renderScene, decals = std::move(decalComponents)]
		{
			for (auto& decal : decals)
			{
				try
				{
					renderScene->UpdateCommand(decal);
				}
				catch (const std::exception& e)
				{
					std::cerr << "Error updating decal command: " << e.what() << std::endl;
				}
			}
		});
	}

	if (!spriteRenderers.empty())
	{
		m_threadPool->Enqueue([&, renderScene, sprites = std::move(spriteRenderers)]
		{
			for (auto& sprite : sprites)
			{
				auto owner = sprite->GetOwner();
				if (nullptr == owner) continue;
				auto scene = owner->GetScene();
				if (scene && scene == m_currentScene)
				{
					try
					{
						renderScene->UpdateCommand(sprite);
					}
					catch (const std::exception& e)
					{
						std::cerr << "Error updating sprite command: " << e.what() << std::endl;
					}
				}

			}
		});
	}

	m_threadPool->NotifyAllAndWait();
	PROFILE_CPU_END();*/

	EffectProxyController::GetInstance()->PrepareCommandBehavior();

	for (auto camera : CameraManagement->GetCameras())
	{
		if (nullptr == camera) continue;

		if (!RenderPassData::VaildCheck(camera.get())) return;
		auto data = RenderPassData::GetData(camera.get());

		data->UpdateData(camera.get());
		data->AddFrame();
	}

	SwapEvent();
	ProxyCommandQueue->AddFrame();
	EffectProxyController::GetInstance()->AddFrame();
}

void SceneRenderer::Clear(const float color[4], float depth, uint8_t stencil)
{
	DirectX11::ClearRenderTargetView(
		m_deviceResources->GetBackBufferRenderTargetView(),
		DirectX::Colors::Transparent
	);

	DirectX11::ClearDepthStencilView(
		m_deviceResources->GetDepthStencilView(),
		D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
		1.0f,
		0
	);
}

void SceneRenderer::SetRenderTargets(Texture& texture, bool enableDepthTest)
{
	ID3D11DepthStencilView* dsv = enableDepthTest ? m_deviceResources->GetDepthStencilView() : nullptr;
	ID3D11RenderTargetView* rtv = texture.GetRTV();

	DirectX11::OMSetRenderTargets(1, &rtv, dsv);
}

void SceneRenderer::ApplyNewCubeMap(std::string_view filename)
{
	m_currentSkyTextureName = filename;
	m_pSkyBoxPass->GenerateCubeMap(filename, *m_renderScene);
	auto envMap = m_pSkyBoxPass->GenerateEnvironmentMap(*m_renderScene);
	auto preFilter = m_pSkyBoxPass->GeneratePrefilteredMap(*m_renderScene);
	auto brdfLUT = m_pSkyBoxPass->GenerateBRDFLUT(*m_renderScene);

	m_pDeferredPass->UseEnvironmentMap(envMap, preFilter, brdfLUT);
	m_pForwardPass->UseEnvironmentMap(envMap, preFilter, brdfLUT);
}

void SceneRenderer::ApplyNewColorGrading(std::string_view filename)
{
	m_pColorGradingPass->SetColorGradingTexture(filename);
}

void SceneRenderer::UnbindRenderTargets()
{
	ID3D11RenderTargetView* nullRTV = nullptr;
	ID3D11DepthStencilView* nullDSV = nullptr;
	DirectX11::OMSetRenderTargets(1, &nullRTV, nullDSV);
}

void SceneRenderer::ReloadShaders()
{
	ShaderSystem->ReloadShaders();
}

void SceneRenderer::ResourceTrim()
{
	m_deviceResources->Trim();
}

