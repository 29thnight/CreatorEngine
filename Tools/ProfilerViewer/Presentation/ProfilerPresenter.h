#pragma once

#include <filesystem>

#include "ProfileReader.h"

namespace ce::profiler_viewer
{
    class client;
}

namespace editor::profiler_view
{
    // The host owns the ImGui context, native window and bounded executor. All
    // callbacks are queued, never invoked inline on the UI thread. Shutdown
    // cancels presentation publication before the host drains that executor.
    void initialize(ce::preparation_dispatch dispatch, ce::profiler_viewer::client& source,
                    ce::preparation_dispatch diagnostics_dispatch);
    void open_path(const std::filesystem::path& path);
    void draw();
    void shutdown();
}
