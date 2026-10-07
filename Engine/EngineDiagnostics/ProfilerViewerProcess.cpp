#include "ProfilerViewerProcess.h"
#include "ProfilerViewerTransport.h"
#include "ProfileScope.h"

#include <bcrypt.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <future>
#include <thread>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Bcrypt.lib")

namespace ce::profiler_viewer
{
    namespace engine_detail
    {
        using namespace transport;

        security::handle create_pipe(const connection_options& options)
        {
            std::vector<std::byte> sid;
            if (!security::logon_sid(GetCurrentProcess(), sid))
            {
                return {};
            }
            LPWSTR text = nullptr;
            if (!ConvertSidToStringSidW(sid.data(), &text))
            {
                return {};
            }
            security::local_memory release_text(text);
            const std::wstring descriptor_text = L"D:P(A;;0x0012019b;;;" + std::wstring(text) + L")";
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor_text.c_str(), SDDL_REVISION_1,
                                                                       &descriptor, nullptr))
            {
                return {};
            }
            security::local_memory release_descriptor(descriptor);
            SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
            return security::handle(CreateNamedPipeW(pipe_name(options).c_str(), PIPE_ACCESS_DUPLEX |
                FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, maximum_packet_bytes, maximum_packet_bytes, 0, &attributes));
        }

        std::wstring minimal_environment()
        {
            wchar_t system[MAX_PATH]{};
            wchar_t windows[MAX_PATH]{};
            const auto system_size = GetSystemDirectoryW(system, MAX_PATH);
            const auto windows_size = GetWindowsDirectoryW(windows, MAX_PATH);
            if (!system_size || system_size >= MAX_PATH || !windows_size || windows_size >= MAX_PATH)
            {
                return {};
            }
            std::wstring value = L"PATH=" + std::wstring(system);
            value.push_back(L'\0');
            value += L"SystemRoot=" + std::wstring(windows);
            value.push_back(L'\0');
            value += L"WINDIR=" + std::wstring(windows);
            value.push_back(L'\0');
            value.push_back(L'\0');
            return value;
        }

        BOOL CALLBACK focus_window(HWND window, LPARAM process)
        {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == static_cast<DWORD>(process) && GetWindow(window, GW_OWNER) == nullptr)
            {
                ShowWindowAsync(window, SW_RESTORE);
                SetForegroundWindow(window);
                return FALSE;
            }
            return TRUE;
        }

        BOOL CALLBACK close_window(HWND window, LPARAM process)
        {
            DWORD owner = 0;
            GetWindowThreadProcessId(window, &owner);
            if (owner == static_cast<DWORD>(process) && GetWindow(window, GW_OWNER) == nullptr)
            {
                PostMessageW(window, WM_CLOSE, 0, 0);
                return FALSE;
            }
            return TRUE;
        }

        struct queued_command
        {
            command kind{};
            std::uint64_t value = 0;
            std::uint64_t capture_generation = 0;
            std::vector<std::byte> diagnostic;
        };

        struct publication
        {
            client_snapshot snapshot;
            std::filesystem::path recording_path;
            std::filesystem::path dx_path;
            std::shared_ptr<const engine_process::diagnostic_encoder> diagnostic_encoder;
        };

        struct encoded_blob
        {
            std::vector<std::byte> bytes;
            message_kind begin = message_kind::capture_begin;
            std::uint64_t clear_revision = 0;
            std::uint64_t capture_generation = 0;
        };
    }

    struct engine_process::state : std::enable_shared_from_this<engine_process::state>
    {
        std::atomic_bool active{false};
        std::atomic_bool closed{false};
        std::atomic_bool focus{false};
        std::atomic_bool close_requested{false};
        std::atomic_bool encoding_busy{false};
        std::atomic_bool diagnostic_encoding_busy{false};
        std::atomic<std::uint32_t> page{0};
        std::uint32_t ui_scale_milli = 1000;
        transport::security::handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        mutable std::mutex mutex;
        launch_status launch;
        target_identity target;
        std::deque<engine_detail::queued_command> commands;
        std::shared_ptr<const engine_detail::publication> latest;
        diagnostic_publisher publish_diagnostics;
        diagnostic_handler apply_diagnostic;
        std::shared_ptr<const diagnostic_encoder> diagnostics;
        std::uint64_t next_publish = 0;
        std::uint64_t next_diagnostics = 0;
        std::uint64_t clear_ticket = 0;
        std::uint64_t clear_revision = 0;
        std::uint64_t command_revision = 0;
        command last_command = command::stop;
        bool command_accepted = false;
        dx_capture::control_error command_dx_error = dx_capture::control_error::none;
        std::uint64_t command_dx_session_id = 0;
        std::string command_message;

        void set_status(bool connected, std::string message, DWORD error = ERROR_SUCCESS, bool failed = true)
        {
            std::lock_guard lock(mutex);
            launch.connected = connected;
            launch.message = std::move(message);
            launch.win32_error = error;
            if (!connected && failed && !closed.load(std::memory_order_acquire))
            {
                ++launch.failure_revision;
            }
        }

        bool queue(const transport::packet& packet, std::uint64_t generation)
        {
            transport::reader in{packet.payload};
            std::uint64_t received_generation = 0;
            if (!in(received_generation) || received_generation != generation)
            {
                return false;
            }
            engine_detail::queued_command request;
            if (packet.kind == message_kind::command)
            {
                if (!in(request.capture_generation) || !in(request.kind) || !in(request.value) || !in.finished() ||
                    !valid_command_value(request.kind, request.value) ||
                    (is_dx_command(request.kind) && request.capture_generation != 0))
                {
                    return false;
                }
            }
            else if (packet.kind == message_kind::diagnostics_command)
            {
                const auto size = packet.payload.size() - in.offset;
                if (size == 0 || size > maximum_command_bytes)
                {
                    return false;
                }
                request.diagnostic.assign(packet.payload.begin() + in.offset, packet.payload.end());
            }
            else
            {
                return false;
            }
            std::lock_guard lock(mutex);
            if (commands.size() >= maximum_pending_commands)
            {
                return false;
            }
            commands.push_back(std::move(request));
            return true;
        }

        void run()
        {
            using namespace transport;
            using namespace engine_detail;
            struct completion
            {
                state& owner;
                ~completion()
                {
                    std::lock_guard lock(owner.mutex);
                    owner.launch.running = false;
                    owner.launch.connected = false;
                    owner.launch.viewer_pid = 0;
                    owner.close_requested.store(false, std::memory_order_release);
                    owner.commands.clear();
                    owner.active.store(false, std::memory_order_release);
                }
            } completed{*this};
            security::handle viewer;
            try
            {
                bool elevated = false;
                DWORD windows_session = 0;
                const auto engine_path = security::image_path(GetCurrentProcess());
                connection_options options;
                options.target_pid = GetCurrentProcessId();
                options.target_creation_time = security::creation_time(GetCurrentProcess());
                if (!security::query_elevation(GetCurrentProcess(), elevated) || elevated ||
                    !options.target_creation_time || !ProcessIdToSessionId(options.target_pid, &windows_session) ||
                    (options.windows_session_id = windows_session) == 0 || !security::editor_image(engine_path))
                {
                    set_status(false, "Viewer launch requires a verified ordinary-privilege Editor", ERROR_ACCESS_DENIED);
                    return;
                }
                security::pinned_image engine_pin, viewer_pin;
                const auto executable = viewer_path(engine_path);
                if (!engine_pin.open(engine_path, false) || !viewer_pin.open(executable, false) ||
                    BCryptGenRandom(nullptr, options.nonce.data(), static_cast<ULONG>(options.nonce.size()),
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || options.nonce == session_nonce{})
                {
                    set_status(false, "ProfilerViewer.exe is missing or its fixed installation path is unsafe", GetLastError());
                    return;
                }
                auto pipe = create_pipe(options);
                auto connection = std::make_unique<operation>();
                if (!pipe || !connection->reserved || !connection->event)
                {
                    set_status(false, "Cannot create private viewer transport", GetLastError());
                    return;
                }
                const BOOL connected = ConnectNamedPipe(pipe.get(), &connection->overlapped);
                const DWORD connect_error = connected ? ERROR_SUCCESS : GetLastError();
                bool pending = !connected && connect_error == ERROR_IO_PENDING;
                struct cancel_connection
                {
                    HANDLE pipe;
                    std::unique_ptr<operation>& pending_io;
                    bool& pending;
                    ~cancel_connection()
                    {
                        if (pending)
                        {
                            DWORD ignored = 0;
                            finish(pipe, pending_io, 0, nullptr, nullptr, ignored);
                        }
                    }
                } cancel{pipe.get(), connection, pending};
                if (!connected && !pending && connect_error != ERROR_PIPE_CONNECTED)
                {
                    set_status(false, "Cannot accept viewer connection", connect_error);
                    return;
                }
                auto environment = minimal_environment();
                auto command_line = L"\"" + executable + L"\" --parent " + std::to_wstring(options.target_pid) +
                    L" --created " + std::to_wstring(options.target_creation_time) + L" --session " +
                    std::to_wstring(options.windows_session_id) + L" --nonce " + security::nonce_text(options.nonce) +
                    L" --ui-scale-milli " + std::to_wstring(ui_scale_milli);
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION information{};
                if (environment.empty() || WaitForSingleObject(stop.get(), 0) != WAIT_TIMEOUT ||
                    !CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                                    CREATE_UNICODE_ENVIRONMENT, environment.data(),
                                    security::directory(executable).c_str(), &startup, &information))
                {
                    set_status(false, "ProfilerViewer.exe could not start", GetLastError());
                    return;
                }
                viewer.reset(information.hProcess);
                security::handle thread(information.hThread);
                const DWORD viewer_pid = GetProcessId(viewer.get());
                const auto viewer_created = security::creation_time(viewer.get());
                AllowSetForegroundWindow(viewer_pid);
                if (!peer_identity(viewer.get(), viewer_pid, viewer_created, options.windows_session_id) ||
                    !security::equal_path(security::image_path(viewer.get()), executable))
                {
                    set_status(false, "Viewer process identity rejected", ERROR_ACCESS_DENIED);
                }
                else
                {
                    {
                        std::lock_guard lock(mutex);
                        launch.viewer_pid = viewer_pid;
                    }
                    DWORD ignored = 0;
                    const bool accepted = !pending || finish(pipe.get(), connection, connection_timeout_ms,
                                                            viewer.get(), stop.get(), ignored);
                    pending = false;
                    ULONG peer = 0;
                    if (accepted && GetNamedPipeClientProcessId(pipe.get(), &peer) && peer == viewer_pid &&
                        peer_identity(viewer.get(), peer, viewer_created, options.windows_session_id))
                    {
                        serve(pipe.get(), viewer.get(), options, viewer_pid, viewer_created);
                    }
                    else
                    {
                        set_status(false, "Viewer connection was not authenticated", ERROR_ACCESS_DENIED);
                    }
                }
                pipe.reset();
            }
            catch (...)
            {
                set_status(false, "Viewer transport failed without affecting engine recording", ERROR_NOT_ENOUGH_MEMORY);
            }
            // Includes exception paths after launch: never create a second
            // viewer merely because its transport/encoder failed.
            while (viewer && WaitForSingleObject(stop.get(), 25) == WAIT_TIMEOUT &&
                   WaitForSingleObject(viewer.get(), 0) == WAIT_TIMEOUT)
            {
                if (focus.exchange(false, std::memory_order_acq_rel))
                {
                    EnumWindows(focus_window, static_cast<LPARAM>(GetProcessId(viewer.get())));
                }
                if (close_requested.exchange(false, std::memory_order_acq_rel))
                {
                    EnumWindows(close_window, static_cast<LPARAM>(GetProcessId(viewer.get())));
                }
            }
        }

        void serve(HANDLE pipe, HANDLE viewer, const connection_options& options,
                   DWORD viewer_pid, std::uint64_t viewer_created)
        {
            using namespace transport;
            std::uint64_t received = 0;
            std::uint64_t sent = 0;
            packet hello;
            if (!wait_message(pipe, viewer, stop.get(), connection_timeout_ms) ||
                !receive(pipe, hello, options.nonce, received, viewer, stop.get()) || hello.kind != message_kind::hello)
            {
                set_status(false, "Viewer handshake failed", ERROR_INVALID_DATA);
                return;
            }
            reader hello_in{hello.payload};
            std::uint32_t peer = 0;
            std::uint64_t created = 0;
            std::uint32_t session = 0;
            if (!hello_in(peer) || !hello_in(created) || !hello_in(session) || !hello_in.finished() ||
                peer != viewer_pid || created != viewer_created || session != options.windows_session_id)
            {
                set_status(false, "Viewer hello identity mismatch", ERROR_INVALID_DATA);
                return;
            }
            LARGE_INTEGER frequency{};
            if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            {
                set_status(false, "Target QPC clock is unavailable", ERROR_INVALID_DATA);
                return;
            }
            target_identity identity;
            identity.connection = options;
            identity.qpc_frequency = static_cast<std::uint64_t>(frequency.QuadPart);
            std::memcpy(&identity.session_generation, options.nonce.data(), sizeof(identity.session_generation));
            identity.session_generation |= 1;
            writer welcome;
            welcome(options.target_pid);
            welcome(options.target_creation_time);
            welcome(options.windows_session_id);
            welcome(identity.qpc_frequency);
            welcome(identity.session_generation);
            if (!send(pipe, message_kind::welcome, welcome.bytes, options.nonce, sent, viewer, stop.get()))
            {
                set_status(false, "Viewer welcome could not be delivered", GetLastError());
                return;
            }
            {
                std::lock_guard lock(mutex);
                target = identity;
                launch.connected = true;
                launch.message = "Profiler viewer connected";
                commands.clear();
            }
            auto heartbeat = GetTickCount64();
            std::uint64_t last_presented = 0;
            auto status_at = std::uint64_t{0};
            auto last_status_recording_id = UINT64_MAX;
            auto artifacts_at = std::uint64_t{0};
            std::string artifact_message;
            capture_session_ptr last_capture;
            std::shared_ptr<const diagnostic_encoder> last_diagnostics;
            std::future<engine_detail::encoded_blob> encoding;
            std::future<std::vector<std::byte>> diagnostic_encoding;
            std::vector<std::byte> diagnostic_outgoing;
            std::size_t diagnostic_offset = 0;
            std::uint64_t diagnostic_transfer_id = 0;
            std::vector<std::byte> outgoing;
            std::size_t outgoing_offset = 0;
            message_kind outgoing_kind = message_kind::capture_part;
            std::uint64_t transfer_id = 0;
            std::uint64_t outgoing_capture_generation = 0;
            std::uint64_t skipped = 0;
            security::pinned_image recording_pin, dx_pin;
            std::filesystem::path recording_path, last_dx_path;
            std::uint64_t recording_source_id = 0;
            std::uint64_t dx_source_id = 0;
            std::uint64_t last_status_dx_session_id = 0;
            auto last_status_dx_state = dx_capture::capture_state::idle;
            bool last_status_dx_busy = false;
            bool healthy = true;
            bool viewer_closing = false;
            while (healthy && WaitForSingleObject(stop.get(), 0) == WAIT_TIMEOUT &&
                   WaitForSingleObject(viewer, 0) == WAIT_TIMEOUT)
            {
                for (std::uint32_t index = 0; index < maximum_pending_commands; ++index)
                {
                    bool ready = false;
                    if (!available(pipe, ready))
                    {
                        healthy = false;
                        break;
                    }
                    if (!ready)
                    {
                        break;
                    }
                    packet request;
                    if (!receive(pipe, request, options.nonce, received, viewer, stop.get()))
                    {
                        healthy = false;
                        break;
                    }
                    if (request.kind == message_kind::heartbeat)
                    {
                        reader in{request.payload};
                        std::uint64_t presented = 0;
                        if (!in(presented) || !in.finished() || presented < last_presented)
                        {
                            healthy = false;
                            break;
                        }
                        last_presented = presented;
                        heartbeat = GetTickCount64();
                        std::lock_guard lock(mutex);
                        launch.presented_frames = presented;
                    }
                    else if (request.kind == message_kind::goodbye && request.payload.empty())
                    {
                        viewer_closing = true;
                        break;
                    }
                    else if (!queue(request, identity.session_generation))
                    {
                        healthy = false;
                        break;
                    }
                }
                const auto now = GetTickCount64();
                if (!healthy || viewer_closing || now - heartbeat > target_lease_ms)
                {
                    break;
                }
                if (focus.exchange(false, std::memory_order_acq_rel))
                {
                    writer value;
                    value(page.exchange(0, std::memory_order_acq_rel));
                    healthy = send(pipe, message_kind::focus, value.bytes, options.nonce, sent, viewer, stop.get());
                }
                if (close_requested.exchange(false, std::memory_order_acq_rel))
                {
                    viewer_closing = true;
                    EnumWindows(engine_detail::close_window, static_cast<LPARAM>(GetProcessId(viewer)));
                    break;
                }
                std::shared_ptr<const engine_detail::publication> publication;
                {
                    std::lock_guard lock(mutex);
                    publication = latest;
                }
                if (healthy && (now - status_at >= 100 ||
                                (publication && (publication->snapshot.recording_id != last_status_recording_id ||
                                                 publication->snapshot.dx_status.session_id != last_status_dx_session_id ||
                                                 publication->snapshot.dx_status.state != last_status_dx_state ||
                                                 publication->snapshot.dx_status.busy != last_status_dx_busy))))
                {
                    if (publication && publication->snapshot.target.session_generation == identity.session_generation)
                    {
                        client_snapshot current = publication->snapshot;
                        current.target = identity;
                        current.skipped_captures = skipped;
                        if (!artifact_message.empty())
                        {
                            current.message = artifact_message;
                        }
                        healthy = send(pipe, message_kind::status, encode_status(current), options.nonce, sent, viewer, stop.get());
                        last_status_recording_id = current.recording_id;
                        last_status_dx_session_id = current.dx_status.session_id;
                        last_status_dx_state = current.dx_status.state;
                        last_status_dx_busy = current.dx_status.busy;
                    }
                    else
                    {
                        healthy = send(pipe, message_kind::heartbeat, {}, options.nonce, sent, viewer, stop.get());
                    }
                    status_at = now;
                }
                if (healthy && publication && publication->snapshot.target.session_generation == identity.session_generation)
                {
                    const auto offer_file = [&](const std::filesystem::path& path, std::filesystem::path& previous,
                                                security::pinned_image& pin, message_kind kind,
                                                std::uint64_t source_id, std::uint64_t& previous_source_id)
                    {
                        if (path.empty() || (path == previous && source_id == previous_source_id))
                        {
                            return true;
                        }
                        security::pinned_image next;
                        if (!bounded_artifact_path(path.wstring()) || !next.open(path.wstring(), false))
                        {
                            artifact_message = "Capture source is not ready for validated read-only handoff";
                            return true; // Writer may still be closing; retry at the next status boundary.
                        }
                        auto bytes = encode_artifact(next, identity.session_generation, source_id);
                        if (bytes.empty() || !send(pipe, kind, bytes, options.nonce, sent, viewer, stop.get()))
                        {
                            return false;
                        }
                        pin = std::move(next);
                        previous = path;
                        previous_source_id = source_id;
                        artifact_message.clear();
                        return true;
                    };
                    if (now >= artifacts_at)
                    {
                        healthy = offer_file(publication->recording_path, recording_path, recording_pin, message_kind::recording_artifact,
                                             publication->snapshot.recording_id, recording_source_id) &&
                                  offer_file(publication->dx_path, last_dx_path, dx_pin, message_kind::dx_artifact,
                                             publication->snapshot.dx_status.session_id, dx_source_id);
                        artifacts_at = now + 500;
                    }
                    // Diagnostic freshness is independent of a large timeline
                    // transfer. Freeze/encode at most one DTO, coalesce pending
                    // DTOs, and service this small lane before capture chunks.
                    if (publication->diagnostic_encoder && publication->diagnostic_encoder != last_diagnostics &&
                        diagnostic_outgoing.empty() && !diagnostic_encoding.valid() &&
                        !diagnostic_encoding_busy.load(std::memory_order_acquire))
                    {
                        last_diagnostics = publication->diagnostic_encoder;
                        diagnostic_encoding_busy.store(true, std::memory_order_release);
                        std::packaged_task<std::vector<std::byte>()> task([owner = shared_from_this(), frozen = last_diagnostics]
                        {
                            struct release_slot
                            {
                                state& owner;
                                ~release_slot() { owner.diagnostic_encoding_busy.store(false, std::memory_order_release); }
                            } release{*owner};
                            auto bytes = (*frozen)();
                            if (bytes.size() > maximum_diagnostic_bytes)
                            {
                                bytes.clear();
                            }
                            return bytes;
                        });
                        diagnostic_encoding = task.get_future();
                        try
                        {
                            std::thread(std::move(task)).detach();
                        }
                        catch (...)
                        {
                            diagnostic_encoding_busy.store(false, std::memory_order_release);
                            throw;
                        }
                    }
                    if (outgoing.empty() && !encoding.valid() && !encoding_busy.load(std::memory_order_acquire))
                    {
                        std::function<engine_detail::encoded_blob()> encode;
                        if (publication->snapshot.capture && publication->snapshot.capture != last_capture)
                        {
                            last_capture = publication->snapshot.capture;
                            encode = [frozen = last_capture, clear_revision = publication->snapshot.clear_revision,
                                      generation = publication->snapshot.capture_epoch]
                            {
                                engine_detail::encoded_blob blob;
                                blob.clear_revision = clear_revision;
                                blob.capture_generation = generation;
                                auto encoded = encode_capture_bounded(*frozen, maximum_capture_bytes);
                                if (encoded)
                                {
                                    blob.bytes = std::move(*encoded);
                                }
                                return blob;
                            };
                        }
                        if (encode)
                        {
                            encoding_busy.store(true, std::memory_order_release);
                            std::packaged_task<engine_detail::encoded_blob()> task([owner = shared_from_this(), encode = std::move(encode)]
                            {
                                struct release_slot
                                {
                                    state& owner;
                                    ~release_slot() { owner.encoding_busy.store(false, std::memory_order_release); }
                                } release{*owner};
                                return encode();
                            });
                            encoding = task.get_future();
                            try
                            {
                                std::thread(std::move(task)).detach();
                            }
                            catch (...)
                            {
                                encoding_busy.store(false, std::memory_order_release);
                                throw;
                            }
                        }
                    }
                }
                if (healthy && diagnostic_outgoing.empty() && diagnostic_encoding.valid() &&
                    diagnostic_encoding.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    diagnostic_outgoing = diagnostic_encoding.get();
                    if (!diagnostic_outgoing.empty())
                    {
                        writer value;
                        value(identity.session_generation);
                        value(++diagnostic_transfer_id);
                        value(static_cast<std::uint32_t>(diagnostic_outgoing.size()));
                        value(std::uint64_t{0});
                        value(std::uint64_t{0});
                        healthy = send(pipe, message_kind::diagnostics_begin, value.bytes, options.nonce, sent, viewer, stop.get());
                        diagnostic_offset = 0;
                    }
                }
                if (healthy && outgoing.empty() && encoding.valid() &&
                    encoding.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
                {
                    auto blob = encoding.get();
                    if (blob.begin == message_kind::capture_begin && publication &&
                        blob.capture_generation != publication->snapshot.capture_generation)
                    {
                        blob.bytes.clear();
                    }
                    outgoing = std::move(blob.bytes);
                    if (outgoing.empty())
                    {
                        ++skipped;
                    }
                    else
                    {
                        outgoing_kind = blob.begin == message_kind::capture_begin ? message_kind::capture_part : message_kind::diagnostics_part;
                        outgoing_capture_generation = blob.capture_generation;
                        writer value;
                        value(identity.session_generation);
                        value(++transfer_id);
                        value(static_cast<std::uint32_t>(outgoing.size()));
                        value(blob.clear_revision);
                        value(blob.capture_generation);
                        healthy = send(pipe, blob.begin, value.bytes, options.nonce, sent, viewer, stop.get());
                        outgoing_offset = 0;
                    }
                }
                if (healthy && !outgoing.empty() && outgoing_kind == message_kind::capture_part && publication &&
                    outgoing_capture_generation != publication->snapshot.capture_generation)
                {
                    writer cancel;
                    cancel(transfer_id);
                    healthy = send(pipe, message_kind::cancel_blob, cancel.bytes, options.nonce, sent, viewer, stop.get());
                    outgoing.clear();
                }
                for (std::uint32_t part = 0; healthy && part < 4 && !diagnostic_outgoing.empty(); ++part)
                {
                    writer value;
                    value(diagnostic_transfer_id);
                    value(static_cast<std::uint32_t>(diagnostic_offset));
                    const auto count = std::min<std::size_t>(maximum_packet_bytes - wire_header_bytes - value.bytes.size(),
                                                             diagnostic_outgoing.size() - diagnostic_offset);
                    value.bytes.insert(value.bytes.end(), diagnostic_outgoing.begin() + diagnostic_offset,
                                       diagnostic_outgoing.begin() + diagnostic_offset + count);
                    healthy = send(pipe, message_kind::diagnostics_part, value.bytes, options.nonce, sent, viewer, stop.get());
                    diagnostic_offset += count;
                    if (diagnostic_offset == diagnostic_outgoing.size())
                    {
                        diagnostic_outgoing.clear();
                    }
                }
                for (std::uint32_t part = 0; healthy && part < 4 && !outgoing.empty(); ++part)
                {
                    writer value;
                    value(transfer_id);
                    value(static_cast<std::uint32_t>(outgoing_offset));
                    const auto count = std::min<std::size_t>(maximum_packet_bytes - wire_header_bytes - value.bytes.size(),
                                                             outgoing.size() - outgoing_offset);
                    value.bytes.insert(value.bytes.end(), outgoing.begin() + outgoing_offset, outgoing.begin() + outgoing_offset + count);
                    healthy = send(pipe, outgoing_kind, value.bytes, options.nonce, sent, viewer, stop.get());
                    outgoing_offset += count;
                    if (outgoing_offset == outgoing.size())
                    {
                        outgoing.clear();
                    }
                }
                WaitForSingleObject(stop.get(), 5);
            }
            // A closing viewer can enqueue goodbye immediately before a pending
            // server write sees broken-pipe. Consume only bounded, fully
            // authenticated remaining packets before classifying that close.
            for (std::uint32_t index = 0; !viewer_closing && index < maximum_pending_commands + 2; ++index)
            {
                bool ready = false;
                packet closing;
                if (!available(pipe, ready) || !ready ||
                    !receive(pipe, closing, options.nonce, received, viewer, nullptr))
                {
                    break;
                }
                if (closing.kind == message_kind::goodbye && closing.payload.empty())
                {
                    viewer_closing = true;
                }
                else if (closing.kind != message_kind::heartbeat && closing.kind != message_kind::command &&
                         closing.kind != message_kind::diagnostics_command)
                {
                    break;
                }
            }
            set_status(false, "Viewer disconnected; recording remains in the target engine",
                       healthy ? ERROR_BROKEN_PIPE : ERROR_INVALID_DATA,
                       !viewer_closing && WaitForSingleObject(viewer, 0) == WAIT_TIMEOUT &&
                       WaitForSingleObject(stop.get(), 0) == WAIT_TIMEOUT);
        }
    };

    engine_process::engine_process() : state_(std::make_shared<state>()) {}
    engine_process::~engine_process() { shutdown(); }

    bool engine_process::open_or_focus(std::uint32_t page, std::uint32_t ui_scale_milli)
    {
        if (page > 1 || ui_scale_milli < 500 || ui_scale_milli > 3000 || state_->closed.load(std::memory_order_acquire))
        {
            return false;
        }
        if (page)
        {
            state_->page.store(page, std::memory_order_release);
        }
        state_->focus.store(true, std::memory_order_release);
        bool expected = false;
        if (!state_->active.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return true;
        }
        if (!state_->stop)
        {
            state_->active.store(false, std::memory_order_release);
            return false;
        }
        state_->ui_scale_milli = ui_scale_milli;
        {
            std::lock_guard lock(state_->mutex);
            const auto failures = state_->launch.failure_revision;
            state_->close_requested.store(false, std::memory_order_release);
            state_->launch = {true, false, 0, "Starting profiler viewer"};
            state_->launch.failure_revision = failures;
            state_->latest.reset();
        }
        try
        {
            std::thread([owner = state_] { owner->run(); }).detach();
        }
        catch (...)
        {
            state_->active.store(false, std::memory_order_release);
            state_->set_status(false, "Cannot start viewer worker", ERROR_NOT_ENOUGH_MEMORY);
            std::lock_guard lock(state_->mutex);
            state_->launch.running = false;
            return false;
        }
        return true;
    }

    void engine_process::pump()
    {
        auto& owner = *state_;
        if (owner.closed.load(std::memory_order_acquire) || !owner.active.load(std::memory_order_acquire))
        {
            return;
        }
        std::deque<engine_detail::queued_command> commands;
        target_identity identity;
        {
            std::unique_lock lock(owner.mutex, std::try_to_lock);
            if (!lock || !owner.launch.connected)
            {
                return;
            }
            commands.swap(owner.commands);
            identity = owner.target;
        }
        auto& profiler = ce::profiler();
        if (!profiler.is_initialized())
        {
            return;
        }
        for (const auto& request : commands)
        {
            if (!request.diagnostic.empty())
            {
                if (owner.apply_diagnostic)
                {
                    owner.apply_diagnostic(request.diagnostic);
                }
                continue;
            }
            owner.command_accepted = true;
            owner.command_dx_error = dx_capture::control_error::none;
            owner.command_dx_session_id = 0;
            owner.command_message = "Target command accepted";
            if (!valid_command_generation(request.kind, request.capture_generation, profiler.capture_generation()))
            {
                owner.command_accepted = false;
                owner.command_message = "Stale capture-generation command rejected";
                owner.last_command = request.kind;
                ++owner.command_revision;
                continue;
            }
            switch (request.kind)
            {
            case command::record:
                if (!owner.clear_ticket && (profiler.state() == recorder_state::stopped ||
                                           profiler.state() == recorder_state::frozen))
                {
                    profiler.record(profiler.current_frame());
                    owner.command_accepted = profiler.state() == recorder_state::starting || profiler.state() == recorder_state::recording;
                    if (!owner.command_accepted)
                    {
                        owner.command_message = "Previous recording is still finalizing";
                    }
                }
                else
                {
                    owner.command_accepted = false;
                    owner.command_message = "Recorder is busy or Clear is still pending";
                }
                break;
            case command::stop:
                if (profiler.state() == recorder_state::recording)
                {
                    profiler.pause();
                }
                else
                {
                    owner.command_accepted = false;
                    owner.command_message = "Recorder is not recording";
                }
                break;
            case command::clear:
                if (!owner.clear_ticket && (profiler.state() == recorder_state::stopped ||
                                           profiler.state() == recorder_state::frozen))
                {
                    owner.clear_ticket = profiler.clear();
                    owner.command_accepted = owner.clear_ticket != 0;
                }
                else
                {
                    owner.command_accepted = false;
                    owner.command_message = "Stop recording before Clear";
                }
                break;
            case command::counter_mask: profiler.set_counter_mask(static_cast<counter_mask>(request.value)); break;
            case command::start_dx:
            case command::stop_dx:
            {
                // CLI and viewer share one admission boundary and spool owner.
                // An accepted request is queued, not a successful ETW capture.
                const auto result = request.kind == command::start_dx ?
                    dx_capture::deep_capture().start_capture() : dx_capture::deep_capture().stop_capture(request.value);
                owner.command_accepted = result.accepted;
                owner.command_dx_error = result.error;
                owner.command_dx_session_id = result.status.session_id;
                owner.command_message = result.message;
                break;
            }
            }
            owner.last_command = request.kind;
            ++owner.command_revision;
        }
        if (owner.clear_ticket && profiler.control_applied(owner.clear_ticket))
        {
            owner.clear_ticket = 0;
            ++owner.clear_revision;
        }
        const auto now = GetTickCount64();
        if (now < owner.next_publish && commands.empty())
        {
            return;
        }
        owner.next_publish = now + 100;
        if (owner.publish_diagnostics && now >= owner.next_diagnostics)
        {
            auto encode = owner.publish_diagnostics();
            if (encode)
            {
                owner.diagnostics = std::make_shared<const diagnostic_encoder>(std::move(encode));
            }
            owner.next_diagnostics = now + 250;
        }
        profiler.request_live_capture();
        auto publication = std::make_shared<engine_detail::publication>();
        auto& snapshot = publication->snapshot;
        snapshot.target = identity;
        snapshot.summary = profiler.summary();
        const auto recording = profiler.recording_publication();
        snapshot.recording = recording.status;
        snapshot.recording_id = recording.writer_id;
        snapshot.counters = profiler.get_counter_mask();
        snapshot.capture_generation = profiler.capture_generation();
        const auto capture = profiler.capture_publication();
        if (capture.second == snapshot.capture_generation)
        {
            snapshot.capture = capture.first;
            snapshot.capture_epoch = capture.second;
        }
        snapshot.clear_pending = owner.clear_ticket != 0;
        snapshot.clear_revision = owner.clear_revision;
        publication->diagnostic_encoder = owner.diagnostics;
        snapshot.command_revision = owner.command_revision;
        snapshot.last_command = owner.last_command;
        snapshot.command_accepted = owner.command_accepted;
        snapshot.command_dx_error = owner.command_dx_error;
        snapshot.command_dx_session_id = owner.command_dx_session_id;
        snapshot.command_message = owner.command_message;
        snapshot.dx_status = dx_capture::deep_capture().status();
#if CE_DX_TIMING_CAPTURE && !CE_SHIPPING
        snapshot.dx_available = true;
#endif
        if (snapshot.recording.state == recording_state::finalized &&
            (snapshot.summary.state == recorder_state::stopped || snapshot.summary.state == recorder_state::frozen))
        {
            publication->recording_path = recording.path;
        }
        if (snapshot.dx_status.session_id != 0 && !snapshot.dx_status.busy && snapshot.dx_status.recording.record_count > 0 &&
            snapshot.dx_status.recording.valid_bytes > 0)
        {
            publication->dx_path = snapshot.dx_status.path;
        }
        std::unique_lock lock(owner.mutex, std::try_to_lock);
        if (lock)
        {
            owner.latest = std::move(publication);
        }
    }

    launch_status engine_process::status() const
    {
        std::lock_guard lock(state_->mutex);
        return state_->launch;
    }

    void engine_process::request_close()
    {
        std::lock_guard lock(state_->mutex);
        if (state_->active.load(std::memory_order_acquire))
        {
            state_->close_requested.store(true, std::memory_order_release);
        }
    }

    void engine_process::shutdown()
    {
        state_->closed.store(true, std::memory_order_release);
        if (state_->stop)
        {
            SetEvent(state_->stop.get());
        }
    }

    void engine_process::set_diagnostic_hooks(diagnostic_publisher publish, diagnostic_handler apply)
    {
        state_->publish_diagnostics = std::move(publish);
        state_->apply_diagnostic = std::move(apply);
    }

    engine_process& viewer_process()
    {
        static engine_process instance;
        return instance;
    }
}
