#pragma once

#include <string>

namespace editor
{
    // Menu and command threads only enqueue a request. The presentation owner
    // launches/focuses the ordinary-privilege viewer at its existing boundary.
    // Call only for an explicit user open/focus action, never from panel draw,
    // workspace restoration or layout presets. Initialization does not launch.
    void request_profiler_viewer(bool rendering_live = false);
    void request_profiler_viewer_close();
    void initialize_profiler_viewer();
    void pump_profiler_viewer();
    void shutdown_profiler_viewer();
    bool profiler_viewer_running();
    std::string take_profiler_viewer_error();
}
