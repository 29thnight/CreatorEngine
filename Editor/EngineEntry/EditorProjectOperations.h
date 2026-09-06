#pragma once
#include "CommandCore/CommandResult.h"
#include <string>

namespace EditorProjectOperations
{
    CommandCore::CommandResult Tags();
    CommandCore::CommandResult HasTag(const std::string& name);
    CommandCore::CommandResult AddTag(const std::string& name);
    CommandCore::CommandResult RemoveTag(const std::string& name);
}
