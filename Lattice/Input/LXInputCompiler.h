#pragma once

#include "LXInputGraph.h"
#include "../../Engine/Utility_Framework/InputGraph.h"

namespace LX
{
    struct LXInputCompileResult final
    {
        Input::InputGraph definition;
        own::shared_owner<const Input::InputGraphProgram> program;
        std::vector<Issue> diagnostics;
    };

    // The only runtime adapter. LX Core and LX ImGui have no Engine dependency.
    LXInputCompileResult CompileInputGraph(const LXInputAsset& asset, std::uint64_t generation = 1);
}
