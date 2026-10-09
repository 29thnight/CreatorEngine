#pragma once

#include "../../../Engine/EngineDiagnostics/ProfilerLiveDiagnostics.h"
#include "ProfilerViewerClient.h"

namespace editor::profiler_view
{
    // Uses the remote source owned by ProfilerPresenter, never a local service.
    ce::profiler_viewer::client& source();
    std::shared_ptr<const ce::profiler_viewer::diagnostics::snapshot> live_diagnostics();
    bool live_diagnostics_actions_enabled();
    std::uint64_t live_target_revision();
    void shutdown_live_diagnostics();
    bool request_memory_snapshot();
    bool request_animation_snapshot(std::uint64_t animator_id, bool force = false);
    bool request_rendering_command(ce::profiler_viewer::diagnostics::rendering_command command);
    const char* live_diagnostic_status();
    void draw_rendering_live();
}
