#pragma once

#include "InputSession.h"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Input::Script
{
    // Flat little-endian POD only. No runtime bool, enum, STL container or pointer
    // is part of the value ABI. Native buffers are borrowed for one call and copied
    // by ScriptCore before dispatch. The managed copy owns its entire lifetime.
    struct Value final
    {
        std::uint32_t type{};
        float x{};
        float y{};
        std::uint32_t reserved{};
    };

    struct State final
    {
        SignalID signal{};
        Value value{};
        std::uint32_t flags{}; // held=1, pressed=2, released=4.
        std::uint32_t reserved{};
    };

    struct Event final
    {
        SignalID signal{};
        Value value{};
        Timestamp sourceTime{};
        Timestamp effectiveTime{};
        std::uint64_t sequence{};
        std::uint64_t routingEpoch{};
        std::uint64_t deviceEpoch{};
        UserID user{};
        std::uint32_t phase{};
        std::uint32_t reason{};
        std::uint32_t flags{}; // late=1.
        std::uint32_t reserved{};
    };

    struct FrameHeader final
    {
        std::uint32_t abiVersion{ kInputABIVersion };
        std::uint32_t schemaVersion{ kInputSchemaVersion };
        std::uint32_t compilerVersion{ kInputCompilerVersion };
        std::uint32_t flags{}; // history gap=1.
        InputSessionHandle session{};
        GraphID graph{};
        std::uint64_t semanticHash{};
        std::uint64_t interfaceHash{};
        std::uint64_t definitionGeneration{};
        UserID user{};
        std::uint64_t sequence{};
        Timestamp begin{};
        Timestamp end{};
        std::uint32_t domain{};
        std::uint32_t stateCount{};
        std::uint32_t eventCount{};
        std::uint32_t reserved{};
    };

    enum class RequestKind : std::uint32_t { Layer, Device, Rebind, Cancel };
    struct Request final
    {
        std::uint32_t kind{};
        std::uint32_t enabled{};
        LayerID target{}; // Layer ID or binding ID for a single-source override.
        DeviceID device{};
        std::uint64_t deviceEpoch{};
        std::uint64_t assignmentEpoch{};
        std::uint32_t sourceKind{};
        std::uint32_t controlCode{};
        float scaleX{ 1.0f };
        float scaleY{};
        std::uint32_t coordinateSpace{};
        std::uint32_t reason{};
    };

    struct Device final
    {
        DeviceID id{};
        std::uint64_t epoch{};
        std::uint64_t assignmentEpoch{};
        UserID user{};
        std::uint32_t kind{};
        std::int32_t controllerIndex{ -1 };
        std::uint32_t connected{};
        std::uint32_t reserved{};
    };

    enum class Result : std::int32_t
    {
        Success, InvalidSession, WrongThread, InvalidArgument, NoFrame, Capacity, Rejected, Failure
    };

    inline constexpr std::uint32_t kMaximumStates = 65'536;
    inline constexpr std::uint32_t kMaximumEvents = 262'144;

    inline Value CopyValue(InputValue value) noexcept
    {
        return { static_cast<std::uint32_t>(value.type), value.x, value.y, 0 };
    }

    inline FrameHeader CopyHeader(const InputFrame& frame) noexcept
    {
        const auto& boundary = frame.GetBoundary();
        return { kInputABIVersion, kInputSchemaVersion, kInputCompilerVersion,
            frame.HasHistoryGap() ? 1u : 0u, frame.GetSession(), frame.GetGraphID(), frame.GetSemanticHash(),
            frame.GetInterfaceHash(), frame.GetDefinitionGeneration(), frame.GetUser(), boundary.sequence, boundary.begin, boundary.end,
            static_cast<std::uint32_t>(frame.GetDomain()), static_cast<std::uint32_t>(frame.GetStates().size()),
            static_cast<std::uint32_t>(frame.GetEvents().size()), 0 };
    }

    inline State CopyState(const InputSignalState& state) noexcept
    {
        return { state.signal, CopyValue(state.value),
            (state.held ? 1u : 0u) | (state.pressed ? 2u : 0u) | (state.released ? 4u : 0u), 0 };
    }

    inline Event CopyEvent(const InputSignalEvent& event) noexcept
    {
        return { event.signal, CopyValue(event.value), event.sourceTime, event.effectiveTime, event.sequence,
            event.routingEpoch, event.deviceEpoch, event.user, static_cast<std::uint32_t>(event.phase),
            static_cast<std::uint32_t>(event.reason), event.late ? 1u : 0u, 0 };
    }

    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(GraphID) == 16 && sizeof(InputSessionHandle) == 16);
    static_assert(sizeof(Value) == 16 && sizeof(State) == 40);
    static_assert(sizeof(Event) == 96 && offsetof(Event, sequence) == 48 && offsetof(Event, phase) == 80);
    static_assert(sizeof(FrameHeader) == 120 && offsetof(FrameHeader, session) == 16);
    static_assert(offsetof(FrameHeader, sequence) == 80 && offsetof(FrameHeader, domain) == 104);
    static_assert(sizeof(Device) == 48 && offsetof(Device, kind) == 32);
    static_assert(std::is_trivially_copyable_v<Device>);
    static_assert(sizeof(Request) == 72 && offsetof(Request, assignmentEpoch) == 40);
    static_assert(std::is_trivially_copyable_v<FrameHeader> && std::is_trivially_copyable_v<State>);
    static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_copyable_v<Request>);
}
