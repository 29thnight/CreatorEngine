#include "../../Engine/Containers/ContainerAPI.h"
#pragma once
#include "../../Engine/Utility_Framework/Foundation/EngineFunc.h"

#include "../../Lattice/Material/LXMaterialGraph.h"
#include "../../Lattice/ImGui/LXCanvas.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

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

    ce::dynamic_array<MaterialNodeMenuEntry> MaterialNodeMenuEntries(const LX::LXMaterialDefinitions& definitions,
                                                               std::string_view search = {});
    std::optional<std::string> DrawMaterialNodeAddMenu(const LX::LXMaterialDefinitions& definitions,
        ce::utf8_string& search);

    enum class MaterialBarAction
    {
        None,
        Save,
        Apply,
        ToggleAutomaticPreview,
        NewGraph,
        SaveAs,
        Assign,
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
                                          const ce::owned_func<void()>& materialMenu,
                                          const ce::owned_func<void()>& overlayMenu, bool automaticPreview = false);
    void DrawMaterialBreadcrumb(ImVec2 origin, ImVec2 size, const std::string& object, const std::string& mesh,
                                const std::string& material, const std::string& status = {});
} // namespace editor::material_editing
