#pragma once
#include "EnginePaths.h"

inline std::filesystem::path ResolveEngineRuntimeDirectory()
{
    auto root = ResolveProcessExecutableDirectory();
    for (int depth = 0; depth <= 2 && !root.empty(); ++depth)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(root / L"Runtime" / L"layout.version", error))
            return root / L"Runtime";
        root = root.parent_path();
    }
    return {};
}
