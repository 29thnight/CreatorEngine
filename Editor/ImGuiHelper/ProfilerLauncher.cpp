#include "ProfilerHUD.h"

#include "ProfilerLiveDiagnosticsBridge.h"
#include "ProfilerViewerProcess.h"
#include "ImGui.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace editor
{
    namespace
    {
        std::atomic_uint32_t requested_page{ 0 };
        std::string launch_error;
        std::uint64_t observed_failure = 0;
    }

    void request_profiler_viewer(bool rendering_live)
    {
        requested_page.store(rendering_live ? 2u : 1u, std::memory_order_release);
    }

    void request_profiler_viewer_close()
    {
        requested_page.store(3u, std::memory_order_release);
    }

    void initialize_profiler_viewer()
    {
        install_profiler_live_diagnostics(ce::profiler_viewer::viewer_process());
    }

    void pump_profiler_viewer()
    {
        auto& process = ce::profiler_viewer::viewer_process();
        const auto request = requested_page.exchange(0, std::memory_order_acq_rel);
        const float scale = ImGui::GetStyle().FontScaleMain;
        const auto scale_milli = std::isfinite(scale)
            ? static_cast<std::uint32_t>(std::clamp(std::round(scale * 1000.0f), 500.0f, 3000.0f))
            : 1000u;
        if (request == 3)
        {
            process.request_close();
        }
        else if (request && !process.open_or_focus(request == 2 ? 1u : 0u, scale_milli))
        {
            launch_error = process.status().message;
        }
        process.pump();
        const auto status = process.status();
        if (status.failure_revision != observed_failure)
        {
            observed_failure = status.failure_revision;
            launch_error = status.message;
        }
    }

    void shutdown_profiler_viewer()
    {
        auto& process = ce::profiler_viewer::viewer_process();
        requested_page.store(0, std::memory_order_release);
        process.set_diagnostic_hooks({}, {});
        process.shutdown();
    }

    bool profiler_viewer_running()
    {
        return ce::profiler_viewer::viewer_process().status().running;
    }

    std::string take_profiler_viewer_error()
    {
        return std::exchange(launch_error, {});
    }
}
