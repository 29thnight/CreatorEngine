#include "DxCaptureTransport.h"
#include "../../Engine/EngineDiagnostics/DxCaptureSecurity.h"

#include <string_view>

#pragma comment(lib, "Advapi32.lib")

namespace ce::dx_capture
{
    namespace
    {
        bool parse_integer(std::wstring_view text, std::uint64_t maximum, std::uint64_t& value)
        {
            value = 0;
            if (text.empty() || text.size() > 20)
            {
                return false;
            }
            for (const auto character : text)
            {
                if (character < L'0' || character > L'9')
                {
                    return false;
                }
                const auto digit = static_cast<std::uint64_t>(character - L'0');
                if (value > (maximum - digit) / 10)
                {
                    return false;
                }
                value = value * 10 + digit;
            }
            return true;
        }

        bool parse_nonce(std::wstring_view text, session_nonce& nonce)
        {
            if (text.size() != 32)
            {
                return false;
            }
            bool nonzero = false;
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                const auto character = text[index];
                const int digit = character >= L'0' && character <= L'9' ? character - L'0' :
                                  character >= L'a' && character <= L'f' ? character - L'a' + 10 : -1;
                if (digit < 0)
                {
                    return false;
                }
                nonce[index / 2] = static_cast<std::uint8_t>((nonce[index / 2] << 4) | digit);
                nonzero = nonzero || digit != 0;
            }
            return nonzero;
        }

        bool empty_control(const record& value)
        {
            const record empty{};
            return std::memcmp(&value, &empty, sizeof(record)) == 0;
        }
    }

    struct transport::state
    {
        security::handle parent;
        security::handle pipe;
        security::pinned_image parent_image;
        security::pinned_image helper_image;
        session_nonce nonce{};
        DWORD parent_id = 0;
        std::uint64_t parent_created = 0;
        DWORD session = 0;
        std::uint64_t received = 0;
        std::uint64_t sent = 0;
        ULONGLONG heartbeat = 0;
        DWORD error = ERROR_SUCCESS;
        bool started = false;
        bool stopping = false;

        bool alive() const
        {
            return parent && pipe && WaitForSingleObject(parent.get(), 0) == WAIT_TIMEOUT &&
                   GetTickCount64() - heartbeat <= parent_lease_ms;
        }

        bool fail(DWORD failure)
        {
            error = failure;
            pipe.reset();
            return false;
        }
    };

    transport::transport() : state_(std::make_unique<state>()) {}
    transport::~transport() = default;

    bool transport::connect(int argument_count, wchar_t** arguments)
    {
        close();
        state_ = std::make_unique<state>();
        auto& state = *state_;
        // 실행 파일/ETL 경로·provider·명령을 받는 옵션은 의도적으로 없다.
        if (argument_count != 9 || !arguments || std::wstring_view(arguments[1]) != L"--parent" ||
            std::wstring_view(arguments[3]) != L"--created" || std::wstring_view(arguments[5]) != L"--session" ||
            std::wstring_view(arguments[7]) != L"--nonce")
        {
            return state.fail(ERROR_INVALID_PARAMETER);
        }
        std::uint64_t parent_id = 0, session = 0;
        if (!parse_integer(arguments[2], MAXDWORD, parent_id) || parent_id == 0 ||
            !parse_integer(arguments[4], UINT64_MAX, state.parent_created) || state.parent_created == 0 ||
            !parse_integer(arguments[6], MAXDWORD, session) || session == 0 ||
            !parse_nonce(arguments[8], state.nonce))
        {
            return state.fail(ERROR_INVALID_PARAMETER);
        }
        state.parent_id = static_cast<DWORD>(parent_id);
        state.session = static_cast<DWORD>(session);
        state.parent.reset(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, state.parent_id));
        if (!security::process_identity(state.parent.get(), state.parent_id, state.parent_created, state.session) ||
            !security::same_user(state.parent.get(), GetCurrentProcess()) ||
            !security::same_logon(state.parent.get(), GetCurrentProcess()))
        {
            return state.fail(ERROR_ACCESS_DENIED);
        }
        DWORD own_session = 0;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &own_session) || own_session != state.session)
        {
            return state.fail(ERROR_ACCESS_DENIED);
        }
        const auto parent_path = security::image_path(state.parent.get());
        const auto helper_path = security::image_path(GetCurrentProcess());
        const auto expected_helper = security::directory(parent_path) +
            L"\\DxCaptureHelper\\CreatorDxCaptureHelper.exe";
        bool require_protected = false;
        if (!security::query_elevation(GetCurrentProcess(), require_protected) || require_protected ||
            !security::editor_image(parent_path) || !security::equal_path(helper_path, expected_helper) ||
            !state.parent_image.open(parent_path, require_protected) ||
            !state.helper_image.open(helper_path, require_protected) ||
            (require_protected && (!security::system_imports_only(state.helper_image.file()) ||
                                  !security::isolated_helper_directory(helper_path))) ||
            !SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32))
        {
            return state.fail(ERROR_ACCESS_DENIED);
        }
        const auto name = security::pipe_name(state.parent_id, state.nonce);
        const auto deadline = GetTickCount64() + connection_timeout_ms;
        while (GetTickCount64() < deadline && WaitForSingleObject(state.parent.get(), 0) == WAIT_TIMEOUT)
        {
            // Identification은 elevated client token을 pipe 서버가 impersonate하지 못하게 한다.
            state.pipe.reset(CreateFileW(name.c_str(), FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES |
                                          FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, 0, nullptr, OPEN_EXISTING,
                                          FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                                          nullptr));
            if (state.pipe)
            {
                break;
            }
            if (GetLastError() != ERROR_PIPE_BUSY)
            {
                return state.fail(GetLastError());
            }
            WaitNamedPipeW(name.c_str(), 100);
        }
        if (!state.pipe)
        {
            return state.fail(ERROR_TIMEOUT);
        }
        ULONG server_id = 0;
        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!GetNamedPipeServerProcessId(state.pipe.get(), &server_id) || server_id != state.parent_id ||
            !security::process_identity(state.parent.get(), server_id, state.parent_created, state.session) ||
            !SetNamedPipeHandleState(state.pipe.get(), &mode, nullptr, nullptr))
        {
            return state.fail(ERROR_ACCESS_DENIED);
        }
        message hello{};
        hello.header.kind = message_kind::hello;
        hello.header.session = state.nonce;
        hello.header.sequence = ++state.sent;
        hello.payload.kind = record_kind::session;
        hello.payload.process_id = GetCurrentProcessId();
        hello.payload.process_creation_time = security::creation_time(GetCurrentProcess());
        hello.payload.windows_session_id = state.session;
        state.heartbeat = GetTickCount64();
        if (!security::transfer(state.pipe.get(), hello, true, state.parent.get()))
        {
            return state.fail(GetLastError());
        }
        return true;
    }

    command transport::try_read_command()
    {
        auto& state = *state_;
        if (!state.alive())
        {
            state.fail(ERROR_BROKEN_PIPE);
            return command::disconnected;
        }
        // heartbeat 적체가 stop을 가리지 않도록 한 번에 제한된 수만 비운다.
        for (std::size_t index = 0; index < 32; ++index)
        {
            bool available = false;
            if (!security::message_available(state.pipe.get(), available))
            {
                state.fail(GetLastError());
                return command::disconnected;
            }
            if (!available)
            {
                return command::none;
            }
            message packet{};
            if (!security::transfer(state.pipe.get(), packet, false, state.parent.get()) ||
                !valid_message(packet, state.nonce, state.received + 1))
            {
                state.fail(ERROR_INVALID_DATA);
                return command::disconnected;
            }
            ++state.received;
            state.heartbeat = GetTickCount64();
            if (packet.header.kind == message_kind::start && !state.started)
            {
                record expected{};
                expected.kind = record_kind::session;
                expected.process_id = state.parent_id;
                expected.process_creation_time = state.parent_created;
                expected.windows_session_id = state.session;
                if (std::memcmp(&packet.payload, &expected, sizeof(record)) != 0)
                {
                    state.fail(ERROR_INVALID_DATA);
                    return command::disconnected;
                }
                state.started = true;
                return command::start;
            }
            if (packet.header.kind == message_kind::stop && state.started && !state.stopping &&
                empty_control(packet.payload))
            {
                state.stopping = true;
                return command::stop;
            }
            if (packet.header.kind == message_kind::heartbeat && state.started && empty_control(packet.payload))
            {
                continue;
            }
            state.fail(ERROR_INVALID_DATA);
            return command::disconnected;
        }
        return command::none;
    }

    bool transport::send_record(const record& value)
    {
        auto& state = *state_;
        if (!state.alive() || !state.started || !valid_record(value) || value.kind == record_kind::submission ||
            (value.process_id != state.parent_id && !(value.kind == record_kind::status && value.process_id == 0)))
        {
            return state.fail(ERROR_INVALID_DATA);
        }
        message packet{};
        packet.header.kind = message_kind::data;
        packet.header.session = state.nonce;
        packet.header.sequence = ++state.sent;
        packet.payload = value;
        if (!security::transfer(state.pipe.get(), packet, true, state.parent.get()))
        {
            return state.fail(GetLastError());
        }
        return true;
    }

    bool transport::parent_alive() const { return state_->alive(); }
    std::uint32_t transport::target_process_id() const { return state_->parent_id; }
    std::uint64_t transport::target_creation_time() const { return state_->parent_created; }
    std::uint32_t transport::windows_session_id() const { return state_->session; }
    std::wstring transport::session_name() const
    {
        return L"CreatorEngine.DxCapture." + std::to_wstring(state_->parent_id) + L"." +
            security::nonce_text(state_->nonce);
    }
    std::uint32_t transport::last_error() const { return state_->error; }
    void transport::close()
    {
        state_->pipe.reset();
        state_->parent.reset();
        state_->parent_image.handles.clear();
        state_->helper_image.handles.clear();
    }
}
