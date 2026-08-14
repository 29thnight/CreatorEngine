#pragma once
#include "Core.Minimal.h"
#include "EngineVersion.h"
#include "SpinLock.h"
#include "Core.Fence.h"
#include "Core.Barrier.h"
#include "RenderPassSettings.h"
#include "DLLAcrossSingleton.h"
#include <yaml-cpp/yaml.h>
#include "TerrainBuffers.h"

namespace MetaYml = YAML;

enum class MSVCVersion
{
	None = 0,
	Comunity2022,
	Comunity2022Preview,
	Comunity2026Insideration,
};

enum class ContentsBrowserStyle
{
	Tile,
	Tree,
};

struct ImGuiContext;
class EngineSetting : public DLLCore::Singleton<EngineSetting>
{
private:
	friend class DLLCore::Singleton<EngineSetting>;
	EngineSetting() : renderBarrier(3) {}
	~EngineSetting() = default;

public:
	bool Initialize();

	MSVCVersion GetMSVCVersion() const { return m_msvcVersion; }
	std::wstring GetMsbuildPath()
	{
		switch (m_msvcVersion)
		{
		case MSVCVersion::Comunity2022:
			return PathFinder::MsbuildPath();
		case MSVCVersion::Comunity2022Preview:
			return PathFinder::MsbuildPreviewPath();
		case MSVCVersion::Comunity2026Insideration:
			return PathFinder::MSBuild18Path();
		default:
			return L"";
		}
	}
	bool IsEditorMode() const { return m_isEditorMode; }
	void SetEditorMode(bool isEditorMode) { m_isEditorMode = isEditorMode; }
    bool IsGameView() const { return m_isGameView.load(); }
    void ToggleGameView() { m_isGameView.store(!m_isGameView.load()); }
    std::string GetGitVersionHash() { return m_currentEngineGitHash; }
	void SetMinimized(bool isMinimized) { m_isMinimized = isMinimized; }
	bool IsMinimized() const { return m_isMinimized; }
	void SetWindowSize(Mathf::Vector2 size) { m_lastWindowSize = size; }
	Mathf::Vector2 GetWindowSize() const { return m_lastWindowSize; }
    RenderPassSettings& GetRenderPassSettingsRW() { return m_renderPassSettings; }
	void SetRenderPassSettings(const RenderPassSettings& settings) { m_renderPassSettings = settings; }
    const RenderPassSettings& GetRenderPassSettings() const { return m_renderPassSettings; }
	ContentsBrowserStyle GetContentsBrowserStyle() const { return m_contentsBrowserStyle; }
	void SetContentsBrowserStyle(ContentsBrowserStyle style) { m_contentsBrowserStyle = style; }
	float GetImGuiScale() { return m_imguiScale; }

	// 씬 RHI의 부팅 선택. 런타임 전환은 EnhancedSceneRenderer가 별도로 맡는다.
	bool IsDx12BackendPreferred() const { return m_useDx12Backend; }
	void SetDx12BackendPreferred(bool preferred) { m_useDx12Backend = preferred; }

	// ── ImGui 백엔드는 씬 백엔드와 별개의 결정이다 ──
	//
	// 하나로 묶었다가 물렸다. ImGui DX12 셸은 자기 스왑체인을 같은 HWND에
	// 만드는데, DXGI는 한 HWND에 스왑체인 둘을 지원하지 않는다 — DX11
	// 스왑체인이 무효화되어 종료가 크래시하고 PIX 캡처가 깨졌다.
	//
	// ★ 2026-08-07(D2·D3): 기본을 켜짐으로 뒤집었다.
	//
	//   위 실패의 원인은 셸이 아니라 순서였다. DX11이 초기화 때 무조건
	//   스왑체인을 만들어 놓고, 나중에 셸이 같은 창에 두 번째를 붙이려 한
	//   것이다. D2에서 소유권을 명시적 결정으로 바꿨다 —
	//   App::SetWindow가 창을 붙이기 전에 정하고, DX11은 소유하지 않으면
	//   아예 만들지 않는다(DeviceResources::SetPresentOwnedExternally).
	//   두 번째 스왑체인이 생길 일 자체가 없어졌다.
	//
	//   실측: 셸을 켜고 590프레임 렌더 · 폴백 없음 · 종료 19단계 정상 ·
	//   검증 레이어 0건. 화면도 확인했다 — 씬 뷰·게임 뷰·Hierarchy·
	//   Content Browser·Inspector가 전부 정상이고 FPS 74가 돈다.
	//
	//   T1 때와 같은 구조다: 당시 판단("DX11을 건드리지 않는다")은 옳았고,
	//   DX11 렌더러 은퇴로 그 전제가 사라졌다.
	//
	// 이제 false는 "끄기"가 아니라 Vulkan renderer backend 선택이다. 공통
	// ImGuiHost가 Win32/컨텍스트를 유지하고 GPU 부분만 교체한다.
	bool IsDx12ImGuiShellEnabled() const { return m_useDx12ImGuiShell; }
	void SetDx12ImGuiShellEnabled(bool enabled) { m_useDx12ImGuiShell = enabled; }

	void SetImGuiInitialized(bool isInitialized)
	{
		m_isImGuiInitialized = isInitialized;
	}

	bool IsImGuiInitialized() const
	{
		return m_isImGuiInitialized;
	}

	bool SaveSettings();
	bool LoadSettings();

	void SetIsDebugMode(bool isDebugMode) { m_isDebugMode = isDebugMode; }
	bool IsDebugMode() const { return m_isDebugMode; }

	std::wstring GetBuildGameName() const { return m_buildGameName; }
	void SetBuildGameName(const std::wstring& name) { m_buildGameName = name; }

	std::wstring GetStartupSceneName() const { return m_startupSceneName; }
	void SetStartupSceneName(const std::wstring& name) { m_startupSceneName = name; }

	std::atomic<bool> m_isRenderPaused{ false };

	std::atomic_flag gameToRenderLock = ATOMIC_FLAG_INIT;
	std::atomic<double> frameDeltaTime{};
	Barrier renderBarrier;
	Fence RenderCommandFence;
	Fence RHICommandFence;
	TerrainBrush* terrainBrush = nullptr;
	float m_imguiScale{ 0.8f };
	// 구 설정 파일과 도구가 필드 이름을 참조하므로 직렬화 표면만 유지한다.
	bool m_useDx12Backend{ true };
	// 기본 켜짐(D3). 스왑체인 소유권이 명시적 결정이 된 뒤로는 셸이 창을
	// 갖는 것이 기본이고, DX11은 자산·잔여 경로만 맡는다.
	bool m_useDx12ImGuiShell{ true };

private:
    std::atomic_bool m_isGameView{ false };
	std::atomic_bool m_isImGuiInitialized{ false };
    std::string m_currentEngineGitHash{ ENGINE_VERSION };
	ContentsBrowserStyle m_contentsBrowserStyle{ ContentsBrowserStyle::Tile };
    bool m_isEditorMode{ true };
	bool m_isMinimized{ false };
	bool m_isDebugMode{ false };


	MSVCVersion m_msvcVersion{ MSVCVersion::None };
    RenderPassSettings m_renderPassSettings{};
	Mathf::Vector2 m_lastWindowSize{ 0.0f, 0.0f };
	std::wstring m_buildGameName{ L"Train Your Asis" };
	std::wstring m_startupSceneName{ L"SampleScene" };
};

static auto EngineSettingInstance = EngineSetting::GetInstance();
