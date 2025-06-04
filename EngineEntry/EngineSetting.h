#pragma once
#include "EngineSetting.h"
#include "Core.Minimal.h"
#include "EngineVersion.h"
#include "Core.Mathf.h"
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
	EngineSetting() = default;
	~EngineSetting() = default;

public:
	bool Initialize()
	{
		bool isSuccess = LoadSettings();
		char* vcInstallDir = nullptr;
		size_t len = 0;

		if (_dupenv_s(&vcInstallDir, &len, "VSINSTALLDIR") != 0 || vcInstallDir == nullptr)
		{
			m_msvcVersion = MSVCVersion::None;
			return false;
		}

		if (std::string(vcInstallDir).find("Preview") != std::string::npos)
		{
			m_msvcVersion = MSVCVersion::Comunity2022Preview;
		}
		else
		{
			m_msvcVersion = MSVCVersion::Comunity2022;
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

private:
    std::atomic_bool m_isGameView{ false };
    std::string m_currentEngineGitHash{ ENGINE_VERSION };
    bool m_isEditorMode{ true };
	bool m_isMinimized{ false };
	MSVCVersion m_msvcVersion{ MSVCVersion::None };
	Mathf::Vector2 m_lastWindowSize{ 0.0f, 0.0f };
};

static inline auto& EngineSettingInstance = EngineSetting::GetInstance();
