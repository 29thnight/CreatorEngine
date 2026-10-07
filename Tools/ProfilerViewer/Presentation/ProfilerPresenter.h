#pragma once

#include <filesystem>

#include "ProfileReader.h"

struct ImVec2;

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

    // Poll once before drawing any controls. The host owns the fixed rail,
    // titlebar and body bounds; none of these surfaces scroll the host window.
    void begin_frame();
    float navigation_width();
    void draw_navigation();
    // Called inside the host's menu bar. Returns the absolute right menu edge.
    float draw_menus(bool compact);
    void draw_record_control(const ImVec2& size);
    // Bounded page and information panels, each with independent scrolling.
    void draw();
    void shutdown();
}
