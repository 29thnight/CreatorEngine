#pragma once

// Shared by the native API table and the compiled host identity query.
inline constexpr int CreatorScriptApiVersion = 46;

// v42 requires the managed OnTickSimulate(float fixedDelta) entry point; old hosts must reject it.
// v43 appends bounded simulation status/command/snapshot copy APIs.

// v44 requires synchronous OnTickSimulate(float fixedDelta); old OnUpdate hosts are incompatible.
// v45 requires statically registered ordinary lifecycle members and the F# managed runtime.
