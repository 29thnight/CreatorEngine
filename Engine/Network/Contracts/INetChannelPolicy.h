#pragma once
#include "INetworkTransport.h"

namespace ce::net
{
    enum class NetMessageClass : std::uint8_t { Handshake, Lifecycle, ConfirmedRpc, Snapshot, Input, Chat, AssetTransfer };
    struct NetChannelRule final
    {
        NetLane lane{};
        NetDelivery delivery{};
        std::uint8_t priority{}, allowed{};
    };
    constexpr NetChannelRule DefaultChannelRule(NetMessageClass kind) noexcept
    {
        switch (kind)
        {
        case NetMessageClass::Handshake:
        case NetMessageClass::Lifecycle:
        case NetMessageClass::ConfirmedRpc: return {NetLane::Control, NetDelivery::ReliableOrdered, 3, 1};
        case NetMessageClass::Snapshot: return {NetLane::State, NetDelivery::UnreliableSequenced, 2, 1};
        case NetMessageClass::Input: return {NetLane::Input, NetDelivery::UnreliableSequenced, 3, 1};
        case NetMessageClass::Chat: return {NetLane::Chat, NetDelivery::ReliableOrdered, 0, 1};
        default: return {}; // asset/patch uses a separate service, never replication
        }
    }
    class INetChannelPolicy
    {
    public:
        virtual ~INetChannelPolicy() = default;
        virtual NetChannelRule Select(NetMessageClass kind) const noexcept = 0;
    };
}
