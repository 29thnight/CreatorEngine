#pragma once

#include "ScriptApiTable.h"
#include <reflgen/runtime/value_descriptor.h>

namespace ce::script::binding
{
    // The host's binding session owns all callback generations. The registry
    // never invokes managed code on worker threads or during retirement.
    void RegisterReflectionCallbackApi(ScriptApiTable& table);
    void EndReflectionCallbackSession() noexcept;
}

namespace ce::script::reflection
{
    // Resolves an opaque registration token to the exact owned_func type the
    // native method expects. The view retains temporary writable call storage;
    // copies held by the engine retain only a disableable native state.
    // Invalid tokens, signatures, generations and threads return an empty view.
    reflgen::value_view ResolveCallback(std::uint64_t token,
        const reflgen::value_descriptor& expected);
}
