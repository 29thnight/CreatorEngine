#pragma once
#include "Utility_Framework/DeviceResources.h"
#include "Utility_Framework/TimeSystem.h"
#include "RenderEngine/DataSystem.h"
#include "RenderEngine/SceneRenderer.h"
#include "ScriptBinder/Scene.h"
#include "RenderEngine/ImGuiRenderer.h"
#include "RenderEngine/Model.h"
#include <memory>
#include <future>

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
        void OnGui();
		void SceneFinalize();

		// IDeviceNotify
		virtual void OnDeviceLost() override;
		virtual void OnDeviceRestored() override;

	private:
		std::shared_ptr<DeviceResources> m_deviceResources;
		TimeSystem m_timeSystem;
		std::shared_ptr<SceneRenderer> m_sceneRenderer;
		std::unique_ptr<ImGuiRenderer> m_imguiRenderer;

		std::thread m_renderThread;

		//std::unique_ptr<Scene> m_scene;
		//BT_Editor m_btEditor;
        bool m_isGameStart = false;
		bool m_isGameView = false;
		std::atomic_bool m_isLoading = false;
		std::atomic_bool m_isChangeScene = false;

		bool m_isSelectUI = false;
		bool m_isSelectText = false;
	};
}
