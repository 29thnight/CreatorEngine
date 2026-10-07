#pragma once

namespace ce::profiler_viewer
{
    class engine_process;
}

namespace editor
{
    // Install once on the presentation owner. engine_process::pump must hold
    // EditorMain's scene-structure lock for both callbacks, including teardown.
    void install_profiler_live_diagnostics(ce::profiler_viewer::engine_process& process);
}
