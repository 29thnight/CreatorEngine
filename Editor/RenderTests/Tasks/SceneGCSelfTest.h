#pragma once

#include <string>

namespace RenderTest
{
    // Source-only regression entry. Invoke only in an initialized, idle host at
    // its joined scene-structure barrier. Does not replace the user's scene.
    bool RunSceneGCSelfTest(std::string& log);
}
