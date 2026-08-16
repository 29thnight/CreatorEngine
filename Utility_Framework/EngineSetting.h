#pragma once
#include "Core.Minimal.h"
#include "EngineVersion.h"
#include "SpinLock.h"
#include "Core.Fence.h"
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

/// 프로세스가 부팅할 때 한 번만 고르는 렌더링 백엔드.
/// 실행 중 setter는 의도적으로 제공하지 않는다.
enum class RenderBackend : uint8_t
{
	DX12,
	Vulkan,
};

inline const char* RenderBackendName(RenderBackend backend) noexcept
{
	return RenderBackend::Vulkan == backend ? "vulkan" : "dx12";
}

struct ImGuiContext;
class EngineSetting : public DLLCore::Singleton<EngineSetting>
{
private:
	friend class DLLCore::Singleton<EngineSetting>;
	EngineSetting() = default;
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

	// ── 불변 backend 계약 (Slice 8-c) ──
	//
	// Editor는 render.backend, Player는 build.render.backend를 읽는다. active
	// 값은 LoadSettings에서 모드에 따라 한 번 복사된 뒤 setter가 없다. 설정 UI가
	// 바꾸는 것은 다음 Editor 부팅 또는 다음 Player 빌드에 쓸 configured 값뿐이다.
	RenderBackend GetActiveRenderBackend() const { return m_activeRenderBackend; }
	RenderBackend GetEditorRenderBackend() const { return m_editorRenderBackend; }
	void SetEditorRenderBackend(RenderBackend backend) { m_editorRenderBackend = backend; }
	RenderBackend GetBuildRenderBackend() const { return m_buildRenderBackend; }
	void SetBuildRenderBackend(RenderBackend backend) { m_buildRenderBackend = backend; }
	bool IsEditorRenderBackendRestartRequired() const
	{
		return m_isEditorMode && m_activeRenderBackend != m_editorRenderBackend;
	}

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
	Fence RenderCommandFence;
	Fence RHICommandFence;
	TerrainBrush* terrainBrush = nullptr;
	float m_imguiScale{ 0.8f };

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
	RenderBackend m_activeRenderBackend{ RenderBackend::DX12 };
	RenderBackend m_editorRenderBackend{ RenderBackend::DX12 };
	RenderBackend m_buildRenderBackend{ RenderBackend::DX12 };
};

static auto EngineSettingInstance = EngineSetting::GetInstance();
