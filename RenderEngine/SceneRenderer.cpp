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
		if (ImGui::IsKeyPressed(ImGuiKey_R))
			mCurrentGizmoOperation = ImGuizmo::ROTATE;
		if (ImGui::IsKeyPressed(ImGuiKey_G)) // r Key
			mCurrentGizmoOperation = ImGuizmo::SCALE;
		if (ImGui::IsKeyPressed(ImGuiKey_F))
			useSnap = !useSnap;
	}

	ImGuiIO& io = ImGui::GetIO();
	float viewManipulateRight = io.DisplaySize.x;
	float viewManipulateTop = 0;
	static ImGuiWindowFlags gizmoWindowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	if (useWindow)
	{
		ImGui::PushStyleColor(ImGuiCol_WindowBg, (ImVec4)ImColor(0.f, 0.f, 0.f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		ImGui::Begin("Gizmo", 0, gizmoWindowFlags);
		ImGuizmo::SetDrawlist();

		float windowWidth = (float)ImGui::GetWindowWidth();
		float windowHeight = (float)ImGui::GetWindowHeight();
		ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, windowWidth, windowHeight);
		viewManipulateRight = ImGui::GetWindowPos().x + windowWidth;
		viewManipulateTop = ImGui::GetWindowPos().y;
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		gizmoWindowFlags |= ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(window->InnerRect.Min, window->InnerRect.Max) ? ImGuiWindowFlags_NoMove : 0;

		float x = windowWidth;//window->InnerRect.Max.x - window->InnerRect.Min.x;
		float y = windowHeight;//window->InnerRect.Max.y - window->InnerRect.Min.y;

        ImGui::Button("Test");
		ImGui::Image((ImTextureID)cam->m_renderTarget->m_pSRV, ImVec2(x, y));

        ImVec2 imagePos = ImGui::GetItemRectMin();
        ImGui::SetCursorScreenPos(ImVec2(imagePos.x + 50, imagePos.y + 50));

        // 인비저블 버튼을 생성하여 이미지 위에 겹쳐 놓습니다.
        if (ImGui::Button("overlay", ImVec2(50, 30))) {
            // 버튼 클릭 시 처리할 내용을 여기에 작성합니다.
        }

		ImGui::PopStyleVar(2);
	}
	else
	{
		ImGuizmo::SetRect(0, 0, io.DisplaySize.x, io.DisplaySize.y);
	}

	if (obj)
	{
		// 기즈모로 변환 후 오브젝트에 적용.
		ImGuizmo::Manipulate(cameraView, cameraProjection, mCurrentGizmoOperation, mCurrentGizmoMode, matrix, NULL, useSnap ? &snap[0] : NULL, boundSizing ? bounds : NULL, boundSizingSnap ? boundsSnap : NULL);

		auto parentMat = m_currentScene->GetSceneObject(obj->m_parentIndex)->m_transform.GetWorldMatrix();
		XMMATRIX parentWorldInverse = XMMatrixInverse(nullptr, parentMat);

		XMMATRIX newLocalMatrix = XMMatrixMultiply(XMMATRIX(matrix), parentWorldInverse);
		obj->m_transform.SetLocalMatrix(newLocalMatrix);
	}

	ImGuizmo::ViewManipulate(cameraView, camDistance, ImVec2(viewManipulateRight - 128, viewManipulateTop + 30), ImVec2(128, 128), 0x10101010);

	{
		// 기즈모로 변환된 카메라 위치, 회전 적용
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
    InitializeDeviceState();

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

	m_pEditorCamera = std::make_unique<Camera>();
	m_pEditorCamera->RegisterContainer();
	m_pEditorCamera->m_applyRenderPipelinePass.m_GridPass = true;

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
        m_diffuseTexture.get(),
        m_metalRoughTexture.get(),
        m_normalTexture.get(),
        m_emissiveTexture.get()
    );

	//skyBoxPass
	m_pSkyBoxPass = std::make_unique<SkyBoxPass>();
	m_pSkyBoxPass->Initialize(PathFinder::Relative("HDR/rosendal_park_sunset_puresky_4k.hdr").string());
	

	//toneMapPass
	m_pToneMapPass = std::make_unique<ToneMapPass>();
	m_pToneMapPass->Initialize(
		m_toneMappedColourTexture.get()
	);

	//spritePass
	m_pSpritePass = std::make_unique<SpritePass>();
	m_pSpritePass->Initialize(m_toneMappedColourTexture.get());

	//blitPass
	m_pBlitPass = std::make_unique<BlitPass>();
	m_pBlitPass->Initialize(m_deviceResources->GetBackBufferRenderTargetView());

	//WireFramePass
	m_pWireFramePass = std::make_unique<WireFramePass>();

	//GridPass
    m_pGridPass = std::make_unique<GridPass>();
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
}

void SceneRenderer::InitializeImGui()
{
    static int lightIndex = 0;

    ImGui::ContextRegister("Light", true, [&]()
    {
        ImGui::Text("Light Index : %d", lightIndex);
        if (ImGui::Button("Add Light")) {
            Light light;
            light.m_color = XMFLOAT4(1, 1, 1, 1);
            light.m_position = XMFLOAT4(0, 0, 0, 0);
            light.m_lightType = LightType::PointLight;

            m_currentScene->m_LightController.AddLight(light);
        }
        if (ImGui::Button("Light index + ")) {
            lightIndex++;
            if (lightIndex >= MAX_LIGHTS) lightIndex = MAX_LIGHTS - 1;
        }
        if (ImGui::Button("Light index - ")) {
            lightIndex--;
            if (lightIndex < 0) lightIndex = 0;
        }
        if (ImGui::Button("Light On")) {
            m_currentScene->m_LightController.GetLight(lightIndex).m_lightStatus = LightStatus::Enabled;
        }
        if (ImGui::Button("Light Off")) {
            m_currentScene->m_LightController.GetLight(lightIndex).m_lightStatus = LightStatus::Disabled;
        }

        ImGui::SliderFloat3("Light Pos", &m_currentScene->m_LightController.GetLight(lightIndex).m_position.x, -10, 10);
        ImGui::SliderFloat3("Light Dir", &m_currentScene->m_LightController.GetLight(lightIndex).m_direction.x, -10, 10);
        ImGui::SliderFloat("Light colorX", &m_currentScene->m_LightController.GetLight(lightIndex).m_color.x, 0, 1);
        ImGui::SliderFloat("Light colorY", &m_currentScene->m_LightController.GetLight(lightIndex).m_color.y, 0, 1);
        ImGui::SliderFloat("Light colorZ", &m_currentScene->m_LightController.GetLight(lightIndex).m_color.z, 0, 1);

    });


    ImGui::ContextRegister("EditCamera", true, [&]()
    {
        ImGui::Checkbox("Camera Type", &m_pEditorCamera->m_isOrthographic);
        ImGui::DragFloat("Camera Speed", &m_pEditorCamera->m_speed, 0.1f, 0.1f, 100.0f);
        ImGui::DragFloat("Camera FOV", &m_pEditorCamera->m_fov, 0.1f, 0.1f, 100.0f);
        ImGui::DragFloat("Camera Near", &m_pEditorCamera->m_nearPlane, 0.1f, 0.1f, 100.0f);
        ImGui::DragFloat("Camera Far", &m_pEditorCamera->m_farPlane, 0.1f, 0.1f, 10000.0f);

        if (m_pEditorCamera->m_isOrthographic)
        {
            ImGui::DragFloat("Orthographic Width", &m_pEditorCamera->m_viewWidth, 0.1f, 0.1f, 1000.0f);
            ImGui::DragFloat("Orthographic Height", &m_pEditorCamera->m_viewHeight, 0.1f, 0.1f, 1000.0f);
        }
        else
        {
        }
    });

}

void SceneRenderer::InitializeTextures()
{
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

		auto lightColour = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		
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

		m_currentScene->EditorSceneObjectHierarchy();
		m_currentScene->EditorSceneObjectInspector();

		ShadowMapRenderDesc desc;
		desc.m_eyePosition = XMLoadFloat4(&(m_currentScene->m_LightController.GetLight(0).m_direction)) * -5.f;
		desc.m_lookAt = XMVectorSet(0, 0, 0, 1);
		desc.m_viewWidth = 16;
		desc.m_viewHeight = 12;
		desc.m_nearPlane = 0.1f;
		desc.m_farPlane = 1000.f;
		desc.m_textureWidth = 8192;
		desc.m_textureHeight = 8192;

		m_currentScene->m_LightController.Initialize();
		m_currentScene->m_LightController.SetLightWithShadows(0, desc);

		model = Model::LoadModel("bangbooExport.fbx");
		//model = Model::LoadModel("Link_SwordAnimation.fbx");
		Model::LoadModelToScene(model, *m_currentScene);

		ImGui::ContextRegister("Test Add Model", true, [&]()
		{
			if (ImGui::Button("Add Model"))
			{
				Model::LoadModelToScene(model, *m_currentScene);
			}
		});

		//model = Model::LoadModel("bangbooExport.fbx");
		//model = Model::LoadModel("Link_SwordAnimation.fbx");
	}
	else
	{
		m_currentScene = _pScene;
	}

	m_currentScene->SetBuffers(m_ModelBuffer.Get());

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
	m_pEditorCamera->HandleMovement(deltaTime);
	PrepareRender();
}

void SceneRenderer::Render()
{
	for(auto& camera : CameraManagement->m_cameras)
	{
		if (nullptr == camera) continue;
		//[1] ShadowMapPass
		{
			//Banchmark banch;
			camera->ClearRenderTarget();
			m_currentScene->ShadowStage(*camera);
			Clear(DirectX::Colors::Transparent, 1.0f, 0);
			UnbindRenderTargets();

			//std::cout << "ShadowMapPass : " << banch.GetElapsedTime() << std::endl;
		}

		//[2] GBufferPass
		{
			//Banchmark banch;
			m_pGBufferPass->Execute(*m_currentScene, *camera);

			//std::cout << "GBufferPass : " << banch.GetElapsedTime() << std::endl;
		}

		//[3] SSAOPass
		{
			//Banchmark banch;
			m_pSSAOPass->Execute(*m_currentScene, *camera);

			//std::cout << "GBufferPass : " << banch.GetElapsedTime() << std::endl;
		}

		//[4] DeferredPass
		{
			//Banchmark banch;
			m_pDeferredPass->UseAmbientOcclusion(m_ambientOcclusionTexture.get());
			m_pDeferredPass->Execute(*m_currentScene, *camera);


			//std::cout << "DeferredPass : " << banch.GetElapsedTime() << std::endl;
		}

		//[*] WireFramePass
		if (useWireFrame)
		{
			//Banchmark banch;
			m_pWireFramePass->Execute(*m_currentScene, *camera);

			//std::cout << "WireFramePass : " << banch.GetElapsedTime() << std::endl;
		}

		//[5] skyBoxPass
		{
			//Banchmark banch;
			m_pSkyBoxPass->Execute(*m_currentScene, *camera);

			//std::cout << "SkyBoxPass : " << banch.GetElapsedTime() << std::endl;
		}

		//[6] ToneMapPass
		{
			//Banchmark banch;
			m_pToneMapPass->Execute(*m_currentScene, *camera);

			//std::cout << "ToneMapPass : " << banch.GetElapsedTime() << std::endl;
		}

		//[*] GridPass
		{
			//Banchmark banch;
			m_pGridPass->Execute(*m_currentScene, *camera);

			//std::cout << "GridPass : " << banch.GetElapsedTime() << std::endl;
		}

		//[7] SpritePass
		{
			//Banchmark banch;
			m_pSpritePass->Execute(*m_currentScene, *camera);

			//std::cout << "SpritePass : " << banch.GetElapsedTime() << std::endl;
		}

		//[8] BlitPass
		{
			//Banchmark banch;
			m_pBlitPass->Execute(*m_currentScene, *camera);

			//std::cout << "BlitPass : " << banch.GetElapsedTime() << std::endl;
		}
	}

	m_pGBufferPass->ClearDeferredQueue();


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
    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    float height = ImGui::GetFrameHeight();

    if (ImGui::BeginViewportSideBar("##MainMenuBar", viewport, ImGuiDir_Up, height, window_flags))
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
                    if (!ImGui::GetContext("RenderPass").IsOpened())
                    {
                        ImGui::GetContext("RenderPass").Open();
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }
        ImGui::End();
    }

    if (ImGui::BeginViewportSideBar("##MainStatusBar", viewport, ImGuiDir_Down, height, window_flags)) {
        if (ImGui::BeginMenuBar()) {
            ImGui::Text("Happy status bar");
            ImGui::EndMenuBar();
        }
        ImGui::End();
    }

	auto obj = m_currentScene->GetSelectSceneObject();
	if (obj) 
	{
		auto mat = obj->m_transform.GetWorldMatrix();
		XMFLOAT4X4 objMat;
		XMStoreFloat4x4(&objMat, mat);
		auto view = m_pEditorCamera->CalculateView();
		XMFLOAT4X4 floatMatrix;
		XMStoreFloat4x4(&floatMatrix, view);
		auto proj = m_pEditorCamera->CalculateProjection();
		XMFLOAT4X4 projMatrix;
		XMStoreFloat4x4(&projMatrix, proj);

		EditTransform(&floatMatrix.m[0][0], &projMatrix.m[0][0], &objMat.m[0][0], true, obj, m_pEditorCamera.get());

	}
	else
	{
		auto view = m_pEditorCamera->CalculateView();
		XMFLOAT4X4 floatMatrix;
		XMStoreFloat4x4(&floatMatrix, view);
		auto proj = m_pEditorCamera->CalculateProjection();
		XMFLOAT4X4 projMatrix;
		XMStoreFloat4x4(&projMatrix, proj);
		XMFLOAT4X4 identityMatrix;
		XMStoreFloat4x4(&identityMatrix, XMMatrixIdentity());

		EditTransform(&floatMatrix.m[0][0], &projMatrix.m[0][0], &identityMatrix.m[0][0], false, nullptr, m_pEditorCamera.get());
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("GameView", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
	{
		ImVec2 size = ImGui::GetContentRegionAvail();

		float convert = DeviceState::g_aspectRatio;
		size.x = size.y * convert;

		ImGui::Image((ImTextureID)m_currentScene->m_MainCamera.m_renderTarget->m_pSRV, size);
	}
	ImGui::End();
	ImGui::PopStyleVar();
}
