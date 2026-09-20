#pragma once
#include <string>

namespace RenderTest
{
    // Process-scoped Commandlet: requires an empty active scene and the managed probe.
    bool RunAnimationPlaybackSelfTest(const std::string& modelPath, std::string& log);
}
