// Source fixtures only. Not built or executed as part of this change.
// Windows integration still needs real dual-process spoof/replay/kill/slow-peer
// coverage; these fixtures cover the shared framing, typed state and codec bounds.
#include "ProfilerViewerTransport.h"

#include <cstdio>
#include <cstdlib>

namespace profiler_viewer_transport_tests
{
    using namespace ce;
    using namespace ce::profiler_viewer;

    void check(bool value, const char* message)
    {
        if (!value)
        {
            std::fprintf(stderr, "ProfilerViewerTransportTests: %s\n", message);
            std::abort();
        }
    }

    void framing_rejects_replay_and_unknown_schema()
    {
        session_nonce nonce{};
        nonce[0] = 47;
        message_header header;
        header.nonce = nonce;
        header.sequence = 1;
        check(valid_header(header, nonce, 1), "fresh exact frame is accepted");
        check(!valid_header(header, nonce, 2), "sequence replay is rejected");
        auto wrong_nonce = nonce;
        wrong_nonce[15] ^= 1;
        check(!valid_header(header, wrong_nonce, 1), "foreign session nonce is rejected");
        header.payload_bytes = maximum_packet_bytes;
        check(!valid_header(header, nonce, 1), "payload excludes header and has a strict bound");
        header.payload_bytes = 0;
        header.version = protocol_version + 1;
        check(!valid_header(header, nonce, 1), "future version is not guessed");
        header.version = protocol_version;
        header.kind = static_cast<message_kind>(UINT16_MAX);
        check(!valid_header(header, nonce, 1), "unknown packet kind is rejected");
    }

    void deployed_viewer_is_editor_sibling()
    {
        check(transport::viewer_path(L"C:\\Build\\Bin\\x64-Debug\\Editor\\CreatorEditor.exe") ==
              L"C:\\Build\\Bin\\x64-Debug\\Tools\\ProfilerViewer\\ProfilerViewer.exe",
              "viewer is in Tools sibling to the Editor output directory");
        check(transport::viewer_path(L"C:\\Build\\Bin\\x64-Release\\Editor\\CreatorEditor.runtime.exe") ==
              L"C:\\Build\\Bin\\x64-Release\\Tools\\ProfilerViewer\\ProfilerViewer.exe",
              "runtime editor uses the same fixed sibling viewer path");
        check(transport::viewer_path(L"C:\\Unexpected\\CreatorEditor.exe").empty(),
              "an unexpected editor directory cannot choose a different launch layout");
    }

    void interleaved_lane_envelopes()
    {
        session_nonce nonce{};
        nonce[0] = 91;
        // A diagnostic begins and finishes between capture chunks. Transfer IDs
        // are lane-local; the authenticated packet sequence is global. A live
        // Windows acceptance fixture must additionally stream 128 MiB while
        // asserting diagnostic age stays within the unchanged owner lease.
        const std::array kinds{message_kind::capture_begin, message_kind::capture_part,
                               message_kind::diagnostics_begin, message_kind::diagnostics_part,
                               message_kind::capture_part, message_kind::goodbye};
        std::uint64_t sequence = 0;
        for (const auto kind : kinds)
        {
            message_header header;
            header.kind = kind;
            header.nonce = nonce;
            header.sequence = ++sequence;
            check(valid_header(header, nonce, sequence), "diagnostic and close envelopes may interleave a capture");
        }
        transport::writer heartbeat;
        heartbeat(std::uint64_t{123});
        transport::reader in{heartbeat.bytes};
        std::uint64_t presented = 0;
        check(in(presented) && in.finished() && presented == 123, "present count heartbeat is one exact typed scalar");
        check(maximum_diagnostic_bytes < maximum_capture_bytes, "the prioritized diagnostics lane has its own smaller bound");
    }

    client_snapshot typed_fixture()
    {
        client_snapshot value;
        value.target.session_generation = 77;
        value.capture_generation = 4;
        value.recording_id = 19;
        value.summary.state = recorder_state::recording;
        value.summary.gpu_issue_last_error = "Example GPU issue, including owned text";
        value.summary.collector.snapshots_built = 19;
        value.summary.memory_budget = kDefaultMemoryBudget;
        value.recording.state = recording_state::failed;
        value.recording.error = capture_file_error::checksum_mismatch;
        value.recording.source_losses.late_events = 42;
        value.dx_status.message = "Ordinary-token permission failure";
        value.dx_status.process.state = dx_capture::process_state::elevation_unavailable;
        value.dx_status.recording.issues = dx_capture::source_error;
        value.counters = counter_bit(counter_category::process) | counter_bit(counter_category::render);
        value.command_revision = 12;
        value.last_command = command::start_dx;
        value.command_message = "Start request rejected";
        value.clear_revision = 3;
        value.clear_pending = true;
        value.skipped_captures = 2;
        return value;
    }

    void typed_status_round_trip_and_truncation()
    {
        const auto source = typed_fixture();
        const auto bytes = transport::encode_status(source);
        client_snapshot target;
        target.target.session_generation = source.target.session_generation;
        check(transport::decode_status(bytes, target), "typed state round trips without copying string/optional object bytes");
        check(target.summary.gpu_issue_last_error == source.summary.gpu_issue_last_error,
              "owned GPU error text survives");
        check(target.recording.error == source.recording.error && target.recording.source_losses.late_events == 42,
              "optional recording error and losses survive");
        check(target.dx_status.process.state == dx_capture::process_state::elevation_unavailable &&
              !target.dx_status.process.elevated && target.dx_status.message == source.dx_status.message,
              "DX unavailable is explicit and never silently elevated");
        check(target.clear_revision == 3 && target.clear_pending && target.command_revision == 12,
              "control acknowledgment and clear completion remain distinct");
        check(target.recording_id == 19, "stable writer identity is independent of transient recorder states");
        for (std::size_t size = 0; size < bytes.size(); ++size)
        {
            auto shortened = target;
            check(!transport::decode_status(std::span(bytes).first(size), shortened), "every truncated status is rejected");
        }
        auto extended = bytes;
        extended.push_back(std::byte{0});
        check(!transport::decode_status(extended, target), "trailing bytes cannot hide an extension");
        target.target.session_generation += 1;
        check(!transport::decode_status(bytes, target), "old target session status is rejected");
    }

    void typed_ranges_do_not_wrap()
    {
        transport::writer bytes;
        bytes(UINT64_MAX);
        transport::reader enum_reader{bytes.bytes};
        recorder_state state{};
        check(!enum_reader(state), "64-bit wire enum cannot truncate into an allowed uint8 state");
        transport::reader bool_reader{bytes.bytes};
        bool flag = false;
        check(!bool_reader(flag), "noncanonical boolean is rejected");
        auto invalid = typed_fixture();
        invalid.counters = 0x80000000u;
        auto target = typed_fixture();
        check(!transport::decode_status(transport::encode_status(invalid), target), "unknown counter mask bits are rejected");
        invalid = typed_fixture();
        invalid.dx_status.process.elevated = true;
        check(!transport::decode_status(transport::encode_status(invalid), target), "unexpected elevated peer status is rejected");
    }

    void bounded_capture_round_trip()
    {
        frame_record frame;
        frame.engine_frame = 99;
        frame.tick_begin = 100;
        frame.tick_end = 200;
        capture_session capture({frame}, {}, {}, capture_environment{10000000}, true, 0);
        const auto encoded = encode_capture_bounded(capture, maximum_capture_bytes);
        check(encoded.has_value(), "small fixture fits preflight");
        const auto decoded = decode_capture_bounded(*encoded, maximum_capture_bytes);
        check(decoded.has_value() && (*decoded)->frame_count() == 1 &&
              (*decoded)->environment().ticks_per_second == 10000000, "stored QPC and frame survive the original file codec");
        check(!encode_capture_bounded(capture, 1), "encode checks cap before allocating output");
        check(!decode_capture_bounded(*encoded, encoded->size() - 1), "decode checks cap before allocating owned events");
        std::vector<frame_record> too_many(4097);
        capture_session oversized(std::move(too_many), {}, {}, capture_environment{10000000}, false, 1);
        check(!encode_capture_bounded(oversized, maximum_capture_bytes), "frame cardinality is bounded independently of bytes");
    }

    void disconnected_controls_and_unsafe_sources()
    {
        client viewer;
        check(!viewer.request(command::record), "disconnected viewer cannot record its own process");
        check(!viewer.request(command::clear), "disconnected clear is not silently accepted");
        const std::array<std::byte, 1> payload{};
        check(!viewer.request_diagnostic(payload, {}), "disconnected diagnostics controls are disabled");
        check(!open_source_artifact(L"relative.ceprof"), "relative file handoff is rejected");
        check(!open_source_artifact(L"\\\\server\\share\\capture.ceprof"), "remote source handoff is rejected");
        check(!open_source_artifact(L"C:\\capture.ceprof:secret"), "alternate data streams are rejected");
        check(!transport::peer_identity(GetCurrentProcess(), GetCurrentProcessId(), 0, 1),
              "PID without exact creation time is insufficient identity");
    }
}

int main()
{
    using namespace profiler_viewer_transport_tests;
    framing_rejects_replay_and_unknown_schema();
    deployed_viewer_is_editor_sibling();
    interleaved_lane_envelopes();
    typed_status_round_trip_and_truncation();
    typed_ranges_do_not_wrap();
    bounded_capture_round_trip();
    disconnected_controls_and_unsafe_sources();
    return 0;
}
