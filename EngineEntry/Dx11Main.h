#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "DeviceResources.h"
#include "TimeSystem.h"
#include "GameObject.h"
#include "DataSystem.h"
#include "GizmoRenderer.h"
#include "ImGuiRenderer.h"
#include "Model.h"
#include "Delegate.h"

#include "SceneViewWindow.h"
#include "GameViewWindow.h"
#include "MenuBarWindow.h"
#include "HierarchyWindow.h"
#include "InspectorWindow.h"
#include "ProgressWindow.h"
#include "AssetBundleWindow.h"
#include "ResourceCounterWindow.h"

#include <memory>
#include <future>

namespace DirectX11
{
	class Dx11Main : public IDeviceNotify
	{
	public:
		Dx11Main(const std::shared_ptr<DeviceResources>& deviceResources);
		~Dx11Main();
		//Game ControlLoop Func
		void Initialize();
		void Finalize();
		void CreateWindowSizeDependentResources();
		void Update();
		void TickScripts(float deltaTime);
		bool ExecuteRenderPass();
        void InfoWindow();
        void OnGui();
		void DisableOrEnable();
		//RenderPipeline Func
		void CommandBuildThread();
		void CommandExecuteThread();

		void InvokeResizeFlag();

		// IDeviceNotify
		virtual void OnDeviceLost() override;
		virtual void OnDeviceRestored() override;

	private:
		//Dx11 Helper
		std::shared_ptr<DeviceResources> m_deviceResources;
		//TimeSystem m_timeSystem;
		// DX11은 에디터 셸/자산 브리지로만 유지한다. 씬 렌더러는
		// EnhancedSceneRenderer 하나다.
		std::shared_ptr<GizmoRenderer> m_gizmoRenderer;
		std::unique_ptr<ImGuiRenderer> m_imguiRenderer;
		//Engine GUI
		std::unique_ptr<SceneViewWindow> m_sceneViewWindow;
		std::unique_ptr<GameViewWindow> m_gameViewWindow;
		std::unique_ptr<MenuBarWindow> m_menuBarWindow;
		std::unique_ptr<HierarchyWindow> m_hierarchyWindow;
		std::unique_ptr<InspectorWindow> m_inspectorWindow;
		std::unique_ptr<AssetBundleWindow> m_projectWindow;
		std::unique_ptr<ResourceCounterWindow> m_resourceCounterWindow;
		//DelegateHandle
        Core::DelegateHandle m_InputEvenetHandle;
        Core::DelegateHandle m_newSceneCreatedHandle;
        Core::DelegateHandle m_activeSceneChangedHandle;
        Core::DelegateHandle m_GUIRenderingEventHandle;
		Core::DelegateHandle m_resizeEventHandle;
		//RenderPipeLine Thread
		std::thread m_CB_Thread;
		std::thread m_CE_Thread;
		//Thread control flag
		bool m_isGameView = false;
		std::atomic_bool m_isLoading = false;
		std::atomic_bool m_isChangeScene = false;
		std::atomic_bool m_isInvokeResize = false;

		void InitializeDeviceState();
		void ShutdownDeviceState();

	};
}
#endif // !DYNAMICCPP_EXPORTS
