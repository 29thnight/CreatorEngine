#pragma once

#include "../../Engine/Utility_Framework/StandardLibrary.h"

namespace editor
{
    // Local style values only; does not access the current ImGui context or GPU.
    bool RunEditorThemeSelfTest(std::string& report);
}
