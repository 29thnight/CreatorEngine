#include "SceneRenderer.h"
#include "SceneRenderer.h"
#include "DeviceState.h"
#include "AssetSystem.h"
#include "Scene.h"
#include "ImGuiRegister.h"

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
	static ImGuiWindowFlags gizmoWindowFlags = 0;
	if (useWindow)
	{
		//ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_Appearing);
		//ImGui::SetNextWindowPos(ImVec2(400, 20), ImGuiCond_Appearing);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.35f, 0.3f, 0.3f));
		ImGui::Begin("Gizmo", 0, gizmoWindowFlags);
		ImGuizmo::SetDrawlist();

		//std::cout << ImGui::GetMousePos().x <<", " << ImGui::GetMousePos().y<< std::endl;

		float windowWidth = (float)ImGui::GetWindowWidth();
		float windowHeight = (float)ImGui::GetWindowHeight();
		ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
		viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
		viewManipulateTop = ImGui::GetWindowPos().y;
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		gizmoWindowFlags = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;

		float x = window->InnerRect.Max.x - window->InnerRect.Min.x;
		float y = window->InnerRect.Max.y - window->InnerRect.Min.y;

		ImGui::Image((ImTextureID)m_toneMappedColourTexture->m_pSRV, ImVec2(x, y));
		//ImGui::Image((ImTextureID)sceneRenderer->GetMeshEditorTarget()->GetSRV(),
		//	ImVec2(450, 560));
		//ImGui::End();

		//std::cout << window->InnerRect.Min.x <<", " << window->InnerRect.Min.y << std::endl;
	}
	else
	{
		ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	}


	//ImGuizmo::DrawCubes(cameraView, cameraProjection, &objectMatrix[0][0], gizmoCount);

	{
		XMVECTOR poss;
		XMVECTOR rots;
		XMVECTOR scales;
		XMMatrixDecompose(&scales, &rots, &poss, XMMATRIX(cameraView));
		//cam->m_eyePosition = poss;
		//cam->m_rotation = rot;
	}

	XMVECTOR pos;
	XMVECTOR rot;
	XMVECTOR scale;
	XMMatrixDecompose(&scale, &rot, &pos, XMMATRIX(matrix));

	if (obj)
	{
		ImGuizmo::Manipulate(cameraView, cameraProjection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix, NULL, useSnap ? &snap[0] : NULL, boundSizing ? bounds : NULL, boundSizingSnap ? boundsSnap : NULL);
		obj->m_transform.SetPosition(pos);
		//obj->SetRotation(rot.ToEuler() * 57.295779513f);
		obj->m_transform.SetScale(scale);
	}

	ImGuizmo::ViewManipulate(cameraView, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop), ImVec2(128, 128), 0x10101010);

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

	//WireFramePass
	m_pWireFramePass = std::make_unique<WireFramePass>();
	m_pWireFramePass->SetRenderTarget(m_colorTexture.get());
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
		desc.m_textureWidth = 8192;//DeviceState::g_ClientRect.width; 
		desc.m_textureHeight = 8192;//DeviceState::g_ClientRect.height;

		m_currentScene->m_LightController.Initialize();
		m_currentScene->m_LightController.SetLightWithShadows(0, desc);

		model = Model::LoadModel("boblampclean.md5mesh");
		//model = Model::LoadModel("BoxHuman.fbx");
		Model::LoadModelToScene(model, *m_currentScene);
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
	PrepareRender();
}

void SceneRenderer::Render()
{

	//[1] ShadowMapPass
	{
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

	//[*] WireFramePass
	if(useWireFrame)
	{
		m_pWireFramePass->Execute(*m_currentScene);
	}

	//[5] skyBoxPass
	{
		m_pSkyBoxPass->Execute(*m_currentScene);
	}

    //[6] ToneMapPass
    {
        m_pToneMapPass->Execute(*m_currentScene);
    }
	
	//[*] GridPass
	{
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

		EditTransform(&floatMatrix.m[0][0], &projMatrix.m[0][0], &objMat.m[0][0], true, obj, &m_currentScene->m_MainCamera);

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

		EditTransform(&floatMatrix.m[0][0], &projMatrix.m[0][0], &identityMatrix.m[0][0], false, nullptr, &m_currentScene->m_MainCamera);
	}

	ImGui::Begin("GameView", 0, ImGuiWindowFlags_NoMove);
	{
		ImVec2 size = ImGui::GetContentRegionAvail();
		ImGui::Image((ImTextureID)m_toneMappedColourTexture->m_pSRV, size);
	}
	ImGui::End();
}
