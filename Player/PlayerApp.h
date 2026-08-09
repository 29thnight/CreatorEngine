#pragma once
#include "CoreWindow.h"
#include "DeviceResources.h"
#include "PlayerMain.h"

#include <memory>

// 게임 플레이어의 앱 셸 (BuildPipelinePlan B0-2) — 창을 세우고, 프레젠트
// 소유권을 정하고, 메인 루프를 건다. 에디터의 Core::App에서 진행 창 ·
// CLI · 드래그 앤 드롭 · ImGui 입력 중계를 뺀 형태다.
namespace Player
{
	class App
	{
	public:
		void Initialize(HINSTANCE hInstance, const wchar_t* title, int width, int height);
		void Finalize();

	private:
		void SetWindow(CoreWindow& coreWindow);
		void RegisterHandler(CoreWindow& coreWindow);
		void Load();
		void Run();

		LRESULT Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);

	private:
		HWND m_hWnd{ nullptr };
		std::shared_ptr<DirectX11::DeviceResources> m_deviceResources;
		std::unique_ptr<PlayerMain> m_main;
	};
}
