#include "../../../Engine/Containers/ContainerAPI.h"
#pragma once
#include "CommandCore/CommandResult.h"
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace EditorCommandlets
{
    // Only the dedicated --commandlet process path calls this entry point.
    // Runs on the game thread after host initialization; never registered with HTTP.
    CommandCore::CommandResult Run(const ce::dynamic_array<std::string>& arguments);
}
