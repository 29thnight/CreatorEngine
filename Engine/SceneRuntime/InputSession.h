#pragma once

#include "../Utility_Framework/InputGraph.h"

#include <array>
#include <thread>
#include <vector>

namespace Input
{
    class InputFrame final
    {
    public:
        GraphID GetGraphID() const noexcept { return m_graph; }
        std::uint64_t GetSemanticHash() const noexcept { return m_semanticHash; }
        std::uint64_t GetInterfaceHash() const noexcept { return m_interfaceHash; }
        std::uint64_t GetDefinitionGeneration() const noexcept { return m_definitionGeneration; }
        InputSessionHandle GetSession() const noexcept { return m_session; }
        UserID GetUser() const noexcept { return m_user; }
        Domain GetDomain() const noexcept { return m_domain; }
        const InputBoundary& GetBoundary() const noexcept { return m_boundary; }
        bool HasHistoryGap() const noexcept { return m_boundary.historyGap; }
        std::span<const InputSignalState> GetStates() const noexcept { return m_states; }
        std::span<const InputSignalEvent> GetEvents() const noexcept { return m_events; }
        const InputSignalState* FindState(SignalID signal, ValueType type) const noexcept;

        template<class T>
        bool TryRead(InputSignal<T> signal, T& value) const noexcept
        {
            value = T{};
            if (signal.graph != m_graph || signal.interfaceHash != m_interfaceHash ||
                signal.schemaVersion != kInputSchemaVersion ||
                signal.abiVersion != kInputABIVersion)
            {
                return false;
            }
            const auto* state = FindState(signal.signal, InputSignal<T>::kType);
            if (state == nullptr)
            {
                return false;
            }
            value = SignalValueTraits<T>::Read(state->value);
            return true;
        }

        template<class T>
        bool TryReadEvent(std::size_t index, InputSignal<T> signal, SignalEvent<T>& result) const noexcept
        {
            result = {};
            T ignored{};
            if (!TryRead(signal, ignored) || index >= m_events.size())
            {
                return false;
            }
            const auto& event = m_events[index];
            if (event.signal != signal.signal || event.value.type != InputSignal<T>::kType)
            {
                return false;
            }
            result = { signal, event.phase, SignalValueTraits<T>::Read(event.value), event.sourceTime,
                event.effectiveTime, event.sequence, event.routingEpoch, event.deviceEpoch, event.user,
                event.reason, event.late };
            return true;
        }

    private:
        friend class InputSession;
        GraphID m_graph{};
        std::uint64_t m_semanticHash{};
        std::uint64_t m_interfaceHash{};
        std::uint64_t m_definitionGeneration{};
        InputSessionHandle m_session{};
        UserID m_user{};
        Domain m_domain{ Domain::Game };
        InputBoundary m_boundary{};
        std::vector<InputSignalState> m_states;
        std::vector<InputSignalEvent> m_events;
    };

    // All calls, including control requests and shutdown, belong to the thread that
    // calls Initialize. Cross-thread producers submit to ingress, never this object.
    // Frames outlive the session through immutable ownership and do not retain it.
    class InputSession final
    {
    public:
        InputSession() = default;
        ~InputSession() = default;
        InputSession(const InputSession&) = delete;
        InputSession& operator=(const InputSession&) = delete;

        bool Initialize(InputSessionHandle handle, UserID user,
            own::shared_owner<const InputGraphProgram> program);
        bool IsInitialized() const noexcept { return m_initialized; }
        bool IsOwnerThread() const noexcept;
        InputSessionHandle GetHandle() const noexcept { return m_handle; }
        UserID GetUser() const noexcept { return m_user; }
        const own::shared_owner<const InputGraphProgram>& GetProgram() const noexcept { return m_program; }

        // Invalid/duplicate boundaries, wrong-thread use and malformed batches fail
        // closed with an empty owner; they never mutate a previously sealed frame.
        // Nonfinite device telemetry instead seals an explicit gap/cancellation.
        // The caller retains future records until their domain boundary is due.
        [[nodiscard]] own::shared_owner<const InputFrame> Evaluate(Domain domain, InputBoundary boundary,
            std::span<const RoutedInputRecord> records);
        bool QueueLayerChange(LayerID layer, bool enabled);
        bool QueueProgram(own::shared_owner<const InputGraphProgram> program,
            CancelReason reason = CancelReason::DefinitionChanged);
        bool QueueRebind(std::span<const InputBindingOverride> overrides,
            std::vector<InputDiagnostic>& diagnostics);
        bool QueueDeviceAssignment(DeviceID device, std::uint64_t deviceEpoch, std::uint64_t assignmentEpoch,
            bool assigned);
        bool QueueCancel(CancelReason reason);
        bool QueueCancel(Domain domain, CancelReason reason);
        // Produces the last cancellation frame per domain, invalidates the handle,
        // and releases the program. Read GetLastFrame to dispatch that final batch.
        bool Shutdown(Timestamp gameTime, Timestamp realTime, CancelReason reason = CancelReason::SessionShutdown);
        const own::shared_owner<const InputFrame>& GetLastFrame(Domain domain) const noexcept;

    private:
        struct ControlState final
        {
            DeviceID device{};
            std::uint64_t deviceEpoch{};
            std::uint64_t assignmentEpoch{};
            ControlID control{};
            InputValue value{};
            std::uint64_t sequence{};
            bool requiresNeutral{};
            bool allowed{ true };
        };
        struct DeviceState final
        {
            DeviceID device{};
            std::uint64_t epoch{};
            std::uint64_t assignmentEpoch{};
            bool assigned{ true };
        };
        struct BindingState final
        {
            InputValue value{};
            std::uint64_t sequence{};
            bool requiresNeutral{};
        };
        struct SignalState final
        {
            InputValue value{};
            bool held{};
            bool active{};
            bool performed{};
            bool timerExhausted{};
            bool requiresNeutral{};
            Timestamp started{};
            Timestamp deadline{};
            Timestamp lastPressTime{};
            std::uint64_t lastPressSequence{};
            std::uint64_t chordObservedSequence{};
            std::size_t chordProgress{};
            RoutedInputRecord origin{};
        };
        struct Command final
        {
            enum class Kind : std::uint8_t { Layer, Program, Device, Cancel };
            Kind kind{ Kind::Cancel };
            std::uint64_t sequence{};
            LayerID layer{};
            DeviceID device{};
            std::uint64_t deviceEpoch{};
            std::uint64_t assignmentEpoch{};
            bool enabled{};
            CancelReason reason{ CancelReason::None };
            own::shared_owner<const InputGraphProgram> program;
        };
        struct DomainState final
        {
            own::shared_owner<const InputGraphProgram> program;
            std::vector<ControlState> controls;
            std::vector<DeviceState> devices;
            std::vector<BindingState> bindings;
            std::vector<SignalState> signals;
            std::vector<bool> layers;
            std::vector<bool> claimedBindings;
            std::vector<std::uint32_t> activeClaimBindings;
            std::vector<bool> dirtyBindings;
            std::vector<Command> commands;
            own::shared_owner<const InputFrame> lastFrame;
            Timestamp frontier{};
            std::uint64_t lastBoundary{};
            std::uint64_t lastSequence{};
            std::vector<std::uint64_t> consumedSequences;
            std::uint64_t retiredSequence{};
            bool hasBoundary{};
            bool focused{ true };
            bool paused{};
            bool owned{ true };
        };

        bool Enqueue(Command command);
        void ResetDefinition(DomainState& state, own::shared_owner<const InputGraphProgram> program);
        void ApplyCommands(Domain domain, DomainState& state, InputFrame& frame, const RoutedInputRecord& at);
        void ProcessRecord(Domain domain, DomainState& state, InputFrame& frame, RoutedInputRecord record);
        void EvaluateSignals(Domain domain, DomainState& state, InputFrame& frame, const RoutedInputRecord& record);
        void AdvanceTimers(Domain domain, DomainState& state, InputFrame& frame, Timestamp time,
            bool includeBoundary, const RoutedInputRecord& origin);
        void CancelSignal(DomainState& state, InputFrame& frame, std::uint32_t slot,
            CancelReason reason, const RoutedInputRecord& record);
        void CancelAll(Domain domain, DomainState& state, InputFrame& frame, CancelReason reason,
            const RoutedInputRecord& record);
        void Emit(DomainState& state, InputFrame& frame, std::uint32_t slot, EventPhase phase,
            const RoutedInputRecord& record, CancelReason reason = CancelReason::None);
        void RebuildFrameStates(Domain domain, DomainState& state, InputFrame& frame);

        InputSessionHandle m_handle{};
        UserID m_user{};
        own::shared_owner<const InputGraphProgram> m_program;
        std::array<DomainState, 2> m_domains;
        std::thread::id m_owner;
        std::uint64_t m_commandSequence{};
        bool m_initialized{};
    };
}
