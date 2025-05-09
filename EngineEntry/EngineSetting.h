#pragma once
#include "EngineSetting.h"
#include "Core.Minimal.h"
#include "EngineVersion.h"

enum class MSVCVerseion
{
	Comunity2022 = 0,
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
		char* vcInstallDir = nullptr;
		size_t len = 0;

		if (_dupenv_s(&vcInstallDir, &len, "VSINSTALLDIR") != 0 || vcInstallDir == nullptr)
		{
			return false;
		}

		if (std::string(vcInstallDir).find("Preview") != std::string::npos)
		{
			m_msvcVersion = MSVCVerseion::Comunity2022Preview;
		}
		else
		{
			m_msvcVersion = MSVCVerseion::Comunity2022;
		}

		return true;
	}

	std::wstring GetMsbuildPath()
	{
		switch (m_msvcVersion)
		{
		case MSVCVerseion::Comunity2022:
			return PathFinder::MsbuildPath();
		case MSVCVerseion::Comunity2022Preview:
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

private:
    std::atomic_bool m_isGameView{ false };
    std::string m_currentEngineGitHash{ ENGINE_VERSION };
    bool m_isEditorMode{ true };
	MSVCVerseion m_msvcVersion{ MSVCVerseion::Comunity2022 };
};

static inline auto& EngineSettingInstance = EngineSetting::GetInstance();
