#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../../Engine/EngineDiagnostics/DxCaptureProtocol.h"

namespace ce::dx_capture
{
    enum class command : std::uint32_t
    {
        none,
        start,
        stop,
        disconnected
    };

    // helper 제어 스레드 하나만 사용한다. ETW 콜백은 자기 고정 큐에 넣고 이
    // 채널을 직접 기다리지 않아야 provider의 소비 속도가 UI에 묶이지 않는다.
    class transport
    {
    public:
        transport();
        ~transport();
        transport(const transport&) = delete;
        transport& operator=(const transport&) = delete;

        bool connect(int argument_count, wchar_t** arguments);
        command try_read_command();
        bool send_record(const record& value);
        bool parent_alive() const;
        std::uint32_t target_process_id() const;
        std::uint64_t target_creation_time() const;
        std::uint32_t windows_session_id() const;
        std::wstring session_name() const;
        std::uint32_t last_error() const;
        void close();

    private:
        struct state;
        std::unique_ptr<state> state_;
    };
}
