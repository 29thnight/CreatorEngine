#include "Dx11Main.h"
#include "Utility_Framework/CoreWindow.h"
#include "InputManager.h"
#include "RenderEngine/ImGuiRegister.h"
#include "RenderEngine/UI_DataSystem.h"
#include "Physics/Common.h"
#include "SoundManager.h"
#include "MeshEditor.h"
#include "GameManager.h"
#include "GridEditor.h"
#include "RenderEngine/FontManager.h"
#include "Utility_Framework/Banchmark.hpp"

DirectX11::Dx11Main::Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources)	: m_deviceResources(deviceResources)
{
	m_deviceResources->RegisterDeviceNotify(this);

	//아래 렌더러	초기화 코드를 여기에 추가합니다.
	m_sceneRenderer = std::make_shared<SceneRenderer>(m_deviceResources);
	m_D2DRenderer = std::make_unique<D2DRenderer>(m_deviceResources);
	m_imguiRenderer = std::make_unique<ImGuiRenderer>(m_deviceResources);

    AssetsSystem2->LoadShaders();
	AssetsSystem2->Initialize();
	UISystem->Initialize(m_deviceResources);
	Sound->initialize((int)ChannelType::MaxChannel);
	FontSystem->Initialize();

	m_D2DRenderer->SetEditorComputeShader();
	m_sceneRenderer->Initialize();

	m_world = new World();

	GameManagement->Initialize();
	EventInitialize();
	SceneInitialize();
}

DirectX11::Dx11Main::~Dx11Main()
{
	m_deviceResources->RegisterDeviceNotify(nullptr);
	m_world->Finalize();
	m_btEditor.Finalize();
}
//test code
void DirectX11::Dx11Main::SceneInitialize()
{
	//m_camera = std::make_unique<PerspacetiveCamera>();
	////m_camera->SetPosition(10.0f, 140.0f, 220.0f);
	////m_camera->pitch = -40.f;
	////m_camera->yaw = 270.f;
	//
	////m_scene = std::make_unique<Scene>();
	////m_world->SetScene(m_scene.get());
	////m_world->SetCamera(m_camera.get());
	////m_world->Initialize();

	//MeshEditorSystem->sceneRenderer = m_sceneRenderer.get();
	//GameManagement->SetWorld(m_world);
	//GridEditorSystem->Initialize(m_world);
	////m_sceneRenderer->SetScene(m_scene.get());
	////m_sceneRenderer->SetCamera(m_camera.get());

	//Physics->Initialize();
	//m_btEditor.Initialize();
	//m_btEditor.SetEditorMenu(m_world);
	//m_D2DRenderer->SetCurCanvas(m_world->GetCurCanvas());
	//m_D2DRenderer->SetEditorMenu(m_world);

	//m_loadingScene = std::thread([&] 
	//{ 
	//	try
	//	{
	//		LoadScene();
	//	}
	//	catch (const std::exception& e)
	//	{
	//		std::cerr << e.what() << std::endl;
	//		std::terminate();
	//	}
	//});
	//m_loadingScene.detach();

	//GameManagement->SetGameState(GameManager::GameState::Play);
	
}

void DirectX11::Dx11Main::CreateWindowSizeDependentResources()
{
	//렌더러의 창 크기에 따라 리소스를 다시 만드는 코드를 여기에 추가합니다.
	m_deviceResources->ResizeResources();
}

void DirectX11::Dx11Main::Update()
{
	if (m_isLoading || m_isChangeScene)
	{
		m_timeSystem.Tick([&]
		{
			//렌더러의 업데이트 코드를 여기에 추가합니다.
			std::wostringstream woss;
			woss.precision(6);
			woss << L"[4Q Project] Bongsu Rabbit - "
				<< L"Width: "
				<< m_deviceResources->GetOutputSize().width
				<< L" Height: "
				<< m_deviceResources->GetOutputSize().height
				<< L" FPS: "
				<< m_timeSystem.GetFramesPerSecond()
				<< L" FrameCount: "
				<< m_timeSystem.GetFrameCount();

			SetWindowText(m_deviceResources->GetWindow()->GetHandle(), woss.str().c_str());

			Sound->update();
		});

		return;
	}

	m_world->Destroy();

	//렌더러의 업데이트 코드를 여기에 추가합니다.
	m_timeSystem.Tick([&]
	{
		m_world->FixedUpdate(m_timeSystem.GetElapsedSeconds()*GameManagement->GetTimeScale());
		if (m_world->ObjectCount() > 0) {
			Physics->PreUpdate();
			Physics->Update(m_timeSystem.GetElapsedSeconds() * GameManagement->GetTimeScale());
			Physics->PostUpdate();
		}
		//렌더러의 업데이트 코드를 여기에 추가합니다.
		std::wostringstream woss;
		woss.precision(6);
		woss << L"[4Q Project] Bongsu Rabbit - "
			<< L"Width: "
			<< m_deviceResources->GetOutputSize().width
			<< L" Height: "
			<< m_deviceResources->GetOutputSize().height
			<< L" FPS: "
			<< m_timeSystem.GetFramesPerSecond()
			<< L" FrameCount: "
			<< m_timeSystem.GetFrameCount();

		SetWindowText(m_deviceResources->GetWindow()->GetHandle(), woss.str().c_str());
		//렌더러의 업데이트 코드를 여기에 추가합니다.
		m_sceneRenderer->Update(m_timeSystem.GetElapsedSeconds());
		m_world->Update((float)m_timeSystem.GetElapsedSeconds());
		m_D2DRenderer->SetCurCanvas(m_world->GetCurCanvas());
		GameManagement->PlayUpdate((float)m_timeSystem.GetElapsedSeconds() * GameManagement->GetTimeScale()); //게임 시스템 업데이트
		m_world->LateUpdate((float)m_timeSystem.GetElapsedSeconds() * GameManagement->GetTimeScale());
	
		Sound->update();
		InputManagement->UpdateControllerVibration(m_timeSystem.GetElapsedSeconds() * GameManagement->GetTimeScale()); //패드 진동 업데이트
	});

	if (InputManagement->IsKeyReleased(VK_F5))
	{
		m_isGameView = !m_isGameView;
	}
#ifdef EDITOR
	if (InputManagement->IsKeyReleased(VK_F6))
	{
		loadlevel = 0;
		Event->Publish("ChangeScene", &loadlevel);
	}
	if (InputManagement->IsKeyDown(VK_F11)) {
		m_sceneRenderer->SetWireFrame();
	}
#endif // !EDITOR
	if (InputManagement->IsKeyReleased(VK_F8))
	{
		loadlevel++;
		Event->Publish("ChangeScene", &loadlevel);
		if (loadlevel > 2)
		{
			loadlevel = 0;
		}
	}
	if (InputManagement->IsKeyReleased(VK_F9)) {
		Physics->ConnectPVD();
	}

#if defined(EDITOR)
#endif // !Editor
	m_world->Destroy();
}

bool DirectX11::Dx11Main::Render()
{
	// 처음 업데이트하기 전에 아무 것도 렌더링하지 마세요.
	if (m_timeSystem.GetFrameCount() == 0)
	{
		return false;
	}

	GameManager::GameState state = GameManagement->GetGameState();
	if (state == GameManager::GameState::Loading)
	{
	/*	m_D2DRenderer->LoadSceneRender(loadlevel);*/
	}
	else
	{
		//m_sceneRenderer->StagePrepare();
		//m_sceneRenderer->UpdateDrawModel();
		//m_sceneRenderer->StageDrawModels();
		//m_sceneRenderer->EndStage();
		//m_sceneRenderer->StageBillboardsPrepare();
		//m_sceneRenderer->UpdateDrawBillboards();
		//m_sceneRenderer->StageDrawBillboards();

		m_sceneRenderer->Render();

		//m_D2DRenderer->UpdateDrawModel();
		//m_D2DRenderer->Render();
		//m_D2DRenderer->SetCanvasEditorStage();
		//m_D2DRenderer->ComputeCanvasEditorStage();
	}

#if defined(EDITOR)
	if(!m_isGameView)
	{
		m_imguiRenderer->BeginRender();
		//m_D2DRenderer->ImGuiRenderStage();
		m_imguiRenderer->Render();
		//MeshEditorSystem->ShowMainUI();
		//m_btEditor.ShowMainUI();
		//GridEditorSystem->ShowGridEditor();
		m_imguiRenderer->EndRender();
	}
#endif // !EDITOR
	
	InputManagement->Update();

	return true;
}

// 릴리스가 필요한 디바이스 리소스를 렌더러에 알립니다.
void DirectX11::Dx11Main::OnDeviceLost()
{

}

// 디바이스 리소스가 이제 다시 만들어질 수 있음을 렌더러에 알립니다.
void DirectX11::Dx11Main::OnDeviceRestored()
{
	CreateWindowSizeDependentResources();
}

void DirectX11::Dx11Main::EventInitialize()
{
}

void DirectX11::Dx11Main::LoadScene()
{
	while(true)
	{
		//if (!m_isChangeScene)
		//{
		//	std::this_thread::sleep_for(std::chrono::milliseconds(100));
		//	continue;
		//}
		//else
		//{
		//	GameManagement->SetGameState(GameManager::GameState::Loading);
		//	std::string name = GameManagement->GetStageName((GameManager::GameLevel)loadlevel);

		//	if (0 == loadlevel)
		//	{

		//	}

		//	m_world->UnRegisterImGui();

		//	World* preloadWorld = new World();
		//	std::unique_ptr<Scene> newScene = std::make_unique<Scene>();

		//	//Physics->ClearActors();
		//	preloadWorld->SetScene(newScene.get());
		//	preloadWorld->SetCamera(m_camera.get());
		//	preloadWorld->Initialize();

		//	GameManagement->SetWorld(preloadWorld);
		//	GridEditorSystem->Initialize(preloadWorld);


		//	m_isLoading = preloadWorld->LoadJson(PathFinder::Relative("Scene\\").string() + name);

		//	std::this_thread::sleep_for(std::chrono::seconds(5));
		//	//화면 스왑
		//	if (m_isLoading)
		//	{
		//		{
		//			std::unique_lock lock(m_mutex);
		//			std::swap(m_world, preloadWorld);
		//			std::swap(m_scene, newScene);
		//		}

		//		//m_sceneRenderer->SetScene(m_scene.get());
		//		m_btEditor.SetEditorMenu(m_world);
		//		m_D2DRenderer->SetEditorMenu(m_world);

		//		preloadWorld->Finalize();
		//		Physics->GetPxScene()->flushSimulation();
		//		delete preloadWorld;
		//		m_isLoading = false;
		//		m_isChangeScene = false;
		//		GameManagement->SetGameState(GameManager::GameState::Play);
		//		//SceneInitialize();
		//	}
		//	else
		//	{
		//		//로딩 실패시
		//		//todo : 로딩 실패시 코드
		//		Log::Error("Scene Load Fail");
		//		throw std::exception("Scene Load Fail");
		//	}
		//}
	}
}

//void DirectX11::Dx11Main::Pause()
//{
//	//todo : 일시정지 코드
//}
//
//void DirectX11::Dx11Main::Resume()
//{
//	//todo : 재개 코드
//}

//void DirectX11::Dx11Main::PlaySound(std::string& name)
//{
	//사운드 재생 코드
	//ex : Sound->playSound(name, 0);
//}




