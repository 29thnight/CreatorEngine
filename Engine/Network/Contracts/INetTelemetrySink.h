#pragma once
#include "NetWireIds.h"

namespace ce::net
{
    enum class NetTelemetryKind : std::uint8_t
    {
        Enqueued, Applied, Dropped, Pending, Invalid, Unauthorized,
        PacketBytes, EncodeNanoseconds, DecodeNanoseconds, Allocations, QueueDepth
    };
    // Local telemetry event, not a protocol payload. Pending/QueueDepth are gauges;
    // other kinds are deltas. reason is a stable adapter-specific numeric code.
    struct NetTelemetryEvent final
    {
        NetTick tick{};
        NetPeerId peer{};
        std::uint64_t value{};
        std::uint32_t reason{};
        NetTelemetryKind kind{};
    };
    class INetTelemetrySink
    {
    public:
        virtual ~INetTelemetrySink() = default;
        virtual void Record(const NetTelemetryEvent& event) noexcept = 0;
    };
}
