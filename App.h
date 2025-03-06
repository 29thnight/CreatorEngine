#pragma once

#include "resource.h"
#include "Utility_Framework/CoreWindow.h"
#include "Utility_Framework/Core.Minimal.h"
#include "Dx11Main.h"
#include <memory>

namespace Core
{
	class App final
	{
	public:
		App() = default;
		~App() = default;
		void Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height);
		void SetWindow(CoreWindow& coreWindow);
		void Load();
		void Run();
		LRESULT Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT ProcessRawInput(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleCharEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);

	private:
		std::shared_ptr<DirectX11::DeviceResources> m_deviceResources;
		std::unique_ptr<DirectX11::Dx11Main> m_main;
		bool m_windowClosed{ false };
		bool m_windowVisible{ true };
	};
}