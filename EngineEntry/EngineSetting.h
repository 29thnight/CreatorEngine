#pragma once
#include "EngineSetting.h"
#include "Core.Minimal.h"
#include "EngineVersion.h"
#include "Core.Mathf.h"
#include "SpinLock.h"
#include "Core.Fence.h"
#include "Core.Barrier.h"
#include <yaml-cpp/yaml.h>

namespace MetaYml = YAML;

enum class MSVCVersion
{
	None = 0,
	Comunity2022,
	Comunity2022Preview,
};

class EngineSetting : public Singleton<EngineSetting>
{
private:
	friend class Singleton;
	EngineSetting() : renderBarrier(3) {}
	~EngineSetting() = default;

public:
	bool Initialize()
	{
		bool isSuccess = LoadSettings();
		char* vcInstallDir = nullptr;
		size_t len = 0;

		std::string output = ExecuteVsWhere();

		if (output.empty())
		{
			std::cout << "Visual Studio not found.\n";
			return false;
		}

		// ÁÙ¹Ù²Þ Á¦°Å
		output.erase(std::remove(output.begin(), output.end(), '\r'), output.end());
		output.erase(std::remove(output.begin(), output.end(), '\n'), output.end());

		std::cout << "VS Install Path: " << output << std::endl;

		if (output.find("Preview") != std::string::npos)
		{
			m_msvcVersion = MSVCVersion::Comunity2022Preview;
		}
		else if (output.find("2022") != std::string::npos)
		{
			m_msvcVersion = MSVCVersion::Comunity2022;
		}
		else
		{
			m_msvcVersion = MSVCVersion::None;
			std::cout << "Unsupported Visual Studio version.\n";
			return false;
		}

		return isSuccess;
	}

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

	bool IsEditorMode() const
	{
		return m_isEditorMode;
	}

	void SetEditorMode(bool isEditorMode)
	{
		m_isEditorMode = isEditorMode;
	}

    bool IsGameView() const
    {
        return m_isGameView.load();
    }

    void ToggleGameView()
    {
        m_isGameView.store(!m_isGameView.load());
    }

    std::string GetGitVersionHash() { return m_currentEngineGitHash; }

	void SetMinimized(bool isMinimized)
	{
		m_isMinimized = isMinimized;
	}

	bool IsMinimized() const
	{
		return m_isMinimized;
	}

	void SetWindowSize(Mathf::Vector2 size)
	{
		m_lastWindowSize = size;
	}

	Mathf::Vector2 GetWindowSize() const
	{
		return m_lastWindowSize;
	}

	void SetImGuiInitialized(bool isInitialized)
	{
		m_isImGuiInitialized = isInitialized;
	}

	bool IsImGuiInitialized() const
	{
		return m_isImGuiInitialized;
	}

	bool SaveSettings()
	{
		// Implement saving logic here
		file::path eingineSettingsPath = PathFinder::ProjectSettingPath("settings.asset");

		std::ofstream settingsFile(eingineSettingsPath);
		MetaYml::Node rootNode;

		rootNode["lastWindowSize"]["x"] = m_lastWindowSize.x;
		rootNode["lastWindowSize"]["y"] = m_lastWindowSize.y;
		rootNode["msvcVersion"] = static_cast<int>(m_msvcVersion);

		settingsFile << rootNode;

		settingsFile.close();
		
		return true;
	}

	bool LoadSettings()
	{
		bool isSuccess = true;
		// Implement loading logic here
		file::path eingineSettingsPath = PathFinder::ProjectSettingPath("settings.asset");

		if (!file::exists(eingineSettingsPath))
		{
			//initialize default settings
			isSuccess = SaveSettings();
		}

		MetaYml::Node rootNode = MetaYml::LoadFile(eingineSettingsPath.string());

		m_lastWindowSize = 
		{ 
			rootNode["lastWindowSize"]["x"].as<float>(), 
			rootNode["lastWindowSize"]["y"].as<float>() 
		};
		m_msvcVersion = static_cast<MSVCVersion>(rootNode["msvcVersion"].as<int>());
		
		return isSuccess;
	}

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
    bool m_isEditorMode{ true };
	bool m_isMinimized{ false };
	MSVCVersion m_msvcVersion{ MSVCVersion::None };
	Mathf::Vector2 m_lastWindowSize{ 0.0f, 0.0f };
};

static inline auto& EngineSettingInstance = EngineSetting::GetInstance();
