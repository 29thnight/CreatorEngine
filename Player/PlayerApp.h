#pragma once
#include "CoreWindow.h"
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
		// SetWindow가 여기 있었다 (2026-08-10) — 프레젠트 소유권을 못박고
		// DX11 DeviceResources에 창을 붙이던 자리다. 그 디바이스가 사라져
		// 남은 일은 셸 설정 한 줄뿐이라 Initialize로 접었다.
		void RegisterHandler(CoreWindow& coreWindow);
		void Load();
		void Run();

		LRESULT Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);

	private:
		HWND m_hWnd{ nullptr };
		std::unique_ptr<PlayerMain> m_main;
	};
}
