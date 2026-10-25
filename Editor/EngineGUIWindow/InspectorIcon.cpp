#include "InspectorIcon.h"

namespace editor::inspector
{
    std::unordered_map<std::size_t, const char*>& inspector_icon_table()
    {
        static std::unordered_map<std::size_t, const char*> table;
        return table;
    }
} // namespace editor::inspector
