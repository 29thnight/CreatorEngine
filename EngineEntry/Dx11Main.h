#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "DeviceResources.h"
#include "TimeSystem.h"
#include "DataSystem.h"
#include "SceneRenderer.h"
#include "GizmoRenderer.h"
#include "Scene.h"
#include "ImGuiRenderer.h"
#include "Model.h"
#include "Delegate.h"

#include "RenderPassWindow.h"
#include "SceneViewWindow.h"
#include "GameViewWindow.h"
#include "MenuBarWindow.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "ProgressWindow.h"

#include <memory>
#include <future>

namespace DirectX11
{
	class Dx11Main : public IDeviceNotify
	{
	public:
		Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources);
		~Dx11Main();

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
		TimeSystem m_timeSystem;
		//Renderer
		std::shared_ptr<SceneRenderer> m_sceneRenderer;
		std::shared_ptr<GizmoRenderer> m_gizmoRenderer;
		std::unique_ptr<ImGuiRenderer> m_imguiRenderer;
		//Engine GUI
		std::unique_ptr<RenderPassWindow> m_renderPassWindow;
		std::unique_ptr<SceneViewWindow> m_sceneViewWindow;
		std::unique_ptr<GameViewWindow> m_gameViewWindow;
		std::unique_ptr<MenuBarWindow> m_menuBarWindow;
		std::unique_ptr<HierarchyWindow> m_hierarchyWindow;
		std::unique_ptr<InspectorWindow> m_inspectorWindow;
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
#endif // !DYNAMICCPP_EXPORTS