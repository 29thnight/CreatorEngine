#pragma once

#include "EngineMode.h"
#include "EnginePaths.h"
#include "WindowDesc.h"
#include "RuntimeSettings.h"

// Process-level composition data. The Editor and Player entry points build this value;
// the common bootstrap only applies it. EngineRunMode remains transitional while
// SceneManager and TagManager still consume the legacy global mode.
using RuntimeContentPrepare = bool (*)(const EnginePaths& paths) noexcept;
using HostSettingsInitialize = bool (*)() noexcept;

struct EngineLaunchConfig
{
    explicit EngineLaunchConfig(
        RuntimeRenderBackendPolicy backendPolicy = RuntimeRenderBackendPolicy::ProjectSettings)
        : renderBackendPolicy(backendPolicy)
    {
    }

    // 진입점의 빌드 고정 정책이다. 프로젝트 설정이나 실행 중 변경으로 선택하지 않는다.
    const RuntimeRenderBackendPolicy renderBackendPolicy;
    EngineRunMode compatibilityRunMode{ EngineRunMode::Unset };
    const char* logSessionName{ "Engine" };
    EnginePaths paths{};
    RuntimeContentPrepare prepareRuntimeContent{};
    HostSettingsInitialize initializeHostSettings{};
    WindowDesc window{};
};
