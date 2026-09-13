#pragma once
#include "CommandCore/CommandResult.h"
#include "EntityHandle.h"
#include <string>

namespace EditorScriptAuthoring
{
    struct Status
    {
        bool busy{};
        bool succeeded{};
        std::string message;
        std::string source;
        std::string log;
        std::string type;
    };
    // UI/commands request work; EditorMain polls the process and applies it under
    // the scene lock on the game thread, even when the Inspector has been closed.
    CommandCore::CommandResult CreateAndAttach(EntityHandle target, const std::string& name);
    CommandCore::CommandResult Retry();
    CommandCore::CommandResult Reload();
    Status GetStatus();
    void Cancel();
    void Tick();
    void Shutdown();
}
