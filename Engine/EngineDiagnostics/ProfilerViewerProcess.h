#pragma once

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "ProfilerViewerProtocol.h"

namespace ce::profiler_viewer
{
    struct launch_status
    {
        bool running = false;
        bool connected = false;
        std::uint32_t win32_error = 0;
        std::string message;
        std::uint64_t failure_revision = 0;
        std::uint32_t viewer_pid = 0;
        std::uint64_t presented_frames = 0;
    };

    class engine_process
    {
    public:
        engine_process();
        ~engine_process();
        engine_process(const engine_process&) = delete;
        engine_process& operator=(const engine_process&) = delete;

        // Starts the pinned ../Tools/ProfilerViewer/ProfilerViewer.exe relative
        // to the Editor directory, or requests focus
        // of this engine's existing viewer. Never elevates; launch/focus never
        // waits on it. The Editor owns the child even if its transport breaks.
        bool open_or_focus(std::uint32_t page = 0, std::uint32_t ui_scale_milli = 1000);
        void request_close();
        // Call from the engine UI owner with its usual scene-lifetime protection.
        // Executes bounded commands against this engine and hands immutable data
        // to the transport worker; no IPC, encoding, file scan or indexing here.
        void pump();
        launch_status status() const;
        // Graceful owned-viewer close, then bounded job-scoped cleanup. Only
        // lifecycle shutdown may wait; independently launched viewers are untouched.
        void shutdown();

        // Optional versioned scene diagnostics. The publisher freezes a bounded
        // immutable DTO in pump and returns its worker-only serializer. The
        // handler also runs in pump and must validate the complete schema/object
        // epoch before effects. The deferred serializer cannot retain scene pointers.
        using diagnostic_encoder = std::function<std::vector<std::byte>()>;
        using diagnostic_publisher = std::function<diagnostic_encoder()>;
        using diagnostic_handler = std::function<bool(std::span<const std::byte>)>;
        void set_diagnostic_hooks(diagnostic_publisher publish, diagnostic_handler apply);

    private:
        struct state;
        std::shared_ptr<state> state_;
    };

    engine_process& viewer_process();
}
