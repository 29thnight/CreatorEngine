#pragma once

#include <string>

namespace editor
{
    // Menu and command threads only enqueue a request. The presentation owner
    // launches/focuses the ordinary-privilege viewer at its existing boundary.
    void request_profiler_viewer(bool rendering_live = false);
    void request_profiler_viewer_close();
    void initialize_profiler_viewer();
    void pump_profiler_viewer();
    void shutdown_profiler_viewer();
    bool profiler_viewer_running();
    std::string take_profiler_viewer_error();
}
