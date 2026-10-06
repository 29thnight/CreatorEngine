#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ce::profiler_viewer
{
    inline constexpr std::uint32_t protocol_magic = 0x56504543; // CEPV
    inline constexpr std::uint16_t protocol_version = 1;
    inline constexpr std::uint32_t maximum_packet_bytes = 64 * 1024;
    inline constexpr std::uint32_t maximum_capture_bytes = 128 * 1024 * 1024;
    inline constexpr std::uint32_t maximum_diagnostic_bytes = 2 * 1024 * 1024;
    inline constexpr std::uint32_t maximum_command_bytes = 4096;
    inline constexpr std::uint32_t maximum_pending_commands = 16;
    inline constexpr std::uint32_t connection_timeout_ms = 10000;
    inline constexpr std::uint32_t io_timeout_ms = 500;
    inline constexpr std::uint32_t target_lease_ms = 3000;
    using session_nonce = std::array<std::uint8_t, 16>;

    struct connection_options
    {
        std::uint32_t target_pid = 0;
        std::uint64_t target_creation_time = 0;
        std::uint32_t windows_session_id = 0;
        session_nonce nonce{};
    };

    struct target_identity
    {
        connection_options connection;
        std::uint64_t qpc_frequency = 0;
        std::uint64_t session_generation = 0;
    };

    enum class command : std::uint32_t
    {
        record = 1,
        stop = 2,
        clear = 3,
        counter_mask = 4,
        start_dx = 5,
        stop_dx = 6
    };

    enum class message_kind : std::uint16_t
    {
        hello = 1,
        welcome = 2,
        status = 3,
        capture_begin = 4,
        capture_part = 5,
        command = 6,
        heartbeat = 7,
        focus = 8,
        recording_artifact = 9,
        dx_artifact = 10,
        diagnostics_begin = 11,
        diagnostics_part = 12,
        diagnostics_command = 13,
        cancel_blob = 14,
        goodbye = 15
    };

    // This describes the wire fields, not an ABI to memcpy. Every integer is
    // serialized little-endian and every payload has an exact validated length.
    struct message_header
    {
        std::uint32_t magic = protocol_magic;
        std::uint16_t version = protocol_version;
        message_kind kind = message_kind::hello;
        std::uint32_t payload_bytes = 0;
        session_nonce nonce{};
        std::uint64_t sequence = 0;
    };
    inline constexpr std::size_t wire_header_bytes = 36;

    inline bool valid_header(const message_header& value, const session_nonce& nonce,
                             std::uint64_t sequence)
    {
        return value.magic == protocol_magic && value.version == protocol_version &&
               value.kind >= message_kind::hello && value.kind <= message_kind::goodbye &&
               value.payload_bytes <= maximum_packet_bytes - wire_header_bytes &&
               value.nonce == nonce && nonce != session_nonce{} && sequence != 0 &&
               value.sequence == sequence;
    }
}
