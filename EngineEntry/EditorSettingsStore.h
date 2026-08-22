#pragma once

#include "BuildSettings.h"
#include "EditorPreferences.h"

class EditorSettingsStore final
{
public:
    static EditorSettingsStore& Get() noexcept;

    bool Initialize() noexcept;
    bool Save() noexcept;

    EditorPreferences& Preferences() noexcept { return m_preferences; }
    const EditorPreferences& Preferences() const noexcept { return m_preferences; }
    BuildSettings& Build() noexcept { return m_buildSettings; }
    const BuildSettings& Build() const noexcept { return m_buildSettings; }

private:
    EditorSettingsStore() = default;

    EditorPreferences m_preferences{};
    BuildSettings m_buildSettings{};
    bool m_initialized{ false };
};
