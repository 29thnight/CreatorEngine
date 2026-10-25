#pragma once
#include "CommandCore/CommandResult.h"
class Scene;
namespace EditorDiagnostics
{
    CommandCore::CommandResult ValidateHierarchy(Scene* scene);
    CommandCore::CommandResult AnimatorStatus(Scene* scene);
}
