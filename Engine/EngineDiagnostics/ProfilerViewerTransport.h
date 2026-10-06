#pragma once

// Private Windows transport/codec shared by the two ordinary-token endpoints.
#include "DxCaptureSecurity.h"
#include "ProfilerViewerClient.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <type_traits>

namespace ce::profiler_viewer::transport
{
    namespace security = dx_capture::security;

    struct writer
    {
        std::vector<std::byte> bytes;
        template<class T> void put(T value)
        {
            static_assert(std::is_unsigned_v<T>);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::byte>(value & 255));
                value >>= 8;
            }
        }
        template<class T> bool operator()(const T& value)
        {
            put<std::uint64_t>(static_cast<std::uint64_t>(value));
            return true;
        }
        bool operator()(const std::string& value)
        {
            const auto size = static_cast<std::uint32_t>(std::min<std::size_t>(value.size(), 4096));
            put(size);
            const auto* data = reinterpret_cast<const std::byte*>(value.data());
            bytes.insert(bytes.end(), data, data + size);
            return true;
        }
    };

    struct reader
    {
        std::span<const std::byte> bytes;
        std::size_t offset = 0;
        template<class T> bool get(T& value)
        {
            static_assert(std::is_unsigned_v<T>);
            if (sizeof(T) > bytes.size() - offset)
            {
                return false;
            }
            value = 0;
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                value |= static_cast<T>(std::to_integer<std::uint8_t>(bytes[offset++])) << (index * 8);
            }
            return true;
        }
        template<class T> bool operator()(T& value)
        {
            std::uint64_t integer = 0;
            if (!get(integer))
            {
                return false;
            }
            if constexpr (!std::is_enum_v<T>)
            {
                if (integer > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
                {
                    return false;
                }
            }
            else if (integer > static_cast<std::uint64_t>(std::numeric_limits<std::underlying_type_t<T>>::max()))
            {
                return false;
            }
            value = static_cast<T>(integer);
            return true;
        }
        bool operator()(std::string& value)
        {
            std::uint32_t size = 0;
            if (!get(size) || size > 4096 || size > bytes.size() - offset)
            {
                return false;
            }
            value.assign(reinterpret_cast<const char*>(bytes.data() + offset), size);
            offset += size;
            return true;
        }
        bool finished() const { return offset == bytes.size(); }
    };

    template<class Codec, class Summary> bool summary_fields(Codec& codec, Summary& value)
    {
        auto& timing = value.collector;
        return codec(value.state) && codec(value.engine_frame) && codec(value.retained_frames) &&
            codec(value.last_frame_events) && codec(value.peak_frame_events) && codec(value.total_events) &&
            codec(value.dropped_events) && codec(value.unbalanced_scopes) && codec(value.thread_count) &&
            codec(value.registered_markers) && codec(value.memory_bytes) && codec(value.memory_budget) &&
            codec(value.free_chunks) && codec(value.chunk_count) && codec(value.page_pool_bytes) &&
            codec(value.late_spans_placed) && codec(value.late_spans_dropped) && codec(value.dropped_counters) &&
            codec(value.late_spans_waiting) && codec(value.pause_unacked_streams) &&
            codec(value.capture_unacked_streams) && codec(value.capture_complete) &&
            codec(value.foreign_stream_touches) && codec(value.abandoned_streams) &&
            codec(value.control_requests_deferred) && codec(value.collector_queued_frames) &&
            codec(value.collector_dropped_frames) && codec(value.collector_os_thread_id) &&
            codec(timing.wait_ticks) && codec(timing.queue_delay_ticks) && codec(timing.page_ingest_ticks) &&
            codec(timing.frame_close_ticks) && codec(timing.snapshot_ticks) && codec(timing.replenish_ticks) &&
            codec(timing.queued_frames_started) && codec(timing.ingest_batches) && codec(timing.frames_closed) &&
            codec(timing.snapshots_built) && codec(value.gpu_query_overflow_passes) &&
            codec(value.gpu_collect_failures) && codec(value.gpu_issue_last_frame) &&
            codec(value.gpu_issue_last_error) && codec(value.stale_chunks_dropped) &&
            codec(value.malformed_pages) && codec(value.ingested_pages) &&
            codec(value.late_events_placed) && codec(value.late_events_dropped);
    }

    template<class Codec, class Status> bool recording_fields(Codec& codec, Status& value)
    {
        return codec(value.state) && codec(value.queued_bytes) && codec(value.queued_batches) &&
            codec(value.written_bytes) && codec(value.flushed_bytes) && codec(value.written_frames) &&
            codec(value.submitted_frames) && codec(value.first_tick) && codec(value.last_tick) &&
            codec(value.dropped_frames) && codec(value.dropped_events) && codec(value.dropped_counters) &&
            codec(value.source_dropped_counters) && codec(value.source_losses.dropped_events) &&
            codec(value.source_losses.dropped_frame_boundaries) && codec(value.source_losses.late_events) &&
            codec(value.source_losses.late_gpu_spans);
    }

    template<class Codec, class Status> bool dx_fields(Codec& codec, Status& value)
    {
        auto& recording = value.recording;
        return codec(value.session_id) && codec(value.state) && codec(value.busy) && codec(value.opening) && codec(value.process.state) &&
            codec(value.process.win32_error) && codec(value.process.dropped_records) &&
            codec(value.process.elevated) && codec(value.message) && codec(recording.valid_bytes) &&
            codec(recording.file_bytes) && codec(recording.dropped_records) && codec(recording.reported_loss_count) &&
            codec(recording.etw_events_lost) && codec(recording.etw_buffers_lost) && codec(recording.first_qpc) &&
            codec(recording.last_qpc) && codec(recording.record_count) && codec(recording.execution_count) &&
            codec(recording.work_count) && codec(recording.submission_count) && codec(recording.unmatched_executions) &&
            codec(recording.ambiguous_executions) && codec(recording.issues) && codec(recording.last_source_status) &&
            codec(recording.source_stopped) && codec(recording.finalized) && codec(recording.complete);
    }

    inline std::vector<std::byte> encode_status(const client_snapshot& value)
    {
        writer out;
        out(value.target.session_generation);
        out(value.capture_generation);
        out(value.recording_id);
        summary_fields(out, value.summary);
        recording_fields(out, value.recording);
        out(value.recording.error ? static_cast<std::uint64_t>(*value.recording.error) + 1 : 0);
        out(value.counters);
        dx_fields(out, value.dx_status);
        out(value.clear_revision);
        out(value.clear_pending);
        out(value.dx_available);
        out(value.command_revision);
        out(value.last_command);
        out(value.command_accepted);
        out(value.command_dx_error);
        out(value.command_dx_session_id);
        out(value.command_message);
        out(value.skipped_captures);
        out(value.message);
        return std::move(out.bytes);
    }

    inline bool decode_status(std::span<const std::byte> bytes, client_snapshot& value)
    {
        reader in{bytes};
        std::uint64_t generation = 0;
        std::uint64_t error = 0;
        if (!in(generation) || generation != value.target.session_generation || !in(value.capture_generation) ||
            value.capture_generation == 0 || !in(value.recording_id) ||
            !summary_fields(in, value.summary) || !recording_fields(in, value.recording) || !in(error) ||
            error > static_cast<std::uint64_t>(capture_file_error::canceled) + 1 ||
            !in(value.counters) || !dx_fields(in, value.dx_status) || !in(value.clear_revision) ||
            !in(value.clear_pending) || !in(value.dx_available) || !in(value.command_revision) ||
            !in(value.last_command) || !in(value.command_accepted) || !in(value.command_dx_error) ||
            !in(value.command_dx_session_id) || !in(value.command_message) || !in(value.skipped_captures) ||
            !in(value.message) || !in.finished() || value.summary.state > recorder_state::starting ||
            value.recording.state > recording_state::failed || (value.counters & ~255u) != 0 ||
            value.dx_status.state > dx_capture::capture_state::unavailable ||
            value.dx_status.process.state > dx_capture::process_state::failed ||
            value.dx_status.process.elevated || value.dx_status.recording.last_source_status > dx_capture::status_code::disconnected ||
            (value.dx_status.recording.issues & ~1023u) != 0 || value.last_command < command::record ||
            value.last_command > command::stop_dx || value.command_dx_error > dx_capture::control_error::failed ||
            (value.command_accepted && value.command_dx_error != dx_capture::control_error::none) ||
            (is_dx_command(value.last_command) &&
             (value.command_accepted ? value.command_dx_session_id == 0 : value.command_dx_error == dx_capture::control_error::none)) ||
            (!is_dx_command(value.last_command) &&
             (value.command_dx_session_id != 0 || value.command_dx_error != dx_capture::control_error::none)))
        {
            return false;
        }
        value.recording.error = error ? std::optional(static_cast<capture_file_error>(error - 1)) : std::nullopt;
        return true;
    }

    inline std::wstring pipe_name(const connection_options& options)
    {
        return L"\\\\.\\pipe\\CreatorEngine.ProfilerViewer.v2." + std::to_wstring(options.target_pid) +
               L"." + security::nonce_text(options.nonce);
    }

    inline std::wstring viewer_path(const std::wstring& engine)
    {
        const auto editor_directory = security::directory(engine);
        const auto last = editor_directory.find_last_of(L'\\');
        if (!security::editor_image(engine) || last == std::wstring::npos ||
            !security::equal_path(editor_directory.substr(last + 1), L"Editor"))
        {
            return {};
        }
        // Editor and Tools are siblings beneath Bin/<platform>-<configuration>.
        return security::directory(editor_directory) + L"\\Tools\\ProfilerViewer\\ProfilerViewer.exe";
    }

    inline bool peer_identity(HANDLE peer, DWORD pid, std::uint64_t creation, DWORD session)
    {
        bool elevated = false;
        return security::process_identity(peer, pid, creation, session) &&
               security::same_user(peer, GetCurrentProcess()) && security::same_logon(peer, GetCurrentProcess()) &&
               security::query_elevation(peer, elevated) && !elevated;
    }

    struct packet
    {
        message_kind kind = message_kind::hello;
        std::vector<std::byte> payload;
    };

    // A process-wide eight-slot allocation ceiling also bounds the pathological
    // case where the kernel does not acknowledge cancelled I/O within 100 ms.
    // Such an operation is quarantined for process lifetime, retaining its slot
    // and buffer. Never free pending OVERLAPPED storage or wait on it indefinitely.
    inline std::atomic<std::uint32_t> operation_slots{0};
    struct operation
    {
        OVERLAPPED overlapped{};
        security::handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        std::vector<std::byte> bytes;
        bool reserved = false;
        operation()
        {
            auto slots = operation_slots.load(std::memory_order_relaxed);
            while (slots < 8)
            {
                if (operation_slots.compare_exchange_weak(slots, slots + 1, std::memory_order_acq_rel))
                {
                    reserved = true;
                    break;
                }
            }
            overlapped.hEvent = event.get();
        }
        ~operation()
        {
            if (reserved)
            {
                operation_slots.fetch_sub(1, std::memory_order_release);
            }
        }
    };

    inline bool finish(HANDLE pipe, std::unique_ptr<operation>& pending, DWORD timeout,
                       HANDLE peer, HANDLE stop, DWORD& bytes)
    {
        HANDLE waits[3]{pending->event.get(), peer, stop};
        DWORD count = 1;
        if (peer)
        {
            waits[count++] = peer;
        }
        if (stop)
        {
            waits[count++] = stop;
        }
        const auto result = WaitForMultipleObjects(count, waits, FALSE, timeout);
        if (result == WAIT_OBJECT_0)
        {
            return GetOverlappedResult(pipe, &pending->overlapped, &bytes, FALSE) != FALSE;
        }
        CancelIoEx(pipe, &pending->overlapped);
        if (WaitForSingleObject(pending->event.get(), 100) != WAIT_OBJECT_0)
        {
            pending.release();
        }
        SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED);
        return false;
    }

    inline bool send(HANDLE pipe, message_kind kind, std::span<const std::byte> payload,
                     const session_nonce& nonce, std::uint64_t& sequence, HANDLE peer, HANDLE stop)
    {
        if (payload.size() > maximum_packet_bytes - wire_header_bytes || sequence == UINT64_MAX)
        {
            return false;
        }
        auto pending = std::make_unique<operation>();
        if (!pending->reserved || !pending->event)
        {
            return false;
        }
        writer out;
        out.put(protocol_magic);
        out.put(protocol_version);
        out.put(static_cast<std::uint16_t>(kind));
        out.put(static_cast<std::uint32_t>(payload.size()));
        for (auto byte : nonce)
        {
            out.put(byte);
        }
        out.put(++sequence);
        pending->bytes = std::move(out.bytes);
        pending->bytes.insert(pending->bytes.end(), payload.begin(), payload.end());
        DWORD bytes = 0;
        const BOOL complete = WriteFile(pipe, pending->bytes.data(), static_cast<DWORD>(pending->bytes.size()),
                                        &bytes, &pending->overlapped);
        if (!complete && (GetLastError() != ERROR_IO_PENDING ||
                          !finish(pipe, pending, io_timeout_ms, peer, stop, bytes)))
        {
            return false;
        }
        return bytes == pending->bytes.size();
    }

    inline bool receive(HANDLE pipe, packet& output, const session_nonce& nonce,
                        std::uint64_t& sequence, HANDLE peer, HANDLE stop)
    {
        DWORD available = 0;
        DWORD current = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, &current) ||
            current < wire_header_bytes || current > maximum_packet_bytes || sequence == UINT64_MAX)
        {
            return false;
        }
        auto pending = std::make_unique<operation>();
        if (!pending->reserved || !pending->event)
        {
            return false;
        }
        pending->bytes.resize(current);
        DWORD bytes = 0;
        const BOOL complete = ReadFile(pipe, pending->bytes.data(), current, &bytes, &pending->overlapped);
        if (!complete && (GetLastError() != ERROR_IO_PENDING ||
                          !finish(pipe, pending, io_timeout_ms, peer, stop, bytes)))
        {
            return false;
        }
        if (bytes != current)
        {
            return false;
        }
        reader in{pending->bytes};
        message_header header;
        std::uint16_t kind = 0;
        if (!in.get(header.magic) || !in.get(header.version) || !in.get(kind) || !in.get(header.payload_bytes))
        {
            return false;
        }
        header.kind = static_cast<message_kind>(kind);
        for (auto& byte : header.nonce)
        {
            if (!in.get(byte))
            {
                return false;
            }
        }
        if (!in.get(header.sequence) || !valid_header(header, nonce, sequence + 1) ||
            header.payload_bytes != current - wire_header_bytes)
        {
            return false;
        }
        output.kind = header.kind;
        output.payload.assign(pending->bytes.begin() + wire_header_bytes, pending->bytes.end());
        ++sequence;
        return true;
    }

    inline bool available(HANDLE pipe, bool& ready)
    {
        DWORD bytes = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes, nullptr))
        {
            return false;
        }
        ready = bytes != 0;
        return true;
    }

    inline bool wait_message(HANDLE pipe, HANDLE peer, HANDLE stop, DWORD timeout)
    {
        const auto deadline = GetTickCount64() + timeout;
        while (GetTickCount64() < deadline && WaitForSingleObject(peer, 0) == WAIT_TIMEOUT &&
               WaitForSingleObject(stop, 0) == WAIT_TIMEOUT)
        {
            bool ready = false;
            if (!available(pipe, ready))
            {
                return false;
            }
            if (ready)
            {
                return true;
            }
            WaitForSingleObject(stop, 5);
        }
        return false;
    }

    struct artifact_identity
    {
        std::uint32_t volume = 0;
        std::uint64_t file = 0;
        std::uint64_t bytes = 0;
        std::uint64_t written = 0;
        bool operator==(const artifact_identity&) const = default;
    };

    inline bool identify_file(HANDLE file, artifact_identity& result)
    {
        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(file, &information) || information.nNumberOfLinks != 1 ||
            (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0)
        {
            return false;
        }
        result.volume = information.dwVolumeSerialNumber;
        result.file = (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32) | information.nFileIndexLow;
        result.bytes = (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32) | information.nFileSizeLow;
        result.written = (static_cast<std::uint64_t>(information.ftLastWriteTime.dwHighDateTime) << 32) |
                         information.ftLastWriteTime.dwLowDateTime;
        return result.bytes != 0 && result.bytes <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    }

    inline bool bounded_artifact_path(const std::wstring& path)
    {
        return !path.empty() && path.size() <= 8192 &&
               std::count(path.begin(), path.end(), L'\\') <= 64 && path.find(L'\0') == std::wstring::npos;
    }

    inline std::vector<std::byte> encode_artifact(const security::pinned_image& pin, std::uint64_t generation,
                                                std::uint64_t source_id)
    {
        artifact_identity identity;
        if (pin.path.size() > 8192 || !identify_file(pin.file(), identity))
        {
            return {};
        }
        writer out;
        out(generation);
        out(source_id);
        out(identity.volume);
        out(identity.file);
        out(identity.bytes);
        out(identity.written);
        out.put(static_cast<std::uint32_t>(pin.path.size()));
        for (wchar_t value : pin.path)
        {
            out.put(static_cast<std::uint16_t>(value));
        }
        return std::move(out.bytes);
    }
}
