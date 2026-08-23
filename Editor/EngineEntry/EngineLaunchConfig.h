#pragma once

#include "EngineMode.h"
#include "EnginePaths.h"
#include "WindowDesc.h"

// Process-level composition data. The Editor and Player entry points build this value;
// the common bootstrap only applies it. EngineRunMode remains transitional while
// SceneManager and TagManager still consume the legacy global mode.
using RuntimeContentPrepare = bool (*)(const EnginePaths& paths) noexcept;
using HostSettingsInitialize = bool (*)() noexcept;

struct EngineLaunchConfig
{
    EngineRunMode compatibilityRunMode{ EngineRunMode::Unset };
    const char* logSessionName{ "Engine" };
    EnginePaths paths{};
    RuntimeContentPrepare prepareRuntimeContent{};
    HostSettingsInitialize initializeHostSettings{};
    WindowDesc window{};
};
