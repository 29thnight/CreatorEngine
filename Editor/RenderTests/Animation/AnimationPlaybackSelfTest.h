#pragma once
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace RenderTest
{
    // Process-scoped Commandlet: requires an empty active scene and the managed probe.
    bool RunAnimationPlaybackSelfTest(const std::string& modelPath, std::string& log);
}
