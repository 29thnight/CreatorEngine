#pragma once
#include "resource.h"
#include "CoreWindow.h"
#include "Core.Minimal.h"
#include "GameMain.h"
#include <memory>

namespace GameBuilder
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
		LRESULT HandleCharEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);

	private:
		HWND m_hWnd{ nullptr };
		std::shared_ptr<DirectX11::DeviceResources> m_deviceResources;
		std::unique_ptr<DirectX11::GameMain> m_main;
		bool m_windowClosed{ false };
		bool m_windowVisible{ true };
	};
}