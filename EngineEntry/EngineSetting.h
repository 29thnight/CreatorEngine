#pragma once
#include "Core.Minimal.h"
#include "EngineVersion.h"
#include "SpinLock.h"
#include "Core.Fence.h"
#include "Core.Barrier.h"
#include "RenderPassSettings.h"
#include <yaml-cpp/yaml.h>
namespace MetaYml = YAML;

enum class MSVCVersion
{
	None = 0,
	Comunity2022,
	Comunity2022Preview,
};

enum class ContentsBrowserStyle
{
	Tile,
	Tree,
};

class EngineSetting : public Singleton<EngineSetting>
{
private:
	friend class Singleton;
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
    RenderPassSettings& GetRenderPassSettings() { return m_renderPassSettings; }
	void SetRenderPassSettings(const RenderPassSettings& settings) { m_renderPassSettings = settings; }
    const RenderPassSettings& GetRenderPassSettings() const { return m_renderPassSettings; }
	ContentsBrowserStyle GetContentsBrowserStyle() const { return m_contentsBrowserStyle; }
	void SetContentsBrowserStyle(ContentsBrowserStyle style) { m_contentsBrowserStyle = style; }

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

	std::atomic<bool> m_isRenderPaused{ false };

	std::atomic_flag gameToRenderLock = ATOMIC_FLAG_INIT;
	std::atomic<double> frameDeltaTime{};
	Barrier renderBarrier;
	Fence RenderCommandFence;
	Fence RHICommandFence;

private:
    std::atomic_bool m_isGameView{ false };
	std::atomic_bool m_isImGuiInitialized{ false };
    std::string m_currentEngineGitHash{ ENGINE_VERSION };
	ContentsBrowserStyle m_contentsBrowserStyle{ ContentsBrowserStyle::Tile };
    bool m_isEditorMode{ true };
	bool m_isMinimized{ false };
	MSVCVersion m_msvcVersion{ MSVCVersion::None };
    RenderPassSettings m_renderPassSettings{};
	Mathf::Vector2 m_lastWindowSize{ 0.0f, 0.0f };
};

static auto& EngineSettingInstance = EngineSetting::GetInstance();