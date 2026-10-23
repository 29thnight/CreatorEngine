#pragma once
#include <cstdint>

namespace ce::net
{
    inline constexpr std::uint32_t contract_version = 1;
    struct NetworkObjectId final { std::uint64_t value{}; };
    struct NetPeerId final { std::uint64_t value{}; };
    struct NetSessionId final { std::uint64_t value{}; };
    struct NetComponentTypeId final { std::uint32_t value{}; };
    struct NetMessageTypeId final { std::uint16_t value{}; };
    struct NetFieldId final { std::uint16_t value{}; };
    struct NetTick final { std::uint64_t value{}; };
    // Network object IDs are monotonically assigned within one session. Zero is
    // invalid. Retired IDs never return during that session (including DDOL).
    // Static type/message/field assignments live in NetWireIds.registry.json.
    inline constexpr NetMessageTypeId handshake_message{1};
}
