#include "SceneRenderer.h"
#include "DeviceState.h"
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
#include "CullingManager.h"
#include "IconsFontAwesome6.h"
#include "fa.h"
#include "Trim.h"

#include <iostream>
#include <string>
#include <regex>

#include "Animator.h"

using namespace lm;

SceneRenderer::SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
{
    InitializeDeviceState();
    InitializeImGui();

	//sampler 생성
	m_linearSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	m_pointSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	InitializeTextures();

	ShaderSystem->Initialize();

	Texture* ao = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"AmbientOcclusion",
		//DXGI_FORMAT_R16_UNORM,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	);
	ao->CreateRTV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	ao->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_ambientOcclusionTexture = MakeUniqueTexturePtr(ao);

	//Buffer 생성
	XMMATRIX identity = XMMatrixIdentity();

	m_ModelBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &Mathf::xMatrixIdentity);
	DirectX::SetName(m_ModelBuffer.Get(), "ModelBuffer");

	m_pEditorCamera = std::make_shared<Camera>();
	m_pEditorCamera->RegisterContainer();
	m_pEditorCamera->m_applyRenderPipelinePass.m_GridPass = true;

	m_spriteBatch = std::make_shared<DirectX::SpriteBatch>(DeviceState::g_pDeviceContext);
    //pass 생성
    //shadowMapPass 는 RenderScene의 맴버
    //gBufferPass
    m_pGBufferPass = std::make_unique<GBufferPass>();
	ID3D11RenderTargetView* views[]{
		m_diffuseTexture->GetRTV(),
		m_metalRoughTexture->GetRTV(),
		m_normalTexture->GetRTV(),
		m_emissiveTexture->GetRTV()
	};
	m_pGBufferPass->SetRenderTargetViews(views, ARRAYSIZE(views));

    //ssaoPass
    m_pSSAOPass = std::make_unique<SSAOPass>();
    m_pSSAOPass->Initialize(
        ao,
        m_deviceResources->GetDepthStencilViewSRV(),
        m_normalTexture.get(),
		m_diffuseTexture.get()
    );

    //deferredPass
    m_pDeferredPass = std::make_unique<DeferredPass>();
    m_pDeferredPass->Initialize(
        m_diffuseTexture.get(),
        m_metalRoughTexture.get(),
        m_normalTexture.get(),
        m_emissiveTexture.get()
    );

	//forwardPass
	m_pForwardPass = std::make_unique<ForwardPass>();

	//skyBoxPass
	m_pSkyBoxPass = std::make_unique<SkyBoxPass>();
	m_pSkyBoxPass->Initialize(PathFinder::Relative("HDR\\rosendal_park_sunset_puresky_4k.hdr").string());
	
	//toneMapPass
	m_pToneMapPass = std::make_unique<ToneMapPass>();
	m_pToneMapPass->Initialize(
		m_toneMappedColourTexture.get()
	);

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
		m_emissiveTexture.get()
	);

	//SSS
	m_pSubsurfaceScatteringPass = std::make_unique<SubsurfaceScatteringPass>();
	m_pSubsurfaceScatteringPass->Initialize(m_diffuseTexture.get(),
		m_metalRoughTexture.get()
	);

	//Vignette
	m_pVignettePass = std::make_unique<VignettePass>();

	//ColorGrading
	m_pColorGradingPass = std::make_unique<ColorGradingPass>();
	m_pColorGradingPass->Initialize(PathFinder::Relative("ColorGrading\\LUT_3.png").string());

	//VolumetricFog
	m_pVolumetricFogPass = std::make_unique<VolumetricFogPass>();
	m_pVolumetricFogPass->Initialize(PathFinder::Relative("VolumetricFog\\blueNoise.dds").string());

	m_pUIPass = std::make_unique<UIPass>();
	m_pUIPass->Initialize(m_toneMappedColourTexture.get(),m_spriteBatch.get());

	//AAPass
	m_pAAPass = std::make_unique<AAPass>();

	m_pPostProcessingPass = std::make_unique<PostProcessingPass>();

	//lightmapPass
	m_pLightMapPass = std::make_unique<LightMapPass>();
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

	m_renderScene = new RenderScene();
	SceneManagers->m_ActiveRenderScene = m_renderScene;
	m_renderScene->Initialize();
	m_renderScene->SetBuffers(m_ModelBuffer.Get());
	//m_pEffectPass = std::make_unique<EffectManager>();
	//m_pEffectPass->MakeEffects(Effect::Sparkle, "asd", float3(0, 0, 0));

	m_threadPool = new ThreadPool;

    m_newSceneCreatedEventHandle = newSceneCreatedEvent.AddRaw(this, &SceneRenderer::NewCreateSceneInitialize);
	m_activeSceneChangedEventHandle = activeSceneChangedEvent.AddLambda([&] 
	{
		m_renderScene->Update(0.f);
	});
}

SceneRenderer::~SceneRenderer()
{
	delete m_threadPool;

	OnResizeEvent -= m_resizeEventHandle;
}

void SceneRenderer::InitializeDeviceState()
{
    DeviceState::g_pDevice = m_deviceResources->GetD3DDevice();
    DeviceState::g_pDeviceContext = m_deviceResources->GetD3DDeviceContext();
    DeviceState::g_pDepthStencilView = m_deviceResources->GetDepthStencilView();
    DeviceState::g_pDepthStencilState = m_deviceResources->GetDepthStencilState();
    DeviceState::g_pRasterizerState = m_deviceResources->GetRasterizerState();
    DeviceState::g_pBlendState = m_deviceResources->GetBlendState();
    DeviceState::g_Viewport = m_deviceResources->GetScreenViewport();
    DeviceState::g_backBufferRTV = m_deviceResources->GetBackBufferRenderTargetView();
    DeviceState::g_depthStancilSRV = m_deviceResources->GetDepthStencilViewSRV();
    DeviceState::g_ClientRect = m_deviceResources->GetOutputSize();
    DeviceState::g_aspectRatio = m_deviceResources->GetAspectRatio();
	DeviceState::g_annotation = m_deviceResources->GetAnnotation();

	m_resizeEventHandle = OnResizeEvent.AddLambda([&](uint32_t width, uint32_t height)
	{
		DeviceState::g_pDevice = m_deviceResources->GetD3DDevice();
		DeviceState::g_pDeviceContext = m_deviceResources->GetD3DDeviceContext();
		DeviceState::g_pDepthStencilView = m_deviceResources->GetDepthStencilView();
		DeviceState::g_pDepthStencilState = m_deviceResources->GetDepthStencilState();
		DeviceState::g_pRasterizerState = m_deviceResources->GetRasterizerState();
		DeviceState::g_pBlendState = m_deviceResources->GetBlendState();
		//TODO : 빌드 옵션에 따라서 GameViewport를 사용하게 해야겠네???
		//DeviceState::g_Viewport = m_deviceResources->GetScreenViewport();
		DeviceState::g_backBufferRTV = m_deviceResources->GetBackBufferRenderTargetView();
		DeviceState::g_depthStancilSRV = m_deviceResources->GetDepthStencilViewSRV();
		DeviceState::g_ClientRect = m_deviceResources->GetLogicalSize();
		DeviceState::g_aspectRatio = m_deviceResources->GetAspectRatio();
		DeviceState::g_annotation = m_deviceResources->GetAnnotation();

		m_pSSAOPass->ReloadDSV(m_deviceResources->GetDepthStencilViewSRV());

		m_pBlitPass->Initialize(m_deviceResources->GetBackBufferRenderTargetView());
	});
}

void SceneRenderer::InitializeImGui()
{
	
}

void SceneRenderer::InitializeTextures()
{
	auto diffuseTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"DiffuseRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_diffuseTexture.swap(diffuseTexture);

	auto metalRoughTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"MetalRoughRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_metalRoughTexture.swap(metalRoughTexture);

	auto normalTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"NormalRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_normalTexture.swap(normalTexture);

	auto emissiveTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EmissiveRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_emissiveTexture.swap(emissiveTexture);

	auto toneMappedColourTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"ToneMappedColourRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
    m_toneMappedColourTexture.swap(toneMappedColourTexture);
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

	ShadowMapRenderDesc desc;
	desc.m_lookAt = XMVectorSet(0, 0, 0, 1);
	desc.m_eyePosition = Mathf::Vector4{ -1, -1, 1, 0 } * -50.f;
	desc.m_viewWidth = 100;
	desc.m_viewHeight = 100;
	desc.m_nearPlane = 0.1f;
	desc.m_farPlane = 1000.0f;
	desc.m_textureWidth = 2048;
	desc.m_textureHeight = 2048;

	m_renderScene->m_LightController->Initialize();
	m_renderScene->m_LightController->SetLightWithShadows(0, desc);

	/*DataSystems->LoadModel("Punch.fbx");
	model[0] = DataSystems->LoadCashedModel("Punch.fbx");
	testt = Model::LoadModelToSceneObj(model[0], *scene);
	player.GetPlayer(testt);*/


	DeviceState::g_pDeviceContext->PSSetSamplers(0, 1, &m_linearSampler->m_SamplerState);
	DeviceState::g_pDeviceContext->PSSetSamplers(1, 1, &m_pointSampler->m_SamplerState);

	m_pSkyBoxPass->GenerateCubeMap(*m_renderScene);
	Texture* envMap = m_pSkyBoxPass->GenerateEnvironmentMap(*m_renderScene);
	Texture* preFilter = m_pSkyBoxPass->GeneratePrefilteredMap(*m_renderScene);
	Texture* brdfLUT = m_pSkyBoxPass->GenerateBRDFLUT(*m_renderScene);

	m_pDeferredPass->UseEnvironmentMap(envMap, preFilter, brdfLUT);
	lightMap.envMap = envMap;
}

void SceneRenderer::OnWillRenderObject(float deltaTime)
{
	//TODO : 이 부분은 PreDepth로 적용해보고 프레임 얼마나 늘어나는지 테스트 필요

	//m_pEffectPass->Update(deltaTime);
}

void SceneRenderer::EndOfFrame(float deltaTime)
{
	m_renderScene->Update(deltaTime);
	PrepareRender();
}

void SceneRenderer::SceneRendering()
{
	if (ShaderSystem->IsReloading())
	{
		ReloadShaders();
	}

	DirectX11::ResetCallCount();

	for(auto& camera : CameraManagement->m_cameras)
	{
		if (nullptr == camera) continue;

		if (camera != m_pEditorCamera.get())
		{
			if(EngineSettingInstance->IsGameView())
			{
				camera->m_applyRenderPipelinePass.m_BlitPass = true;
			}
			else
			{
				camera->m_applyRenderPipelinePass.m_BlitPass = false;
			}
		}

		std::wstring name =  L"Camera" + std::to_wstring(camera->m_cameraIndex);
		DirectX11::BeginEvent(name);
		//[1] ShadowMapPass
		{
			DirectX11::BeginEvent(L"ShadowMapPass");
			static int count = 0;
			Benchmark banch;
			camera->ClearRenderTarget();
			m_renderScene->ShadowStage(*camera);
			Clear(DirectX::Colors::Transparent, 1.0f, 0);
			UnbindRenderTargets();
			RenderStatistics->UpdateRenderState("ShadowMapPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//[2] GBufferPass
		{
			DirectX11::BeginEvent(L"GBufferPass");
			Benchmark banch;
			ID3D11RenderTargetView* views[]{
				m_diffuseTexture->GetRTV(),
				m_metalRoughTexture->GetRTV(),
				m_normalTexture->GetRTV(),
				m_emissiveTexture->GetRTV()
			};
			m_pGBufferPass->SetRenderTargetViews(views, ARRAYSIZE(views));
			m_pGBufferPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("GBufferPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		if (useTestLightmap)
		{
			DirectX11::BeginEvent(L"LightMapPass");
			Benchmark banch;
			m_pLightMapPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("LightMapPass", banch.GetElapsedTime());
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
				DirectX11::BeginEvent(L"DeferredPass");
				Benchmark banch;
				m_pDeferredPass->UseAmbientOcclusion(m_ambientOcclusionTexture.get());
				m_pDeferredPass->Execute(*m_renderScene, *camera);
				RenderStatistics->UpdateRenderState("DeferredPass", banch.GetElapsedTime());
				DirectX11::EndEvent();
			}
		}

		{
			DirectX11::BeginEvent(L"ForwardPass");
			Benchmark banch;
			m_pForwardPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ForwardPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//SSS
		{
			DirectX11::BeginEvent(L"SubsurfaceScatteringPass");
			Benchmark banch;
			m_pSubsurfaceScatteringPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SubsurfaceScatteringPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//[5] skyBoxPass
		{
			DirectX11::BeginEvent(L"SkyBoxPass");
			Benchmark banch;
			m_pSkyBoxPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SkyBoxPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}
		//SSR
		if (m_pEditorCamera.get() != camera)
		{
			DirectX11::BeginEvent(L"ScreenSpaceReflectionPass");
			Benchmark banch;
			m_pScreenSpaceReflectionPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ScreenSpaceReflectionPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//VolumetricFogPass
		if (m_pEditorCamera.get() != camera)
		{
			DirectX11::BeginEvent(L"VolumetricFogPass");
			Benchmark banch;
			m_pVolumetricFogPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("VolumetricFogPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

        //[*] PostProcessPass
        {
			DirectX11::BeginEvent(L"PostProcessPass");
			Benchmark banch;
            m_pPostProcessingPass->Execute(*m_renderScene, *camera);
            RenderStatistics->UpdateRenderState("PostProcessPass", banch.GetElapsedTime());
            DirectX11::EndEvent();
        }

		//[6] AAPass
		{
			DirectX11::BeginEvent(L"AAPass");
			Benchmark banch;
			m_pAAPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("AAPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//[7] ToneMapPass
		{
			DirectX11::BeginEvent(L"ToneMapPass");
			Benchmark banch;
			m_pToneMapPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ToneMapPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//Vignette
		{
			DirectX11::BeginEvent(L"VignettePass");
			Benchmark banch;
			m_pVignettePass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("VignettePass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//m_pColorGradingPass
		{
			DirectX11::BeginEvent(L"ColorGradingPass");
			Benchmark banch;
			m_pColorGradingPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("ColorGradingPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		{
			//DirectX11::BeginEvent(L"EffectPass");
			//Benchmark banch;
			//m_pEffectPass->Execute(*m_renderScene, *camera);
			//RenderStatistics->UpdateRenderState("EffectPass", banch.GetElapsedTime());
			//DirectX11::EndEvent();
		}

		//[7] SpritePass
		{
			DirectX11::BeginEvent(L"SpritePass");
			Benchmark banch;
			m_pSpritePass->SetGizmoRendering(false);
			m_pSpritePass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("SpritePass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//[]  UIPass
		{
			DirectX11::BeginEvent(L"UIPass");
			Benchmark banch;
			m_pUIPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("UIPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		//[8] BlitPass
		{
			DirectX11::BeginEvent(L"BlitPass");
			Benchmark banch;
			m_pBlitPass->Execute(*m_renderScene, *camera);
			RenderStatistics->UpdateRenderState("BlitPass", banch.GetElapsedTime());
			DirectX11::EndEvent();
		}

		DirectX11::EndEvent();
	}
}

void SceneRenderer::ReApplyCurrCubeMap()
{
	ApplyNewCubeMap(m_pSkyBoxPass->CurrentSkyBoxTextureName().string());
}

void SceneRenderer::PrepareRender()
{
	auto GameSceneStart = SceneManagers->m_isGameStart && !SceneManagers->m_isEditorSceneLoaded;
	auto GameSceneEnd = !SceneManagers->m_isGameStart && SceneManagers->m_isEditorSceneLoaded;

	if (GameSceneStart || GameSceneEnd)
	{
		for (auto camera : CameraManagement->m_cameras)
		{
			if (nullptr == camera) continue;
			camera->ClearRenderQueue();
		}

		return;
	}

	Benchmark banch;
	auto renderScene = m_renderScene;
	auto m_currentScene = SceneManagers->GetActiveScene();
	std::vector<MeshRenderer*> staticMeshes = m_currentScene->GetStaticMeshRenderers();
	std::vector<MeshRenderer*> skinnedMeshes = m_currentScene->GetSkinnedMeshRenderers();

	for (auto& mesh : staticMeshes)
	{
		if (false == mesh->IsEnabled() || 
			false == mesh->IsNeedUpdateCulling()) continue;

		CullingManagers->UpdateMesh(mesh);
	}

	for (auto camera : CameraManagement->m_cameras)
	{
		if (nullptr == camera) continue;

		camera->ClearRenderQueue();

		m_threadPool->Enqueue([camera, &staticMeshes, &skinnedMeshes, renderScene]
		{
			//std::vector<MeshRenderer*> culledMeshes;
			//CullingManagers->SmartCullMeshes(camera->GetFrustum(), culledMeshes);

			for (auto& culledMesh : staticMeshes)
			{
				auto frustum = camera->GetFrustum();
				if (frustum.Intersects(culledMesh->GetBoundingBox()))
				{
					auto proxyObject = renderScene->FindProxy(culledMesh->GetInstanceID());
					if (proxyObject)
					{
						renderScene->UpdateCommand(culledMesh);
						camera->PushRenderQueue(proxyObject);
					}
				}
			}

			for (auto& skinnedMesh : skinnedMeshes)
			{
				if (false == skinnedMesh->IsEnabled()) continue;

				auto frustum = camera->GetFrustum();
				if (frustum.Intersects(skinnedMesh->GetBoundingBox()))
				{
					auto proxyObject = renderScene->FindProxy(skinnedMesh->GetInstanceID());
					//for test
					if(proxyObject)
					{
						renderScene->UpdateCommand(skinnedMesh);
						camera->PushRenderQueue(proxyObject);
					}
				}
			}
		});
	}

	m_threadPool->NotifyAllAndWait();
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

void SceneRenderer::ApplyNewCubeMap(const std::string_view& filename)
{
	m_pSkyBoxPass->GenerateCubeMap(filename, *m_renderScene);
	Texture* envMap = m_pSkyBoxPass->GenerateEnvironmentMap(*m_renderScene);
	Texture* preFilter = m_pSkyBoxPass->GeneratePrefilteredMap(*m_renderScene);
	Texture* brdfLUT = m_pSkyBoxPass->GenerateBRDFLUT(*m_renderScene);

	m_pDeferredPass->UseEnvironmentMap(envMap, preFilter, brdfLUT);
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

