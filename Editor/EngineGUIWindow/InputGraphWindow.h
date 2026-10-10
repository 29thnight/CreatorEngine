#pragma once

#include <filesystem>
#include <string>

class InputSessionComponent;

namespace editor::input_editing
{
    bool Open(const std::filesystem::path& path, std::string& error);
    void Draw();
    void DrawInspector(InputSessionComponent& component);
}
