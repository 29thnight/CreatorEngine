#include "DxCaptureProcess.h"
#include "DxCaptureSecurity.h"

#include <bcrypt.h>

#include <atomic>
#include <thread>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Bcrypt.lib")

namespace ce::dx_capture
{
    namespace
    {
        enum class attempt_result
        {
            stopped,
            denied,
            failed
        };

        std::wstring system_directory()
        {
            std::wstring path(32768, L'\0');
            const auto size = GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
            if (size == 0 || size >= path.size())
            {
                return {};
            }
            path.resize(size);
            return path;
        }

        std::wstring minimal_environment(const std::wstring& system)
        {
            std::wstring windows(32768, L'\0');
            const auto size = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
            if (size == 0 || size >= windows.size())
            {
                return {};
            }
            windows.resize(size);
            std::wstring environment = L"PATH=" + system;
            environment.push_back(L'\0');
            environment += L"SystemRoot=" + windows;
            environment.push_back(L'\0');
            environment += L"WINDIR=" + windows;
            environment.push_back(L'\0');
            environment.push_back(L'\0');
            return environment;
        }

        security::handle create_pipe(const std::wstring& name)
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
            // GenericWrite에는 CreatePipeInstance 비트도 있다. 읽기/쓰기 개별 권한만
            // 주고 로그온 SID로 다른 사용자의 세션과 같은 사용자의 다른 로그온을 막는다.
            const std::wstring descriptor_text = L"D:P(A;;0x0012019b;;;" + std::wstring(text) + L")";
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(descriptor_text.c_str(), SDDL_REVISION_1,
                                                                       &descriptor, nullptr))
            {
                return {};
            }
            security::local_memory release_descriptor(descriptor);
            SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
            return security::handle(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                                      FILE_FLAG_FIRST_PIPE_INSTANCE,
                                                      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                                                      PIPE_REJECT_REMOTE_CLIENTS,
                                                      1, sizeof(message) * 64, sizeof(message) * 64, 0, &attributes));
        }
    }

    struct process_client::state
    {
        std::atomic<process_state> current{process_state::stopped};
        std::atomic<DWORD> error{ERROR_SUCCESS};
        std::atomic<bool> is_elevated{false};
        std::atomic<bool> active{false};
        std::atomic<bool> stopping{false};
        std::atomic<bool> closed{false};
        std::atomic<std::uint64_t> dropped{0};
        security::handle stop_event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        security::handle finished_event{CreateEventW(nullptr, TRUE, TRUE, nullptr)};
        std::array<record, maximum_queued_records> queue{};
        alignas(64) std::atomic<std::uint64_t> write_position{0};
        alignas(64) std::atomic<std::uint64_t> read_position{0};
        std::uint64_t reported_loss = 0;

        void publish(process_state value, DWORD failure = ERROR_SUCCESS)
        {
            error.store(failure, std::memory_order_relaxed);
            current.store(value, std::memory_order_release);
        }

        bool push_raw(const record& value)
        {
            const auto write = write_position.load(std::memory_order_relaxed);
            if (write - read_position.load(std::memory_order_acquire) >= queue.size())
            {
                return false;
            }
            queue[write % queue.size()] = value;
            write_position.store(write + 1, std::memory_order_release);
            return true;
        }

        void push(const record& value, DWORD parent_id)
        {
            const auto lost = dropped.load(std::memory_order_relaxed);
            if (lost != reported_loss)
            {
                record gap{};
                gap.kind = record_kind::status;
                gap.status = status_code::events_lost;
                gap.flags = record_flags::transport_loss | record_flags::incomplete;
                gap.process_id = parent_id;
                gap.loss_count = lost;
                if (push_raw(gap))
                {
                    reported_loss = lost;
                }
            }
            if (!push_raw(value))
            {
                dropped.fetch_add(1, std::memory_order_relaxed);
            }
        }

        attempt_result fail(process_state value, DWORD failure)
        {
            publish(value, failure == ERROR_SUCCESS ? ERROR_GEN_FAILURE : failure);
            return attempt_result::failed;
        }

        attempt_result capture_once()
        {
            // 외부에서 elevated Editor를 실행했더라도 첫 시도가 high token을
            // 상속해 개발 폴더 helper를 먼저 실행하는 우회는 허용하지 않는다.
            bool editor_elevated = false;
            if (!security::query_elevation(GetCurrentProcess(), editor_elevated) || editor_elevated)
            {
                return fail(process_state::unavailable, ERROR_ACCESS_DENIED);
            }
            const DWORD parent_id = GetCurrentProcessId();
            const auto parent_created = security::creation_time(GetCurrentProcess());
            DWORD session = 0;
            const auto parent_path = security::image_path(GetCurrentProcess());
            if (!parent_created || !ProcessIdToSessionId(parent_id, &session) || session == 0 ||
                !security::editor_image(parent_path))
            {
                return fail(process_state::unavailable, ERROR_ACCESS_DENIED);
            }
            const auto helper_path = security::directory(parent_path) +
                L"\\DxCaptureHelper\\CreatorDxCaptureHelper.exe";
            security::pinned_image parent_image, helper_image;
            if (!parent_image.open(parent_path, false) || !helper_image.open(helper_path, false))
            {
                return fail(process_state::unavailable, GetLastError());
            }
            session_nonce nonce{};
            if (BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()),
                               BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0 || nonce == session_nonce{})
            {
                return fail(process_state::failed, ERROR_GEN_FAILURE);
            }
            auto pipe = create_pipe(security::pipe_name(parent_id, nonce));
            if (!pipe)
            {
                return fail(process_state::failed, GetLastError());
            }
            auto connection = std::make_unique<security::operation>();
            if (!connection->event)
            {
                return fail(process_state::failed, GetLastError());
            }
            const BOOL connected = ConnectNamedPipe(pipe.get(), &connection->overlapped);
            const DWORD connection_error = connected ? ERROR_SUCCESS : GetLastError();
            if (!connected && connection_error != ERROR_IO_PENDING && connection_error != ERROR_PIPE_CONNECTED)
            {
                return fail(process_state::failed, connection_error);
            }
            bool connection_pending = !connected && connection_error == ERROR_IO_PENDING;
            // 실패 분기도 pending OVERLAPPED가 끝나기 전에 스택/heap을 파괴하지 않는다.
            struct cancel_connection
            {
                HANDLE pipe;
                std::unique_ptr<security::operation>& operation;
                bool& pending;
                ~cancel_connection()
                {
                    if (pending)
                    {
                        DWORD ignored = 0;
                        security::finish_operation(pipe, operation, 0, nullptr, nullptr, ignored);
                    }
                }
            } cancel{pipe.get(), connection, connection_pending};

            const auto system = system_directory();
            if (system.empty())
            {
                return fail(process_state::failed, GetLastError());
            }
            const std::wstring arguments = L"--parent " + std::to_wstring(parent_id) + L" --created " +
                std::to_wstring(parent_created) + L" --session " + std::to_wstring(session) + L" --nonce " +
                security::nonce_text(nonce);
            security::handle helper;
            if (stopping.load(std::memory_order_acquire))
            {
                return attempt_result::stopped;
            }
            {
                auto command_line = L"\"" + helper_path + L"\" " + arguments;
                auto environment = minimal_environment(system);
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                PROCESS_INFORMATION information{};
                if (environment.empty() || !CreateProcessW(helper_path.c_str(), command_line.data(), nullptr, nullptr,
                                                            FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                                                            environment.data(), system.c_str(), &startup, &information))
                {
                    return fail(process_state::unavailable, GetLastError());
                }
                helper.reset(information.hProcess);
                security::handle thread(information.hThread);
            }
            if (!helper)
            {
                return fail(process_state::failed, ERROR_INVALID_HANDLE);
            }
            const auto helper_id = GetProcessId(helper.get());
            const auto helper_created = security::creation_time(helper.get());
            bool helper_elevated = false;
            if (!security::process_identity(helper.get(), helper_id, helper_created, session) ||
                !security::same_user(helper.get(), GetCurrentProcess()) ||
                !security::same_logon(helper.get(), GetCurrentProcess()) ||
                !security::equal_path(security::image_path(helper.get()), helper_path) ||
                !security::query_elevation(helper.get(), helper_elevated) || helper_elevated)
            {
                return fail(process_state::protocol_error, ERROR_ACCESS_DENIED);
            }
            is_elevated.store(helper_elevated, std::memory_order_relaxed);
            if (connection_pending)
            {
                DWORD ignored = 0;
                const bool ready = security::finish_operation(pipe.get(), connection, connection_timeout_ms,
                                                               helper.get(), stop_event.get(), ignored);
                connection_pending = false;
                if (!ready)
                {
                    if (stopping.load(std::memory_order_acquire))
                    {
                        return attempt_result::stopped;
                    }
                    return fail(process_state::failed, GetLastError());
                }
            }
            ULONG peer_id = 0;
            if (!GetNamedPipeClientProcessId(pipe.get(), &peer_id) || peer_id != helper_id ||
                !security::process_identity(helper.get(), peer_id, helper_created, session))
            {
                return fail(process_state::protocol_error, ERROR_ACCESS_DENIED);
            }
            message hello{};
            record expected_hello{};
            expected_hello.kind = record_kind::session;
            expected_hello.process_id = helper_id;
            expected_hello.process_creation_time = helper_created;
            expected_hello.windows_session_id = session;
            if (!security::transfer(pipe.get(), hello, false, helper.get(), stop_event.get()) ||
                !valid_message(hello, nonce, 1) || hello.header.kind != message_kind::hello ||
                std::memcmp(&hello.payload, &expected_hello, sizeof(record)) != 0)
            {
                if (stopping.load(std::memory_order_acquire))
                {
                    return attempt_result::stopped;
                }
                return fail(process_state::protocol_error, ERROR_INVALID_DATA);
            }
            std::uint64_t sent = 0;
            std::uint64_t received = 1;
            const auto send_control = [&](message_kind kind)
            {
                message packet{};
                packet.header.kind = kind;
                packet.header.session = nonce;
                packet.header.sequence = ++sent;
                if (kind == message_kind::start)
                {
                    packet.payload.kind = record_kind::session;
                    packet.payload.process_id = parent_id;
                    packet.payload.process_creation_time = parent_created;
                    packet.payload.windows_session_id = session;
                }
                return security::transfer(pipe.get(), packet, true, helper.get());
            };
            if (stopping.load(std::memory_order_acquire))
            {
                return attempt_result::stopped;
            }
            if (!send_control(message_kind::start))
            {
                return fail(process_state::failed, GetLastError());
            }
            record staged_session{};
            bool have_session = false;
            bool accepted = false;
            bool stop_sent = false;
            ULONGLONG stop_deadline = 0;
            auto last_heartbeat = GetTickCount64();
            const auto maximum_deadline = last_heartbeat + 70000;
            while (true)
            {
                const auto now = GetTickCount64();
                const bool helper_running = WaitForSingleObject(helper.get(), 0) == WAIT_TIMEOUT;
                if (helper_running && !stop_sent &&
                    (stopping.load(std::memory_order_acquire) || now >= maximum_deadline))
                {
                    publish(process_state::stopping);
                    if (!send_control(message_kind::stop) && WaitForSingleObject(helper.get(), 0) == WAIT_TIMEOUT)
                    {
                        return fail(process_state::failed, GetLastError());
                    }
                    stop_sent = true;
                    stop_deadline = now + parent_lease_ms;
                }
                if (stop_sent && now >= stop_deadline)
                {
                    return fail(process_state::failed, ERROR_TIMEOUT);
                }
                if (helper_running && now - last_heartbeat >= 100)
                {
                    if (!send_control(message_kind::heartbeat) && WaitForSingleObject(helper.get(), 0) == WAIT_TIMEOUT)
                    {
                        return fail(process_state::failed, GetLastError());
                    }
                    last_heartbeat = now;
                }
                bool made_progress = false;
                for (std::size_t index = 0; index < 64; ++index)
                {
                    bool available = false;
                    if (!security::message_available(pipe.get(), available))
                    {
                        return fail(process_state::failed, GetLastError());
                    }
                    if (!available)
                    {
                        break;
                    }
                    made_progress = true;
                    message packet{};
                    if (!security::transfer(pipe.get(), packet, false) || !valid_message(packet, nonce, received + 1) ||
                        packet.header.kind != message_kind::data || packet.payload.kind == record_kind::submission ||
                        (packet.payload.process_id != parent_id &&
                         !(packet.payload.kind == record_kind::status && packet.payload.process_id == 0)))
                    {
                        return fail(process_state::protocol_error, ERROR_INVALID_DATA);
                    }
                    ++received;
                    const auto& value = packet.payload;
                    if (value.kind == record_kind::session)
                    {
                        if (have_session || accepted || value.qpc_frequency == 0 || value.qpc_begin == 0 ||
                            value.process_creation_time != parent_created || value.windows_session_id != session)
                        {
                            return fail(process_state::protocol_error, ERROR_INVALID_DATA);
                        }
                        staged_session = value;
                        have_session = true;
                        continue;
                    }
                    if (!accepted)
                    {
                        if (value.kind != record_kind::status)
                        {
                            return fail(process_state::protocol_error, ERROR_INVALID_DATA);
                        }
                        if (value.status == status_code::stopped &&
                            (stop_sent || stopping.load(std::memory_order_acquire)))
                        {
                            // provider 초기화 도중 Stop을 받으면 session이 없을 수 있다.
                            // 취소를 실패나 빈 성공 파일로 바꾸지 않는다.
                            return attempt_result::stopped;
                        }
                        if (value.status == status_code::permission_denied)
                        {
                            // 명시적 ETW 권한 실패와 종료 코드 둘 다 확인한다.
                            // decoder 경계를 검증하기 전에는 이 결과로 승격하지 않는다.
                            DWORD exit_code = 0;
                            if (WaitForSingleObject(helper.get(), io_timeout_ms) != WAIT_OBJECT_0 ||
                                !GetExitCodeProcess(helper.get(), &exit_code) ||
                                exit_code != permission_denied_exit_code)
                            {
                                return fail(process_state::failed, ERROR_ACCESS_DENIED);
                            }
                            error.store(value.win32_error, std::memory_order_relaxed);
                            return attempt_result::denied;
                        }
                        if (value.status == status_code::capturing && have_session)
                        {
                            push(staged_session, parent_id);
                            accepted = true;
                            publish(stop_sent ? process_state::stopping : process_state::capturing);
                        }
                        else if (value.status == status_code::ready)
                        {
                            continue;
                        }
                        else
                        {
                            return fail(process_state::failed, value.win32_error);
                        }
                    }
                    push(value, parent_id);
                    if (value.kind == record_kind::status && value.win32_error != ERROR_SUCCESS &&
                        (value.status == status_code::malformed_event ||
                         value.status == status_code::provider_failure ||
                         value.status == status_code::unsupported || value.status == status_code::permission_denied ||
                         value.status == status_code::disconnected || value.status == status_code::stopped))
                    {
                        // 종료 메시지의 구체적인 오류를 뒤따르는 pipe EOF로 덮지 않는다.
                        const auto failure_state = value.status == status_code::unsupported ?
                            process_state::unavailable : process_state::failed;
                        return fail(failure_state, value.win32_error);
                    }
                    if (value.kind == record_kind::status && value.status == status_code::stopped)
                    {
                        // 성공 상태는 마지막 enqueue 뒤에만 공개된다. 소비자는 terminal을
                        // 본 뒤 큐를 비우면 다시 레코드가 생기지 않는다는 계약을 갖는다.
                        return attempt_result::stopped;
                    }
                }
                if (WaitForSingleObject(helper.get(), 0) != WAIT_TIMEOUT)
                {
                    // 한 차례의 64개 처리 한도보다 종료 직전 버퍼가 클 수 있다.
                    // 잔여 메시지를 먼저 비운 뒤 stopped 누락을 판정한다.
                    bool remaining = false;
                    if (security::message_available(pipe.get(), remaining) && remaining)
                    {
                        continue;
                    }
                    return fail(process_state::failed, ERROR_BROKEN_PIPE);
                }
                if (!made_progress)
                {
                    // stop은 위에서 메시지로 처리한다. signaled event로 busy-spin하지 않는다.
                    Sleep(5);
                }
            }
        }

        void run()
        {
            struct finish
            {
                state& owner;
                ~finish()
                {
                    SetEvent(owner.finished_event.get());
                    owner.active.store(false, std::memory_order_release);
                }
            } completion{*this};
            try
            {
                const auto result = capture_once();
                if (result == attempt_result::denied)
                {
                    publish(process_state::elevation_unavailable, error.load(std::memory_order_relaxed));
                }
                if (result == attempt_result::stopped)
                {
                    publish(process_state::stopped);
                }
            }
            catch (...)
            {
                publish(process_state::failed, ERROR_NOT_ENOUGH_MEMORY);
            }
        }
    };

    process_client::process_client() : state_(std::make_shared<state>()) {}
    process_client::~process_client() { shutdown(); }

    bool process_client::start(bool allow_elevation)
    {
        // 허용 플래그로도 안전 게이트를 우회할 수 없다. runas 경로 자체를 두지 않는다.
        static_cast<void>(allow_elevation);
        auto& state = *state_;
        bool expected = false;
        if (state.closed.load(std::memory_order_acquire) || !state.stop_event || !state.finished_event ||
            !state.active.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            return false;
        }
        state.stopping.store(false, std::memory_order_relaxed);
        state.is_elevated.store(false, std::memory_order_relaxed);
        state.dropped.store(0, std::memory_order_relaxed);
        state.write_position.store(0, std::memory_order_relaxed);
        state.read_position.store(0, std::memory_order_relaxed);
        state.reported_loss = 0;
        ResetEvent(state.stop_event.get());
        ResetEvent(state.finished_event.get());
        state.publish(process_state::starting);
        try
        {
            std::thread([state = state_] { state->run(); }).detach();
        }
        catch (...)
        {
            state.publish(process_state::failed, ERROR_NOT_ENOUGH_MEMORY);
            SetEvent(state.finished_event.get());
            state.active.store(false, std::memory_order_release);
            return false;
        }
        return true;
    }

    void process_client::request_stop()
    {
        state_->stopping.store(true, std::memory_order_release);
        if (state_->stop_event)
        {
            SetEvent(state_->stop_event.get());
        }
    }

    bool process_client::try_pop(record& value)
    {
        auto& state = *state_;
        const auto read = state.read_position.load(std::memory_order_relaxed);
        if (read == state.write_position.load(std::memory_order_acquire))
        {
            return false;
        }
        value = state.queue[read % state.queue.size()];
        state.read_position.store(read + 1, std::memory_order_release);
        return true;
    }

    process_status process_client::status() const
    {
        process_status result{};
        result.state = state_->current.load(std::memory_order_acquire);
        result.win32_error = state_->error.load(std::memory_order_relaxed);
        result.dropped_records = state_->dropped.load(std::memory_order_relaxed);
        result.elevated = state_->is_elevated.load(std::memory_order_relaxed);
        return result;
    }

    void process_client::shutdown()
    {
        if (state_->closed.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        request_stop();
        if (state_->finished_event)
        {
            // 느린 OS I/O에도 worker는 shared state만 참조하므로 UI 수명과
            // 분리된다. 제한 시간이 끝나면 소비자는 녹화를 incomplete로 봉인한다.
            WaitForSingleObject(state_->finished_event.get(), shutdown_timeout_ms);
        }
    }
}
