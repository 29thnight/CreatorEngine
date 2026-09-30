#pragma once

#include "LXMaterialGraph.h"

namespace LX
{
// Extends the Blender-derived registry with explicitly engine-owned operators.
void RegisterMaterialOperators(LXMaterialDefinitions& result, LXNodeDefinitionRegistry& registry);
} // namespace LX
