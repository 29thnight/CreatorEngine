#pragma once

#include "../../Lattice/Material/LXMaterialGraph.h"
#include "../../Lattice/ImGui/LXCanvas.h"
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace editor::material_editing
{
LX::LXStyleSheet BlenderStyles(const LX::LXMaterialDefinitions& definitions);
LX::LXNodeItemRegistry MaterialItems(const LX::LXMaterialDefinitions& definitions);

struct MaterialNodeMenuEntry
{
    std::string type;
    std::string label;
    std::string group;
};

std::vector<MaterialNodeMenuEntry> MaterialNodeMenuEntries(const LX::LXMaterialDefinitions& definitions,
                                                           std::string_view search = {});
std::optional<std::string> DrawMaterialNodeAddMenu(const LX::LXMaterialDefinitions& definitions, std::string& search);

enum class MaterialBarAction
{
    None,
    Save,
    Apply,
    NewGraph,
    Reload,
    FrameAll,
    ToggleGrid,
    ToggleSnap,
    ClosePanel
};

class MaterialHeaderScope
{
  public:
    explicit MaterialHeaderScope(ImVec4 background);
    ~MaterialHeaderScope();
    MaterialHeaderScope(const MaterialHeaderScope&) = delete;
    MaterialHeaderScope& operator=(const MaterialHeaderScope&) = delete;
};

void DrawMaterialContextSelector();
MaterialBarAction DrawMaterialDataBar(std::string& name, bool dirty, bool editable, bool showGrid, bool snapToGrid,
                                      const std::function<void()>& materialMenu,
                                      const std::function<void()>& overlayMenu);
void DrawMaterialBreadcrumb(ImVec2 origin, ImVec2 size, const std::string& object, const std::string& mesh,
                            const std::string& material, const std::string& status = {});
} // namespace editor::material_editing
