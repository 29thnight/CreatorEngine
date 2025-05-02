#pragma once
#include "EngineSetting.h"
#include "Core.Minimal.h"
#include "EngineVersion.h"

class EngineSetting : public Singleton<EngineSetting>
{
private:
	friend class Singleton;
	EngineSetting() = default;
	~EngineSetting() = default;

public:
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
};

static inline auto& EngineSettingInstance = EngineSetting::GetInstance();
