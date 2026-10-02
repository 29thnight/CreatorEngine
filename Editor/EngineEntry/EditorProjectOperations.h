#pragma once
#include "CommandCore/CommandResult.h"
#include <string>
#include "ProjectLayerSettings.h"

namespace EditorProjectOperations
{
CommandCore::CommandResult Layers();
CommandCore::CommandResult AddLayer(const std::string& name);
CommandCore::CommandResult RenameLayer(const std::string& name, const std::string& replacement);
CommandCore::CommandResult SetCollision(ce::layers::layer_id left, ce::layers::layer_id right, bool enabled);
CommandCore::CommandResult Tags();
CommandCore::CommandResult HasTag(const std::string& name);
CommandCore::CommandResult AddTag(const std::string& name);
CommandCore::CommandResult RemoveTag(const std::string& name);
} // namespace EditorProjectOperations
