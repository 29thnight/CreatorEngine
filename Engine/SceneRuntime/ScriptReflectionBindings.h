#pragma once
#include "ScriptApiTable.h"

namespace ce::script::binding
{
    // Called on the managed host's owner thread while building its API table.
    void RegisterReflectionApi(ScriptApiTable& table);
    // Invalidates response tickets and rejects further object access.
    void EndReflectionSession() noexcept;
}
