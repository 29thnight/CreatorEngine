#pragma once

#include <filesystem>
#include <expected>
#include <memory>
#include <span>
#include <string>

#include "DxCaptureService.h"
#include "ProfileService.h"
#include "ProfilerViewerProtocol.h"

namespace ce::profiler_viewer
{
    // A verified, read-only pinned source. Retain this object for as long as a
    // recording index uses path(): it prevents replacement/write/delete races.
    class source_artifact
    {
    public:
        const std::filesystem::path& path() const;
        std::uint64_t size() const;
    private:
        struct implementation;
        explicit source_artifact(std::shared_ptr<implementation> value);
        std::shared_ptr<implementation> implementation_;
        friend class client;
        friend std::expected<std::shared_ptr<const source_artifact>, std::string>
        open_source_artifact(const std::filesystem::path& path);
    };
    using source_artifact_ptr = std::shared_ptr<const source_artifact>;
    std::expected<source_artifact_ptr, std::string>
    open_source_artifact(const std::filesystem::path& path);

    struct client_snapshot
    {
        bool connected = false;
        target_identity target;
        live_summary summary;
        ce::recording_status recording;
        std::uint64_t recording_id = 0;
        counter_mask counters = 0;
        capture_session_ptr capture;
        std::uint64_t capture_generation = 0;
        std::uint64_t capture_epoch = 0;
        source_artifact_ptr recording_source;
        std::uint64_t recording_source_id = 0;
        // Retained completed file from before a new Record, never presented as
        // that new recording's source. Ring-only Clear preserves recording_source.
        source_artifact_ptr previous_recording_source;
        std::uint64_t previous_recording_id = 0;
        dx_capture::capture_status dx_status;
        source_artifact_ptr dx_source;
        std::shared_ptr<const std::vector<std::byte>> diagnostics;
        std::uint64_t revision = 0;
        std::uint64_t clear_revision = 0;
        bool clear_pending = false;
        bool dx_available = false;
        std::uint64_t command_revision = 0;
        command last_command = command::stop;
        bool command_accepted = false;
        bool command_pending = false;
        std::string command_message;
        std::uint64_t skipped_captures = 0;
        std::string message;
    };

    class client
    {
    public:
        client();
        ~client();
        client(const client&) = delete;
        client& operator=(const client&) = delete;
        bool connect(const connection_options& options);
        void disconnect();
        std::shared_ptr<const client_snapshot> snapshot() const;
        // Enqueues a bounded request stamped with the current target/session.
        // False means disconnected or full; never silently redirect to self.
        bool request(command kind, std::uint32_t value = 0);
        bool request_diagnostic(std::span<const std::byte> payload, const target_identity& expected_target);
        bool take_focus_request();
        std::uint32_t take_page_request();
        void note_frame_presented();
    private:
        struct state;
        std::shared_ptr<state> state_;
    };
}
