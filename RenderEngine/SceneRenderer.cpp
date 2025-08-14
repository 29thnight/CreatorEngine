#include "SceneRenderer.h"
#include "DeviceState.h"
#include "EngineSetting.h"
#include "ShaderSystem.h"
#include "ImGuiRegister.h"
#include "Benchmark.hpp"
#include "RenderScene.h"
#include "SceneManager.h"
#include "Scene.h"
#include "RenderableComponents.h"
#include "ImageComponent.h"
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

SceneRenderer::SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
{
    InitializeDeviceState();
    InitializeShadowMapDesc();

	m_threadPool = new ThreadPool;
	m_commandThreadPool = std::make_unique<RenderThreadPool>(DeviceState::g_pDevice);
	m_renderScene = std::make_shared<RenderScene>();
	SceneManagers->SetRenderScene(m_renderScene.get());
	
	//sampler 생성
	//m_linearSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	//m_pointSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	InitializeTextures();

	ShaderSystem->Initialize();

	auto ao = Texture::CreateShared(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
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
    m_pGBufferPass = std::make_unique<GBufferPass>();
	ID3D11RenderTargetView* views[]{
		m_diffuseTexture->GetRTV(),
		m_metalRoughTexture->GetRTV(),
		m_normalTexture->GetRTV(),
		m_emissiveTexture->GetRTV(),
		m_bitmaskTexture->GetRTV()
	};
	m_pGBufferPass->SetRenderTargetViews(views, ARRAYSIZE(views));

    //ssaoPass
    m_pSSAOPass = std::make_unique<SSAOPass>();
    m_pSSAOPass->Initialize(
        ao,
        m_deviceResources->GetDepthStencilViewSRV(),
        m_normalTexture,
		m_diffuseTexture
    );
    m_pSSAOPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().ssao);

    //deferredPass
    m_pDeferredPass = std::make_unique<DeferredPass>();
    m_pDeferredPass->Initialize(
        m_diffuseTexture,
        m_metalRoughTexture,
        m_normalTexture,
        m_emissiveTexture,
		m_bitmaskTexture
    );
    m_pDeferredPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().deferred);

	//forwardPass
	m_pForwardPass = std::make_unique<ForwardPass>();

	//skyBoxPass
	m_pSkyBoxPass = std::make_unique<SkyBoxPass>();
	m_currentSkyTextureName = PathFinder::Relative("HDR\\rustig_koppie_puresky_8k.hdr").string();
	m_pSkyBoxPass->Initialize(m_currentSkyTextureName);
	
	//toneMapPass
	m_pToneMapPass = std::make_unique<ToneMapPass>();
	m_pToneMapPass->Initialize(
		m_toneMappedColourTexture
	);

    m_pToneMapPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().toneMap);
	//spritePass
	m_pSpritePass = std::make_unique<SpritePass>();
	//m_pSpritePass->Initialize(m_toneMappedColourTexture.get());

	//blitPass
	m_pBlitPass = std::make_unique<BlitPass>();
	m_pBlitPass->Initialize(m_deviceResources->GetBackBufferRenderTargetView());

	//PositionMapPass
	m_pPositionMapPass = std::make_unique<PositionMapPass>();

	//LightMap
	lightMap.Initialize();

	//SSR
	m_pScreenSpaceReflectionPass = std::make_unique<ScreenSpaceReflectionPass>();
	m_pScreenSpaceReflectionPass->Initialize(m_diffuseTexture.get(),
		m_metalRoughTexture.get(),
		m_normalTexture.get(),
		m_emissiveTexture.get(),
		m_bitmaskTexture.get()
	);

	//SSS
	m_pSubsurfaceScatteringPass = std::make_unique<SubsurfaceScatteringPass>();
	m_pSubsurfaceScatteringPass->Initialize(m_diffuseTexture.get(),
		m_metalRoughTexture.get()
	);

	//Vignette
	m_pVignettePass = std::make_unique<VignettePass>();
    m_pVignettePass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().vignette);

	//ColorGrading
	m_pColorGradingPass = std::make_unique<ColorGradingPass>();
	m_pColorGradingPass->Initialize();
    m_pColorGradingPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().colorGrading);
	//m_pColorGradingPass->Initialize(PathFinder::Relative("ColorGrading\\LUT_3.png").string());

	//VolumetricFog
	m_pVolumetricFogPass = std::make_unique<VolumetricFogPass>();
	m_pVolumetricFogPass->Initialize(PathFinder::Relative("VolumetricFog\\blueNoise.dds").string());

	m_pUIPass = std::make_unique<UIPass>();
	m_pUIPass->Initialize(m_toneMappedColourTexture.get());

	//AAPass
	m_pAAPass = std::make_unique<AAPass>();
    m_pAAPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().aa);

	m_pPostProcessingPass = std::make_unique<PostProcessingPass>();
    m_pPostProcessingPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().bloom);

	//lightmapPass
	m_pLightMapPass = std::make_unique<LightMapPass>();


	//SSGIPass
	m_pSSGIPass = std::make_unique<SSGIPass>();
	m_pSSGIPass->Initialize(m_diffuseTexture.get(), m_normalTexture.get(), m_lightingTexture.get(), m_metalRoughTexture.get(), ao.get());
    m_pSSGIPass->ApplySettings(EngineSettingInstance->GetRenderPassSettings().ssgi);

	//BitmaskPass
	m_pBitMaskPass = std::make_unique<BitMaskPass>();
	m_pBitMaskPass->Initialize(m_bitmaskTexture.get());

	//DecalPass
	m_pDecalPass = std::make_unique<DecalPass>();
	m_pDecalPass->Initialize(m_diffuseTexture.get(), m_normalTexture.get());

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
	m_deviceResources.reset();
}

void SceneRenderer::Finalize()
{
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

	DeviceState::g_pDevice				= nullptr;
	DeviceState::g_pDeviceContext		= nullptr;
	DeviceState::g_pDepthStencilView	= nullptr;
	DeviceState::g_pDepthStencilState	= nullptr;
	DeviceState::g_pRasterizerState		= nullptr;
	DeviceState::g_pBlendState			= nullptr;
	DeviceState::g_backBufferRTV		= nullptr;
	DeviceState::g_depthStancilSRV		= nullptr;
	DeviceState::g_annotation			= nullptr;

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
    DeviceState::g_pDevice				= m_deviceResources->GetD3DDevice();
    DeviceState::g_pDeviceContext		= m_deviceResources->GetD3DDeviceContext();
    DeviceState::g_pDepthStencilView	= m_deviceResources->GetDepthStencilView();
    DeviceState::g_pDepthStencilState	= m_deviceResources->GetDepthStencilState();
    DeviceState::g_pRasterizerState		= m_deviceResources->GetRasterizerState();
    DeviceState::g_pBlendState			= m_deviceResources->GetBlendState();
    DeviceState::g_Viewport				= m_deviceResources->GetScreenViewport();
    DeviceState::g_backBufferRTV		= m_deviceResources->GetBackBufferRenderTargetView();
    DeviceState::g_depthStancilSRV		= m_deviceResources->GetDepthStencilViewSRV();
    DeviceState::g_ClientRect			= m_deviceResources->GetOutputSize();
    DeviceState::g_aspectRatio			= m_deviceResources->GetAspectRatio();
	DeviceState::g_annotation			= m_deviceResources->GetAnnotation();

	m_resizeEventHandle = OnResizeEvent.AddLambda([&](uint32_t width, uint32_t height)
	{
		DeviceState::g_pDevice				= m_deviceResources->GetD3DDevice();
		DeviceState::g_pDeviceContext		= m_deviceResources->GetD3DDeviceContext();
		DeviceState::g_pDepthStencilView	= m_deviceResources->GetDepthStencilView();
		DeviceState::g_pDepthStencilState	= m_deviceResources->GetDepthStencilState();
		DeviceState::g_pRasterizerState		= m_deviceResources->GetRasterizerState();
		DeviceState::g_pBlendState			= m_deviceResources->GetBlendState();
		//TODO : 빌드 옵션에 따라서 GameViewport를 사용하게 해야겠네???
		//DeviceState::g_Viewport = m_deviceResources->GetScreenViewport();
		DeviceState::g_backBufferRTV		= m_deviceResources->GetBackBufferRenderTargetView();
		DeviceState::g_depthStancilSRV		= m_deviceResources->GetDepthStencilViewSRV();
		DeviceState::g_ClientRect			= m_deviceResources->GetLogicalSize();
		DeviceState::g_aspectRatio			= m_deviceResources->GetAspectRatio();
		DeviceState::g_annotation			= m_deviceResources->GetAnnotation();

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
	auto diffuseTexture = TextureHelper::CreateSharedRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"DiffuseRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_diffuseTexture.swap(diffuseTexture);

	auto metalRoughTexture = TextureHelper::CreateSharedRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"MetalRoughRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_metalRoughTexture.swap(metalRoughTexture);

	auto normalTexture = TextureHelper::CreateSharedRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"NormalRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_normalTexture.swap(normalTexture);

	auto emissiveTexture = TextureHelper::CreateSharedRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EmissiveRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_emissiveTexture.swap(emissiveTexture);

	auto bitmaskTexture = TextureHelper::CreateSharedRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"BitmaskRTV",
		DXGI_FORMAT_R32_UINT
	);
	m_bitmaskTexture.swap(bitmaskTexture);

	auto toneMappedColourTexture = TextureHelper::CreateSharedRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"ToneMappedColourRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_toneMappedColourTexture.swap(toneMappedColourTexture);

	auto lightingTexture = TextureHelper::CreateSharedRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"LightingRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_lightingTexture.swap(lightingTexture);
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
	m_renderScene->EraseRenderPassData();
	m_renderScene->Update(deltaTime);
	m_renderScene->OnProxyDestroy();
	PrepareRender();
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

	for (auto& camera : CameraManagement->m_cameras)
	{
		if (!RenderPassData::VaildCheck(camera)) continue;
		auto renderData = RenderPassData::GetData(camera);

#ifndef BUILD_FLAG
		if (camera != m_pEditorCamera.get())
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

		// DecalPass
		{
			DirectX11::BeginEvent(L"DecalPass");
			Benchmark banch;
			m_pDecalPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("DecalPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
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

		if(camera == m_pEditorCamera.get())
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
		
		if (m_pEditorCamera.get() != camera)
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
		if (m_pEditorCamera.get() != camera)
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

		// EffectPass
		{
			PROFILE_CPU_BEGIN("EffectPass");
			DirectX11::BeginEvent(L"EffectPass");
			Benchmark banch;
			float deltaTime = Time->GetElapsedSeconds();
			m_EffectEditor->Update(deltaTime);
			EffectManagers->Update(deltaTime);
			EffectManagers->Execute(*m_renderScene, *camera);
			m_EffectEditor->Render(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("EffectPass", banch.GetElapsedTime());
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

	for (auto& camera : CameraManagement->m_cameras)
	{
		if (!RenderPassData::VaildCheck(camera)) return;
		auto data = RenderPassData::GetData(camera);

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

		data->SortRenderQueue();
		data->SortShadowRenderQueue();
		data->ClearCullDataBuffer();
		data->ClearShadowRenderDataBuffer();
		PROFILE_CPU_END();

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("ShadowPassCommandList");
			m_renderScene->CreateShadowCommandList(deferredContext , *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			{
				PROFILE_CPU_BEGIN("TerrainPassCommandList");
				m_pGBufferPass->TerrainRenderCommandList(deferredContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}

			{
				PROFILE_CPU_BEGIN("GBufferPassCommandList");
				m_pGBufferPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}

			if (useTestLightmap)
			{
				PROFILE_CPU_BEGIN("LightMapPassCommandList");
				m_pLightMapPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}
			else
			{
				PROFILE_CPU_BEGIN("DeferredPassCommandList");
				m_pDeferredPass->UseAmbientOcclusion(m_ambientOcclusionTexture);
				m_pDeferredPass->UseLightAndEmissiveRTV(m_lightingTexture);
				m_pDeferredPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			}
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("DecalPassCommandList");
			m_pDecalPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("SSAOPassCommandList");
			m_pSSAOPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("SSGIPassCommandList");
			m_pSSGIPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("ForwardPassCommandList");
			m_pForwardPass->CreateFoliageCommandList(deferredContext, *m_renderScene, *camera);
			m_pForwardPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("SubsurfaceScatteringPassCommandList");
			m_pSubsurfaceScatteringPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("SkyBoxPassCommandList");
			m_pSkyBoxPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
				PROFILE_CPU_BEGIN("BitMaskPassCommandList");
				m_pBitMaskPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
		});
		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("ScreenSpaceReflectionPassCommandList");
			m_pScreenSpaceReflectionPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		if (m_pEditorCamera.get() != camera)
		{
			m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
			{
				PROFILE_CPU_BEGIN("VolumetricFogPassCommandList");
				m_pVolumetricFogPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			});
		}
		else
		{
			m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
			{
				PROFILE_CPU_BEGIN("TerrainGizmoPassCommandList");
				m_pTerrainGizmoPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
				PROFILE_CPU_END();
			});
		}

		m_commandThreadPool->Enqueue([&](ID3D11DeviceContext* deferredContext)
		{
			PROFILE_CPU_BEGIN("UIPassCommnadList");
			m_pUIPass->SortUIObjects();
			m_pUIPass->CreateRenderCommandList(deferredContext, *m_renderScene, *camera);
			PROFILE_CPU_END();
		});

		m_commandThreadPool->NotifyAllAndWait();

		data->ClearRenderQueue();
		data->ClearShadowRenderQueue();
	}

	m_pUIPass->ClearFrameQueue();

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
}

void SceneRenderer::PrepareRender()
{
	auto GameSceneStart = SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
	auto GameSceneEnd = !SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;
	
	auto renderScene = m_renderScene;
	auto m_currentScene = SceneManagers->GetActiveScene();
	std::vector<MeshRenderer*> allMeshes = m_currentScene->GetMeshRenderers();
	std::vector<MeshRenderer*> staticMeshes = m_currentScene->GetStaticMeshRenderers();
	std::vector<MeshRenderer*> skinnedMeshes = m_currentScene->GetSkinnedMeshRenderers();
	std::vector<TerrainComponent*> terrainComponents = m_currentScene->GetTerrainComponent();
	std::vector<FoliageComponent*> foliageComponents = m_currentScene->GetFoliageComponents();

	m_threadPool->Enqueue([=]
	{
		for (auto& terrain : terrainComponents)
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

	m_threadPool->Enqueue([=]
	{
		for (auto& mesh : allMeshes)
		{
			try
			{
				renderScene->UpdateCommand(mesh);
			}
			catch (const std::exception& e)
			{
				std::cerr << "Error updating mesh command: " << e.what() << std::endl;
			}
		}
	});

	m_threadPool->Enqueue([=]
	{
		for (auto& foliage : foliageComponents)
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

	EffectProxyController::GetInstance()->PrepareCommandBehavior();
	//for (auto& mesh : staticMeshes)
	//{
	//	if (false == mesh->IsEnabled() || 
	//		false == mesh->IsNeedUpdateCulling()) continue;

	//	CullingManagers->UpdateMesh(mesh);
	//}

	for (auto camera : CameraManagement->m_cameras)
	{
		if (nullptr == camera) continue;

		if (!RenderPassData::VaildCheck(camera)) return;
		auto data = RenderPassData::GetData(camera);

		//std::vector<MeshRenderer*> culledMeshes;
		//CullingManagers->SmartCullMeshes(camera->GetFrustum(), culledMeshes);

		m_threadPool->Enqueue([=]
		{
			for (auto& terrainComponent : terrainComponents)
			{
				if (terrainComponent->IsEnabled())
				{
					auto proxy = renderScene->FindProxy(terrainComponent->GetInstanceID());
					if(proxy)
					{
						data->PushRenderQueue(proxy);
					}
				}
			}

			for (auto& foliageComponent : foliageComponents)
			{
				if (foliageComponent->IsEnabled())
				{
					auto proxy = renderScene->FindProxy(foliageComponent->GetInstanceID());
					if (proxy)
					{
						data->PushRenderQueue(proxy);
					}
				}
			}

			data->UpdateData(camera);

			data->AddFrame();
		});
	}

	m_threadPool->NotifyAllAndWait();

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

