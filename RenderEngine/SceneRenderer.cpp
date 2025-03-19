#include "SceneRenderer.h"
#include "SceneRenderer.h"
#include "DeviceState.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "ImGuiRegister.h"
#include "Banchmark.hpp"

#pragma region ImGuizmo
#include "ImGuizmo.h"

static const float identityMatrix[16] = { 
	1.f, 0.f, 0.f, 0.f,
	0.f, 1.f, 0.f, 0.f,
	0.f, 0.f, 1.f, 0.f,
	0.f, 0.f, 0.f, 1.f 
};
bool useWindow = true;
bool editWindow = true;
int gizmoCount = 1;
float camDistance = 8.f;
static ImGuizmo::OPERATION mCurrentGizmoOperation(ImGuizmo::TRANSLATE);


void SceneRenderer::EditTransform(float* cameraView, float* cameraProjection, float* matrix, bool editTransformDecomposition, SceneObject* obj, Camera* cam)
{
	static ImGuizmo::MODE mCurrentGizmoMode(ImGuizmo::LOCAL);
	static bool useSnap = false;
	static float snap[3] = { 1.f, 1.f, 1.f };
	static float bounds[] = { -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f };
	static float boundsSnap[] = { 0.1f, 0.1f, 0.1f };
	static bool boundSizing = false;
	static bool boundSizingSnap = false;

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::BeginFrame();

	if (editTransformDecomposition)
	{
		if (ImGui::IsKeyPressed(ImGuiKey_T))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		if (ImGui::IsKeyPressed(ImGuiKey_G))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		if (ImGui::IsKeyPressed(ImGuiKey_R)) // r Key
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		if (ImGui::RadioButton("Translate", mCurrentGizmoOperation == ImGuizmo::TRANSLATE))
			mCurrentGizmoOperation = ImGuizmo::TRANSLATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", mCurrentGizmoOperation == ImGuizmo::ROTATE))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale", mCurrentGizmoOperation == ImGuizmo::SCALE))
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		if (ImGui::RadioButton("Universal", mCurrentGizmoOperation == ImGuizmo::UNIVERSAL))
			mCurrentGizmoOperation = ImGuizmo::UNIVERSAL;
		float matrixTranslation[3], matrixRotation[3], matrixScale[3];
		ImGuizmo::DecomposeMatrixToComponents(matrix, matrixTranslation, matrixRotation, matrixScale);
		ImGui::InputFloat3("Tr", matrixTranslation);
		ImGui::InputFloat3("Rt", matrixRotation);
		ImGui::InputFloat3("Sc", matrixScale);
		ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, matrix);

		if (mCurrentGizmoOperation != ImGuizmo::SCALE)
		{
			if (ImGui::RadioButton("Local", mCurrentGizmoMode == ImGuizmo::LOCAL))
				mCurrentGizmoMode = ImGuizmo::LOCAL;
			ImGui::SameLine();
			if (ImGui::RadioButton("World", mCurrentGizmoMode == ImGuizmo::WORLD))
				mCurrentGizmoMode = ImGuizmo::WORLD;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_F))
			useSnap = !useSnap;
		ImGui::Checkbox("##UseSnap", &useSnap);
		ImGui::SameLine();

		switch (mCurrentGizmoOperation)
		{
		case ImGuizmo::TRANSLATE:
			ImGui::InputFloat3("Snap", &snap[0]);
			break;
		case ImGuizmo::ROTATE:
			ImGui::InputFloat("Angle Snap", &snap[0]);
			break;
		case ImGuizmo::SCALE:
			ImGui::InputFloat("Scale Snap", &snap[0]);
			break;
		}
		ImGui::Checkbox("Bound Sizing", &boundSizing);
		if (boundSizing)
		{
			ImGui::PushID(3);
			ImGui::Checkbox("##BoundSizing", &boundSizingSnap);
			ImGui::SameLine();
			ImGui::InputFloat3("Snap", boundsSnap);
			ImGui::PopID();
		}
	}

	ImGuiIO& io = ImGui::GetIO();
	float viewManipulateRight = io.DisplaySize.x;
	float viewManipulateTop = 0;
	static ImGuiWindowFlags gizmoWindowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	if (useWindow)
	{
		//ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_Appearing);
		//ImGui::SetNextWindowPos(ImVec2(400, 20), ImGuiCond_Appearing);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.35f, 0.3f, 0.3f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
		ImGui::Begin("Gizmo", 0, gizmoWindowFlags);
		ImGuizmo::SetDrawlist();

		//std::cout << ImGui::GetMousePos().x <<", " << ImGui::GetMousePos().y<< std::endl;

		float windowWidth = (float)ImGui::GetWindowWidth();
		float windowHeight = (float)ImGui::GetWindowHeight();
		ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
		viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
		viewManipulateTop = ImGui::GetWindowPos().y;
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		gizmoWindowFlags |= ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;

		float x = window->InnerRect.Max.x - window->InnerRect.Min.x;
		float y = window->InnerRect.Max.y - window->InnerRect.Min.y;

		ImGui::Image((ImTextureID)m_gridTexture->m_pSRV, ImVec2(x, y));
		ImGui::PopStyleVar();
	}
	else
	{
		ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	}


	//ImGuizmo::DrawCubes(cameraView, cameraProjection, &objectMatrix[0][0], gizmoCount);


	if (obj)
	{
		ImGuizmo::Manipulate(cameraView, cameraProjection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix, NULL, useSnap ? &snap[0] : NULL, boundSizing ? bounds : NULL, boundSizingSnap ? boundsSnap : NULL);

		XMVECTOR pos;
		XMVECTOR rot;
		XMVECTOR scale;
		XMMatrixDecompose(&scale, &rot, &pos, XMMATRIX(matrix));

		obj->m_transform.SetPosition(pos);
		obj->m_transform.SetRotation(rot);
		obj->m_transform.SetScale(scale);
	}

	ImGuizmo::ViewManipulate(cameraView, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128), 0x10101010);

	{
		XMVECTOR poss;
		XMVECTOR rots;
		XMVECTOR scales;
		XMMatrixDecompose(&scales, &rots, &poss, XMMatrixInverse(nullptr, XMMATRIX(cameraView)));
		cam->m_eyePosition = poss;
		cam->m_rotation = rots;

		XMVECTOR rotDir = XMVector3Rotate(cam->FORWARD, rots);

		cam->m_forward = rotDir;
	}

	if (useWindow)
	{
		ImGui::End();
		ImGui::PopStyleColor(1);
	}
}
#pragma endregion

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

	InitializeTextures();

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

	CD3D11_TEXTURE2D_DESC1 depthStencilDesc(
		DXGI_FORMAT_R24G8_TYPELESS,
		lround(DeviceState::g_ClientRect.width),
		lround(DeviceState::g_ClientRect.height),
		1,
		1,
		D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
	);

	ComPtr<ID3D11Texture2D1> depthStencil;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateTexture2D1(
			&depthStencilDesc,
			nullptr,
			&depthStencil
		)
	);

	CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2D);
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilView(
			depthStencil.Get(),
			&depthStencilViewDesc,
			&m_depthStencilView
		)
	);
	DirectX::SetName(depthStencil.Get(), "DepthStencil");

	DeviceState::g_pEditorDepthStencilView = m_depthStencilView;

	CD3D11_SHADER_RESOURCE_VIEW_DESC depthStencilSRVDesc(D3D11_SRV_DIMENSION_TEXTURE2D);
	depthStencilSRVDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depthStencilSRVDesc.Texture2D.MipLevels = 1;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateShaderResourceView(
			depthStencil.Get(),
			&depthStencilSRVDesc,
			&m_depthStencilSRV
		)
	);

	DeviceState::g_depthStancilSRV = m_depthStencilSRV;

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

	ID3D11RenderTargetView* editorViews[]{
		m_editDiffuseTexture->GetRTV(),
		m_editMetalRoughTexture->GetRTV(),
		m_editNormalTexture->GetRTV(),
		m_editEmissiveTexture->GetRTV()
	};
	m_pGBufferPass->SetEditorRenderTargetViews(editorViews, ARRAYSIZE(editorViews));

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

	m_pDeferredPass->EditorInitialize(
		m_editColorTexture.get(),
		m_editDiffuseTexture.get(),
		m_editMetalRoughTexture.get(),
		m_editNormalTexture.get(),
		m_editEmissiveTexture.get()
	);

	//skyBoxPass
	m_pSkyBoxPass = std::make_unique<SkyBoxPass>();
	m_pSkyBoxPass->SetRenderTarget(m_colorTexture.get());
	m_pSkyBoxPass->Initialize(PathFinder::Relative("HDR/rosendal_park_sunset_puresky_4k.hdr").string());
	

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

	//WireFramePass
	m_pWireFramePass = std::make_unique<WireFramePass>();
	m_pWireFramePass->SetRenderTarget(m_colorTexture.get());

    m_pGridPass = std::make_unique<GridPass>();
    m_pGridPass->Initialize(m_editColorTexture.get(), m_gridTexture.get());
}


void SceneRenderer::InitializeTextures()
{
	//RTV's 생성
	m_colorTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"ColorRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_editColorTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EditColorRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_diffuseTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"DiffuseRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_editDiffuseTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EditDiffuseRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_metalRoughTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"MetalRoughRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_editMetalRoughTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EditMetalRoughRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_normalTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"NormalRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_editNormalTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EditNormalRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_emissiveTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EmissiveRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_editEmissiveTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"EditEmissiveRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_toneMappedColourTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"ToneMappedColourRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);

	m_gridTexture = TextureHelper::CreateRenderTexture(
		DeviceState::g_ClientRect.width,
		DeviceState::g_ClientRect.height,
		"GridRTV",
		DXGI_FORMAT_R16G16B16A16_FLOAT
	);
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
		desc.m_nearPlane = 0.1f;
		desc.m_farPlane = 1000.f;
		desc.m_textureWidth = 1920;
		desc.m_textureHeight = 1080;

		m_currentScene->m_LightController.Initialize();
		m_currentScene->m_LightController.SetLightWithShadows(0, desc);

		model = Model::LoadModel("bangbooExport.fbx");
		//model = Model::LoadModel("untitled.gltf");
		Model::LoadModelToScene(model, *m_currentScene);
		//model = Model::LoadModel("BoxHuman.fbx");
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

	ImGui::ContextRegister("RenderPass", true, [&]()
	{
		if (ImGui::BeginTabBar("RenderPass Control Panel"))
		{
			if (ImGui::BeginTabItem("ShadowPass"))
			{
				m_currentScene->m_LightController.m_shadowMapPass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("GBufferPass"))
			{
				m_pGBufferPass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("SSAOPass"))
			{
				m_pSSAOPass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("DeferredPass"))
			{
				m_pDeferredPass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("SkyBoxPass"))
			{
				m_pSkyBoxPass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("ToneMapPass"))
			{
				m_pToneMapPass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("SpritePass"))
			{
				m_pSpritePass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("BlitPass"))
			{
				m_pBlitPass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("WireFramePass"))
			{
				m_pWireFramePass->ControlPanel();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("GridPass"))
			{
				m_pGridPass->ControlPanel();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	});
}

void SceneRenderer::Update(float deltaTime)
{
	m_currentScene->Update(deltaTime);
	m_editorCamera.GetCamera()->HandleMovement(deltaTime);
	PrepareRender();
}

void SceneRenderer::Render()
{
	//[1] ShadowMapPass
	{
		Banchmark banch;
		m_currentScene->ShadowStage();
		Clear(DirectX::Colors::Transparent, 1.0f, 0);
		UnbindRenderTargets();

		std::cout << "ShadowMapPass : " << banch.GetElapsedTime() << std::endl;
	}

	//[2] GBufferPass
	{
		Banchmark banch;
		m_pGBufferPass->Execute(*m_currentScene);

		std::cout << "GBufferPass : " << banch.GetElapsedTime() << std::endl;
	}

	//[3] SSAOPass
	{
		Banchmark banch;
        m_pSSAOPass->Execute(*m_currentScene);

		std::cout << "GBufferPass : " << banch.GetElapsedTime() << std::endl;
	}

    //[4] DeferredPass
    {
		Banchmark banch;
		m_pDeferredPass->UseAmbientOcclusion(m_ambientOcclusionTexture.get());
        m_pDeferredPass->Execute(*m_currentScene);


		std::cout << "DeferredPass : " << banch.GetElapsedTime() << std::endl;
    }

	//[*] WireFramePass
	if(useWireFrame)
	{
		Banchmark banch;
		m_pWireFramePass->Execute(*m_currentScene);

		std::cout << "WireFramePass : " << banch.GetElapsedTime() << std::endl;
	}

	//[5] skyBoxPass
	{
		Banchmark banch;
		m_pSkyBoxPass->Execute(*m_currentScene);

		std::cout << "SkyBoxPass : " << banch.GetElapsedTime() << std::endl;
	}

    //[6] ToneMapPass
    {
		Banchmark banch;
        m_pToneMapPass->Execute(*m_currentScene);

		std::cout << "ToneMapPass : " << banch.GetElapsedTime() << std::endl;
    }
	
	//[*] GridPass
	{
		Banchmark banch;
		m_pGridPass->PrepareCameraType(m_editorCamera.GetCamera());
        m_pGridPass->Execute(*m_currentScene);

		std::cout << "GridPass : " << banch.GetElapsedTime() << std::endl;
	}

	//[7] SpritePass
	{
		Banchmark banch;
		m_pSpritePass->Execute(*m_currentScene);

		std::cout << "SpritePass : " << banch.GetElapsedTime() << std::endl;
	}

	//[8] BlitPass
	{
		Banchmark banch;
		m_pBlitPass->Execute(*m_currentScene);

		std::cout << "BlitPass : " << banch.GetElapsedTime() << std::endl;
	}

	m_pGBufferPass->ExecuteEditor(*m_currentScene, *m_editorCamera.GetCamera());
	m_pDeferredPass->ExecuteEditor(*m_currentScene, *m_editorCamera.GetCamera());

}

void SceneRenderer::PrepareRender()
{
	for (auto& obj : m_currentScene->m_SceneObjects)
	{
		if (!obj->m_meshRenderer.m_IsEnabled) continue;

		Material* mat = obj->m_meshRenderer.m_Material;

		switch (mat->m_renderingMode)
		{
		case Material::RenderingMode::Opaque:
			m_pGBufferPass->PushDeferredQueue(obj.get());
			break;
		case Material::RenderingMode::Transparent:
			break;
		}
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

	DirectX11::ClearDepthStencilView(
		m_depthStencilView,
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

void SceneRenderer::EditorView()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Exit"))
			{
				// Exit action
				PostQuitMessage(0);
			}
			ImGui::EndMenu();

		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Pipeline Setting"))
			{
				if (ImGui::GetContext("RenderPass").IsOpened())
				{
					ImGui::GetContext("RenderPass").Open();
				}
			}
			//if(ImGui::MenuItem("Edit Plane"))
			//{
			//	static bool isEdit = true;
			//	isEdit = !isEdit;
			//	_scene->SetEditorMode(isEdit);
			//}

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	auto obj = m_currentScene->GetSelectSceneObject();
	if (obj) 
	{
		auto mat = obj->m_transform.GetLocalMatrix();
		XMFLOAT4X4 objMat;
		XMStoreFloat4x4(&objMat, mat);
		auto view = m_currentScene->m_MainCamera.CalculateView();
		XMFLOAT4X4 floatMatrix;
		XMStoreFloat4x4(&floatMatrix, view);
		auto proj = m_currentScene->m_MainCamera.CalculateProjection();
		XMFLOAT4X4 projMatrix;
		XMStoreFloat4x4(&projMatrix, proj);

		EditTransform(&floatMatrix.m[0][0], &projMatrix.m[0][0], &objMat.m[0][0], true, obj, m_editorCamera.GetCamera());

	}
	else
	{
		auto view = m_currentScene->m_MainCamera.CalculateView();
		XMFLOAT4X4 floatMatrix;
		XMStoreFloat4x4(&floatMatrix, view);
		auto proj = m_currentScene->m_MainCamera.CalculateProjection();
		XMFLOAT4X4 projMatrix;
		XMStoreFloat4x4(&projMatrix, proj);
		XMFLOAT4X4 identityMatrix;
		XMStoreFloat4x4(&identityMatrix, XMMatrixIdentity());

		EditTransform(&floatMatrix.m[0][0], &projMatrix.m[0][0], &identityMatrix.m[0][0], false, nullptr, m_editorCamera.GetCamera());
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("GameView", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
	{
		ImVec2 size = ImGui::GetContentRegionAvail();

		float convert = DeviceState::g_aspectRatio;
		size.x = size.y * convert;

		ImGui::Image((ImTextureID)m_toneMappedColourTexture->m_pSRV, size);
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

EditorCamera::EditorCamera()
{
	m_perspacetiveEditCamera = new PerspacetiveCamera();
	m_orthographicEditCamera = new OrthographicCamera();

	ImGui::ContextRegister("Editor Camera", true, [&]()
	{
		const char* cameraTypes[] = { "Perspective Camera", "Orthographic Camera" };
		int cameraType = isOrthographic;  // 0: Perspective, 1: Orthographic
		ImGui::Combo("Camera Type", &cameraType, cameraTypes, 2);
		isOrthographic = cameraType;  // 선택된 값 업데이트

		PerspacetiveCamera* perspacetiveCamera = reinterpret_cast<PerspacetiveCamera*>(m_perspacetiveEditCamera);
		OrthographicCamera* orthographicCamera = reinterpret_cast<OrthographicCamera*>(m_orthographicEditCamera);

		if (isOrthographic)
		{
			ImGui::DragFloat("View Width", &orthographicCamera->m_viewWidth, 0.1f, 0.1f, 100.f);
			ImGui::DragFloat("View Height", &orthographicCamera->m_viewHeight, 0.1f, 0.1f, 100.f);
			ImGui::DragFloat("Near Plane", &orthographicCamera->m_nearPlane, 0.1f, 0.1f, 100.f);
			ImGui::DragFloat("Far Plane", &orthographicCamera->m_farPlane, 1.f, 1.f, 100000.f);
			ImGui::DragFloat("Speed", &orthographicCamera->m_speed, 1.f, 1.f, 100.f);
			ImGui::DragFloat("Pitch", &orthographicCamera->m_pitch, 0.01f, -pi, pi);
			ImGui::DragFloat("Yaw", &orthographicCamera->m_yaw, 0.01f, -pi, pi);
			ImGui::DragFloat("Roll", &orthographicCamera->m_roll, 0.01f, -pi, pi);

			ImGui::Text("Eye Position");
			ImGui::DragFloat3("##Eye Position", &orthographicCamera->m_eyePosition.m128_f32[0], -1000, 1000);
			ImGui::Text("Forward");
			ImGui::DragFloat3("##Forward", &orthographicCamera->m_forward.m128_f32[0], -1000, 1000);
			ImGui::Text("Right");
			ImGui::DragFloat3("##Right", &orthographicCamera->m_right.m128_f32[0], -1000, 1000);
		}
		else
		{
			ImGui::DragFloat("FOV", &perspacetiveCamera->m_fov, 1.f, 1.f, 179.f);
			ImGui::DragFloat("Aspect Ratio", &perspacetiveCamera->m_aspectRatio, 0.1f, 0.1f, 10.f);
			ImGui::DragFloat("Near Plane", &perspacetiveCamera->m_nearPlane, 0.1f, 0.1f, 100.f);
			ImGui::DragFloat("Far Plane", &perspacetiveCamera->m_farPlane, 1.f, 1.f, 100000.f);
			ImGui::DragFloat("Speed", &perspacetiveCamera->m_speed, 1.f, 1.f, 100.f);
			ImGui::DragFloat("Pitch", &perspacetiveCamera->m_pitch, 0.01f, -pi, pi);
			ImGui::DragFloat("Yaw", &perspacetiveCamera->m_yaw, 0.01f, -pi, pi);
			ImGui::DragFloat("Roll", &perspacetiveCamera->m_roll, 0.01f, -pi, pi);

			ImGui::Text("Eye Position");
			ImGui::DragFloat3("##Eye Position", &perspacetiveCamera->m_eyePosition.m128_f32[0], -1000, 1000);
			ImGui::Text("Forward");
			ImGui::DragFloat3("##Forward", &perspacetiveCamera->m_forward.m128_f32[0], -1000, 1000);
			ImGui::Text("Right");
			ImGui::DragFloat3("##Right", &perspacetiveCamera->m_right.m128_f32[0], -1000, 1000);
		}


	});
}
