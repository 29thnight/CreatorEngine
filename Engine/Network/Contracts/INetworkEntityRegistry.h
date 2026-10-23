#pragma once
#include "NetWireIds.h"

namespace ce::net
{
    // Process-local opaque token supplied by the ND-b adapter. Never encode it.
    struct NetEntityToken final { std::uint64_t value{}; };
    struct NetSceneToken final { std::uint64_t value{}; };
    struct NetEntityBinding final
    {
        NetworkObjectId object{};
        NetEntityToken entity{};
        NetSceneToken scene{};
        std::uint64_t revision{};
    };
    enum class NetRegistryResult : std::uint8_t
    {
        Ok, NotFound, InvalidArgument, AlreadyBound, StaleBinding, Retired, Full
    };
    // GT only, session-scoped. Bind accepts a fresh object/token pair only.
    // Rebind is compare-and-swap on the complete previous binding, advances its
    // revision and preserves object ID across DDOL/scene transfer. Unbind checks
    // that binding, tombstones the object and clears Resolve. No ID resurrection.
    // Resolve returns a value token for the ND-b local adapter, never an address.
    class INetworkEntityRegistry
    {
    public:
        virtual ~INetworkEntityRegistry() = default;
        virtual NetRegistryResult Bind(NetworkObjectId object, NetEntityToken entity, NetSceneToken scene,
            NetEntityBinding& bound) noexcept = 0;
        virtual NetRegistryResult Rebind(const NetEntityBinding& expected, NetEntityToken entity,
            NetSceneToken scene, NetEntityBinding& rebound) noexcept = 0;
        virtual NetRegistryResult Unbind(const NetEntityBinding& expected) noexcept = 0;
        virtual NetRegistryResult Resolve(NetworkObjectId object, NetEntityBinding& bound) const noexcept = 0;
    };
}
