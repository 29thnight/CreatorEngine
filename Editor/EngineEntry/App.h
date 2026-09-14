#pragma once
#include "resource.h"
#include "CoreWindow.h"
#include "Core.Minimal.h"
#include "EditorMain.h"
#include <memory>

namespace Core
{
	class App final : public Noncopyable
	{
	public:
		App() = default;
		~App() = default;
		//App Func
		void Initialize(CoreWindow& coreWindow);
		void Finalize();
		void SetWindow(CoreWindow& coreWindow);
        void RegisterHandler(CoreWindow& coreWindow);
		void Load();
		void Run();
		//Window Event Func
		LRESULT Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT ProcessRawInput(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT ImGuiKeyDownHandler(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT ImGuiKeyUpHandler(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleCharEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleMaximizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleSettingWindowEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleDropFileEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);

	private:
		/// 이번 상태를 밀봉해 RenderThread에 발행한다. 반환은 넘긴 뷰의 수.
		uint32_t							PublishRenderFrame();
		/// 첫 그림이 설 때까지 프레임을 돌린다(창을 보여주기 전에만).
		void								WarmUpFirstRenderedFrame();

        HWND								m_hWnd{ nullptr };
		// DeviceResources(DX11) 멤버가 여기 있었다 (2026-08-10). 창은
		// CoreWindow가, 디바이스는 DX12 쪽이 소유한다.
		std::unique_ptr<Editor::EditorMain>	m_main;
		bool								m_windowClosed{ false };
		bool								m_windowVisible{ true };
		bool								m_isMinimized{ false };
	};
}
