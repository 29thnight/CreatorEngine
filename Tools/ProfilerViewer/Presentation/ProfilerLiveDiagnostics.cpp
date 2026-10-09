#include "ProfilerLiveDiagnostics.h"
#include "ProfilerView.h"
#include "ImGui.h"

#include <atomic>
#include <chrono>
#include <string>
#include <utility>

namespace editor::profiler_view
{
    namespace
    {
        namespace dto = ce::profiler_viewer::diagnostics;
        using clock = std::chrono::steady_clock;

        struct decode_operation
        {
            std::shared_ptr<const std::vector<std::byte>> raw;
            std::shared_ptr<const dto::snapshot> decoded;
            std::uint64_t target_revision{};
            clock::time_point received_at{};
            std::atomic_bool done{};
            std::atomic_bool canceled{};
        };

        struct decode_admission
        {
            std::shared_ptr<decode_operation> operation;
            std::atomic_bool started{};
            ~decode_admission()
            {
                if (!started.load(std::memory_order_acquire))
                {
                    operation->done.store(true, std::memory_order_release);
                }
            }
        };

        struct diagnostic_view_state
        {
            ce::profiler_viewer::session_nonce nonce{};
            ce::profiler_viewer::target_identity target;
            std::uint64_t connection_generation{};
            std::uint64_t target_revision{};
            std::uint64_t next_command_id{};
            std::uint64_t pending_command_id{};
            std::shared_ptr<const std::vector<std::byte>> raw;
            std::shared_ptr<const std::vector<std::byte>> processed;
            std::shared_ptr<const dto::snapshot> decoded;
            std::shared_ptr<decode_operation> operation;
            clock::time_point received_at{};
            clock::time_point raw_received_at{};
            clock::time_point animation_requested_at{};
            clock::time_point command_requested_at{};
            std::string status;
            bool connected{};
            bool stopped{};
            int refreshed_frame{ -1 };
        };

        diagnostic_view_state& state()
        {
            static diagnostic_view_state value;
            return value;
        }

        void refresh()
        {
            auto& value = state();
            if (value.stopped)
            {
                return;
            }
            const int frame = ImGui::GetFrameCount();
            if (value.refreshed_frame == frame)
            {
                return;
            }
            // All page getters/actions in one UI frame observe one immutable
            // source epoch, even if the IPC worker publishes during drawing.
            value.refreshed_frame = frame;
            const auto remote = source().snapshot();
            const bool connected = remote && remote->connected;
            const bool changed_target = connected &&
                (remote->target.connection.nonce != value.nonce ||
                    remote->target.session_generation != value.connection_generation);
            if (changed_target)
            {
                ++value.target_revision;
                if (value.operation)
                {
                    value.operation->canceled.store(true, std::memory_order_release);
                }
                value.raw.reset();
                value.processed.reset();
                value.decoded.reset();
                value.pending_command_id = 0;
                value.status.clear();
                value.animation_requested_at = {};
                value.nonce = remote->target.connection.nonce;
                value.connection_generation = remote->target.session_generation;
                value.target = remote->target;
            }
            value.connected = connected;
            if (connected && remote->diagnostics && remote->diagnostics != value.raw)
            {
                // One latest raw value plus one in-flight decode. Intermediate
                // publications coalesce; decoding never runs on the UI thread.
                value.raw = remote->diagnostics;
                value.raw_received_at = clock::now();
            }
            if (value.operation && value.operation->done.load(std::memory_order_acquire))
            {
                const auto completed = std::move(value.operation);
                if (completed->target_revision == value.target_revision &&
                    !completed->canceled.load(std::memory_order_acquire))
                {
                    value.processed = completed->raw;
                    if (completed->decoded &&
                        (!value.decoded || completed->decoded->generation > value.decoded->generation))
                    {
                        value.decoded = completed->decoded;
                        value.received_at = completed->received_at;
                        if (value.pending_command_id != 0 &&
                            value.decoded->last_command_id == value.pending_command_id)
                        {
                            value.status = value.decoded->last_command_accepted ? "" :
                                "The target rejected the diagnostic request; refresh the selection and retry.";
                            value.pending_command_id = 0;
                        }
                    }
                    else
                    {
                        value.status = "A diagnostic snapshot could not be prepared; retaining the last valid snapshot.";
                    }
                }
            }
            if (!value.operation && value.raw && value.raw != value.processed)
            {
                auto operation = std::make_shared<decode_operation>();
                operation->raw = value.raw;
                operation->received_at = value.raw_received_at;
                operation->target_revision = value.target_revision;
                value.operation = operation;
                auto admission = std::make_shared<decode_admission>();
                admission->operation = operation;
                dispatch_diagnostics([operation, admission]
                {
                    admission->started.store(true, std::memory_order_release);
                    try
                    {
                        if (!operation->canceled.load(std::memory_order_acquire))
                        {
                            auto decoded = std::make_shared<dto::snapshot>();
                            if (dto::decode_snapshot(*operation->raw, *decoded))
                            {
                                operation->decoded = std::move(decoded);
                            }
                        }
                    }
                    catch (...)
                    {
                        operation->decoded.reset();
                    }
                    operation->done.store(true, std::memory_order_release);
                });
            }
            if (value.pending_command_id != 0 &&
                clock::now() - value.command_requested_at > std::chrono::seconds(2))
            {
                value.pending_command_id = 0;
                value.status = "No diagnostic acknowledgement was received; refresh the target and retry.";
            }
        }

        bool request(dto::command_kind kind, std::uint64_t payload, bool show_status)
        {
            if (!live_diagnostics_actions_enabled())
            {
                return false;
            }
            auto& value = state();
            dto::request command;
            command.kind = kind;
            command.command_id = ++value.next_command_id;
            command.generation = value.decoded->generation;
            command.scene_id = value.decoded->scene_id;
            command.value = payload;
            command.acknowledge = show_status;
            std::vector<std::byte> bytes;
            if (!dto::encode_request(command, bytes) || !source().request_diagnostic(bytes, value.target))
            {
                if (show_status)
                {
                    value.status = "Target disconnected or its command queue is full; retry when connected.";
                }
                return false;
            }
            if (show_status)
            {
                value.pending_command_id = command.command_id;
                value.command_requested_at = clock::now();
                value.status = "Diagnostic request queued for the target.";
            }
            return true;
        }
    }

    std::shared_ptr<const dto::snapshot> live_diagnostics()
    {
        refresh();
        return state().decoded;
    }

    bool live_diagnostics_actions_enabled()
    {
        refresh();
        const auto& value = state();
        return !value.stopped && value.connected && value.decoded &&
            clock::now() - value.received_at <= std::chrono::milliseconds(1500);
    }

    std::uint64_t live_target_revision()
    {
        refresh();
        return state().target_revision;
    }

    void shutdown_live_diagnostics()
    {
        auto& value = state();
        value.stopped = true;
        if (value.operation)
        {
            value.operation->canceled.store(true, std::memory_order_release);
        }
    }

    bool request_memory_snapshot()
    {
        return request(dto::command_kind::capture_memory, 0, true);
    }

    bool request_animation_snapshot(std::uint64_t animator_id, bool force)
    {
        const auto now = clock::now();
        auto& value = state();
        if (!force && now - value.animation_requested_at < std::chrono::milliseconds(500))
        {
            return true;
        }
        if (!request(dto::command_kind::request_animation, animator_id, force))
        {
            return false;
        }
        value.animation_requested_at = now;
        return true;
    }

    bool request_rendering_command(dto::rendering_command command)
    {
        return request(dto::command_kind::rendering, static_cast<std::uint64_t>(command), true);
    }

    const char* live_diagnostic_status()
    {
        refresh();
        const auto& value = state();
        if (!value.connected)
        {
            return value.decoded ? "Target disconnected. Showing its last snapshot; controls are disabled." :
                "No connected live target. File captures do not contain scene diagnostics.";
        }
        if (!value.decoded)
        {
            return value.status.empty() ? "Waiting for the target's live diagnostic snapshot." : value.status.c_str();
        }
        if (clock::now() - value.received_at > std::chrono::milliseconds(1500))
        {
            return "Live diagnostics are stale. Showing the last snapshot; controls are disabled.";
        }
        return value.status.c_str();
    }
}
