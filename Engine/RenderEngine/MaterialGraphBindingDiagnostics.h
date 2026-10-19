#pragma once

#include "../EngineDiagnostics/ProfileScope.h"
#include <cstdlib>
#include <optional>

namespace material_graph
{
    // Keep the common parent markers comparable. Per-texture nested scopes are
    // opt-in: their recording cost must not become part of the normal hit path.
    class BindingDetailScope final
    {
      public:
        explicit BindingDetailScope(ce::marker_id marker)
        {
            static const bool enabled = []
            {
                char value[8]{};
                std::size_t bytes{};
                getenv_s(&bytes, value, sizeof(value), "CREATOR_BINDING_DETAIL_SCOPES");
                return value[0] == '1' && value[1] == '\0';
            }();
            if (enabled)
            {
                scope_.emplace(marker);
            }
        }

      private:
        std::optional<ce::profile_scope> scope_;
    };
} // namespace material_graph
