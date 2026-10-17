#pragma once

#include "../../../Engine/Utility_Framework/StandardLibrary.h"

namespace RenderTest
{
    // Regression entry. Invoke only in an initialized, idle host at
    // its joined scene-structure barrier. Does not replace the user's scene.
    bool RunSceneGCSelfTest(std::string& log);
}
