#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#include "../Tools/build/ModuleAdapters/Creator.Core.Window.h"
#else
import "Creator.WinAPI.h";
import Creator.Core.Window;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../Tools/build/ModuleAdapters/Creator.AppBackend.WinAPI.h"
#else
import Creator.AppBackend.WinAPI;
#endif
#include "PlayerMain.h"

#include "../Engine/Utility_Framework/Foundation/StandardLibrary.h"

// 게임 플레이어의 앱 셸 (BuildPipelinePlan B0-2) — 창을 세우고, 프레젠트
// 소유권을 정하고, 메인 루프를 건다. 에디터의 Core::App에서 진행 창 ·
// editor UI와 입력 중계를 뺀 형태다.
namespace Player
{
	class App
	{
	public:
		void Initialize(AppBackendWinAPI& coreWindow, CoreWindow& window);
		void Finalize();

	private:
		// SetWindow가 여기 있었다 (2026-08-10) — 프레젠트 소유권을 못박고
		// DX11 DeviceResources에 창을 붙이던 자리다. 그 디바이스가 사라져
		// 남은 일은 셸 설정 한 줄뿐이라 Initialize로 접었다.
		void RegisterHandler(AppBackendWinAPI& coreWindow);
		void Load();
		void Run(CoreWindow& window);

		LRESULT Shutdown(HWND hWnd, WPARAM wParam, LPARAM lParam);
		LRESULT HandleResizeEvent(HWND hWnd, WPARAM wParam, LPARAM lParam);

	private:
		HWND m_hWnd{ nullptr };
		std::unique_ptr<PlayerMain> m_main;
	};
}
