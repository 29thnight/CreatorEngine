#pragma once
#include <string>
namespace RenderTest
{
    // Isolated commandlet only: creates fixtures and replaces its active scene.
    bool RunSceneLoadJobsSelfTest(const std::string& directory, const std::string& modelPath, std::string& log);
    bool FinishSceneLoadJobsSelfTest(const std::string& directory, std::string& log);
}
