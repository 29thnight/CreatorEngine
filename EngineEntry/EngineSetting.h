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

	// 렌더러 교체 스위치(3-9)의 부팅 기본값. render.backend가 기록하고
	// SaveSettings가 영속화하며, App::Run 초기화 끝에 적용된다.
	bool IsDx12BackendPreferred() const { return m_useDx12Backend; }
	void SetDx12BackendPreferred(bool preferred) { m_useDx12Backend = preferred; }

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
	// 3-9 승격(2026-08-06, 사용자 결정): 기본값 DX12. 판정 기준 ①~③은 수치로
	// 충족했고 ④(에디터 기즈모·UI 표시)는 미배선 상태를 알고 승격한다 —
	// ImGui 백엔드 DX12 전환(PHASE 8-2 선취)과 기즈모 체인 배선이 후속이다.
	// DX11 폴백은 render.backend dx11로 즉시 복귀 가능(한 릴리스 주기 유지).
	bool m_useDx12Backend{ true };

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