#include "InputSession.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <queue>
#include <utility>

namespace Input
{
    namespace
    {
        constexpr std::size_t kMaximumFrameEvents = 65'536;

        std::size_t DomainIndex(Domain domain) { return static_cast<std::size_t>(domain); }

        bool IsDelta(ControlID control)
        {
            return control.kind == SourceKind::MouseDelta || control.kind == SourceKind::MouseWheel;
        }

        double Magnitude(InputValue value)
        {
            return value.type == ValueType::Vector2 ? std::hypot(static_cast<double>(value.x),
                static_cast<double>(value.y)) : std::abs(static_cast<double>(value.x));
        }

        bool IsFinite(InputValue value)
        {
            return static_cast<unsigned>(value.type) <= static_cast<unsigned>(ValueType::Vector2) &&
                std::isfinite(value.x) && std::isfinite(value.y);
        }

        bool HasValidControlType(const RoutedInputRecord& record)
        {
            ValueType expected = ValueType::Button;
            switch (record.control.kind)
            {
            case SourceKind::Key:
            case SourceKind::MouseButton:
            case SourceKind::GamepadButton:
                expected = ValueType::Button;
                break;
            case SourceKind::MouseDelta:
            case SourceKind::PointerPosition:
                expected = ValueType::Vector2;
                break;
            case SourceKind::MouseWheel:
                expected = ValueType::Float;
                break;
            case SourceKind::GamepadAxis:
                if (record.control.code > 3)
                {
                    return false;
                }
                expected = record.control.code < 2 ? ValueType::Vector2 : ValueType::Float;
                break;
            default:
                return false;
            }
            return record.value.type == expected && (expected == ValueType::Vector2 || record.value.y == 0.0f) &&
                (expected != ValueType::Button || record.value.x == 0.0f || record.value.x == 1.0f);
        }

        Timestamp AddTime(Timestamp time, Timestamp duration)
        {
            const auto maximum = (std::numeric_limits<Timestamp>::max)();
            return time > maximum - duration ? maximum : time + duration;
        }

        InputValue ProcessValue(InputValue value, std::span<const InputProcessor> processors)
        {
            for (const auto& processor : processors)
            {
                switch (processor.kind)
                {
                case ProcessorKind::Scale:
                    value.x *= processor.x;
                    value.y *= processor.y;
                    break;
                case ProcessorKind::Invert:
                    value.x = -value.x;
                    value.y = -value.y;
                    break;
                case ProcessorKind::Normalize:
                {
                    const double magnitude = Magnitude(value);
                    if (magnitude > 0.0f)
                    {
                        value.x = static_cast<float>(value.x / magnitude);
                        value.y = static_cast<float>(value.y / magnitude);
                    }
                    break;
                }
                case ProcessorKind::Deadzone:
                {
                    const double magnitude = Magnitude(value);
                    if (magnitude <= processor.x)
                    {
                        value.x = 0.0f;
                        value.y = 0.0f;
                    }
                    else
                    {
                        const double rescaled = (std::min)(1.0, (magnitude - processor.x) / (processor.y - processor.x));
                        value.x = static_cast<float>(value.x * (rescaled / magnitude));
                        value.y = static_cast<float>(value.y * (rescaled / magnitude));
                    }
                    break;
                }
                case ProcessorKind::Clamp:
                    value.x = std::clamp(value.x, processor.x, processor.y);
                    value.y = std::clamp(value.y, processor.x, processor.y);
                    break;
                }
            }
            if (value.type != ValueType::Vector2)
            {
                value.y = 0.0f;
            }
            return value;
        }

        bool SharesControl(const InputBinding& left, const InputBinding& right)
        {
            for (const auto& source : left.sources)
            {
                for (const auto& other : right.sources)
                {
                    if (source.control == other.control)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        template<class State>
        bool IsBindingNeutral(const State& state, const InputBinding& binding, float threshold)
        {
            for (const auto& source : binding.sources)
            {
                auto control = std::lower_bound(state.controls.begin(), state.controls.end(), source.control,
                    [](const auto& value, ControlID key) { return value.control < key; });
                for (; control != state.controls.end() && control->control == source.control; ++control)
                {
                    if (Magnitude(control->value) > threshold)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        template<class State>
        InputValue ReadBinding(const State& state, const InputBinding& binding, ValueType type,
            std::uint64_t& sequence)
        {
            auto value = InputValue::Zero(type);
            sequence = 0;
            for (const auto& source : binding.sources)
            {
                auto found = std::lower_bound(state.controls.begin(), state.controls.end(), source.control,
                    [](const auto& value, ControlID key) { return value.control < key; });
                for (; found != state.controls.end() && found->control == source.control; ++found)
                {
                    const auto& control = *found;
                    if (!control.allowed)
                    {
                        continue;
                    }
                    sequence = (std::max)(sequence, control.sequence);
                    if (control.value.type == ValueType::Vector2)
                    {
                        if (type == ValueType::Button)
                        {
                            value.x += static_cast<float>(Magnitude(control.value) * source.scaleX);
                        }
                        else
                        {
                            value.x += control.value.x * source.scaleX;
                            // A vector source uses scaleX for both axes unless scaleY is explicit.
                            value.y += control.value.y * (source.scaleY == 0.0f ? source.scaleX : source.scaleY);
                        }
                    }
                    else
                    {
                        value.x += control.value.x * source.scaleX;
                        value.y += control.value.x * source.scaleY;
                    }
                }
            }
            return ProcessValue(value, binding.processors);
        }
    }

    const InputSignalState* InputFrame::FindState(SignalID signal, ValueType type) const noexcept
    {
        const auto found = std::lower_bound(m_states.begin(), m_states.end(), signal,
            [](const InputSignalState& state, SignalID key) { return state.signal < key; });
        if (found == m_states.end() || found->signal != signal || found->value.type != type)
        {
            return nullptr;
        }
        return &*found;
    }

    bool InputSession::IsOwnerThread() const noexcept
    {
        return m_initialized && m_owner == std::this_thread::get_id();
    }

    bool InputSession::Initialize(InputSessionHandle handle, UserID user,
        own::shared_owner<const InputGraphProgram> program)
    {
        if (m_initialized || handle.id == 0 || handle.generation == 0 || user == 0 || !program)
        {
            return false;
        }
        m_owner = std::this_thread::get_id();
        m_handle = handle;
        m_user = user;
        m_program = std::move(program);
        m_commandSequence = 0;
        for (auto& state : m_domains)
        {
            state = {};
            ResetDefinition(state, m_program);
        }
        m_initialized = true;
        return true;
    }

    void InputSession::ResetDefinition(DomainState& state, own::shared_owner<const InputGraphProgram> program)
    {
        state.program = std::move(program);
        const auto& graph = state.program->GetDefinition();
        state.bindings.assign(graph.bindings.size(), {});
        state.signals.assign(graph.signals.size(), {});
        state.layers.clear();
        for (const auto& layer : graph.layers)
        {
            state.layers.push_back(layer.enabled);
        }
        for (std::size_t index = 0; index < graph.signals.size(); ++index)
        {
            state.signals[index].value = InputValue::Zero(graph.signals[index].type);
        }
        state.claimedBindings.assign(graph.bindings.size(), false);
        state.activeClaimBindings.clear();
        state.dirtyBindings.assign(graph.bindings.size(), true);
    }

    bool InputSession::Enqueue(Command command)
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        constexpr std::size_t kMaximumPendingCommands = 4'096;
        for (const auto& state : m_domains)
        {
            if (state.commands.size() >= kMaximumPendingCommands)
            {
                return false;
            }
        }
        command.sequence = ++m_commandSequence;
        for (auto& state : m_domains)
        {
            state.commands.push_back(command);
        }
        return true;
    }

    bool InputSession::QueueLayerChange(LayerID layer, bool enabled)
    {
        if (!IsOwnerThread() || m_program->FindLayer(layer) == kInvalidSlot)
        {
            return false;
        }
        Command command;
        command.kind = Command::Kind::Layer;
        command.layer = layer;
        command.enabled = enabled;
        return Enqueue(std::move(command));
    }

    bool InputSession::QueueProgram(own::shared_owner<const InputGraphProgram> program, CancelReason reason)
    {
        if (!IsOwnerThread() || !program || program->GetDefinition().id != m_program->GetDefinition().id ||
            (reason != CancelReason::DefinitionChanged && reason != CancelReason::Rebound))
        {
            return false;
        }
        // An existing stable signal ID cannot change type even across a valid reload.
        for (const auto& signal : m_program->GetDefinition().signals)
        {
            const auto& signals = program->GetDefinition().signals;
            const auto replacement = std::lower_bound(signals.begin(), signals.end(), signal.id,
                [](const InputSignalDefinition& candidate, SignalID id) { return candidate.id < id; });
            if (replacement != signals.end() && signal.id == replacement->id && signal.type != replacement->type)
            {
                return false;
            }
        }
        Command command;
        command.kind = Command::Kind::Program;
        command.reason = reason;
        command.program = program;
        if (!Enqueue(std::move(command)))
        {
            return false;
        }
        m_program = std::move(program);
        return true;
    }

    bool InputSession::QueueRebind(std::span<const InputBindingOverride> overrides,
        std::vector<InputDiagnostic>& diagnostics)
    {
        if (!IsOwnerThread())
        {
            diagnostics = { { {}, {}, "Input rebind must be requested on the session owner thread." } };
            return false;
        }
        auto program = ApplyInputOverrides(*m_program, overrides, diagnostics);
        return program && QueueProgram(std::move(program), CancelReason::Rebound);
    }

    bool InputSession::QueueDeviceAssignment(DeviceID device, std::uint64_t deviceEpoch,
        std::uint64_t assignmentEpoch, bool assigned)
    {
        if (device == 0 || deviceEpoch == 0 || assignmentEpoch == 0)
        {
            return false;
        }
        Command command;
        command.kind = Command::Kind::Device;
        command.device = device;
        command.deviceEpoch = deviceEpoch;
        command.assignmentEpoch = assignmentEpoch;
        command.enabled = assigned;
        return Enqueue(std::move(command));
    }

    bool InputSession::QueueCancel(CancelReason reason)
    {
        if (reason == CancelReason::None || static_cast<unsigned>(reason) > static_cast<unsigned>(CancelReason::InteractionTimeout))
        {
            return false;
        }
        Command command;
        command.kind = Command::Kind::Cancel;
        command.reason = reason;
        return Enqueue(std::move(command));
    }

    bool InputSession::QueueCancel(Domain domain, CancelReason reason)
    {
        if (!IsOwnerThread() || DomainIndex(domain) >= m_domains.size() || reason == CancelReason::None ||
            static_cast<unsigned>(reason) > static_cast<unsigned>(CancelReason::InteractionTimeout))
        {
            return false;
        }
        auto& commands = m_domains[DomainIndex(domain)].commands;
        if (commands.size() >= 4'096)
        {
            return false;
        }
        Command command;
        command.kind = Command::Kind::Cancel;
        command.reason = reason;
        command.sequence = ++m_commandSequence;
        commands.push_back(std::move(command));
        return true;
    }

    const own::shared_owner<const InputFrame>& InputSession::GetLastFrame(Domain domain) const noexcept
    {
        // Invalid enums return an empty owner instead of indexing outside the domain array.
        static const own::shared_owner<const InputFrame> empty;
        return DomainIndex(domain) < m_domains.size() ? m_domains[DomainIndex(domain)].lastFrame : empty;
    }

    void InputSession::Emit(DomainState& state, InputFrame& frame, std::uint32_t slot, EventPhase phase,
        const RoutedInputRecord& record, CancelReason reason)
    {
        if (phase != EventPhase::Canceled && frame.m_events.size() >= kMaximumFrameEvents)
        {
            frame.m_boundary.historyGap = true;
            return;
        }
        const auto& definition = state.program->GetDefinition().signals[slot];
        const auto& signal = state.signals[slot];
        frame.m_events.push_back({ definition.id, phase, signal.value, record.sourceTime,
            record.GetTime(frame.m_domain), record.sequence, record.routingEpoch, record.deviceEpoch,
            m_user, reason, record.late });
    }

    void InputSession::CancelSignal(DomainState& state, InputFrame& frame, std::uint32_t slot,
        CancelReason reason, const RoutedInputRecord& record)
    {
        auto& signal = state.signals[slot];
        const auto& definition = state.program->GetDefinition().signals[slot];
        if (signal.active || signal.held || signal.chordProgress != 0)
        {
            Emit(state, frame, slot, EventPhase::Canceled, record, reason);
        }
        signal = {};
        signal.value = InputValue::Zero(definition.type);
        const bool needsNeutral = !definition.resumePersistentValue && definition.lifetime == ValueLifetime::Persistent;
        signal.requiresNeutral = needsNeutral;
        for (const auto binding : state.program->GetSignals()[slot].bindingIndices)
        {
            state.bindings[binding].requiresNeutral = needsNeutral;
            state.bindings[binding].value = InputValue::Zero(definition.type);
            state.claimedBindings[binding] = false;
            state.dirtyBindings[binding] = true;
        }
    }

    void InputSession::CancelAll(Domain domain, DomainState& state, InputFrame& frame, CancelReason reason,
        const RoutedInputRecord& record)
    {
        const auto& graph = state.program->GetDefinition();
        for (std::uint32_t index = 0; index < graph.signals.size(); ++index)
        {
            if (graph.signals[index].domain == domain)
            {
                CancelSignal(state, frame, index, reason, record);
            }
        }
        for (auto& control : state.controls)
        {
            if (IsDelta(control.control))
            {
                control.value = InputValue::Zero(control.value.type);
            }
            control.requiresNeutral = Magnitude(control.value) > 0.0f;
        }
    }

    void InputSession::ApplyCommands(Domain domain, DomainState& state, InputFrame& frame,
        const RoutedInputRecord& at)
    {
        for (const auto& command : state.commands)
        {
            auto record = at;
            switch (command.kind)
            {
            case Command::Kind::Layer:
                record.kind = RecordKind::LayerChanged;
                record.layer = command.layer;
                record.enabled = command.enabled;
                ProcessRecord(domain, state, frame, record);
                break;
            case Command::Kind::Device:
                record.kind = command.enabled ? RecordKind::DeviceAssigned : RecordKind::DeviceDisconnected;
                record.device = command.device;
                record.deviceEpoch = command.deviceEpoch;
                record.assignmentEpoch = command.assignmentEpoch;
                record.enabled = command.enabled;
                record.reason = CancelReason::DeviceReassigned;
                ProcessRecord(domain, state, frame, record);
                break;
            case Command::Kind::Cancel:
                CancelAll(domain, state, frame, command.reason, record);
                break;
            case Command::Kind::Program:
                if (command.program->GetSemanticHash() != state.program->GetSemanticHash())
                {
                    CancelAll(domain, state, frame, command.reason, record);
                    const auto previousLayers = state.layers;
                    const auto previousProgram = state.program;
                    ResetDefinition(state, command.program);
                    for (std::uint32_t index = 0; index < state.layers.size(); ++index)
                    {
                        const auto previous = previousProgram->FindLayer(state.program->GetDefinition().layers[index].id);
                        if (previous != kInvalidSlot)
                        {
                            state.layers[index] = previousLayers[previous];
                        }
                    }
                    for (std::size_t index = 0; index < state.signals.size(); ++index)
                    {
                        const auto& definition = state.program->GetDefinition().signals[index];
                        const bool gated = !definition.resumePersistentValue && definition.lifetime == ValueLifetime::Persistent;
                        state.signals[index].requiresNeutral = gated;
                        for (const auto binding : state.program->GetSignals()[index].bindingIndices)
                        {
                            state.bindings[binding].requiresNeutral = gated;
                        }
                    }
                }
                else
                {
                    // Layout/display-only publication keeps FSM and claims intact.
                    state.program = command.program;
                }
                break;
            }
        }
        state.commands.clear();
    }

    void InputSession::ProcessRecord(Domain domain, DomainState& state, InputFrame& frame, RoutedInputRecord record)
    {
        if (record.kind != RecordKind::Control && record.kind != RecordKind::Resync && !record.IsRecipient(domain))
        {
            return;
        }
        const auto& graph = state.program->GetDefinition();
        const auto cancelDevice = [&](DeviceID device, CancelReason reason)
        {
            std::vector<bool> affected(graph.signals.size());
            for (const auto slot : state.program->GetEvaluationOrder())
            {
                const auto& prepared = state.program->GetSignals()[slot];
                for (const auto binding : prepared.bindingIndices)
                {
                    for (const auto& source : graph.bindings[binding].sources)
                    {
                        for (const auto& control : state.controls)
                        {
                            if (control.device == device && control.control == source.control)
                            {
                                affected[slot] = true;
                            }
                        }
                    }
                }
                if (prepared.gateIndex != kInvalidSlot && affected[prepared.gateIndex])
                {
                    affected[slot] = true;
                }
                for (const auto dependency : prepared.chordIndices)
                {
                    affected[slot] = affected[slot] || affected[dependency];
                }
                if (affected[slot] && graph.signals[slot].domain == domain)
                {
                    CancelSignal(state, frame, slot, reason, record);
                }
            }
            std::erase_if(state.controls, [&](const ControlState& control) { return control.device == device; });
            std::fill(state.dirtyBindings.begin(), state.dirtyBindings.end(), true);
        };

        switch (record.kind)
        {
        case RecordKind::FocusLost:
            state.focused = false;
            CancelAll(domain, state, frame, CancelReason::FocusLost, record);
            return;
        case RecordKind::FocusGained:
            state.focused = true;
            return;
        case RecordKind::Pause:
            if (domain == Domain::Game)
            {
                state.paused = true;
                CancelAll(domain, state, frame, CancelReason::Paused, record);
            }
            return;
        case RecordKind::Resume:
            if (domain == Domain::Game)
            {
                state.paused = false;
            }
            return;
        case RecordKind::HistoryGap:
        case RecordKind::ClockDiscontinuity:
            frame.m_boundary.historyGap = true;
            CancelAll(domain, state, frame, record.kind == RecordKind::HistoryGap ?
                CancelReason::HistoryGap : CancelReason::ClockDiscontinuity, record);
            return;
        case RecordKind::OwnershipChanged:
            if (record.IsRecipient(domain))
            {
                state.owned = record.enabled;
                if (!state.owned)
                {
                    CancelAll(domain, state, frame, record.reason == CancelReason::None ?
                        CancelReason::UIOwnership : record.reason, record);
                }
            }
            return;
        case RecordKind::LayerChanged:
        {
            const auto layer = state.program->FindLayer(record.layer);
            if (layer != kInvalidSlot && state.layers[layer] != record.enabled)
            {
                state.layers[layer] = record.enabled;
                for (std::uint32_t slot = 0; slot < graph.signals.size(); ++slot)
                {
                    if (state.program->GetSignals()[slot].layerIndex == layer && graph.signals[slot].domain == domain)
                    {
                        // Enabling also starts neutral; no inherited held press.
                        CancelSignal(state, frame, slot, CancelReason::LayerBlocked, record);
                    }
                }
                EvaluateSignals(domain, state, frame, record);
            }
            return;
        }
        case RecordKind::DeviceAssigned:
        case RecordKind::DeviceDisconnected:
        {
            auto found = std::find_if(state.devices.begin(), state.devices.end(),
                [&](const DeviceState& device) { return device.device == record.device; });
            if (found != state.devices.end() && (record.deviceEpoch < found->epoch ||
                record.assignmentEpoch < found->assignmentEpoch))
            {
                return;
            }
            if (found == state.devices.end())
            {
                if (state.devices.size() >= 64)
                {
                    frame.m_boundary.historyGap = true;
                    CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                    return;
                }
                state.devices.push_back({ record.device, record.deviceEpoch, record.assignmentEpoch,
                    record.kind == RecordKind::DeviceAssigned });
            }
            else
            {
                const bool changed = found->epoch != record.deviceEpoch ||
                    found->assignmentEpoch != record.assignmentEpoch ||
                    found->assigned != (record.kind == RecordKind::DeviceAssigned);
                if (changed)
                {
                    cancelDevice(record.device, record.reason == CancelReason::None ?
                        (record.kind == RecordKind::DeviceDisconnected ? CancelReason::DeviceDisconnected :
                            CancelReason::DeviceReassigned) : record.reason);
                }
                *found = { record.device, record.deviceEpoch, record.assignmentEpoch,
                    record.kind == RecordKind::DeviceAssigned };
            }
            return;
        }
        case RecordKind::Control:
        case RecordKind::Resync:
            break;
        }

        auto device = std::find_if(state.devices.begin(), state.devices.end(),
            [&](const DeviceState& item) { return item.device == record.device; });
        if (device != state.devices.end())
        {
            if (record.deviceEpoch < device->epoch || record.assignmentEpoch < device->assignmentEpoch ||
                (!device->assigned && record.assignmentEpoch <= device->assignmentEpoch))
            {
                return;
            }
            if (record.deviceEpoch != device->epoch || record.assignmentEpoch != device->assignmentEpoch)
            {
                cancelDevice(record.device, CancelReason::DeviceReassigned);
                *device = { record.device, record.deviceEpoch, record.assignmentEpoch, true };
                // A new device incarnation establishes a baseline before accepting edges.
                record.kind = RecordKind::Resync;
            }
        }
        else
        {
            if (state.devices.size() >= 64)
            {
                frame.m_boundary.historyGap = true;
                CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                return;
            }
            state.devices.push_back({ record.device, record.deviceEpoch, record.assignmentEpoch, true });
        }
        auto control = std::lower_bound(state.controls.begin(), state.controls.end(), record, [](const ControlState& item,
            const RoutedInputRecord& key)
        {
            return item.control != key.control ? item.control < key.control : item.device < key.device;
        });
        if (control == state.controls.end() || control->device != record.device || control->control != record.control)
        {
            if (state.controls.size() >= 16'384)
            {
                frame.m_boundary.historyGap = true;
                CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                return;
            }
            control = state.controls.insert(control, { record.device, record.deviceEpoch, record.assignmentEpoch, record.control,
                InputValue::Zero(record.value.type), 0, frame.HasHistoryGap(), record.IsRecipient(domain) });
        }
        control->deviceEpoch = record.deviceEpoch;
        control->assignmentEpoch = record.assignmentEpoch;
        control->sequence = record.sequence;
        control->allowed = record.IsRecipient(domain);
        if (IsDelta(record.control) && (record.kind == RecordKind::Resync || !control->allowed ||
            !state.focused || state.paused || !state.owned))
        {
            control->value = InputValue::Zero(record.value.type);
        }
        else if (IsDelta(record.control) && record.kind == RecordKind::Control)
        {
            control->value.x += record.value.x;
            control->value.y += record.value.y;
        }
        else
        {
            control->value = record.value;
        }
        // Source hysteresis uses the signal's threshold below. Here only exact
        // digital/axis neutral clears a baseline gate; per-binding neutral also
        // accepts the configured release threshold.
        if (Magnitude(control->value) == 0.0f)
        {
            control->requiresNeutral = false;
        }
        else if (record.kind == RecordKind::Resync || !control->allowed || !state.focused || state.paused || !state.owned)
        {
            control->requiresNeutral = true;
        }
        const auto controls = state.program->GetControls();
        const auto lookup = std::lower_bound(controls.begin(), controls.end(), record.control,
            [](const PreparedControlLookup& item, ControlID key) { return item.control < key; });
        if (lookup != controls.end() && lookup->control == record.control)
        {
            const bool selectiveCancel = record.kind == RecordKind::Resync || !record.IsRecipient(domain);
            std::vector<bool> affected(selectiveCancel ? graph.signals.size() : 0);
            for (const auto binding : lookup->bindingIndices)
            {
                state.dirtyBindings[binding] = true;
                if (record.kind == RecordKind::Resync || !record.IsRecipient(domain))
                {
                    affected[state.program->GetBindingSignals()[binding]] = true;
                }
            }
            if (record.kind == RecordKind::Resync || !record.IsRecipient(domain))
            {
                for (const auto slot : state.program->GetEvaluationOrder())
                {
                    const auto& prepared = state.program->GetSignals()[slot];
                    if (prepared.gateIndex != kInvalidSlot && affected[prepared.gateIndex])
                    {
                        affected[slot] = true;
                    }
                    for (const auto dependency : prepared.chordIndices)
                    {
                        affected[slot] = affected[slot] || affected[dependency];
                    }
                    if (affected[slot] && graph.signals[slot].domain == domain)
                    {
                        if (!record.IsRecipient(domain))
                        {
                            CancelSignal(state, frame, slot, CancelReason::UIOwnership, record);
                        }
                        else if (!graph.signals[slot].resumePersistentValue)
                        {
                            CancelSignal(state, frame, slot, record.reason == CancelReason::None ?
                                CancelReason::HistoryGap : record.reason, record);
                        }
                    }
                }
            }
        }
        if (record.kind == RecordKind::Resync)
        {
            // Baselines never manufacture Started/Performed/Completed. A position
            // may be read immediately only through its explicit resume policy.
            EvaluateSignals(domain, state, frame, record);
            return;
        }
        EvaluateSignals(domain, state, frame, record);
    }

    void InputSession::EvaluateSignals(Domain domain, DomainState& state, InputFrame& frame,
        const RoutedInputRecord& record)
    {
        const auto& graph = state.program->GetDefinition();
        const auto preparedSignals = state.program->GetSignals();
        std::fill(state.claimedBindings.begin(), state.claimedBindings.end(), false);
        state.activeClaimBindings.clear();
        const bool enabled = state.focused && !state.paused && state.owned && !frame.HasHistoryGap();
        for (const auto slot : state.program->GetEvaluationOrder())
        {
            const auto& definition = graph.signals[slot];
            if (definition.domain != domain)
            {
                continue;
            }
            const auto& prepared = preparedSignals[slot];
            auto& signal = state.signals[slot];
            const auto& interaction = definition.interaction;
            bool allNeutral = true;
            bool routingBlocked = false;
            bool hasContribution = false;
            auto combined = InputValue::Zero(definition.type);
            std::uint64_t bestSequence = 0;
            std::int32_t bestPriority = (std::numeric_limits<std::int32_t>::min)();
            for (const auto bindingIndex : prepared.bindingIndices)
            {
                const auto& binding = graph.bindings[bindingIndex];
                auto& bindingState = state.bindings[bindingIndex];
                const bool neutral = IsBindingNeutral(state, binding, interaction.releaseThreshold);
                allNeutral = allNeutral && neutral;
                if (neutral)
                {
                    bindingState.requiresNeutral = false;
                }
                bool blocked = false;
                for (const auto claimant : state.activeClaimBindings)
                {
                    const auto owner = state.program->GetBindingSignals()[claimant];
                    if (owner != kInvalidSlot && preparedSignals[owner].layerIndex != prepared.layerIndex &&
                        SharesControl(binding, graph.bindings[claimant]))
                    {
                        blocked = true;
                        break;
                    }
                }
                if (blocked)
                {
                    bindingState.requiresNeutral = true;
                    routingBlocked = true;
                }
                if (state.dirtyBindings[bindingIndex])
                {
                    bindingState.value = ReadBinding(state, binding, definition.type,
                        bindingState.sequence);
                    state.dirtyBindings[bindingIndex] = false;
                    if (!IsFinite(bindingState.value))
                    {
                        frame.m_boundary.historyGap = true;
                        bindingState.value = InputValue::Zero(definition.type);
                        CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                        return;
                    }
                }
                if (blocked || bindingState.requiresNeutral)
                {
                    continue;
                }
                const auto value = bindingState.value;
                const bool contributes = Magnitude(value) > 0.0f;
                switch (definition.combine)
                {
                case CombinePolicy::Sum:
                    combined.x += value.x;
                    combined.y += value.y;
                    break;
                case CombinePolicy::MaximumMagnitude:
                    // Binding arrays are sorted by stable ID; strict greater keeps
                    // the smallest ID on ties, independent of event arrival order.
                    if (Magnitude(value) > Magnitude(combined))
                    {
                        combined = value;
                    }
                    break;
                case CombinePolicy::MostRecent:
                    if (contributes && (!hasContribution || bindingState.sequence > bestSequence))
                    {
                        combined = value;
                        bestSequence = bindingState.sequence;
                    }
                    break;
                case CombinePolicy::Priority:
                    if (contributes && (!hasContribution || binding.priority > bestPriority))
                    {
                        combined = value;
                        bestPriority = binding.priority;
                    }
                    break;
                }
                hasContribution = hasContribution || contributes;
            }

            if (!enabled || !state.layers[prepared.layerIndex] || routingBlocked)
            {
                const auto reason = routingBlocked || !state.layers[prepared.layerIndex] ?
                    CancelReason::LayerBlocked : (!state.focused ? CancelReason::FocusLost :
                        (state.paused ? CancelReason::Paused : CancelReason::UIOwnership));
                if (signal.active || signal.held || signal.chordProgress != 0)
                {
                    CancelSignal(state, frame, slot, reason, record);
                }
                signal.value = InputValue::Zero(definition.type);
                continue;
            }
            const bool gateOpen = prepared.gateIndex == kInvalidSlot || state.signals[prepared.gateIndex].held;
            if (!gateOpen)
            {
                if (signal.active || signal.held)
                {
                    CancelSignal(state, frame, slot, CancelReason::LayerBlocked, record);
                }
                signal.value = InputValue::Zero(definition.type);
                continue;
            }
            if (interaction.kind == InteractionKind::Chord)
            {
                allNeutral = true;
                for (const auto dependency : prepared.chordIndices)
                {
                    allNeutral = allNeutral && !state.signals[dependency].held;
                }
            }
            if (signal.requiresNeutral)
            {
                if (allNeutral)
                {
                    signal.requiresNeutral = false;
                    // Establish analog neutral as a silent baseline. Otherwise a
                    // small residual value would emit Performed on the next empty
                    // frame even though no new device record arrived.
                    signal.value = definition.type == ValueType::Button ? InputValue::Zero(definition.type) :
                        ProcessValue(combined, definition.processors);
                    if (!IsFinite(signal.value))
                    {
                        frame.m_boundary.historyGap = true;
                        CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                        return;
                    }
                }
                else
                {
                    signal.value = InputValue::Zero(definition.type);
                }
                continue;
            }
            if (!IsFinite(combined))
            {
                frame.m_boundary.historyGap = true;
                CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                return;
            }
            combined = ProcessValue(combined, definition.processors);
            if (!IsFinite(combined))
            {
                frame.m_boundary.historyGap = true;
                CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                return;
            }
            const auto previousValue = signal.value;
            const bool wasHeld = signal.held;
            bool performedNow = false;
            bool held = Magnitude(combined) >= (wasHeld ? interaction.releaseThreshold : interaction.pressThreshold);
            if (wasHeld && Magnitude(combined) <= interaction.releaseThreshold)
            {
                held = false;
            }
            const auto time = record.GetTime(domain);
            if (record.kind == RecordKind::Resync)
            {
                if (definition.resumePersistentValue)
                {
                    signal.value = combined;
                    signal.held = held;
                    signal.active = held;
                }
                continue;
            }
            if (interaction.kind == InteractionKind::Chord)
            {
                bool allHeld = true;
                Timestamp firstPress = (std::numeric_limits<Timestamp>::max)();
                Timestamp lastPress = 0;
                for (const auto dependency : prepared.chordIndices)
                {
                    const auto& participant = state.signals[dependency];
                    allHeld = allHeld && participant.held;
                    if (participant.held)
                    {
                        firstPress = (std::min)(firstPress, participant.lastPressTime);
                        lastPress = (std::max)(lastPress, participant.lastPressTime);
                    }
                }
                if (interaction.chordOrder == ChordOrder::Sequential)
                {
                    for (std::size_t position = 0; position < prepared.chordIndices.size(); ++position)
                    {
                        const auto& participant = state.signals[prepared.chordIndices[position]];
                        if (!participant.held || participant.lastPressSequence != record.sequence ||
                            participant.lastPressSequence <= signal.chordObservedSequence)
                        {
                            continue;
                        }
                        signal.chordObservedSequence = participant.lastPressSequence;
                        if (position == signal.chordProgress)
                        {
                            if (position == 0)
                            {
                                signal.started = time;
                                signal.deadline = AddTime(time, interaction.duration);
                                signal.origin = record;
                            }
                            ++signal.chordProgress;
                        }
                        else
                        {
                            signal.chordProgress = position == 0 ? 1 : 0;
                            signal.started = time;
                            signal.deadline = AddTime(time, interaction.duration);
                        }
                    }
                    held = allHeld && signal.chordProgress == prepared.chordIndices.size() &&
                        time <= signal.deadline;
                }
                else
                {
                    held = allHeld && lastPress - firstPress <= interaction.duration;
                    if (!signal.active && firstPress != (std::numeric_limits<Timestamp>::max)())
                    {
                        signal.started = firstPress;
                        signal.deadline = AddTime(firstPress, interaction.duration);
                        signal.origin = record;
                        signal.chordProgress = 1;
                    }
                }
                combined = InputValue::FromButton(held);
            }
            signal.held = held;
            signal.value = definition.type == ValueType::Button ? InputValue::FromButton(held) : combined;
            if (held && !wasHeld)
            {
                signal.active = true;
                signal.performed = false;
                signal.timerExhausted = false;
                signal.started = time;
                signal.deadline = AddTime(time, interaction.duration);
                signal.lastPressTime = time;
                signal.lastPressSequence = record.sequence;
                signal.origin = record;
                Emit(state, frame, slot, EventPhase::Started, record);
                if (interaction.kind == InteractionKind::Press || interaction.kind == InteractionKind::Chord)
                {
                    signal.performed = true;
                    performedNow = true;
                    Emit(state, frame, slot, EventPhase::Performed, record);
                }
            }
            else if (!held && wasHeld)
            {
                if (interaction.kind == InteractionKind::Tap && signal.active && time <= signal.deadline)
                {
                    signal.performed = true;
                    performedNow = true;
                    Emit(state, frame, slot, EventPhase::Performed, record);
                }
                if (signal.active)
                {
                    Emit(state, frame, slot, EventPhase::Completed, record);
                }
                signal.active = false;
                signal.performed = false;
                signal.deadline = 0;
                signal.chordProgress = 0;
                if (interaction.kind == InteractionKind::Chord)
                {
                    signal.requiresNeutral = true;
                }
            }
            else if (definition.type != ValueType::Button && signal.value != previousValue &&
                Magnitude(signal.value) > 0.0f)
            {
                Emit(state, frame, slot, EventPhase::Performed, record);
                performedNow = true;
            }
            const auto claim = graph.layers[prepared.layerIndex].claim;
            if (claim != ClaimPolicy::PassThrough &&
                ((claim == ClaimPolicy::OnPress && (signal.held || signal.chordProgress != 0)) ||
                    (claim == ClaimPolicy::OnPerformed && (signal.performed || performedNow))))
            {
                for (const auto binding : prepared.bindingIndices)
                {
                    if (!state.claimedBindings[binding])
                    {
                        state.claimedBindings[binding] = true;
                        state.activeClaimBindings.push_back(binding);
                    }
                }
                if (interaction.kind == InteractionKind::Chord && interaction.claimChordControls)
                {
                    for (const auto dependency : prepared.chordIndices)
                    {
                        for (const auto binding : preparedSignals[dependency].bindingIndices)
                        {
                            if (!state.claimedBindings[binding])
                            {
                                state.claimedBindings[binding] = true;
                                state.activeClaimBindings.push_back(binding);
                            }
                        }
                    }
                }
            }
        }
    }

    void InputSession::AdvanceTimers(Domain domain, DomainState& state, InputFrame& frame, Timestamp time,
        bool includeBoundary, const RoutedInputRecord& origin)
    {
        if (!state.focused || state.paused || !state.owned || frame.HasHistoryGap())
        {
            return;
        }
        struct Timer final
        {
            Timestamp time{};
            std::uint32_t slot{};
            std::uint32_t order{};
        };
        const auto later = [](const Timer& left, const Timer& right)
        {
            return left.time != right.time ? left.time > right.time : left.order > right.order;
        };
        std::priority_queue<Timer, std::vector<Timer>, decltype(later)> timers(later);
        const auto& graph = state.program->GetDefinition();
        const auto evaluationOrder = state.program->GetEvaluationOrder();
        for (std::uint32_t order = 0; order < evaluationOrder.size(); ++order)
        {
            const auto slot = evaluationOrder[order];
            const auto& signal = state.signals[slot];
            const auto& interaction = graph.signals[slot].interaction;
            if (graph.signals[slot].domain != domain || signal.requiresNeutral ||
                (!signal.active && signal.chordProgress == 0) || interaction.kind == InteractionKind::Press ||
                signal.timerExhausted ||
                (interaction.kind == InteractionKind::Hold && signal.performed && interaction.repeatInterval == 0) ||
                (interaction.kind == InteractionKind::Chord && signal.performed))
            {
                continue;
            }
            timers.push({ signal.deadline, slot, order });
        }
        while (!timers.empty())
        {
            const auto timer = timers.top();
            if (timer.time > time || (!includeBoundary && timer.time == time))
            {
                break;
            }
            timers.pop();
            auto& signal = state.signals[timer.slot];
            const auto& interaction = graph.signals[timer.slot].interaction;
            if (signal.requiresNeutral || signal.deadline != timer.time ||
                (!signal.active && signal.chordProgress == 0))
            {
                continue;
            }
            auto record = signal.origin;
            record.sequence = origin.sequence;
            record.realTime = timer.time;
            record.gameTime = timer.time;
            if (interaction.kind == InteractionKind::Hold && signal.held)
            {
                signal.performed = true;
                Emit(state, frame, timer.slot, EventPhase::Performed, record);
                if (interaction.repeatInterval != 0)
                {
                    const auto next = AddTime(timer.time, interaction.repeatInterval);
                    if (next > timer.time)
                    {
                        signal.deadline = next;
                        timers.push({ next, timer.slot, timer.order });
                    }
                    else
                    {
                        signal.timerExhausted = true;
                    }
                }
            }
            else if (interaction.kind == InteractionKind::Tap || interaction.kind == InteractionKind::Chord)
            {
                CancelSignal(state, frame, timer.slot, CancelReason::InteractionTimeout, record);
            }
            if (frame.HasHistoryGap())
            {
                CancelAll(domain, state, frame, CancelReason::HistoryGap, record);
                break;
            }
            // An OnPerformed claim is visible to lower layers only from this
            // timer forward. It never removes earlier frame events.
            EvaluateSignals(domain, state, frame, record);
        }
    }

    void InputSession::RebuildFrameStates(Domain domain, DomainState& state, InputFrame& frame)
    {
        frame.m_states.clear();
        const auto& graph = state.program->GetDefinition();
        for (std::size_t slot = 0; slot < graph.signals.size(); ++slot)
        {
            if (graph.signals[slot].domain == domain)
            {
                frame.m_states.push_back({ graph.signals[slot].id, state.signals[slot].value,
                    state.signals[slot].held, false, false });
            }
        }
        for (const auto& event : frame.m_events)
        {
            const auto found = std::lower_bound(frame.m_states.begin(), frame.m_states.end(), event.signal,
                [](const InputSignalState& value, SignalID key) { return value.signal < key; });
            if (found != frame.m_states.end() && found->signal == event.signal)
            {
                found->pressed = found->pressed || event.phase == EventPhase::Started;
                found->released = found->released || event.phase == EventPhase::Completed;
            }
        }
    }

    own::shared_owner<const InputFrame> InputSession::Evaluate(Domain domain, InputBoundary boundary,
        std::span<const RoutedInputRecord> records)
    {
        if (!IsOwnerThread() || DomainIndex(domain) >= m_domains.size() || boundary.sequence == 0 ||
            boundary.begin < 0 || boundary.end < boundary.begin || records.size() > 262'144)
        {
            return {};
        }
        auto& state = m_domains[DomainIndex(domain)];
        if (state.hasBoundary && (boundary.sequence <= state.lastBoundary || boundary.begin < state.frontier))
        {
            return {};
        }
        boundary.historyGap = boundary.historyGap || (state.hasBoundary && boundary.begin > state.frontier);
        std::vector<RoutedInputRecord> ordered;
        ordered.reserve(records.size());
        for (auto record : records)
        {
            const bool control = record.kind == RecordKind::Control || record.kind == RecordKind::Resync;
            if (record.user != m_user && (record.user != 0 || control))
            {
                continue;
            }
            if (record.targetSessionId != 0 && (record.targetSessionId != m_handle.id ||
                record.targetSessionGeneration != m_handle.generation))
            {
                continue;
            }
            if ((record.targetSessionId == 0) != (record.targetSessionGeneration == 0))
            {
                return {};
            }
            if (std::binary_search(state.consumedSequences.begin(), state.consumedSequences.end(), record.sequence) ||
                record.GetTime(domain) > boundary.end)
            {
                continue;
            }
            if (record.sequence != 0 && record.sequence <= state.retiredSequence)
            {
                boundary.historyGap = true;
                continue;
            }
            if (record.sequence == 0 ||
                static_cast<unsigned>(record.kind) > static_cast<unsigned>(RecordKind::ClockDiscontinuity) ||
                static_cast<unsigned>(record.recipients) > static_cast<unsigned>(Recipient::Both) ||
                static_cast<unsigned>(record.reason) > static_cast<unsigned>(CancelReason::InteractionTimeout) ||
                ((control || record.kind == RecordKind::DeviceAssigned || record.kind == RecordKind::DeviceDisconnected) &&
                    (record.device == 0 || record.deviceEpoch == 0 || record.assignmentEpoch == 0)))
            {
                return {};
            }
            if (control && !IsFinite(record.value))
            {
                boundary.historyGap = true;
                continue;
            }
            if (control && !HasValidControlType(record))
            {
                return {};
            }
            const auto effective = (std::max)(boundary.begin, record.GetTime(domain));
            record.late = record.late || effective != record.GetTime(domain);
            if (domain == Domain::Game)
            {
                record.gameTime = effective;
            }
            else
            {
                record.realTime = effective;
            }
            ordered.push_back(record);
        }
        std::sort(ordered.begin(), ordered.end(), [domain](const auto& left, const auto& right)
        {
            return left.GetTime(domain) != right.GetTime(domain) ?
                left.GetTime(domain) < right.GetTime(domain) : left.sequence < right.sequence;
        });
        // Reject duplicate sequence identities before touching live state. The same
        // raw record can be read by both domains, but not twice by one domain.
        std::vector<std::uint64_t> sequences;
        sequences.reserve(ordered.size());
        for (const auto& record : ordered)
        {
            sequences.push_back(record.sequence);
        }
        std::sort(sequences.begin(), sequences.end());
        if (std::adjacent_find(sequences.begin(), sequences.end()) != sequences.end())
        {
            return {};
        }

        auto frame = own::make_shared<InputFrame>();
        frame->m_session = m_handle;
        frame->m_user = m_user;
        frame->m_domain = domain;
        frame->m_boundary = boundary;
        RoutedInputRecord origin;
        origin.user = m_user;
        origin.sourceTime = boundary.begin;
        origin.realTime = boundary.begin;
        origin.gameTime = boundary.begin;
        origin.sequence = state.lastSequence;
        for (auto& control : state.controls)
        {
            if (IsDelta(control.control))
            {
                control.value = InputValue::Zero(control.value.type);
            }
        }
        std::fill(state.dirtyBindings.begin(), state.dirtyBindings.end(), true);
        ApplyCommands(domain, state, *frame, origin);
        if (boundary.historyGap)
        {
            CancelAll(domain, state, *frame, CancelReason::HistoryGap, origin);
        }
        EvaluateSignals(domain, state, *frame, origin);
        std::size_t index = 0;
        while (index < ordered.size())
        {
            const auto time = ordered[index].GetTime(domain);
            AdvanceTimers(domain, state, *frame, time, false, origin);
            do
            {
                origin = ordered[index];
                ProcessRecord(domain, state, *frame, origin);
                state.lastSequence = (std::max)(state.lastSequence, origin.sequence);
                ++index;
            } while (index < ordered.size() && ordered[index].GetTime(domain) == time);
            // Input/policy records at a deadline win their sequence order before
            // any timer at that same time (Tap <= threshold, Hold still held).
            AdvanceTimers(domain, state, *frame, time, true, origin);
        }
        AdvanceTimers(domain, state, *frame, boundary.end, true, origin);
        if (frame->HasHistoryGap())
        {
            origin.realTime = boundary.end;
            origin.gameTime = boundary.end;
            CancelAll(domain, state, *frame, CancelReason::HistoryGap, origin);
        }
        frame->m_graph = state.program->GetDefinition().id;
        frame->m_semanticHash = state.program->GetSemanticHash();
        frame->m_interfaceHash = state.program->GetInterfaceHash();
        frame->m_definitionGeneration = state.program->GetDefinition().generation;
        RebuildFrameStates(domain, state, *frame);
        // A later sequence can have an earlier scheduled time. Keep explicit,
        // bounded consumed identities instead of dropping every lower sequence.
        // Ingress retains future records; exhausting this deduplication horizon is
        // surfaced as a gap if such an old record eventually arrives.
        std::vector<std::uint64_t> consumed;
        consumed.reserve(state.consumedSequences.size() + sequences.size());
        std::set_union(state.consumedSequences.begin(), state.consumedSequences.end(), sequences.begin(), sequences.end(),
            std::back_inserter(consumed));
        constexpr std::size_t kConsumedSequenceCapacity = 65'536;
        if (consumed.size() > kConsumedSequenceCapacity)
        {
            const auto expired = consumed.size() - kConsumedSequenceCapacity;
            state.retiredSequence = (std::max)(state.retiredSequence, consumed[expired - 1]);
            consumed.erase(consumed.begin(), consumed.begin() + static_cast<std::ptrdiff_t>(expired));
        }
        state.consumedSequences = std::move(consumed);
        state.frontier = boundary.end;
        state.lastBoundary = boundary.sequence;
        state.hasBoundary = true;
        state.lastFrame = frame;
        return frame;
    }

    bool InputSession::Shutdown(Timestamp gameTime, Timestamp realTime, CancelReason reason)
    {
        if (!IsOwnerThread())
        {
            return false;
        }
        for (std::size_t index = 0; index < m_domains.size(); ++index)
        {
            auto& state = m_domains[index];
            const auto domain = static_cast<Domain>(index);
            auto frame = own::make_shared<InputFrame>();
            frame->m_graph = state.program->GetDefinition().id;
            frame->m_semanticHash = state.program->GetSemanticHash();
            frame->m_interfaceHash = state.program->GetInterfaceHash();
            frame->m_definitionGeneration = state.program->GetDefinition().generation;
            frame->m_session = m_handle;
            frame->m_user = m_user;
            frame->m_domain = domain;
            const auto end = (std::max)(state.frontier, domain == Domain::Game ? gameTime : realTime);
            frame->m_boundary = { state.lastBoundary + 1, state.frontier, end, false };
            RoutedInputRecord record;
            record.sourceTime = end;
            record.realTime = end;
            record.gameTime = end;
            record.sequence = state.lastSequence;
            record.user = m_user;
            CancelAll(domain, state, *frame, reason, record);
            RebuildFrameStates(domain, state, *frame);
            state.lastFrame = frame;
            state.commands.clear();
            state.controls.clear();
            state.devices.clear();
            state.program = {};
        }
        m_program = {};
        ++m_handle.generation;
        m_initialized = false;
        return true;
    }
}
