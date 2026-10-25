#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif

#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.EngineMode.h"
#else
import Creator.Core.EngineMode;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Platform.Windows.h"
#else
import Creator.Platform.Windows;
#endif
#include "../Utility_Framework/Runtime/RuntimeSettings.h"

// Process-level composition data. The Editor and Player entry points build this value;
// the common bootstrap only applies it. EngineRunMode remains transitional while
// SceneManager and TagManager still consume the legacy global mode.
using RuntimeContentPrepare = bool (*)(const ce::paths::roots& paths) noexcept;
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
    ce::paths::roots paths{};
    RuntimeContentPrepare prepareRuntimeContent{};
    HostSettingsInitialize initializeHostSettings{};
    WindowsWindowDesc window{};
};
