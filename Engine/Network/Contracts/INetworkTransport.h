#pragma once
#include "NetWireIds.h"
#include <cstddef>
#include <span>

namespace ce::net
{
    enum class NetLane : std::uint8_t { Control = 0, State = 1, Input = 2, Chat = 3 };
    enum class NetDelivery : std::uint8_t { ReliableOrdered = 0, UnreliableSequenced = 1 };
    enum class NetConnectionState : std::uint8_t { Disconnected, Connecting, Connected, Draining, Closed };
    enum class NetTransportResult : std::uint8_t
    {
        Ok, Empty, Full, BufferTooSmall, Disconnected, InvalidArgument, Unsupported, Failed
    };
    enum class NetAddressFamily : std::uint8_t { Loopback = 0, IPv4 = 1, IPv6 = 2 };
    // Local configuration, not a protocol record. IPv4 uses address_low's low
    // 32 bits (127.0.0.1 = 0x7f000001), high/unused bits zero. IPv6's network
    // octets form high then low as unsigned numeric values. Port is host numeric.
    struct NetEndpoint final
    {
        std::uint64_t address_high{}, address_low{};
        std::uint16_t port{};
        NetAddressFamily family{};
    };
    struct NetReceivedMessage final
    {
        NetPeerId peer{};
        NetLane lane{};
        NetDelivery delivery{};
        std::uint32_t bytes{};
    };
    struct NetTransportStatistics final
    {
        std::uint64_t sent_messages{}, received_messages{}, sent_bytes{}, received_bytes{};
        std::uint64_t pending_messages{}, pending_bytes{}, dropped_messages{};
    };
    // Single owner/pump thread. No blocking calls or callbacks. Configuration
    // (queue/message bounds, crypto, sockets) belongs to the concrete adapter.
    // Send copies a complete message on Ok; Full accepts nothing. Receive copies
    // one message; BufferTooSmall sets metadata.bytes and does not dequeue it.
    // Drain rejects new sends, pumps accepted sends, then reaches Closed.
    class INetworkTransport
    {
    public:
        virtual ~INetworkTransport() = default;
        virtual NetTransportResult Connect(NetPeerId peer, const NetEndpoint& endpoint) noexcept = 0;
        virtual NetTransportResult Send(NetPeerId peer, NetLane lane, NetDelivery delivery,
            std::span<const std::byte> payload) noexcept = 0;
        virtual NetTransportResult Receive(NetReceivedMessage& metadata, std::span<std::byte> output) noexcept = 0;
        virtual void Pump() noexcept = 0;
        virtual void BeginDrain(NetPeerId peer) noexcept = 0;
        virtual void Disconnect(NetPeerId peer) noexcept = 0;
        virtual NetConnectionState State(NetPeerId peer) const noexcept = 0;
        virtual NetTransportStatistics Statistics() const noexcept = 0;
    };
}
