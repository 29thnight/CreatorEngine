#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Input
{
    // Stable IDs are serialized as two unsigned 64-bit words, never as slot indices.
    template<class Tag>
    struct StableID final
    {
        std::uint64_t high{};
        std::uint64_t low{};

        constexpr bool IsValid() const noexcept { return high != 0 || low != 0; }
        auto operator<=>(const StableID&) const = default;
    };

    struct GraphTag;
    struct SignalTag;
    struct BindingTag;
    struct LayerTag;
    using GraphID = StableID<GraphTag>;
    using SignalID = StableID<SignalTag>;
    using BindingID = StableID<BindingTag>;
    using LayerID = StableID<LayerTag>;
    using Timestamp = std::int64_t; // Nanoseconds on the corresponding monotonic domain clock.
    using UserID = std::uint64_t;
    using DeviceID = std::uint64_t;
    inline constexpr std::uint32_t kInputSchemaVersion = 1;
    inline constexpr std::uint32_t kInputCompilerVersion = 1;
    inline constexpr std::uint32_t kInputABIVersion = 1;
    inline constexpr std::uint32_t kInvalidSlot = UINT32_MAX;

    struct InputVector2 final
    {
        float x{};
        float y{};
        bool operator==(const InputVector2&) const = default;
    };

    struct Button final
    {
        bool held{};
        bool operator==(const Button&) const = default;
    };

    enum class ValueType : std::uint8_t { Button, Float, Vector2 };
    enum class ValueLifetime : std::uint8_t { Persistent, Delta };
    enum class Domain : std::uint8_t { Game, UI };
    enum class DeviceKind : std::uint8_t { Keyboard, Mouse, Gamepad };
    enum class SourceKind : std::uint8_t
    {
        Key, MouseButton, MouseDelta, PointerPosition, MouseWheel, GamepadButton, GamepadAxis
    };
    enum class CoordinateSpace : std::uint8_t { None, ClientPixels, RelativeCounts, Normalized };
    enum class CombinePolicy : std::uint8_t { Sum, MaximumMagnitude, MostRecent, Priority };
    enum class ClaimPolicy : std::uint8_t { OnPress, OnPerformed, PassThrough };
    enum class ProcessorKind : std::uint8_t { Deadzone, Normalize, Scale, Invert, Clamp };
    enum class InteractionKind : std::uint8_t { Press, Hold, Tap, Chord };
    enum class ChordOrder : std::uint8_t { Simultaneous, Sequential };
    enum class EventPhase : std::uint8_t { Started, Performed, Completed, Canceled };
    enum class CancelReason : std::uint8_t
    {
        None, LayerBlocked, UIOwnership, FocusLost, Paused, DeviceDisconnected, DeviceReassigned,
        DefinitionChanged, Rebound, HistoryGap, ClockDiscontinuity, SessionShutdown, ScriptReload, InteractionTimeout
    };
    enum class RecordKind : std::uint8_t
    {
        Control, Resync, FocusLost, FocusGained, Pause, Resume, DeviceDisconnected, DeviceAssigned,
        LayerChanged, OwnershipChanged, HistoryGap, ClockDiscontinuity
    };
    enum class Recipient : std::uint8_t { None = 0, Game = 1, UI = 2, Both = 3 };

    struct InputValue final
    {
        ValueType type{ ValueType::Button };
        float x{};
        float y{};

        static InputValue FromButton(bool value) noexcept { return { ValueType::Button, value ? 1.0f : 0.0f, 0.0f }; }
        static InputValue FromFloat(float value) noexcept { return { ValueType::Float, value, 0.0f }; }
        static InputValue FromVector2(InputVector2 value) noexcept { return { ValueType::Vector2, value.x, value.y }; }
        static InputValue Zero(ValueType type) noexcept { return { type, 0.0f, 0.0f }; }
        bool operator==(const InputValue&) const = default;
    };

    template<class T>
    struct SignalValueTraits;
    template<>
    struct SignalValueTraits<Button>
    {
        static constexpr ValueType kType = ValueType::Button;
        static Button Read(InputValue value) noexcept { return { value.x != 0.0f }; }
    };
    template<>
    struct SignalValueTraits<float>
    {
        static constexpr ValueType kType = ValueType::Float;
        static float Read(InputValue value) noexcept { return value.x; }
    };
    template<>
    struct SignalValueTraits<InputVector2>
    {
        static constexpr ValueType kType = ValueType::Vector2;
        static InputVector2 Read(InputValue value) noexcept { return { value.x, value.y }; }
    };

    template<class T>
    struct InputSignal final
    {
        GraphID graph{};
        SignalID signal{};
        std::uint64_t interfaceHash{}; // Rebind/processor settings do not invalidate generated accessors.
        std::uint32_t schemaVersion{ kInputSchemaVersion };
        std::uint32_t abiVersion{ kInputABIVersion };
        static constexpr ValueType kType = SignalValueTraits<T>::kType;
    };

    struct ControlID final
    {
        SourceKind kind{ SourceKind::Key };
        std::uint32_t code{}; // Physical key/button/axis ID; never text or IME composition.
        auto operator<=>(const ControlID&) const = default;
    };

    // Ingress publishes these by value. Routing decisions are immutable and are not
    // recomputed when another domain advances ahead of this domain's cursor.
    struct RoutedInputRecord final
    {
        RecordKind kind{ RecordKind::Control };
        Timestamp sourceTime{};
        Timestamp receivedTime{};
        Timestamp realTime{};
        Timestamp gameTime{};
        std::uint64_t sequence{};
        std::uint64_t routingEpoch{};
        std::uint64_t deviceEpoch{};
        std::uint64_t assignmentEpoch{};
        UserID user{};
        std::uint64_t targetSessionId{}; // Zero pair broadcasts within the routed user/domain scope.
        std::uint64_t targetSessionGeneration{};
        DeviceID device{};
        ControlID control{};
        InputValue value{};
        Recipient recipients{ Recipient::Both }; // Control routing decision; policy record target domains.
        LayerID layer{};
        bool enabled{}; // OwnershipChanged/LayerChanged resulting state for the targeted domains.
        bool late{};
        CancelReason reason{ CancelReason::None };

        Timestamp GetTime(Domain domain) const noexcept { return domain == Domain::Game ? gameTime : realTime; }
        bool IsRecipient(Domain domain) const noexcept
        {
            const auto mask = domain == Domain::Game ? Recipient::Game : Recipient::UI;
            return (static_cast<std::uint8_t>(recipients) & static_cast<std::uint8_t>(mask)) != 0;
        }
    };

    struct InputBoundary final
    {
        std::uint64_t sequence{}; // Fixed game tick or independent UI pump sequence.
        Timestamp begin{};
        Timestamp end{}; // The interval is (begin, end]; initial/late records clamp to begin.
        bool historyGap{};
    };

    struct InputSignalState final
    {
        SignalID signal{};
        InputValue value{};
        bool held{};
        bool pressed{};
        bool released{};
    };

    struct InputSignalEvent final
    {
        SignalID signal{};
        EventPhase phase{ EventPhase::Started };
        InputValue value{};
        Timestamp sourceTime{};
        Timestamp effectiveTime{};
        std::uint64_t sequence{};
        std::uint64_t routingEpoch{};
        std::uint64_t deviceEpoch{};
        UserID user{};
        CancelReason reason{ CancelReason::None };
        bool late{};
    };

    template<class T>
    struct SignalEvent final
    {
        InputSignal<T> signal{};
        EventPhase phase{ EventPhase::Started };
        T value{};
        Timestamp sourceTime{};
        Timestamp effectiveTime{};
        std::uint64_t sequence{};
        std::uint64_t routingEpoch{};
        std::uint64_t deviceEpoch{};
        UserID user{};
        CancelReason reason{ CancelReason::None };
        bool late{};
    };

    struct InputSessionHandle final
    {
        std::uint64_t id{};
        std::uint64_t generation{};
        bool operator==(const InputSessionHandle&) const = default;
    };
}
