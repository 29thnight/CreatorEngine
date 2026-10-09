#pragma once

#include <string>

namespace RenderTest
{
    // Offline texture byte/quality contracts. These checks are intentionally
    // independent from GPU execution and never run during ordinary importing.
    [[nodiscard]] bool RunExperimentTexturePipelineSelfTest(std::string& outLog);
}
