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
        HWND								m_hWnd{ nullptr };
		// DeviceResources(DX11) 멤버가 여기 있었다 (2026-08-10). 창은
		// CoreWindow가, 디바이스는 DX12 쪽이 소유한다.
		std::unique_ptr<Editor::EditorMain>	m_main;
		bool								m_windowClosed{ false };
		bool								m_windowVisible{ true };
		bool								m_isMinimized{ false };
	};
}
