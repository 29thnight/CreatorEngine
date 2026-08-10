#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "TimeSystem.h"
#include "GameObject.h"
#include "DataSystem.h"
#include "GizmoRenderer.h"
// 에디터 레이어(EngineGUIWindow, Academy_4Q Editor 필터). 구 ImGuiRenderer의
// 후계다 — 백엔드는 IImGuiHost 경계 뒤로 갔다.
#include "EditorRenderer.h"
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
#include "EnhancedRenderDebugWindow.h"

#include <atomic>
#include <memory>
#include <thread>

namespace Editor
{
	// 에디터의 메인 루프 (구 DirectX11::Dx11Main 재작성, 2026-08-10).
	//
	// ── 왜 재작성인가 ──
	//
	// 이름부터 남은 것이 없었다. Dx11Main은 DirectX11 네임스페이스에 살면서
	// DX11 디바이스를 들고 DX11 스왑체인에 Present하는 클래스였는데, 그 셋이
	// 차례로 사라졌다(DX11 렌더러 은퇴 → D2 스왑체인 이관 → D4 전역 배선 제거).
	// 남은 것은 '그 이름이 붙은 껍데기'와 그 껍데기가 끌고 다니던 잔해들이다:
	//
	//   · DeviceResources — 창 핸들 하나 때문에 들고 있었다
	//   · IDeviceNotify   — OnDeviceLost는 비어 있고 OnDeviceRestored는 리사이즈
	//                       경로를 한 번 더 부르는 것뿐이었다
	//   · InitializeDeviceState / ShutdownDeviceState — 전자는 버스 첫 값 세팅,
	//                       후자는 한 번도 대입된 적 없는 핸들을 해제했다
	//   · imgui_impl_dx11.h · Present 분기 · 죽은 멤버 넷
	//
	// ── 무엇이 바뀌었나 ──
	//
	// 창은 CoreWindow 싱글턴에서 온다. 디바이스는 없다 — 씬은
	// EnhancedSceneRenderer(DX12)가, 화면 출력은 ImGui DX12 셸이 소유한다.
	// 이 클래스가 하는 일은 셋뿐이다: 부팅 순서, 프레임 루프, 종료 순서.
	//
	// ── #ifdef EDITOR를 걷었다 ──
	//
	// 구 코드에는 EDITOR 가드가 여섯 군데 있었는데 그 매크로는 ImGuiRegister.h가
	// 무조건 켜 두는 것이라(#define EDITOR) 한 번도 꺼진 적이 없다. 게다가 이
	// 파일은 에디터 exe에만 링크되므로 가드가 표현할 대비 자체가 없다 —
	// 플레이어는 Player/PlayerMain이라는 별개의 루프를 쓴다.
	class EditorMain
	{
	public:
		EditorMain();
		~EditorMain();

		void Initialize();
		void Finalize();

		/// 게임 스레드의 한 프레임. 두 번의 랑데뷰로 CB·CE 스레드와 만난다.
		void Update();

		/// WM_SIZE가 세우는 깃발. 실제 리사이즈는 CE 스레드가 프레임 경계에서 한다.
		void InvokeResizeFlag();

	private:
		void TickScripts(float deltaTime);
		bool ExecuteRenderPass();
		void UpdateTitleBar();
		void OnGui();
		void HandleWindowResize();
		void CommandBuildThread();
		void CommandExecuteThread();

		std::shared_ptr<GizmoRenderer>             m_gizmoRenderer;
		std::unique_ptr<EditorRenderer>            m_editorRenderer;

		// 에디터 창들
		std::unique_ptr<SceneViewWindow>           m_sceneViewWindow;
		std::unique_ptr<GameViewWindow>            m_gameViewWindow;
		std::unique_ptr<MenuBarWindow>             m_menuBarWindow;
		std::unique_ptr<HierarchyWindow>           m_hierarchyWindow;
		std::unique_ptr<InspectorWindow>           m_inspectorWindow;
		std::unique_ptr<AssetBundleWindow>         m_projectWindow;
		std::unique_ptr<ResourceCounterWindow>     m_resourceCounterWindow;
		// "RenderPass" 컨텍스트의 소유자. 이 객체가 없으면 Settings >
		// Pipeline Setting이 등록되지 않은 이름을 열어 아무 창도 뜨지 않는다.
		std::unique_ptr<EnhancedRenderDebugWindow> m_renderDebugWindow;

		Core::DelegateHandle m_inputEventHandle;
		Core::DelegateHandle m_newSceneCreatedHandle;
		Core::DelegateHandle m_activeSceneChangedHandle;
		Core::DelegateHandle m_guiRenderingEventHandle;

		std::thread m_commandBuildThread;
		std::thread m_commandExecuteThread;

		std::atomic_bool m_isInvokeResize{ false };
	};
}
#endif // !DYNAMICCPP_EXPORTS
