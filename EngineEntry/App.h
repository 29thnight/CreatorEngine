#pragma once

#include "resource.h"
#include "CoreWindow.h"
#include "Core.Minimal.h"
#include "Dx11Main.h"
#include "ProjectSetting.h"
#include <memory>

namespace Core
{
	class App final
	{
	public:
		App() = default;
		~App() = default;
		void Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height);
		void Finalize();
		void SetWindow(CoreWindow& coreWindow);
        void RegisterHandler(CoreWindow& coreWindow);
		void Load();
		void Run();
		LRESULT Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT ProcessRawInput(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT ImGuiKeyDownHandler(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT ImGuiKeyUpHandler(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleCharEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleSettingWindowEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleDropFileEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);

	private:
        HWND m_hWnd{ nullptr };
		std::shared_ptr<DirectX11::DeviceResources> m_deviceResources;
		std::unique_ptr<DirectX11::Dx11Main> m_main;
		std::unique_ptr<ProjectSetting> m_projectSetting;
		bool m_windowClosed{ false };
		bool m_windowVisible{ true };
	};
}
