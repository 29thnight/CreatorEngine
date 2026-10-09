#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace ce::dx_capture
{
    // 역참조할 포인터·문자열·경로를 전송하지 않는다. API queue ID는 숫자 식별자일 뿐이다.
    // 같은 Windows little-endian ABI의
    // 명시적 버전만 허용하여 helper가 명령 실행/파일 쓰기 프록시가 되지 않게 한다.
    inline constexpr std::uint32_t protocol_magic = 0x58444543; // CEDX
    inline constexpr std::uint16_t protocol_version = 1;
    inline constexpr std::uint32_t maximum_queued_records = 8192;
    inline constexpr std::uint32_t connection_timeout_ms = 10000;
    inline constexpr std::uint32_t io_timeout_ms = 500;
    inline constexpr std::uint32_t parent_lease_ms = 3000;
    inline constexpr std::uint32_t shutdown_timeout_ms = 1500;
    inline constexpr std::uint32_t permission_denied_exit_code = 5;

    using session_nonce = std::array<std::uint8_t, 16>;

    enum class record_kind : std::uint32_t
    {
        session = 1,
        gpu_execution_begin = 2,
        gpu_work = 3,
        gpu_execution_end = 4,
        submission = 5,
        status = 6
    };

    enum class status_code : std::uint32_t
    {
        none = 0,
        ready = 1,
        capturing = 2,
        stopped = 3,
        permission_denied = 4,
        unsupported = 5,
        provider_failure = 6,
        events_lost = 7,
        malformed_event = 8,
        disconnected = 9
    };

    enum record_flags : std::uint32_t
    {
        no_flags = 0,
        has_cpu_submit = 1u << 0,
        incomplete = 1u << 1,
        ambiguous = 1u << 2,
        transport_loss = 1u << 3,
        etw_loss = 1u << 4,
        engine_correlated = 1u << 5,
        raw_api_marker_ids = 1u << 6,
        pix_markers_unavailable = 1u << 7,
        calibrated_qpc = 1u << 8
    };
    inline constexpr std::uint32_t known_record_flags = (1u << 9) - 1;

    // 이름을 복원하지 않는 원시 API marker ID다. PIX Begin/End 의미를 추측하지 않는다.
    // session의 qpc_begin은 캡처 시작, qpc_frequency는 원시 QPC의 주파수다.
    // submission의 qpc_begin/end는 ExecuteCommandLists 전후 CPU QPC 경계다.
    struct record
    {
        record_kind kind = record_kind::status;
        std::uint32_t flags = 0;
        std::uint64_t qpc_begin = 0;
        std::uint64_t qpc_end = 0;
        std::uint64_t cpu_submit_qpc = 0;
        std::uint32_t process_id = 0;
        std::uint32_t thread_id = 0;
        std::uint64_t api_queue_id = 0;
        std::uint64_t hardware_queue_id = 0;
        std::uint64_t execution_id = 0;
        std::uint32_t command_list_index = 0;
        status_code status = status_code::none;
        std::uint64_t api_marker_id = 0;
        std::uint64_t present_token = 0;
        std::uint64_t engine_frame_id = 0;
        std::uint64_t submission_id = 0;
        std::uint64_t render_view_id = 0;
        std::uint64_t loss_count = 0;
        std::uint64_t qpc_frequency = 0;
        std::uint64_t process_creation_time = 0;
        std::uint32_t windows_session_id = 0;
        std::uint32_t win32_error = 0;
        std::uint64_t adapter_luid = 0;
        std::uint32_t command_list_count = 0;
        std::uint32_t reserved = 0;
        // upstream의 ns와 변환한 QPC를 별도로 남겨 보정/반올림을 숨기지 않는다.
        std::uint64_t source_begin_ns = 0;
        std::uint64_t source_end_ns = 0;
        std::uint64_t source_cpu_submit_ns = 0;
        std::uint64_t etw_events_lost = 0;
        std::uint64_t etw_buffers_lost = 0;
    };

    enum class message_kind : std::uint16_t
    {
        hello = 1,
        start = 2,
        stop = 3,
        heartbeat = 4,
        data = 5
    };

    struct message_header
    {
        std::uint32_t magic = protocol_magic;
        std::uint16_t version = protocol_version;
        message_kind kind = message_kind::hello;
        std::uint32_t payload_bytes = sizeof(record);
        std::uint32_t reserved = 0;
        session_nonce session{};
        std::uint64_t sequence = 0;
    };

    struct message
    {
        message_header header{};
        record payload{};
    };

    static_assert(sizeof(record) == 200 && alignof(record) == 8);
    static_assert(offsetof(record, api_marker_id) == 72);
    static_assert(offsetof(record, reserved) == 156);
    static_assert(sizeof(message_header) == 40 && sizeof(message) == 240);
    static_assert(std::is_trivially_copyable_v<record> && std::is_standard_layout_v<record>);
    static_assert(std::is_trivially_copyable_v<message> && std::is_standard_layout_v<message>);

    inline bool valid_record(const record& value)
    {
        return value.kind >= record_kind::session && value.kind <= record_kind::status &&
               value.status <= status_code::disconnected && (value.flags & ~known_record_flags) == 0 &&
               value.reserved == 0 && (value.qpc_end == 0 || value.qpc_end >= value.qpc_begin);
    }

    inline bool valid_message(const message& value, const session_nonce& session, std::uint64_t sequence)
    {
        return value.header.magic == protocol_magic && value.header.version == protocol_version &&
               value.header.kind >= message_kind::hello && value.header.kind <= message_kind::data &&
               value.header.payload_bytes == sizeof(record) && value.header.reserved == 0 &&
               value.header.session == session && sequence != 0 && value.header.sequence == sequence &&
               valid_record(value.payload);
    }
}
