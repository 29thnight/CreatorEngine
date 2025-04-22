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

private:
    std::atomic_bool m_isGameView{ false };
    std::string m_currentEngineGitHash{ ENGINE_VERSION };
};

static inline auto& EngineSettingInstance = EngineSetting::GetInstance();
