#include "../../Engine/Utility_Framework/Foundation/EngineNodeMap.h"
#include "InspectorIcon.h"

namespace editor::inspector
{
    ce::engine_node_map<std::size_t, const char*>& inspector_icon_table()
    {
        static ce::engine_node_map<std::size_t, const char*> table;
        return table;
    }
} // namespace editor::inspector
