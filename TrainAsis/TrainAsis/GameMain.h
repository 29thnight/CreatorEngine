#pragma once
#include "DeviceResources.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneRenderer.h"
#include "Scene.h"
#include "ImGuiRenderer.h"
#include "Model.h"
#include "Delegate.h"

#include <memory>
#include <future>

namespace DirectX11
{
	class GameMain : public IDeviceNotify
	{
	public:
		GameMain(const std::shared_ptr<DeviceResources>& deviceResources);
		~GameMain();

		void Initialize();
		void Finalize();
		void CreateWindowSizeDependentResources();
		void Update();
		bool ExecuteRenderPass();
		void InfoWindow();
		void OnGui();
		void DisableOrEnable();

		void CommandBuildThread();
		void CommandExecuteThread();

		void InvokeResizeFlag();

		// IDeviceNotify
		virtual void OnDeviceLost() override;
		virtual void OnDeviceRestored() override;

	private:
		std::shared_ptr<DeviceResources> m_deviceResources;
		//TimeSystem m_timeSystem;
		//Renderer
		std::shared_ptr<SceneRenderer> m_sceneRenderer;
		std::unique_ptr<ImGuiRenderer> m_imguiRenderer;
		//DelegateHandle
		Core::DelegateHandle m_InputEvenetHandle;
		Core::DelegateHandle m_SceneRenderingEventHandle;
		Core::DelegateHandle m_OnGizmoEventHandle;
		Core::DelegateHandle m_GUIRenderingEventHandle;
		Core::DelegateHandle m_EndOfFrameEventHandle;

		std::thread m_CB_Thread;
		std::thread m_CE_Thread;

		//std::unique_ptr<Scene> m_scene;
		//BT_Editor m_btEditor;
		bool m_isGameView = false;
		std::atomic_bool m_isLoading = false;
		std::atomic_bool m_isChangeScene = false;
		std::atomic_bool m_isInvokeResize = false;

		bool m_isSelectUI = false;
		bool m_isSelectText = false;

	};
}