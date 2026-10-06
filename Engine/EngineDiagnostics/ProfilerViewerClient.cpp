#include "ProfilerViewerClient.h"
#include "ProfilerViewerTransport.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <future>
#include <thread>

#pragma comment(lib, "Advapi32.lib")

namespace ce::profiler_viewer
{
    struct source_artifact::implementation
    {
        transport::security::pinned_image pin;
        std::filesystem::path path;
        transport::artifact_identity identity;
    };

    source_artifact::source_artifact(std::shared_ptr<implementation> value) : implementation_(std::move(value)) {}
    const std::filesystem::path& source_artifact::path() const { return implementation_->path; }
    std::uint64_t source_artifact::size() const { return implementation_->identity.bytes; }

    std::expected<source_artifact_ptr, std::string> open_source_artifact(const std::filesystem::path& path)
    {
        try
        {
            auto value = std::make_shared<source_artifact::implementation>();
            // Reject UNC/devices, alternate streams, junctions/reparse points,
            // hard links and write/delete sharing. All components remain pinned.
            if (!transport::bounded_artifact_path(path.wstring()) || !value->pin.open(path.wstring(), false) ||
                !transport::identify_file(value->pin.file(), value->identity))
            {
                return std::unexpected("Capture source must be an existing read-only local regular file on a fixed drive (no links or reparse paths)");
            }
            value->path = value->pin.path;
            return source_artifact_ptr(new source_artifact(std::move(value)));
        }
        catch (...)
        {
            return std::unexpected("Capture source could not be pinned");
        }
    }

    struct client::state
    {
        std::atomic_bool active{false};
        std::atomic_bool attempted{false};
        std::atomic_bool closed{false};
        std::atomic_bool focus{false};
        std::atomic<std::uint32_t> page{0};
        std::atomic<std::uint64_t> presented_frames{0};
        transport::security::handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        mutable std::mutex mutex;
        std::shared_ptr<const client_snapshot> published = std::make_shared<const client_snapshot>();
        std::deque<transport::packet> commands;
        bool command_pending = false;
        std::uint64_t command_baseline = 0;

        void publish(client_snapshot& value)
        {
            ++value.revision;
            std::lock_guard lock(mutex);
            if (!value.connected || value.command_revision > command_baseline)
            {
                command_pending = false;
            }
            value.command_pending = command_pending;
            published = std::make_shared<const client_snapshot>(value);
        }

        void run(connection_options options)
        {
            using namespace transport;
            client_snapshot current;
            current.target.connection = options;
            current.message = "Connecting to target engine";
            struct completion
            {
                state& owner;
                client_snapshot& current;
                ~completion()
                {
                    current.connected = false;
                    current.clear_pending = false;
                    if (current.message.empty())
                    {
                        current.message = "Target disconnected; last immutable capture retained";
                    }
                    try
                    {
                        owner.publish(current);
                    }
                    catch (...)
                    {
                    }
                    std::lock_guard lock(owner.mutex);
                    owner.commands.clear();
                    owner.active.store(false, std::memory_order_release);
                }
            } completed{*this, current};
            try
            {
                bool elevated = false;
                DWORD own_session = 0;
                security::handle target(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, options.target_pid));
                if (!security::query_elevation(GetCurrentProcess(), elevated) || elevated ||
                    !peer_identity(target.get(), options.target_pid, options.target_creation_time, options.windows_session_id) ||
                    !ProcessIdToSessionId(GetCurrentProcessId(), &own_session) || own_session != options.windows_session_id)
                {
                    current.message = "Target identity or ordinary-privilege token was rejected";
                    return;
                }
                const auto engine_path = security::image_path(target.get());
                const auto self_path = security::image_path(GetCurrentProcess());
                security::pinned_image engine_pin, self_pin;
                if (!security::editor_image(engine_path) || !security::equal_path(self_path, viewer_path(engine_path)) ||
                    !engine_pin.open(engine_path, false) || !self_pin.open(self_path, false))
                {
                    current.message = "Viewer and target must use the verified fixed installation paths";
                    return;
                }
                security::handle pipe;
                const auto deadline = GetTickCount64() + connection_timeout_ms;
                while (GetTickCount64() < deadline && WaitForSingleObject(target.get(), 0) == WAIT_TIMEOUT &&
                       WaitForSingleObject(stop.get(), 0) == WAIT_TIMEOUT)
                {
                    pipe.reset(CreateFileW(pipe_name(options).c_str(), FILE_READ_DATA | FILE_WRITE_DATA |
                        FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
                    if (pipe)
                    {
                        break;
                    }
                    const auto error = GetLastError();
                    if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND)
                    {
                        current.message = "Cannot open the target's private local pipe";
                        return;
                    }
                    WaitForSingleObject(stop.get(), 25);
                }
                ULONG server = 0;
                DWORD mode = PIPE_READMODE_MESSAGE;
                if (!pipe || !GetNamedPipeServerProcessId(pipe.get(), &server) || server != options.target_pid ||
                    !peer_identity(target.get(), server, options.target_creation_time, options.windows_session_id) ||
                    !SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr))
                {
                    current.message = "Private pipe server authentication failed";
                    return;
                }
                std::uint64_t sent = 0;
                std::uint64_t received = 0;
                writer hello;
                hello(GetCurrentProcessId());
                hello(security::creation_time(GetCurrentProcess()));
                hello(own_session);
                packet welcome;
                if (!send(pipe.get(), message_kind::hello, hello.bytes, options.nonce, sent, target.get(), nullptr) ||
                    !wait_message(pipe.get(), target.get(), stop.get(), connection_timeout_ms) ||
                    !receive(pipe.get(), welcome, options.nonce, received, target.get(), stop.get()) ||
                    welcome.kind != message_kind::welcome)
                {
                    current.message = "Target protocol handshake failed";
                    return;
                }
                reader in{welcome.payload};
                connection_options echoed;
                if (!in(echoed.target_pid) || !in(echoed.target_creation_time) || !in(echoed.windows_session_id) ||
                    !in(current.target.qpc_frequency) || !in(current.target.session_generation) || !in.finished() ||
                    echoed.target_pid != options.target_pid || echoed.target_creation_time != options.target_creation_time ||
                    echoed.windows_session_id != options.windows_session_id || current.target.qpc_frequency == 0 ||
                    current.target.qpc_frequency > 1000000000 || current.target.session_generation == 0)
                {
                    current.message = "Target identity, session generation or QPC clock mismatch";
                    return;
                }
                current.message = "Waiting for target state";
                publish(current);
                auto heartbeat_at = std::uint64_t{0};
                auto last_received = GetTickCount64();
                std::vector<std::byte> incoming;
                std::vector<std::byte> diagnostic_incoming;
                std::uint32_t diagnostic_expected_size = 0;
                std::uint64_t diagnostic_transfer_id = 0;
                std::uint64_t diagnostic_previous_transfer = 0;
                std::uint32_t expected_size = 0;
                std::uint64_t transfer_id = 0;
                std::uint64_t previous_transfer = 0;
                std::uint64_t incoming_clear_revision = 0;
                std::uint64_t decoding_clear_revision = 0;
                std::uint64_t queued_clear_revision = 0;
                std::uint64_t incoming_capture_generation = 0;
                std::uint64_t queued_capture_generation = 0;
                std::uint64_t decoding_capture_generation = 0;
                std::future<std::expected<capture_session_ptr, capture_file_error>> decoding;
                std::shared_ptr<std::vector<std::byte>> queued_decode;
                message_kind incoming_kind = message_kind::capture_part;
                bool healthy = true;
                bool outgoing_order_known = true;
                while (healthy && WaitForSingleObject(stop.get(), 0) == WAIT_TIMEOUT &&
                       WaitForSingleObject(target.get(), 0) == WAIT_TIMEOUT)
                {
                    const auto now = GetTickCount64();
                    if (now - heartbeat_at >= 100)
                    {
                        writer heartbeat;
                        heartbeat(presented_frames.load(std::memory_order_relaxed));
                        // Intentional Close never cancels a packet midway. Each
                        // write completes or reaches its existing 500 ms bound;
                        // the next ordered packet can then be goodbye.
                        healthy = send(pipe.get(), message_kind::heartbeat, heartbeat.bytes, options.nonce, sent, target.get(), nullptr);
                        outgoing_order_known = healthy;
                        heartbeat_at = now;
                    }
                    std::deque<packet> requests;
                    {
                        std::lock_guard lock(mutex);
                        requests.swap(commands);
                    }
                    for (const auto& request : requests)
                    {
                        if (closed.load(std::memory_order_acquire))
                        {
                            break;
                        }
                        if (!healthy || !send(pipe.get(), request.kind, request.payload, options.nonce, sent, target.get(), nullptr))
                        {
                            healthy = false;
                            outgoing_order_known = false;
                            break;
                        }
                    }
                    bool changed = false;
                    if (healthy && decoding.valid() && decoding.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                    {
                        const auto decoded = decoding.get();
                        healthy = decoded && (*decoded)->environment().ticks_per_second == current.target.qpc_frequency;
                        if (healthy && decoding_clear_revision == current.clear_revision &&
                            decoding_capture_generation == current.capture_generation)
                        {
                            current.capture = *decoded;
                            current.capture_epoch = decoding_capture_generation;
                            changed = true;
                        }
                    }
                    if (healthy && !decoding.valid() && queued_decode)
                    {
                        std::packaged_task<std::expected<capture_session_ptr, capture_file_error>()> task(
                            [bytes = std::move(queued_decode)] { return decode_capture_bounded(*bytes, maximum_capture_bytes); });
                        decoding_clear_revision = queued_clear_revision;
                        decoding_capture_generation = queued_capture_generation;
                        decoding = task.get_future();
                        std::thread(std::move(task)).detach();
                    }
                    for (std::uint32_t index = 0; healthy && index < 16; ++index)
                    {
                        bool ready = false;
                        if (!available(pipe.get(), ready))
                        {
                            healthy = false;
                            break;
                        }
                        if (!ready)
                        {
                            break;
                        }
                        packet value;
                        if (!receive(pipe.get(), value, options.nonce, received, target.get(), stop.get()))
                        {
                            healthy = false;
                            break;
                        }
                        last_received = GetTickCount64();
                        if (value.kind == message_kind::status)
                        {
                            const auto clear_revision = current.clear_revision;
                            const auto capture_generation = current.capture_generation;
                            auto candidate = current;
                            healthy = decode_status(value.payload, candidate) &&
                                      candidate.capture_generation >= current.capture_generation &&
                                      candidate.clear_revision >= current.clear_revision &&
                                      candidate.command_revision >= current.command_revision;
                            if (healthy)
                            {
                                current = std::move(candidate);
                                current.connected = true;
                            }
                            if (healthy && (current.clear_revision != clear_revision ||
                                            current.capture_generation != capture_generation))
                            {
                                current.capture.reset();
                                current.capture_epoch = 0;
                            }
                            if (healthy && current.recording_source && current.recording_source_id != current.recording_id)
                            {
                                current.previous_recording_source = std::move(current.recording_source);
                                current.previous_recording_id = current.recording_source_id;
                                current.recording_source_id = 0;
                            }
                            changed = true;
                        }
                        else if (value.kind == message_kind::heartbeat && value.payload.empty())
                        {
                            // Target transport is alive while the owner prepares its first publication.
                        }
                        else if (value.kind == message_kind::focus)
                        {
                            reader in{value.payload};
                            std::uint32_t requested_page = 0;
                            healthy = in(requested_page) && in.finished() && requested_page <= 1;
                            if (healthy)
                            {
                                focus.store(true, std::memory_order_release);
                                if (requested_page)
                                {
                                    page.store(requested_page, std::memory_order_release);
                                }
                            }
                        }
                        else if (value.kind == message_kind::diagnostics_begin)
                        {
                            reader in{value.payload};
                            std::uint64_t generation = 0;
                            std::uint64_t clear_revision = 0;
                            std::uint64_t capture_generation = 0;
                            healthy = diagnostic_expected_size == 0 && in(generation) && in(diagnostic_transfer_id) &&
                                      in(diagnostic_expected_size) && in(clear_revision) && in(capture_generation) && in.finished() &&
                                      generation == current.target.session_generation &&
                                      diagnostic_transfer_id == diagnostic_previous_transfer + 1 &&
                                      diagnostic_expected_size != 0 && diagnostic_expected_size <= maximum_diagnostic_bytes &&
                                      clear_revision == 0 && capture_generation == 0;
                            if (healthy)
                            {
                                diagnostic_incoming.clear();
                                diagnostic_incoming.reserve(diagnostic_expected_size);
                            }
                        }
                        else if (value.kind == message_kind::diagnostics_part)
                        {
                            reader in{value.payload};
                            std::uint64_t id = 0;
                            std::uint32_t offset = 0;
                            healthy = diagnostic_expected_size != 0 && in(id) && in(offset) &&
                                      id == diagnostic_transfer_id && offset == diagnostic_incoming.size() &&
                                      in.offset < value.payload.size() && value.payload.size() - in.offset <=
                                      diagnostic_expected_size - diagnostic_incoming.size();
                            if (healthy)
                            {
                                diagnostic_incoming.insert(diagnostic_incoming.end(), value.payload.begin() + in.offset, value.payload.end());
                                if (diagnostic_incoming.size() == diagnostic_expected_size)
                                {
                                    current.diagnostics = std::make_shared<const std::vector<std::byte>>(std::move(diagnostic_incoming));
                                    diagnostic_incoming.clear();
                                    diagnostic_expected_size = 0;
                                    diagnostic_previous_transfer = diagnostic_transfer_id;
                                    changed = true;
                                }
                            }
                        }
                        else if (value.kind == message_kind::capture_begin)
                        {
                            reader in{value.payload};
                            std::uint64_t generation = 0;
                            healthy = expected_size == 0 && in(generation) && in(transfer_id) && in(expected_size) &&
                                in(incoming_clear_revision) && in(incoming_capture_generation) && in.finished() &&
                                generation == current.target.session_generation && transfer_id == previous_transfer + 1 &&
                                expected_size != 0 && expected_size <= maximum_capture_bytes;
                            if (healthy)
                            {
                                incoming.clear();
                                incoming.reserve(expected_size);
                                incoming_kind = message_kind::capture_part;
                            }
                        }
                        else if (value.kind == message_kind::capture_part)
                        {
                            reader in{value.payload};
                            std::uint64_t id = 0;
                            std::uint32_t offset = 0;
                            healthy = expected_size != 0 && value.kind == incoming_kind && in(id) && in(offset) &&
                                      id == transfer_id && offset == incoming.size() && in.offset < value.payload.size() &&
                                      value.payload.size() - in.offset <= expected_size - incoming.size();
                            if (healthy)
                            {
                                incoming.insert(incoming.end(), value.payload.begin() + in.offset, value.payload.end());
                                if (incoming.size() == expected_size)
                                {
                                    queued_decode = std::make_shared<std::vector<std::byte>>(std::move(incoming));
                                    queued_clear_revision = incoming_clear_revision;
                                    queued_capture_generation = incoming_capture_generation;
                                    incoming.clear();
                                    expected_size = 0;
                                    previous_transfer = transfer_id;
                                    changed = true;
                                }
                            }
                        }
                        else if (value.kind == message_kind::cancel_blob)
                        {
                            reader in{value.payload};
                            std::uint64_t id = 0;
                            healthy = expected_size != 0 && in(id) && in.finished() && id == transfer_id;
                            if (healthy)
                            {
                                incoming.clear();
                                expected_size = 0;
                                previous_transfer = transfer_id;
                            }
                        }
                        else if (value.kind == message_kind::recording_artifact || value.kind == message_kind::dx_artifact)
                        {
                            std::uint64_t source_id = 0;
                            auto artifact = read_artifact(value.payload, current.target.session_generation, source_id);
                            healthy = artifact != nullptr && (value.kind == message_kind::recording_artifact ?
                                      source_id != 0 && source_id == current.recording_id : source_id == 0);
                            if (healthy)
                            {
                                if (value.kind == message_kind::recording_artifact)
                                {
                                    current.recording_source = std::move(artifact);
                                    current.recording_source_id = source_id;
                                }
                                else
                                {
                                    current.dx_source = std::move(artifact);
                                }
                                changed = true;
                            }
                        }
                        else
                        {
                            healthy = false;
                        }
                    }
                    if (changed && healthy)
                    {
                        publish(current);
                    }
                    if (GetTickCount64() - last_received > target_lease_ms)
                    {
                        healthy = false;
                    }
                    WaitForSingleObject(stop.get(), 5);
                }
                if (outgoing_order_known && closed.load(std::memory_order_acquire) &&
                    WaitForSingleObject(target.get(), 0) == WAIT_TIMEOUT)
                {
                    // Authenticated intent precedes backend/file-worker teardown;
                    // a still-alive closing viewer is not a failed launch.
                    send(pipe.get(), message_kind::goodbye, {}, options.nonce, sent, target.get(), nullptr);
                }
                current.message = healthy ? "Target stopped or disconnected; last immutable capture retained" :
                                            "Target transport closed or invalid data rejected; last immutable capture retained";
            }
            catch (...)
            {
                current.message = "Viewer transport resource failure; last immutable capture retained";
            }
        }

        source_artifact_ptr read_artifact(std::span<const std::byte> bytes, std::uint64_t generation,
                                          std::uint64_t& source_id)
        {
            transport::reader in{bytes};
            std::uint64_t received_generation = 0;
            transport::artifact_identity expected;
            std::uint32_t size = 0;
            if (!in(received_generation) || received_generation != generation || !in(source_id) || !in(expected.volume) ||
                !in(expected.file) || !in(expected.bytes) || !in(expected.written) || !in.get(size) ||
                size == 0 || size > 8192 || size * 2 != bytes.size() - in.offset)
            {
                return {};
            }
            std::wstring path;
            path.reserve(size);
            for (std::uint32_t index = 0; index < size; ++index)
            {
                std::uint16_t character = 0;
                if (!in.get(character) || character == 0)
                {
                    return {};
                }
                path.push_back(static_cast<wchar_t>(character));
            }
            auto artifact = open_source_artifact(path);
            if (!artifact || !in.finished() || (*artifact)->implementation_->identity != expected)
            {
                return {};
            }
            return *artifact;
        }
    };

    client::client() : state_(std::make_shared<state>()) {}
    client::~client() { disconnect(); }

    bool client::connect(const connection_options& options)
    {
        if (options.target_pid == 0 || options.target_creation_time == 0 || options.windows_session_id == 0 ||
            options.nonce == session_nonce{} || state_->closed.load(std::memory_order_acquire) || !state_->stop)
        {
            return false;
        }
        bool expected = false;
        if (!state_->attempted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return false;
        }
        state_->active.store(true, std::memory_order_release);
        try
        {
            std::thread([owner = state_, options] { owner->run(options); }).detach();
        }
        catch (...)
        {
            state_->active.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    void client::disconnect()
    {
        state_->closed.store(true, std::memory_order_release);
        if (state_->stop)
        {
            SetEvent(state_->stop.get());
        }
    }

    std::shared_ptr<const client_snapshot> client::snapshot() const
    {
        std::lock_guard lock(state_->mutex);
        return state_->published;
    }

    bool client::request(command kind, std::uint32_t value)
    {
        if (kind < command::record || kind > command::stop_dx ||
            (kind != command::counter_mask && value != 0) || (kind == command::counter_mask && (value & ~255u) != 0))
        {
            return false;
        }
        std::lock_guard lock(state_->mutex);
        if (state_->closed.load(std::memory_order_acquire) || !state_->published->connected ||
            state_->command_pending || state_->commands.size() >= maximum_pending_commands)
        {
            return false;
        }
        transport::writer out;
        out(state_->published->target.session_generation);
        out(state_->published->capture_generation);
        out(kind);
        out(value);
        state_->commands.push_back({message_kind::command, std::move(out.bytes)});
        state_->command_pending = true;
        state_->command_baseline = state_->published->command_revision;
        auto pending = std::make_shared<client_snapshot>(*state_->published);
        pending->command_pending = true;
        state_->published = std::move(pending);
        return true;
    }

    bool client::request_diagnostic(std::span<const std::byte> payload, const target_identity& expected_target)
    {
        if (payload.empty() || payload.size() > maximum_command_bytes)
        {
            return false;
        }
        std::lock_guard lock(state_->mutex);
        const auto& target = state_->published->target;
        if (state_->closed.load(std::memory_order_acquire) || !state_->published->connected ||
            state_->commands.size() >= maximum_pending_commands ||
            target.connection.target_pid != expected_target.connection.target_pid ||
            target.connection.target_creation_time != expected_target.connection.target_creation_time ||
            target.connection.windows_session_id != expected_target.connection.windows_session_id ||
            target.connection.nonce != expected_target.connection.nonce ||
            target.session_generation != expected_target.session_generation ||
            target.qpc_frequency != expected_target.qpc_frequency)
        {
            return false;
        }
        transport::writer out;
        out(state_->published->target.session_generation);
        out.bytes.insert(out.bytes.end(), payload.begin(), payload.end());
        state_->commands.push_back({message_kind::diagnostics_command, std::move(out.bytes)});
        return true;
    }

    bool client::take_focus_request() { return state_->focus.exchange(false, std::memory_order_acq_rel); }
    std::uint32_t client::take_page_request() { return state_->page.exchange(0, std::memory_order_acq_rel); }
    void client::note_frame_presented() { state_->presented_frames.fetch_add(1, std::memory_order_relaxed); }
}
