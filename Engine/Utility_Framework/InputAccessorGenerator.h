#pragma once

#include "InputGraph.h"
#include <string>
#include <string_view>

namespace Input
{
    struct InputAccessorSources final
    {
        std::string cpp;
        std::string csharp;
    };

    // The compiled LX output is the only source of IDs/types/versions/hash.
    // Invalid or colliding code identifiers fail export rather than changing IDs.
    bool GenerateInputAccessors(const InputGraphProgram& program, std::string_view className,
        InputAccessorSources& sources, std::string& diagnostic);
}
