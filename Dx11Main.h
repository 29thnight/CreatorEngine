#pragma once
#include "Utility_Framework/DeviceResources.h"
#include "Utility_Framework/TimeSystem.h"
#include "RenderEngine/DataSystem.h"
#include "RenderEngine/SceneRenderer.h"
#include "RenderEngine/Camera.h"
#include "RenderEngine/Scene.h"
#include "RenderEngine/ImGuiRenderer.h"
#include "RenderEngine/Model.h"
#include "BT_Editor.h"
#include <memory>
#include <future>

class Player;
namespace DirectX11
{
	class Dx11Main : public IDeviceNotify
	{
	public:
		Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources);
		~Dx11Main();
		void SceneInitialize();
		void CreateWindowSizeDependentResources();
		void Update();
		bool Render();

		void RenderThread();

		// IDeviceNotify
		virtual void OnDeviceLost() override;
		virtual void OnDeviceRestored() override;

		//전역 이벤트 초기화
		void EventInitialize();

		//전역 이벤트 실행 함수
		//void PlaySound(std::string& name);
		//화면 전환 이벤트 함수
		void LoadScene(); //씬 전환 이벤트
		//void Pause(); //일시정지 이벤트
		//void Resume(); //재개 이벤트

	private:
		std::shared_ptr<DeviceResources> m_deviceResources;
		TimeSystem m_timeSystem;
		World* m_world;
		std::shared_ptr<SceneRenderer> m_sceneRenderer;
		std::unique_ptr<ImGuiRenderer> m_imguiRenderer;

		std::thread m_renderThread;

		//std::unique_ptr<Scene> m_scene;
		BT_Editor m_btEditor;
		bool m_isGameView = false;
		std::thread m_loadingScene;
		std::mutex m_mutex;
		std::atomic_bool m_isLoading = false;
		std::atomic_bool m_isChangeScene = false;

		bool m_isSelectUI = false;
		bool m_isSelectText = false;

		int loadlevel;
	};
}
