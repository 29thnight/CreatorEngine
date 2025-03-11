#include "SceneRenderer.h"
#include "DeviceState.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "ImGuiRegister.h"

SceneRenderer::SceneRenderer(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources) :
	m_deviceResources(deviceResources)
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

	//sampler 생성
	m_linearSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	m_pointSampler = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	AssetsSystems->LoadShaders();

	//RTV's 생성
	m_colorTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"ColorRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_diffuseTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"DiffuseRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_metalRoughTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"MetalRoughRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_normalTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"NormalRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_emissiveTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EmissiveRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_toneMappedColourTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"ToneMappedColourRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	Texture* ao = Texture::Create(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"AmbientOcclusion",
		DXGI_FORMAT_R16_UNORM,
		D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE
	);
	ao->CreateRTV(DXGI_FORMAT_R16_UNORM);
	ao->CreateSRV(DXGI_FORMAT_R16_UNORM);
	m_ambientOcclusionTexture = std::unique_ptr<Texture>(std::move(ao));

	//Buffer 생성
	XMMATRIX identity = XMMatrixIdentity();

	m_ModelBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ModelBuffer.Get(), "ModelBuffer");
	m_ViewBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ViewBuffer.Get(), "ViewBuffer");
	m_ProjBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix), D3D11_BIND_FLAG::D3D11_BIND_CONSTANT_BUFFER, &identity);
	DirectX::SetName(m_ProjBuffer.Get(), "ProjBuffer");

    //pass 생성
    //shadowMapPass 는 Scene의 맴버

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
        m_normalTexture.get()
    );

    //deferredPass
    m_pDeferredPass = std::make_unique<DeferredPass>();
    m_pDeferredPass->Initialize(
        m_colorTexture.get(),
        m_diffuseTexture.get(),
        m_metalRoughTexture.get(),
        m_normalTexture.get(),
        m_emissiveTexture.get()
    );

	//skyBoxPass
	m_pSkyBoxPass = std::make_unique<SkyBoxPass>();
	m_pSkyBoxPass->SetRenderTarget(m_colorTexture.get());
	m_pSkyBoxPass->Initialize(PathFinder::Relative("HDR/Malibu_Overlook_3k.hdr").string());

	//toneMapPass
	m_pToneMapPass = std::make_unique<ToneMapPass>();
	m_pToneMapPass->Initialize(
		m_colorTexture.get(),
		m_toneMappedColourTexture.get()
	);

	//spritePass
	m_pSpritePass = std::make_unique<SpritePass>();
	m_pSpritePass->Initialize(m_toneMappedColourTexture.get());

	//blitPass
	m_pBlitPass = std::make_unique<BlitPass>();
	m_pBlitPass->Initialize(m_toneMappedColourTexture.get(), 
		m_deviceResources->GetBackBufferRenderTargetView());
}

void SceneRenderer::Initialize(Scene* _pScene)
{
	if (!_pScene)
	{
		m_currentScene = new Scene();

		auto lightColour = XMFLOAT4(5.0f, 5.0f, 5.0f, 1.0f);
		
		Light pointLight;
		pointLight.m_color = XMFLOAT4(1, 1, 0, 0);
		pointLight.m_position = XMFLOAT4(4, 3, 0, 0);
		pointLight.m_lightType = LightType::PointLight;

		Light dirLight;
		dirLight.m_color = lightColour;
		dirLight.m_direction = XMFLOAT4(-1, -1, 1, 0);
		dirLight.m_lightType = LightType::DirectionalLight;

		Light spotLight;
		spotLight.m_color = XMFLOAT4(Colors::Magenta);
		spotLight.m_direction = XMFLOAT4(0, -1, 0, 0);
		spotLight.m_position = XMFLOAT4(3, 2, 0, 0);
		spotLight.m_lightType = LightType::SpotLight;
		spotLight.m_spotLightAngle = 3.142 / 4.0;

		m_currentScene->m_LightController
			.AddLight(dirLight)
			.AddLight(pointLight)
			.AddLight(spotLight)
			.SetGlobalAmbient(XMFLOAT4(0.1f, 0.1f, 0.1f, 1.0f));

		ShadowMapRenderDesc desc;
		desc.m_eyePosition = XMLoadFloat4(&(m_currentScene->m_LightController.GetLight(0).m_direction)) * -5.f;
		desc.m_lookAt = XMVectorSet(0, 0, 0, 1);
		desc.m_viewWidth = 16;
		desc.m_viewHeight = 12;
		desc.m_nearPlane = 1.f;
		desc.m_farPlane = 20.f;
		desc.m_textureWidth = DeviceState::g_ClientRect.width;
		desc.m_textureHeight = DeviceState::g_ClientRect.height;

		m_currentScene->m_LightController.Initialize();
		m_currentScene->m_LightController.SetLightWithShadows(0, desc);

		model = Model::LoadModel("Prop_Block.fbx");
		Model::LoadModelToScene(model, *m_currentScene);
		ImGui::ContextRegister("TestModelMaterial", [&]()
		{
			ImGui::SliderFloat4("BaseColor", &model->m_SceneObject->m_meshRenderer.m_Material->m_materialInfo.m_baseColor.x, 0.0f, 1.0f);
			ImGui::SliderFloat("Metallic", &model->m_SceneObject->m_meshRenderer.m_Material->m_materialInfo.m_metallic, 0.0f, 1.0f);
			ImGui::SliderFloat("Roughness", &model->m_SceneObject->m_meshRenderer.m_Material->m_materialInfo.m_roughness, 0.1f, 1.0f);
		});
	}
	else
	{
		m_currentScene = _pScene;
	}

	m_currentScene->SetBuffers(m_ModelBuffer.Get(), m_ViewBuffer.Get(), m_ProjBuffer.Get());

	DeviceState::g_pDeviceContext->PSSetSamplers(0, 1, &m_linearSampler->m_SamplerState);
	DeviceState::g_pDeviceContext->PSSetSamplers(1, 1, &m_pointSampler->m_SamplerState);

	m_pSkyBoxPass->GenerateCubeMap(*m_currentScene);
	Texture* envMap = m_pSkyBoxPass->GenerateEnvironmentMap(*m_currentScene);
	Texture* preFilter = m_pSkyBoxPass->GeneratePrefilteredMap(*m_currentScene);
	Texture* brdfLUT = m_pSkyBoxPass->GenerateBRDFLUT(*m_currentScene);

	m_pDeferredPass->UseEnvironmentMap(envMap, preFilter, brdfLUT);

}

void SceneRenderer::Update(float deltaTime)
{
	m_currentScene->Update(deltaTime);
}

void SceneRenderer::Render()
{
	model->m_SceneObject->m_transform
		//.SetScale({ 0.01f, 0.01f, 0.01f })
		.SetPosition({ 2.f, 0.5f, -2.f });

	//[1] ShadowMapPass
	{
		Texture& shadowMapTexture = (*m_currentScene->m_LightController.GetShadowMapTexture());
		SetRenderTargets(shadowMapTexture);
		m_currentScene->ShadowStage();
		Clear(DirectX::Colors::Transparent, 1.0f, 0);
		UnbindRenderTargets();
	}

	//[2] GBufferPass
	{
		m_pGBufferPass->Execute(*m_currentScene);
	}

	//[3] SSAOPass
	{
        m_pSSAOPass->Execute(*m_currentScene);
	}

    //[4] DeferredPass
    {
		m_pDeferredPass->UseAmbientOcclusion(m_ambientOcclusionTexture.get());
        m_pDeferredPass->Execute(*m_currentScene);
    }

	//[5] skyBoxPass
	{
		m_pSkyBoxPass->Execute(*m_currentScene);
	}

    //[6] ToneMapPass
    {
        m_pToneMapPass->Execute(*m_currentScene);
    }

	//[7] SpritePass
	{
		m_pSpritePass->Execute(*m_currentScene);
	}

	//[8] BlitPass
	{
		m_pBlitPass->Execute(*m_currentScene);
	}
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

void SceneRenderer::UnbindRenderTargets()
{
	ID3D11RenderTargetView* nullRTV = nullptr;
	ID3D11DepthStencilView* nullDSV = nullptr;
	DirectX11::OMSetRenderTargets(1, &nullRTV, nullDSV);
}
