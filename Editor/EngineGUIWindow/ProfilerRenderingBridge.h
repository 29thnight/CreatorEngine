#pragma once

#include "ProfilerRenderingDiagnostics.h"

namespace editor
{
    // Invoked by the diagnostics bridge only at the engine owner-thread boundary.
    // Returned values never retain renderer, scene, or ImGui objects.
    ce::profiler_viewer::diagnostics::rendering_snapshot capture_rendering_diagnostics();

    // The enclosing bridge validates target/session/generation before entry.
    bool apply_rendering_command(ce::profiler_viewer::diagnostics::rendering_command command);
}
