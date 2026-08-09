#pragma once
#include "DeviceResources.h"
#include "ImGuiRenderer.h"
#include "Delegate.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

// 게임 플레이어의 메인 루프 (BuildPipelinePlan B0-2).
//
// ── 계보 ──
//
// TrainAsis/GameMain의 후계다. 그 파일은 에디터 메인(Dx11Main)의 76% 클론이
// DX12 이관을 따라가지 못한 채 얼어 있던 것이라(삭제된 SceneRenderer 소비),
// 소생 대신 현재의 에디터 부트 패턴을 참조해 새로 썼다. Dx11Main과의 중복은
// 알고 만든 빚이다 — 공용 부트 추출(L4')이 갚는다.
//
// ── 에디터 메인과 무엇이 다른가 ──
//
// · 에디터 GUI 창·기즈모·Undo 계통이 없다. ImGui는 위젯이 아니라 표시
//   경로로만 쓴다 — DX12 셸이 유일한 프레젠트 소유자이고(D4에서 DX11
//   폴백이 걷혔다), 게임 카메라의 표시 슬롯을 전체 화면으로 블릿한다.
// · 씬 델리게이트가 기본 오브젝트(카메라·라이트)를 저작하지 않는다 —
//   플레이어의 씬은 파일에서 온다.
// · WinProcProxy 드레인이 없다 — 큐 적재 자체가 에디터 모드 전용이다
//   (CoreWindow::WndProc의 B0-1 분기).

namespace Player
{
	// --smoke N: N프레임 렌더 후 스스로 종료하고, 성패를 종료 코드와 로그
	// 마커로 알린다(BuildPipelinePlan §2.3 Verify). wWinMain이 파싱해 채운다.
	struct SmokeOptions
	{
		uint64_t frameLimit{ 0 };

		bool IsActive() const { return 0 != frameLimit; }
	};

	inline SmokeOptions g_smoke{};

	class PlayerMain : public DirectX11::IDeviceNotify
	{
	public:
		explicit PlayerMain(const std::shared_ptr<DirectX11::DeviceResources>& deviceResources);
		~PlayerMain();

		void Initialize();
		void Finalize();
		void Update();
		void InvokeResizeFlag();

		// IDeviceNotify
		void OnDeviceLost() override;
		void OnDeviceRestored() override;

	private:
		void TickScripts(float deltaTime);
		bool ExecuteRenderPass();
		void OnGui();
		void CommandBuildThread();
		void CommandExecuteThread();
		void CreateWindowSizeDependentResources();

	private:
		std::shared_ptr<DirectX11::DeviceResources> m_deviceResources;
		std::unique_ptr<ImGuiRenderer> m_imguiRenderer;

		Core::DelegateHandle m_inputEventHandle;
		Core::DelegateHandle m_newSceneCreatedHandle;
		Core::DelegateHandle m_activeSceneChangedHandle;

		std::thread m_CB_Thread;
		std::thread m_CE_Thread;
		std::atomic_bool m_isInvokeResize = false;
	};
}
