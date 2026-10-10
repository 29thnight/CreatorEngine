// Source-only contract probe. See InputGraphContractTests.README.md.
// Intentionally uses checks that remain active in Release, not assert().
#include "../../Engine/SceneRuntime/InputSession.h"
#include "../../Engine/SceneRuntime/InputSubscriptions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace Input;

    constexpr GraphID kGraph{ 1, 1 };
    constexpr LayerID kLayer{ 2, 1 };
    constexpr SignalID kSignal{ 3, 1 };
    constexpr BindingID kBinding{ 4, 1 };
    constexpr UserID kUser = 7;
    constexpr DeviceID kDevice = 11;
    constexpr ControlID kKey{ SourceKind::Key, 32 };
    constexpr ControlID kOtherKey{ SourceKind::Key, 33 };
    constexpr ControlID kAxis{ SourceKind::GamepadAxis, 2 };
    constexpr ControlID kDelta{ SourceKind::MouseDelta, 0 };

    const char* g_case = "setup";
    std::size_t g_checks = 0;

    static_assert(!std::is_convertible_v<SignalID, BindingID>);
    static_assert(!std::is_convertible_v<InputSignal<Button>, InputSignal<float>>);
    static_assert(std::is_same_v<decltype(std::declval<const InputFrame&>().GetEvents()),
        std::span<const InputSignalEvent>>);
    static_assert(std::is_same_v<decltype(std::declval<const InputFrame&>().GetStates()),
        std::span<const InputSignalState>>);

    void Check(bool condition, const char* label)
    {
        ++g_checks;
        if (!condition)
        {
            std::fprintf(stderr, "InputGraph FAIL [%s] %s\n", g_case, label);
            std::exit(EXIT_FAILURE);
        }
    }

    bool Near(float left, float right)
    {
        return std::abs(left - right) <= 0.00001f;
    }

    InputSource Source(ControlID control)
    {
        InputSource source;
        source.control = control;
        if (control.kind == SourceKind::MouseDelta)
        {
            source.space = CoordinateSpace::RelativeCounts;
        }
        else if (control.kind == SourceKind::PointerPosition)
        {
            source.space = CoordinateSpace::ClientPixels;
        }
        else if (control.kind == SourceKind::GamepadAxis)
        {
            source.space = CoordinateSpace::Normalized;
        }
        return source;
    }

    void AddOutput(InputGraph& graph, SignalID signalID, BindingID bindingID,
        LayerID layerID, ControlID control, ValueType type = ValueType::Button,
        Domain domain = Domain::Game)
    {
        InputSignalDefinition signal;
        signal.id = signalID;
        signal.layer = layerID;
        signal.name = "Output";
        signal.type = type;
        signal.domain = domain;
        if (control.kind == SourceKind::MouseDelta || control.kind == SourceKind::MouseWheel)
        {
            signal.lifetime = ValueLifetime::Delta;
        }
        graph.signals.push_back(signal);
        InputBinding binding;
        binding.id = bindingID;
        binding.signal = signalID;
        binding.sources.push_back(Source(control));
        graph.bindings.push_back(binding);
    }

    InputGraph Graph(ValueType type = ValueType::Button, ControlID control = kKey)
    {
        InputGraph graph;
        graph.id = kGraph;
        graph.layers.push_back({ kLayer, "Gameplay", 0, ClaimPolicy::PassThrough, true });
        AddOutput(graph, kSignal, kBinding, kLayer, control, type);
        return graph;
    }

    own::shared_owner<const InputGraphProgram> Compile(const InputGraph& graph)
    {
        std::vector<InputDiagnostic> diagnostics;
        auto program = CompileInputGraph(graph, diagnostics);
        if (!program)
        {
            for (const auto& diagnostic : diagnostics)
            {
                std::fprintf(stderr, "Compile diagnostic: %s\n", diagnostic.message.c_str());
            }
        }
        Check(static_cast<bool>(program), "valid graph compiles");
        Check(diagnostics.empty(), "successful compilation has no error diagnostics");
        return program;
    }

    void Reject(const InputGraph& graph, const char* label)
    {
        std::vector<InputDiagnostic> diagnostics;
        Check(!CompileInputGraph(graph, diagnostics), label);
        Check(!diagnostics.empty(), "invalid graph explains rejection");
    }

    template<class T>
    InputSignal<T> Key(const InputGraphProgram& program, SignalID signal = kSignal)
    {
        return { program.GetDefinition().id, signal, program.GetInterfaceHash(),
            kInputSchemaVersion, kInputABIVersion };
    }

    RoutedInputRecord Control(Timestamp time, std::uint64_t sequence, InputValue value,
        ControlID control = kKey, UserID user = kUser)
    {
        RoutedInputRecord record;
        record.kind = RecordKind::Control;
        record.sourceTime = time;
        record.receivedTime = time;
        record.realTime = time;
        record.gameTime = time;
        record.sequence = sequence;
        record.routingEpoch = 1;
        record.deviceEpoch = 1;
        record.assignmentEpoch = 1;
        record.user = user;
        record.device = kDevice;
        record.control = control;
        record.value = value;
        record.recipients = Recipient::Both;
        return record;
    }

    RoutedInputRecord Edge(Timestamp time, std::uint64_t sequence, bool held,
        ControlID control = kKey, UserID user = kUser)
    {
        return Control(time, sequence, InputValue::FromButton(held), control, user);
    }

    RoutedInputRecord Policy(RecordKind kind, Timestamp time, std::uint64_t sequence)
    {
        auto record = Edge(time, sequence, false);
        record.kind = kind;
        return record;
    }

    struct Fixture final
    {
        own::shared_owner<const InputGraphProgram> program;
        InputSession session;

        explicit Fixture(const InputGraph& graph, UserID user = kUser,
            InputSessionHandle handle = { 21, 1 }) : program(Compile(graph))
        {
            Check(session.Initialize(handle, user, program), "session initializes");
        }

        Fixture(own::shared_owner<const InputGraphProgram> sharedProgram, UserID user,
            InputSessionHandle handle) : program(std::move(sharedProgram))
        {
            Check(session.Initialize(handle, user, program), "session initializes with shared definition");
        }

        own::shared_owner<const InputFrame> Frame(std::uint64_t sequence,
            Timestamp begin, Timestamp end, std::initializer_list<RoutedInputRecord> records = {},
            Domain domain = Domain::Game, bool gap = false)
        {
            auto frame = session.Evaluate(domain, { sequence, begin, end, gap },
                std::span<const RoutedInputRecord>(records.begin(), records.size()));
            Check(static_cast<bool>(frame), "valid boundary seals a frame");
            Check(frame->GetBoundary().sequence == sequence && frame->GetBoundary().begin == begin &&
                frame->GetBoundary().end == end && frame->GetDomain() == domain,
                "sealed frame identifies its boundary and domain");
            return frame;
        }
    };

    const InputSignalState& State(const InputFrame& frame, SignalID id = kSignal,
        ValueType type = ValueType::Button)
    {
        const auto* state = frame.FindState(id, type);
        Check(state != nullptr, "expected typed signal state exists");
        return *state;
    }

    std::vector<InputSignalEvent> Events(const InputFrame& frame, SignalID id = kSignal)
    {
        std::vector<InputSignalEvent> result;
        for (const auto& event : frame.GetEvents())
        {
            if (event.signal == id)
            {
                result.push_back(event);
            }
        }
        return result;
    }

    void Phases(const InputFrame& frame, std::initializer_list<EventPhase> expected,
        SignalID id = kSignal)
    {
        const auto events = Events(frame, id);
        Check(events.size() == expected.size(), "exact event count");
        std::size_t index = 0;
        for (const auto phase : expected)
        {
            Check(events[index].phase == phase, "exact event phase order");
            Check(events[index].user == frame.GetUser(), "event is stamped with the session user");
            ++index;
        }
    }

    std::size_t Count(const InputFrame& frame, EventPhase phase, SignalID id = kSignal)
    {
        return static_cast<std::size_t>(std::count_if(frame.GetEvents().begin(), frame.GetEvents().end(),
            [&](const InputSignalEvent& event)
            {
                return event.signal == id && event.phase == phase;
            }));
    }

    void SchemaValidation()
    {
        g_case = "schema validation";
        const auto valid = Graph();
        Compile(valid);
        auto graph = valid;
        graph.id = {};
        Reject(graph, "zero graph ID is rejected");
        graph = valid;
        graph.generation = 0;
        Reject(graph, "zero graph generation is rejected");
        graph = valid;
        ++graph.schemaVersion;
        Reject(graph, "unknown schema version is rejected");
        graph = valid;
        ++graph.compilerVersion;
        Reject(graph, "unknown compiler version is rejected");
        graph = valid;
        ++graph.abiVersion;
        Reject(graph, "unknown ABI version is rejected");
        graph = valid;
        graph.signals[0].id = {};
        Reject(graph, "zero signal ID is rejected");
        graph = valid;
        graph.bindings[0].id = {};
        Reject(graph, "zero binding ID is rejected");
        graph = valid;
        graph.layers[0].id = {};
        Reject(graph, "zero layer ID is rejected");
        graph = valid;
        graph.signals.push_back(graph.signals[0]);
        Reject(graph, "duplicate signal IDs are rejected");
        graph = valid;
        graph.bindings.push_back(graph.bindings[0]);
        Reject(graph, "duplicate binding IDs are rejected");
        graph = valid;
        graph.layers.push_back(graph.layers[0]);
        Reject(graph, "duplicate layer IDs are rejected");
        graph = valid;
        graph.signals[0].type = static_cast<ValueType>(255);
        Reject(graph, "unknown output type is rejected instead of becoming Button");
        graph = valid;
        graph.signals[0].layer = { 2, 99 };
        Reject(graph, "unknown signal layer is rejected");
        graph = valid;
        graph.bindings[0].signal = { 3, 99 };
        Reject(graph, "unknown binding output is rejected");
        graph = valid;
        graph.bindings.clear();
        Reject(graph, "unbound ordinary output is rejected");
        graph = valid;
        graph.signals[0].resumePersistentValue = true;
        Reject(graph, "Button cannot opt into resumed persistent values");
        graph = valid;
        graph.signals[0].interaction.pressThreshold = std::numeric_limits<float>::quiet_NaN();
        Reject(graph, "nonfinite threshold is rejected");
        graph = valid;
        graph.signals[0].interaction.releaseThreshold = graph.signals[0].interaction.pressThreshold;
        Reject(graph, "inverted hysteresis bounds are rejected");
        graph = valid;
        graph.signals[0].interaction.duration = -1;
        Reject(graph, "negative interaction duration is rejected");
        graph = valid;
        graph.bindings[0].sources[0].scaleX = std::numeric_limits<float>::infinity();
        Reject(graph, "nonfinite source scale is rejected");
        graph = valid;
        graph.bindings[0].sources[0].control.kind = static_cast<SourceKind>(255);
        Reject(graph, "unknown source capability is rejected");
        graph = Graph(ValueType::Float, kDelta);
        Reject(graph, "Vector2 source cannot silently narrow to Float");
        graph = Graph(ValueType::Vector2, kDelta);
        graph.signals[0].lifetime = ValueLifetime::Persistent;
        Reject(graph, "mouse delta cannot masquerade as a persistent value");
        graph = Graph(ValueType::Vector2, kDelta);
        graph.bindings[0].sources[0].space = CoordinateSpace::ClientPixels;
        Reject(graph, "mouse delta requires its explicit coordinate space");
        graph = valid;
        graph.bindings[0].sources.resize(65, Source(kKey));
        Reject(graph, "source count is bounded before preparation");
        graph = valid;
        graph.bindings[0].processors.resize(65);
        Reject(graph, "processor count is bounded before preparation");
        graph = valid;
        graph.signals[0].processors.push_back({ static_cast<ProcessorKind>(255), 1.0f, 1.0f });
        Reject(graph, "unknown post-combine processor is rejected");
        graph = valid;
        graph.signals[0].processors.push_back({ ProcessorKind::Clamp, 1.0f, -1.0f });
        Reject(graph, "post-combine processor bounds are validated too");

        for (const auto processor : std::array<InputProcessor, 5>{
            InputProcessor{ ProcessorKind::Scale, std::numeric_limits<float>::infinity(), 1.0f },
            InputProcessor{ ProcessorKind::Scale, 1.0f, std::numeric_limits<float>::quiet_NaN() },
            InputProcessor{ ProcessorKind::Deadzone, -0.1f, 1.0f },
            InputProcessor{ ProcessorKind::Deadzone, 0.5f, 0.5f },
            InputProcessor{ ProcessorKind::Clamp, 2.0f, -2.0f } })
        {
            graph = valid;
            graph.bindings[0].processors.push_back(processor);
            Reject(graph, "invalid processor finite parameters or bounds are rejected");
        }

        graph = valid;
        AddOutput(graph, { 3, 2 }, { 4, 2 }, kLayer, kOtherKey);
        graph.signals[0].gate = graph.signals[1].id;
        graph.signals[1].gate = graph.signals[0].id;
        Reject(graph, "dependency cycle is rejected before evaluation");
        graph.signals[1].gate = {};
        const auto dag = Compile(graph);
        const auto order = dag->GetEvaluationOrder();
        Check(order.size() == 2 && dag->GetDefinition().signals[order[0]].id == SignalID{ 3, 2 },
            "valid DAG evaluates dependency before dependent despite stable-ID order");
        graph.signals[1].domain = Domain::UI;
        Reject(graph, "cross-domain stateful dependency is rejected");
        graph.signals[1].domain = Domain::Game;
        graph.layers.push_back({ { 2, 2 }, "Other", 1, ClaimPolicy::PassThrough, true });
        graph.signals[1].layer = { 2, 2 };
        Reject(graph, "cross-layer stateful dependency is rejected");
    }

    void HashesAndOverrides()
    {
        g_case = "stable interface and semantic hashes";
        auto graph = Graph();
        AddOutput(graph, { 3, 2 }, { 4, 2 }, kLayer, kOtherKey);
        const auto original = Compile(graph);
        graph.signals[0].name = "Renamed";
        graph.layers[0].name = "DisplayOnly";
        graph.generation = 2;
        std::reverse(graph.signals.begin(), graph.signals.end());
        std::reverse(graph.bindings.begin(), graph.bindings.end());
        const auto display = Compile(graph);
        Check(display->GetSemanticHash() == original->GetSemanticHash(),
            "names, publication generation, and authoring order do not change execution semantics");
        Check(display->GetInterfaceHash() == original->GetInterfaceHash(),
            "display edits preserve stable generated identity");
        InputBindingOverride override;
        override.graph = kGraph;
        override.binding = kBinding;
        override.sources = { Source({ SourceKind::Key, 34 }) };
        std::vector<InputDiagnostic> diagnostics;
        const auto rebound = ApplyInputOverrides(*original, std::span(&override, 1), diagnostics);
        Check(rebound && diagnostics.empty(), "valid rebind compiles atomically");
        Check(rebound->GetInterfaceHash() == original->GetInterfaceHash(),
            "rebind does not invalidate the generated typed accessor");
        Check(rebound->GetSemanticHash() != original->GetSemanticHash(),
            "rebind changes execution semantics");
        Check(rebound->GetDefinition().generation == original->GetDefinition().generation + 1,
            "rebind advances definition generation");
        Check(original->GetDefinition().bindings[0].sources[0].control == kKey,
            "rebind leaves shared source definition untouched");
        override.binding = { 4, 99 };
        Check(!ApplyInputOverrides(*original, std::span(&override, 1), diagnostics) && !diagnostics.empty(),
            "orphan override fails with diagnostics");
        override.binding = kBinding;
        override.graph = { 1, 99 };
        Check(!ApplyInputOverrides(*original, std::span(&override, 1), diagnostics),
            "foreign graph override fails");
        override.graph = kGraph;
        override.sources.clear();
        Check(!ApplyInputOverrides(*original, std::span(&override, 1), diagnostics),
            "empty replacement binding fails");
        override.sources = { Source(kOtherKey) };
        const std::array duplicates{ override, override };
        Check(!ApplyInputOverrides(*original, duplicates, diagnostics), "duplicate override targets fail");
        graph = original->GetDefinition();
        graph.bindings[0].processors.push_back({ ProcessorKind::Scale, 2.0f, 2.0f });
        const auto processed = Compile(graph);
        Check(processed->GetSemanticHash() != original->GetSemanticHash() &&
            processed->GetInterfaceHash() == original->GetInterfaceHash(),
            "processor edit changes semantics but preserves typed interface");
    }

    void SameTickTransitionsAndTypedReads()
    {
        g_case = "same-tick transitions and typed reads";
        Fixture fixture(Graph());
        const auto frame = fixture.Frame(1, 0, 100,
            { Edge(10, 1, true), Edge(20, 2, false), Edge(30, 3, true), Edge(40, 4, false) });
        Phases(*frame, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed,
            EventPhase::Started, EventPhase::Performed, EventPhase::Completed });
        const auto& state = State(*frame);
        Check(!state.held && state.pressed && state.released && state.value == InputValue::FromButton(false),
            "both press/release flags survive two taps while final held is false");
        Check(Count(*frame, EventPhase::Performed) == 2, "two presses produce two performed events");
        const std::array<std::uint64_t, 6> sequences{ 1, 1, 2, 3, 3, 4 };
        const std::array<Timestamp, 6> times{ 10, 10, 20, 30, 30, 40 };
        for (std::size_t index = 0; index < frame->GetEvents().size(); ++index)
        {
            const auto& event = frame->GetEvents()[index];
            Check(event.sequence == sequences[index] && event.sourceTime == times[index] &&
                event.effectiveTime == times[index] && !event.late,
                "event provenance preserves record order and timestamps");
            SignalEvent<Button> typed;
            Check(frame->TryReadEvent(index, Key<Button>(*fixture.program), typed) &&
                typed.phase == event.phase && typed.sequence == event.sequence,
                "typed event view exactly matches the sealed event stream");
        }
        Button value{ true };
        Check(frame->TryRead(Key<Button>(*fixture.program), value) && !value.held,
            "typed Button read succeeds");
        Check(frame->TryRead(Key<Button>(*fixture.program), value) && !value.held &&
            frame->GetEvents().size() == 6, "repeated reads do not consume state or events");
        float wrongValue = 9.0f;
        Check(!frame->TryRead(Key<float>(*fixture.program), wrongValue) && wrongValue == 0.0f,
            "incorrect typed key fails closed and zeroes output");
        auto staleKey = Key<Button>(*fixture.program);
        ++staleKey.interfaceHash;
        Check(!frame->TryRead(staleKey, value), "stale interface key is rejected");
        auto foreignKey = Key<Button>(*fixture.program);
        foreignKey.graph = { 1, 99 };
        Check(!frame->TryRead(foreignKey, value), "foreign graph key is rejected");
        auto badABI = Key<Button>(*fixture.program);
        ++badABI.abiVersion;
        Check(!frame->TryRead(badABI, value), "unknown generated ABI key is rejected");
        SignalEvent<Button> outOfRange;
        Check(!frame->TryReadEvent(6, Key<Button>(*fixture.program), outOfRange),
            "typed event index cannot escape the frame");
        const auto idle = fixture.Frame(2, 100, 200);
        Phases(*idle, {});
        Check(!State(*idle).pressed && !State(*idle).released, "edge flags reset at next boundary");
        Check(frame->GetEvents().size() == 6 && State(*frame).pressed && State(*frame).released,
            "later evaluation leaves retained frame untouched");
        const auto held = fixture.Frame(3, 200, 300, { Edge(210, 5, true), Edge(220, 6, true) });
        Phases(*held, { EventPhase::Started, EventPhase::Performed });
        Check(State(*held).held, "OS repeat does not create a second press");
    }

    void DeltaAndProcessorOrder()
    {
        g_case = "one-tick delta and processor order";
        Fixture delta(Graph(ValueType::Vector2, kDelta));
        const auto first = delta.Frame(1, 0, 100,
            { Control(20, 1, InputValue::FromVector2({ 2.0f, -1.0f }), kDelta),
                Control(30, 2, InputValue::FromVector2({ 3.0f, 4.0f }), kDelta) });
        const auto& firstState = State(*first, kSignal, ValueType::Vector2);
        Check(firstState.value == InputValue::FromVector2({ 5.0f, 3.0f }),
            "delta accumulates all contributions in its own tick");
        const auto second = delta.Frame(2, 100, 200);
        const auto third = delta.Frame(3, 200, 300);
        Check(State(*second, kSignal, ValueType::Vector2).value == InputValue::Zero(ValueType::Vector2) &&
            State(*third, kSignal, ValueType::Vector2).value == InputValue::Zero(ValueType::Vector2),
            "catch-up ticks do not reuse a previous delta");
        Check(Count(*second, EventPhase::Performed) == 0 && Count(*third, EventPhase::Performed) == 0,
            "clearing delta does not replay performed events");
        Check(State(*first, kSignal, ValueType::Vector2).value == InputValue::FromVector2({ 5.0f, 3.0f }),
            "resetting live accumulators cannot mutate a retained delta frame");

        auto graph = Graph(ValueType::Float, kAxis);
        graph.bindings[0].processors = { { ProcessorKind::Scale, 2.0f, 1.0f },
            { ProcessorKind::Clamp, 0.0f, 1.0f } };
        Fixture scaleThenClamp(graph);
        std::reverse(graph.bindings[0].processors.begin(), graph.bindings[0].processors.end());
        Fixture clampThenScale(graph);
        const auto a = scaleThenClamp.Frame(1, 0, 100,
            { Control(10, 1, InputValue::FromFloat(0.75f), kAxis) });
        const auto b = clampThenScale.Frame(1, 0, 100,
            { Control(10, 1, InputValue::FromFloat(0.75f), kAxis) });
        Check(Near(State(*a, kSignal, ValueType::Float).value.x, 1.0f) &&
            Near(State(*b, kSignal, ValueType::Float).value.x, 1.5f),
            "processor order is observable and not silently reordered");
    }

    void ThresholdsAndLateRecords()
    {
        g_case = "Hold/Tap thresholds and late record clamping";
        auto graph = Graph();
        graph.signals[0].interaction.kind = InteractionKind::Hold;
        graph.signals[0].interaction.duration = 20;
        Fixture exactHold(graph);
        const auto releasedAtDeadline = exactHold.Frame(1, 0, 100,
            { Edge(10, 1, true), Edge(30, 2, false) });
        Phases(*releasedAtDeadline, { EventPhase::Started, EventPhase::Completed });
        Check(Count(*releasedAtDeadline, EventPhase::Performed) == 0,
            "equal-time release precedes Hold timer and prevents success");
        Fixture successfulHold(graph);
        const auto heldThroughDeadline = successfulHold.Frame(1, 0, 100,
            { Edge(10, 1, true), Edge(31, 2, false) });
        Phases(*heldThroughDeadline, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed });
        Check(Events(*heldThroughDeadline)[1].effectiveTime == 30,
            "Hold succeeds at its timer deadline, not at the tick end");
        Fixture lateHold(graph);
        const auto sealed = lateHold.Frame(1, 0, 100);
        const auto lateStarted = lateHold.Frame(2, 100, 110, { Edge(10, 1, true) });
        Phases(*lateStarted, { EventPhase::Started });
        Check(Events(*lateStarted)[0].effectiveTime == 100 && Events(*lateStarted)[0].sourceTime == 10 &&
            Events(*lateStarted)[0].late, "late press preserves source provenance but starts at the frontier");
        const auto latePerformed = lateHold.Frame(3, 110, 120);
        Phases(*latePerformed, { EventPhase::Performed });
        Check(Events(*latePerformed)[0].effectiveTime == 120,
            "late Hold duration begins at clamped time instead of original source time");
        Phases(*sealed, {});

        graph.signals[0].interaction.kind = InteractionKind::Tap;
        Fixture exactTap(graph);
        const auto tapped = exactTap.Frame(1, 0, 100, { Edge(10, 1, true), Edge(30, 2, false) });
        Phases(*tapped, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed });
        Check(Events(*tapped)[1].effectiveTime == 30, "Tap accepts normal release at threshold");
        Fixture expiredTap(graph);
        const auto expired = expiredTap.Frame(1, 0, 100, { Edge(10, 1, true), Edge(31, 2, false) });
        Check(Count(*expired, EventPhase::Performed) == 0, "Tap cannot succeed after its threshold");
        Fixture lateTap(graph);
        lateTap.Frame(1, 0, 100);
        const auto compressed = lateTap.Frame(2, 100, 110,
            { Edge(10, 1, true), Edge(90, 2, false) });
        Phases(*compressed, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed });
        const auto compressedEvents = Events(*compressed);
        Check(compressedEvents[0].effectiveTime == 100 && compressedEvents[1].effectiveTime == 100 &&
            compressedEvents[2].effectiveTime == 100 && compressedEvents[0].late && compressedEvents[1].late &&
            compressedEvents[0].sequence == 1 && compressedEvents[1].sequence == 2,
            "late press/release clamp to zero elapsed time while preserving ingress order");
    }

    void FocusCancellationAndNeutralGate()
    {
        g_case = "focus cancellation and neutral rearm";
        auto graph = Graph();
        graph.signals[0].interaction.kind = InteractionKind::Tap;
        graph.signals[0].interaction.duration = 100;
        Fixture fixture(graph);
        const auto focused = fixture.Frame(1, 0, 20, { Edge(10, 1, true) });
        Phases(*focused, { EventPhase::Started });
        const auto lost = fixture.Frame(2, 20, 40,
            { Policy(RecordKind::FocusLost, 25, 2), Edge(30, 3, false) });
        Phases(*lost, { EventPhase::Canceled });
        Check(Events(*lost)[0].reason == CancelReason::FocusLost && !State(*lost).held &&
            !State(*lost).released, "focus loss is cancellation, never normal release or Tap success");
        fixture.Frame(3, 40, 60, { Edge(45, 4, true) });
        const auto restored = fixture.Frame(4, 60, 80,
            { Policy(RecordKind::FocusGained, 65, 5), Edge(70, 6, true) });
        Phases(*restored, {});
        Check(!State(*restored).held, "held key remains gated after focus returns");
        const auto neutral = fixture.Frame(5, 80, 100, { Edge(90, 7, false) });
        Phases(*neutral, {});
        const auto newTap = fixture.Frame(6, 100, 140, { Edge(110, 8, true), Edge(120, 9, false) });
        Phases(*newTap, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed });
        Check(State(*focused).held && Events(*focused).size() == 1,
            "focus cancellation does not rewrite the earlier held frame");
    }

    void BoundaryOrderAndFutureRecords()
    {
        g_case = "boundary, equal-time order, and future records";
        Fixture fixture(Graph());
        const auto future = Edge(101, 3, true);
        const auto first = fixture.Frame(1, 0, 100,
            { future, Edge(100, 2, false), Edge(0, 1, true) });
        Phases(*first, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed });
        Check(Events(*first)[0].effectiveTime == 0 && Events(*first)[2].effectiveTime == 100 &&
            !State(*first).held, "initial start and exact end belong to the first boundary");
        // The ingress/cursor owner retains future records; this evaluator must not
        // consume their sequence identities before their domain interval arrives.
        const auto next = fixture.Frame(2, 100, 110, { future });
        Phases(*next, { EventPhase::Started, EventPhase::Performed });
        Check(Events(*next)[0].sequence == 3 && Events(*next)[0].effectiveTime == 101,
            "future record is accepted only in its eventual interval");
        const auto repeatedRaw = fixture.Frame(3, 110, 120, { future });
        Phases(*repeatedRaw, {});
        Check(State(*repeatedRaw).held, "re-reading a consumed ingress record cannot emit another press");

        Fixture tied(Graph());
        const auto sameTime = tied.Frame(1, 0, 100,
            { Edge(20, 4, false), Edge(20, 2, false), Edge(20, 3, true), Edge(20, 1, true) });
        Phases(*sameTime, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed,
            EventPhase::Started, EventPhase::Performed, EventPhase::Completed });
        const auto events = Events(*sameTime);
        Check(events[0].sequence == 1 && events[2].sequence == 2 &&
            events[3].sequence == 3 && events[5].sequence == 4,
            "global sequence orders equal-time records regardless of batch container order");

        Fixture reordered(Graph());
        const auto earlierReceivedFuture = Edge(150, 1, true);
        const auto neutralNow = Edge(10, 2, false);
        const auto earlierTick = reordered.Frame(1, 0, 100, { earlierReceivedFuture, neutralNow });
        Phases(*earlierTick, {});
        const auto futureTick = reordered.Frame(2, 100, 200, { earlierReceivedFuture });
        Phases(*futureTick, { EventPhase::Started, EventPhase::Performed });
        Check(Events(*futureTick)[0].sequence == 1 && Events(*futureTick)[0].effectiveTime == 150,
            "a consumed higher sequence cannot erase a lower-sequence future record");
    }

    void PostCombineProcessorsAndStableTies()
    {
        g_case = "post-combine processors and deterministic binding ties";
        auto graph = Graph(ValueType::Vector2);
        graph.signals[0].combine = CombinePolicy::Sum;
        graph.signals[0].processors = { { ProcessorKind::Normalize, 1.0f, 1.0f } };
        auto vertical = graph.bindings[0];
        vertical.id = { 4, 2 };
        vertical.sources = { Source(kOtherKey) };
        vertical.sources[0].scaleX = 0.0f;
        vertical.sources[0].scaleY = 1.0f;
        graph.bindings.push_back(vertical);
        Fixture normalizeSum(graph);
        const auto normalized = normalizeSum.Frame(1, 0, 100,
            { Edge(10, 1, true), Edge(20, 2, true, kOtherKey) });
        const auto value = State(*normalized, kSignal, ValueType::Vector2).value;
        Check(Near(value.x, 0.70710678f) && Near(value.y, 0.70710678f),
            "output Normalize applies to the complete diagonal sum");
        graph.signals[0].processors.clear();
        for (auto& binding : graph.bindings)
        {
            binding.processors = { { ProcessorKind::Normalize, 1.0f, 1.0f } };
        }
        Fixture normalizeParts(graph);
        const auto independent = normalizeParts.Frame(1, 0, 100,
            { Edge(10, 1, true), Edge(20, 2, true, kOtherKey) });
        Check(State(*independent, kSignal, ValueType::Vector2).value == InputValue::FromVector2({ 1.0f, 1.0f }),
            "per-binding normalization is intentionally distinct from post-combine normalization");

        graph = Graph(ValueType::Float, kAxis);
        auto alternate = graph.bindings[0];
        alternate.id = { 4, 2 };
        alternate.sources = { Source({ SourceKind::GamepadAxis, 3 }) };
        graph.bindings.push_back(alternate);
        std::reverse(graph.bindings.begin(), graph.bindings.end());
        for (const auto policy : { CombinePolicy::MaximumMagnitude, CombinePolicy::Priority })
        {
            graph.signals[0].combine = policy;
            Fixture tie(graph);
            const auto tied = tie.Frame(1, 0, 100,
                { Control(10, 1, InputValue::FromFloat(-0.75f), { SourceKind::GamepadAxis, 3 }),
                    Control(20, 2, InputValue::FromFloat(0.75f), kAxis) });
            Check(Near(State(*tied, kSignal, ValueType::Float).value.x, 0.75f),
                "equal magnitude/priority chooses the lower stable binding ID");
        }
        graph.signals[0].combine = CombinePolicy::MostRecent;
        Fixture recent(graph);
        const auto latest = recent.Frame(1, 0, 100,
            { Control(10, 1, InputValue::FromFloat(0.75f), kAxis),
                Control(20, 2, InputValue::FromFloat(-0.75f), { SourceKind::GamepadAxis, 3 }) });
        Check(Near(State(*latest, kSignal, ValueType::Float).value.x, -0.75f),
            "MostRecent selects the actual latest contribution instead of stable-ID priority");
    }

    void ChordOrderingAndExpiry()
    {
        g_case = "Chord ordering, equal-time threshold, and expiry";
        auto graph = Graph();
        AddOutput(graph, { 3, 2 }, { 4, 2 }, kLayer, kOtherKey);
        InputSignalDefinition chord;
        chord.id = { 3, 3 };
        chord.layer = kLayer;
        chord.name = "Chord";
        chord.interaction.kind = InteractionKind::Chord;
        chord.interaction.duration = 20;
        chord.interaction.chord = { kSignal, { 3, 2 } };
        graph.signals.push_back(chord);
        Fixture simultaneous(graph);
        const auto threshold = simultaneous.Frame(1, 0, 50,
            { Edge(10, 1, true), Edge(30, 2, true, kOtherKey), Edge(31, 3, false) });
        Phases(*threshold, { EventPhase::Started, EventPhase::Performed, EventPhase::Completed }, chord.id);
        Check(Events(*threshold, chord.id)[0].effectiveTime == 30,
            "second participant at exact Chord window deadline wins before timeout");
        Fixture expiry(graph);
        const auto partial = expiry.Frame(1, 0, 40, { Edge(10, 1, true) });
        Phases(*partial, { EventPhase::Canceled }, chord.id);
        Check(Events(*partial, chord.id)[0].reason == CancelReason::InteractionTimeout &&
            Events(*partial, chord.id)[0].effectiveTime == 30,
            "partial chord expires once with explicit reason at deadline");
        graph.signals.back().interaction.chordOrder = ChordOrder::Sequential;
        Fixture ordered(graph);
        const auto accepted = ordered.Frame(1, 0, 40,
            { Edge(10, 1, true), Edge(20, 2, true, kOtherKey) });
        Phases(*accepted, { EventPhase::Started, EventPhase::Performed }, chord.id);
        Fixture reversed(graph);
        const auto rejected = reversed.Frame(1, 0, 30,
            { Edge(10, 1, true, kOtherKey), Edge(20, 2, true) });
        Check(Count(*rejected, EventPhase::Performed, chord.id) == 0 && !State(*rejected, chord.id).held,
            "reverse participant order cannot succeed as a sequential chord");
        graph.signals.back().interaction.chord.push_back(kSignal);
        Reject(graph, "duplicate Chord dependency is rejected");
    }

    InputGraph LayerGraph(ClaimPolicy claim)
    {
        auto graph = Graph();
        graph.layers.push_back({ { 2, 2 }, "Modal", 100, claim, true });
        AddOutput(graph, { 3, 2 }, { 4, 2 }, { 2, 2 }, kKey);
        graph.signals[1].interaction.kind = InteractionKind::Hold;
        graph.signals[1].interaction.duration = 20;
        return graph;
    }

    void LayerClaims()
    {
        g_case = "layer claims and nonretroactive performed consumption";
        constexpr SignalID upper{ 3, 2 };
        constexpr LayerID modal{ 2, 2 };
        Fixture onPress(LayerGraph(ClaimPolicy::OnPress));
        const auto candidate = onPress.Frame(1, 0, 15, { Edge(10, 1, true) });
        Phases(*candidate, { EventPhase::Started }, upper);
        Phases(*candidate, {});
        Check(!State(*candidate).held, "upper Hold claims the candidate before it succeeds");
        const auto failed = onPress.Frame(2, 15, 25, { Edge(20, 2, false) });
        Phases(*failed, { EventPhase::Completed }, upper);
        Phases(*failed, {});
        Check(!State(*failed).pressed, "failed upper Hold cannot replay its old press to lower layer");
        Check(onPress.session.QueueLayerChange(modal, false), "queue modal disable");
        const auto lower = onPress.Frame(3, 25, 40, { Edge(30, 3, true) });
        Phases(*lower, { EventPhase::Started, EventPhase::Performed });
        Phases(*lower, {}, upper);

        Fixture onPerformed(LayerGraph(ClaimPolicy::OnPerformed));
        const auto beforeSuccess = onPerformed.Frame(1, 0, 15, { Edge(10, 1, true) });
        Phases(*beforeSuccess, { EventPhase::Started }, upper);
        Phases(*beforeSuccess, { EventPhase::Started, EventPhase::Performed });
        const auto success = onPerformed.Frame(2, 15, 30);
        Phases(*success, { EventPhase::Performed }, upper);
        Phases(*success, { EventPhase::Canceled });
        Check(Events(*success)[0].reason == CancelReason::LayerBlocked &&
            Events(*success)[0].effectiveTime == 30,
            "OnPerformed claim cancels lower signal at the actual Hold deadline");
        Check(Count(*beforeSuccess, EventPhase::Performed) == 1 && State(*beforeSuccess).held,
            "later claim cannot remove lower events already sealed");

        Fixture passThrough(LayerGraph(ClaimPolicy::PassThrough));
        const auto shared = passThrough.Frame(1, 0, 40, { Edge(10, 1, true) });
        Check(Count(*shared, EventPhase::Performed) == 1 &&
            Count(*shared, EventPhase::Performed, upper) == 1 && State(*shared).held,
            "pass-through allows both layers to observe without cancellation");

        auto equalDeadlineGraph = LayerGraph(ClaimPolicy::OnPerformed);
        equalDeadlineGraph.signals[0].interaction.kind = InteractionKind::Hold;
        equalDeadlineGraph.signals[0].interaction.duration = 20;
        Fixture equalDeadline(equalDeadlineGraph);
        const auto timerTie = equalDeadline.Frame(1, 0, 30, { Edge(10, 1, true) });
        Phases(*timerTie, { EventPhase::Started, EventPhase::Performed }, upper);
        Phases(*timerTie, { EventPhase::Started, EventPhase::Canceled });
        Check(Count(*timerTie, EventPhase::Performed) == 0 &&
            Events(*timerTie)[1].reason == CancelReason::LayerBlocked &&
            Events(*timerTie)[1].effectiveTime == 30,
            "higher-priority Hold claims a shared deadline before lower Hold can perform");
        Check(timerTie->GetEvents().size() == 4 &&
            timerTie->GetEvents()[2].signal == upper && timerTie->GetEvents()[2].phase == EventPhase::Performed &&
            timerTie->GetEvents()[3].signal == kSignal && timerTie->GetEvents()[3].phase == EventPhase::Canceled,
            "equal timer deadlines follow compiled layer order, not lower numeric signal slot");
    }

    void ExplicitPersistentResume()
    {
        g_case = "explicit persistent axis resume and resync";
        auto graph = Graph(ValueType::Float, kAxis);
        graph.signals[0].resumePersistentValue = true;
        Fixture resumed(graph);
        resumed.Frame(1, 0, 20, { Control(10, 1, InputValue::FromFloat(0.8f), kAxis) });
        auto snapshot = Control(40, 4, InputValue::FromFloat(0.9f), kAxis);
        snapshot.kind = RecordKind::Resync;
        const auto restored = resumed.Frame(2, 20, 50,
            { Policy(RecordKind::FocusLost, 25, 2), Policy(RecordKind::FocusGained, 35, 3), snapshot });
        Phases(*restored, { EventPhase::Canceled });
        Check(Events(*restored)[0].reason == CancelReason::FocusLost &&
            Near(State(*restored, kSignal, ValueType::Float).value.x, 0.9f),
            "opted-in persistent snapshot restores value without invented action edges");
        Check(!State(*restored, kSignal, ValueType::Float).pressed &&
            !State(*restored, kSignal, ValueType::Float).released,
            "resumed snapshot does not manufacture press or release flags");

        graph.signals[0].resumePersistentValue = false;
        Fixture gated(graph);
        snapshot.sequence = 1;
        snapshot.sourceTime = snapshot.realTime = snapshot.gameTime = 10;
        const auto baseline = gated.Frame(1, 0, 20, { snapshot });
        Phases(*baseline, {});
        Check(State(*baseline, kSignal, ValueType::Float).value == InputValue::Zero(ValueType::Float),
            "persistent axes default to neutral-gated zero after resync");
        const auto neutral = gated.Frame(2, 20, 40,
            { Control(30, 2, InputValue::FromFloat(0.1f), kAxis) });
        Phases(*neutral, {});
        const auto rearmed = gated.Frame(3, 40, 60,
            { Control(50, 3, InputValue::FromFloat(0.8f), kAxis) });
        const auto rearmedEvents = Events(*rearmed);
        Check(Count(*rearmed, EventPhase::Started) == 1 && !rearmedEvents.empty() &&
            rearmedEvents.back().phase == EventPhase::Performed && rearmedEvents.back().effectiveTime == 50 &&
            Near(State(*rearmed, kSignal, ValueType::Float).value.x, 0.8f),
            "analog release-threshold neutral rearms the next actual input");
    }

    void SessionIsolation()
    {
        g_case = "two sessions share definition but isolate mutable state";
        auto graph = Graph();
        graph.signals[0].interaction.kind = InteractionKind::Hold;
        graph.signals[0].interaction.duration = 20;
        const auto shared = Compile(graph);
        Fixture first(shared, kUser, { 21, 1 });
        Fixture second(shared, 8, { 22, 1 });
        const auto a = first.Frame(1, 0, 20, { Edge(10, 1, true), Edge(12, 2, true, kKey, 8) });
        const auto b = second.Frame(1, 0, 20, { Edge(10, 1, true), Edge(12, 2, true, kKey, 8) });
        Phases(*a, { EventPhase::Started });
        Phases(*b, { EventPhase::Started });
        Check(Events(*a)[0].effectiveTime == 10 && Events(*b)[0].effectiveTime == 12 &&
            a->GetSession() != b->GetSession(), "each session consumes only its own routed user records");
        Check(first.session.QueueLayerChange(kLayer, false), "disable only the first session layer");
        const auto blocked = first.Frame(2, 20, 40);
        const auto continued = second.Frame(2, 20, 40);
        Phases(*blocked, { EventPhase::Canceled });
        Phases(*continued, { EventPhase::Performed });
        Check(Events(*continued)[0].effectiveTime == 32 && State(*continued).held,
            "other session Hold timer and layer state remain independent");
        InputBindingOverride override;
        override.graph = kGraph;
        override.binding = kBinding;
        override.sources = { Source(kOtherKey) };
        std::vector<InputDiagnostic> diagnostics;
        Check(first.session.QueueRebind(std::span(&override, 1), diagnostics), "queue only first session rebind");
        first.Frame(3, 40, 60);
        const auto untouched = second.Frame(3, 40, 60);
        Check(second.session.GetProgram()->GetSemanticHash() == shared->GetSemanticHash() &&
            first.session.GetProgram()->GetSemanticHash() != shared->GetSemanticHash() && State(*untouched).held,
            "session overrides cannot mutate another session or the shared definition");
    }

    void RoutedHistoryAcrossDomains()
    {
        g_case = "UI-leading routed history and game catch-up";
        auto graph = Graph();
        constexpr SignalID uiSignal{ 3, 2 };
        AddOutput(graph, uiSignal, { 4, 2 }, kLayer, kKey, ValueType::Button, Domain::UI);
        Fixture fixture(graph);
        auto beforeCapture = Edge(10, 1, true);
        beforeCapture.recipients = Recipient::Game;
        beforeCapture.routingEpoch = 1;
        auto capture = Policy(RecordKind::OwnershipChanged, 20, 2);
        // Policy recipients select the target domain; enabled is its new
        // ownership state. Control recipients remain immutable routing results.
        capture.recipients = Recipient::Game;
        capture.enabled = false;
        capture.routingEpoch = 2;
        auto release = Edge(30, 3, false);
        release.recipients = Recipient::UI;
        release.routingEpoch = 2;
        auto uiPress = Edge(40, 4, true);
        uiPress.recipients = Recipient::UI;
        uiPress.routingEpoch = 2;
        const auto uiAhead = fixture.Frame(1, 0, 50,
            { beforeCapture, capture, release, uiPress }, Domain::UI);
        Phases(*uiAhead, { EventPhase::Started, EventPhase::Performed }, uiSignal);
        Check(uiAhead->FindState(kSignal, ValueType::Button) == nullptr,
            "UI frame exposes only UI-domain slots");
        Check(Events(*uiAhead, uiSignal)[0].sequence == 4 &&
            Events(*uiAhead, uiSignal)[0].routingEpoch == 2, "UI uses the explicit routed press provenance");

        const auto gameBeforeCapture = fixture.Frame(1, 0, 15, { beforeCapture });
        Phases(*gameBeforeCapture, { EventPhase::Started, EventPhase::Performed });
        Check(Events(*gameBeforeCapture)[0].routingEpoch == 1 && State(*gameBeforeCapture).held,
            "advanced UI ownership cannot retroactively consume a historical game press");
        const auto gameAfterCapture = fixture.Frame(2, 15, 50, { capture, release, uiPress });
        Phases(*gameAfterCapture, { EventPhase::Canceled });
        Check(Events(*gameAfterCapture)[0].reason == CancelReason::UIOwnership &&
            Events(*gameAfterCapture)[0].effectiveTime == 20 && !State(*gameAfterCapture).held,
            "game cancels at the committed ownership record rather than newest UI cursor");
        Check(Count(*gameBeforeCapture, EventPhase::Performed) == 1 &&
            State(*uiAhead, uiSignal).held, "domain catch-up does not rewrite either retained frame");

        auto handBack = Policy(RecordKind::OwnershipChanged, 60, 5);
        handBack.recipients = Recipient::Game;
        handBack.enabled = true;
        handBack.routingEpoch = 3;
        auto stillHeld = Edge(70, 6, true);
        stillHeld.routingEpoch = 3;
        const auto gated = fixture.Frame(3, 50, 80, { handBack, stillHeld });
        Phases(*gated, {});
        Check(!State(*gated).held, "handoff cannot synthesize a press from a still-held UI key");
        auto neutral = Edge(90, 7, false);
        neutral.routingEpoch = 3;
        auto fresh = Edge(100, 8, true);
        fresh.routingEpoch = 3;
        const auto restored = fixture.Frame(4, 80, 110, { neutral, fresh });
        Phases(*restored, { EventPhase::Started, EventPhase::Performed });
        Check(Events(*restored)[0].sequence == 8, "neutral then new press restores game ownership safely");
    }

    void DeviceEpochsAndResync()
    {
        g_case = "device incarnation, disconnect, and resync";
        Fixture fixture(Graph());
        const auto held = fixture.Frame(1, 0, 20, { Edge(10, 1, true) });
        const auto disconnected = fixture.Frame(2, 20, 40,
            { Policy(RecordKind::DeviceDisconnected, 30, 2) });
        Phases(*disconnected, { EventPhase::Canceled });
        Check(Events(*disconnected)[0].reason == CancelReason::DeviceDisconnected &&
            !State(*disconnected).released, "disconnect cancels instead of completing old action");
        const auto stale = fixture.Frame(3, 40, 60, { Edge(50, 3, true) });
        Phases(*stale, {});
        Check(!State(*stale).held, "old disconnected assignment cannot revive a held action");
        auto assigned = Policy(RecordKind::DeviceAssigned, 70, 4);
        assigned.deviceEpoch = 2;
        assigned.assignmentEpoch = 2;
        auto snapshot = Edge(75, 5, true);
        snapshot.kind = RecordKind::Resync;
        snapshot.deviceEpoch = 2;
        snapshot.assignmentEpoch = 2;
        const auto reconnected = fixture.Frame(4, 60, 80, { assigned, snapshot });
        Phases(*reconnected, {});
        Check(!State(*reconnected).held, "reconnected held baseline waits for neutral");
        auto release = Edge(90, 6, false);
        release.deviceEpoch = release.assignmentEpoch = 2;
        auto press = Edge(100, 7, true);
        press.deviceEpoch = press.assignmentEpoch = 2;
        const auto fresh = fixture.Frame(5, 80, 110, { release, press });
        Phases(*fresh, { EventPhase::Started, EventPhase::Performed });
        Check(Events(*fresh)[0].deviceEpoch == 2 && State(*fresh).held,
            "fresh incarnation emits only newly armed transitions");
        const auto oldRelease = fixture.Frame(6, 110, 130, { Edge(120, 8, false) });
        Phases(*oldRelease, {});
        Check(State(*oldRelease).held, "stale incarnation release cannot modify new-device state");
        Check(State(*held).held && Events(*held)[0].deviceEpoch == 1,
            "new-device incarnation leaves old sealed provenance immutable");
    }

    void PauseAndTargetedDomainCancellation()
    {
        g_case = "game pause and targeted domain cancellation";
        auto graph = Graph();
        graph.signals[0].interaction.kind = InteractionKind::Hold;
        graph.signals[0].interaction.duration = 20;
        constexpr SignalID uiSignal{ 3, 2 };
        AddOutput(graph, uiSignal, { 4, 2 }, kLayer, kKey, ValueType::Button, Domain::UI);
        graph.signals[1].interaction.kind = InteractionKind::Hold;
        graph.signals[1].interaction.duration = 20;
        Fixture fixture(graph);
        auto pause = Policy(RecordKind::Pause, 20, 2);
        pause.recipients = Recipient::Game;
        const auto uiWhilePaused = fixture.Frame(1, 0, 40, { Edge(10, 1, true), pause }, Domain::UI);
        Phases(*uiWhilePaused, { EventPhase::Started, EventPhase::Performed }, uiSignal);
        Check(Events(*uiWhilePaused, uiSignal)[1].effectiveTime == 30,
            "UI Hold advances even before a game tick and ignores Game-only pause");
        const auto paused = fixture.Frame(1, 0, 40, { Edge(10, 1, true), pause });
        Phases(*paused, { EventPhase::Started, EventPhase::Canceled });
        Check(Events(*paused)[1].reason == CancelReason::Paused && Count(*paused, EventPhase::Performed) == 0,
            "game pause cancels unfinished Hold without success");
        const auto duringPause = fixture.Frame(2, 40, 100, { Edge(50, 3, true) });
        Phases(*duringPause, {});
        auto resume = Policy(RecordKind::Resume, 110, 4);
        resume.recipients = Recipient::Game;
        const auto resumed = fixture.Frame(3, 100, 150,
            { resume, Edge(115, 5, true), Edge(120, 6, false), Edge(125, 7, true) });
        Phases(*resumed, { EventPhase::Started, EventPhase::Performed });
        Check(Events(*resumed)[0].effectiveTime == 125 && Events(*resumed)[1].effectiveTime == 145,
            "resume waits for neutral then starts a fresh Hold without paused backlog");
        Check(fixture.session.QueueCancel(Domain::Game, CancelReason::ScriptReload),
            "queue cancellation for one domain");
        const auto gameReload = fixture.Frame(4, 150, 170);
        Phases(*gameReload, { EventPhase::Canceled });
        Check(Events(*gameReload)[0].reason == CancelReason::ScriptReload,
            "targeted cancellation preserves exact requested reason");
        const auto uiContinues = fixture.Frame(2, 40, 170, {}, Domain::UI);
        Phases(*uiContinues, {}, uiSignal);
        Check(State(*uiContinues, uiSignal).held,
            "Game-only cancellation cannot disturb UI state or replay UI Hold success");
    }

    void RepeatDeadlinesAndOverload()
    {
        g_case = "bounded repeat deadlines and explicit history gaps";
        auto graph = Graph();
        graph.signals[0].interaction.kind = InteractionKind::Hold;
        graph.signals[0].interaction.duration = 10;
        graph.signals[0].interaction.repeatInterval = 5;
        Fixture regular(graph);
        const auto repeated = regular.Frame(1, 0, 40, { Edge(10, 1, true), Edge(36, 2, false) });
        Phases(*repeated, { EventPhase::Started, EventPhase::Performed, EventPhase::Performed,
            EventPhase::Performed, EventPhase::Performed, EventPhase::Completed });
        const auto events = Events(*repeated);
        Check(events[1].effectiveTime == 20 && events[2].effectiveTime == 25 &&
            events[3].effectiveTime == 30 && events[4].effectiveTime == 35,
            "repeat deadlines advance by interval inside the tick");

        graph.signals[0].interaction.duration = 1;
        graph.signals[0].interaction.repeatInterval = 1;
        Fixture overload(graph);
        const auto bounded = overload.Frame(1, 0, 100'000, { Edge(1, 1, true) });
        Check(bounded->HasHistoryGap(), "repeat overload advertises history loss");
        Check(bounded->GetEvents().size() <= 65'537 && Count(*bounded, EventPhase::Performed) > 1,
            "repeat catch-up has a finite event budget plus terminal cancellation");
        Check(Count(*bounded, EventPhase::Canceled) == 1 && !State(*bounded).held &&
            bounded->GetEvents().back().phase == EventPhase::Canceled &&
            bounded->GetEvents().back().reason == CancelReason::HistoryGap,
            "overflow cancels the active interaction exactly once and clears held state");
        const auto noBacklog = overload.Frame(2, 100'000, 100'010);
        Phases(*noBacklog, {});
        Check(!State(*noBacklog).held, "overflow cannot dump an old repeat backlog into the next tick");

        const auto maximum = (std::numeric_limits<Timestamp>::max)();
        graph.signals[0].interaction.duration = 20;
        graph.signals[0].interaction.repeatInterval = 0;
        Fixture saturation(graph);
        const auto nearLimit = saturation.Frame(1, maximum - 20, maximum - 1,
            { Edge(maximum - 10, 1, true) });
        Phases(*nearLimit, { EventPhase::Started });
        const auto atLimit = saturation.Frame(2, maximum - 1, maximum);
        Phases(*atLimit, { EventPhase::Performed });
        Check(Events(*atLimit)[0].effectiveTime == maximum,
            "deadline arithmetic saturates instead of wrapping into the past");
        const auto stillAtLimit = saturation.Frame(3, maximum, maximum);
        Phases(*stillAtLimit, {});

        graph.signals[0].interaction.duration = 1;
        graph.signals[0].interaction.repeatInterval = 1;
        Fixture repeatedLimit(graph);
        const auto beforeLastDeadline = repeatedLimit.Frame(1, maximum - 3, maximum - 1,
            { Edge(maximum - 2, 1, true) });
        Phases(*beforeLastDeadline, { EventPhase::Started, EventPhase::Performed });
        Check(Events(*beforeLastDeadline)[1].effectiveTime == maximum - 1,
            "penultimate repeating deadline fires normally");
        const auto lastDeadline = repeatedLimit.Frame(2, maximum - 1, maximum);
        Phases(*lastDeadline, { EventPhase::Performed });
        Check(Events(*lastDeadline)[0].effectiveTime == maximum,
            "a pending saturated deadline is not confused with an exhausted timer");
        const auto exhausted = repeatedLimit.Frame(3, maximum, maximum);
        Phases(*exhausted, {});
    }

    void MalformedBatchesAndGapRecovery()
    {
        g_case = "malformed batches, boundary atomicity, and gap recovery";
        Fixture fixture(Graph());
        const auto held = fixture.Frame(1, 0, 20, { Edge(10, 1, true) });
        const std::array duplicateSequence{ Edge(25, 2, false), Edge(30, 2, true) };
        Check(!fixture.session.Evaluate(Domain::Game, { 2, 20, 40, false }, duplicateSequence),
            "duplicate sequence batch is rejected before live mutation");
        Check(fixture.session.GetLastFrame(Domain::Game)->GetBoundary().sequence == 1 && State(*held).held,
            "rejected batch preserves publication and prior held snapshot");
        const auto validRetry = fixture.Frame(2, 20, 40, { Edge(25, 2, false) });
        Phases(*validRetry, { EventPhase::Completed });
        Check(!fixture.session.Evaluate(Domain::Game, { 2, 20, 40, false }, {}),
            "duplicate evaluation boundary is rejected");
        Check(!fixture.session.Evaluate(Domain::Game, { 3, 30, 50, false }, {}),
            "boundary cannot overlap an already sealed frontier");
        Check(!fixture.session.Evaluate(Domain::Game, { 3, 40, 39, false }, {}),
            "reversed interval is rejected");
        Check(!fixture.session.Evaluate(static_cast<Domain>(255), { 3, 40, 60, false }, {}),
            "invalid domain is rejected without indexing live storage");
        std::array malformedRecords{ Edge(50, 3, true), Edge(50, 3, true), Edge(50, 3, true),
            Edge(50, 3, true), Edge(50, 3, true) };
        malformedRecords[0].deviceEpoch = 0;
        malformedRecords[1].assignmentEpoch = 0;
        malformedRecords[2].recipients = static_cast<Recipient>(255);
        malformedRecords[3].value = InputValue::FromVector2({ 1.0f, 0.0f });
        malformedRecords[4].sequence = 0;
        for (const auto& malformedRecord : malformedRecords)
        {
            Check(!fixture.session.Evaluate(Domain::Game, { 3, 40, 60, false },
                std::span(&malformedRecord, 1)), "malformed routing identity/type fails without consuming boundary");
        }
        const auto pressed = fixture.Frame(3, 40, 60, { Edge(50, 3, true) });
        Phases(*pressed, { EventPhase::Started, EventPhase::Performed });
        auto malformed = Edge(70, 4, true);
        malformed.value.x = std::numeric_limits<float>::quiet_NaN();
        const auto gap = fixture.Frame(4, 60, 80, { malformed });
        Check(gap->HasHistoryGap(), "nonfinite payload becomes an explicit safety gap");
        Phases(*gap, { EventPhase::Canceled });
        Check(Events(*gap)[0].reason == CancelReason::HistoryGap && !State(*gap).held,
            "malformed payload cannot become a successful action or poison state");
        auto snapshot = Edge(90, 5, true);
        snapshot.kind = RecordKind::Resync;
        const auto resynced = fixture.Frame(5, 80, 100, { snapshot });
        Phases(*resynced, {});
        Check(!State(*resynced).held, "held resync snapshot after gap is not a recovered press");
        const auto rearmed = fixture.Frame(6, 100, 140, { Edge(110, 6, false), Edge(120, 7, true) });
        Phases(*rearmed, { EventPhase::Started, EventPhase::Performed });

        auto axisGraph = Graph(ValueType::Float, kAxis);
        axisGraph.bindings[0].processors = {
            { ProcessorKind::Scale, (std::numeric_limits<float>::max)(), 1.0f },
            { ProcessorKind::Scale, 2.0f, 1.0f } };
        Fixture numericOverflow(axisGraph);
        const auto overflow = numericOverflow.Frame(1, 0, 20,
            { Control(10, 1, InputValue::FromFloat(0.75f), kAxis) });
        Check(overflow->HasHistoryGap() &&
            State(*overflow, kSignal, ValueType::Float).value == InputValue::Zero(ValueType::Float),
            "finite input and parameters cannot leak a nonfinite computed value");
        Check(Count(*overflow, EventPhase::Performed) == 0,
            "numeric overflow cannot publish successful performed events");
    }

    void QueuedReloadAndRetainedFrames()
    {
        g_case = "queued reload, generation stamps, shutdown, and frame immutability";
        auto graph = Graph();
        Fixture fixture(graph);
        const auto initial = fixture.Frame(1, 0, 20, { Edge(10, 1, true) });
        graph.generation = 2;
        graph.signals[0].name = "DisplayRename";
        const auto layout = Compile(graph);
        Check(fixture.session.QueueProgram(layout), "queue display-only publication");
        Check(initial->GetDefinitionGeneration() == 1 && State(*initial).held,
            "queued command cannot mutate current sealed frame");
        const auto renamed = fixture.Frame(2, 20, 40);
        Phases(*renamed, {});
        Check(renamed->GetDefinitionGeneration() == 2 && State(*renamed).held &&
            renamed->GetSemanticHash() == initial->GetSemanticHash(),
            "display-only publication advances generation while preserving active state");
        graph.generation = 3;
        graph.signals[0].interaction.pressThreshold = 0.75f;
        const auto semantic = Compile(graph);
        Check(fixture.session.QueueProgram(semantic), "queue semantic replacement");
        const auto changed = fixture.Frame(3, 40, 60);
        Phases(*changed, { EventPhase::Canceled });
        Check(changed->GetDefinitionGeneration() == 3 &&
            Events(*changed)[0].reason == CancelReason::DefinitionChanged && !State(*changed).held,
            "semantic replacement cancels at the next input boundary and stamps its generation");
        Check(changed->GetInterfaceHash() == initial->GetInterfaceHash() &&
            changed->GetSemanticHash() != initial->GetSemanticHash(),
            "semantic edit retains compatible generated accessors");
        const auto rearmed = fixture.Frame(4, 60, 100, { Edge(70, 2, false), Edge(80, 3, true) });
        Phases(*rearmed, { EventPhase::Started, EventPhase::Performed });
        Button readOldKey;
        Check(rearmed->TryRead(Key<Button>(*fixture.program), readOldKey) && readOldKey.held,
            "original typed key remains valid across compatible semantic reload");
        auto incompatible = graph;
        incompatible.generation = 4;
        incompatible.signals[0].type = ValueType::Float;
        const auto changedType = Compile(incompatible);
        Check(!fixture.session.QueueProgram(changedType), "existing stable signal cannot change its type on reload");
        incompatible = graph;
        incompatible.id = { 1, 99 };
        Check(!fixture.session.QueueProgram(Compile(incompatible)), "foreign graph cannot replace session definition");
        Check(fixture.session.GetProgram()->GetDefinition().generation == 3,
            "failed replacements preserve last accepted program");
        const auto handle = fixture.session.GetHandle();
        Check(fixture.session.Shutdown(100, 100), "session shutdown succeeds on owner");
        const auto finalFrame = fixture.session.GetLastFrame(Domain::Game);
        Check(static_cast<bool>(finalFrame), "shutdown publishes terminal cancellation frame");
        Phases(*finalFrame, { EventPhase::Canceled });
        Check(Events(*finalFrame)[0].reason == CancelReason::SessionShutdown &&
            finalFrame->GetSession() == handle && fixture.session.GetHandle().generation != handle.generation &&
            !fixture.session.IsInitialized(), "shutdown invalidates live handle after final generation-stamped batch");
        Check(!fixture.session.Evaluate(Domain::Game, { 6, 100, 120, false }, {}) &&
            !fixture.session.QueueLayerChange(kLayer, true), "ended session rejects subsequent input and commands");
        Check(initial->GetDefinitionGeneration() == 1 && State(*initial).held &&
            renamed->GetDefinitionGeneration() == 2 && State(*renamed).held &&
            State(*rearmed).held && Count(*rearmed, EventPhase::Performed) == 1,
            "retained frames outlive reload and shutdown without state/event mutation");
    }

    void NativeSubscriptionParityAndExceptions()
    {
        g_case = "native typed dispatch parity, duplicates, reentrancy, and exceptions";
        Fixture fixture(Graph());
        const auto frame = fixture.Frame(1, 0, 100,
            { Edge(10, 1, true), Edge(20, 2, false), Edge(30, 3, true), Edge(40, 4, false) });
        InputSubscriptionRegistry registry;
        InputSubscriptionScope target(100, 1);
        std::size_t throwingCalls = 0;
        std::size_t wrongTypeCalls = 0;
        std::vector<SignalEvent<Button>> delivered;
        auto throwing = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>&)
            {
                ++throwingCalls;
                throw std::runtime_error("intentional subscriber exception");
            });
        auto healthy = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>& event)
            {
                delivered.push_back(event);
                if (delivered.size() == 1)
                {
                    Check(!registry.Dispatch(*frame), "reentrant dispatch cannot replay the current frame");
                }
                Button value{ true };
                Check(frame->TryRead(Key<Button>(*fixture.program), value) && !value.held,
                    "callback reads the same final sealed state as polling");
            });
        auto wrongType = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<float>(*fixture.program), target, [&](const SignalEvent<float>&)
            {
                ++wrongTypeCalls;
            });
        (void)wrongType; // Retain the deliberately mismatched RAII token throughout dispatch.
        Check(throwing.IsValid() && healthy.IsValid(), "typed subscriptions retain valid lifetime tokens");
        Check(registry.Dispatch(*frame), "first owner-thread dispatch accepts the sealed frame");
        Check(delivered.size() == 6 && throwingCalls == 6 && registry.GetExceptionCount() == 6,
            "one throwing subscriber cannot suppress later subscribers or subsequent events");
        Check(wrongTypeCalls == 0, "mismatched typed callback cannot receive another value type");
        for (std::size_t index = 0; index < delivered.size(); ++index)
        {
            const auto& expected = frame->GetEvents()[index];
            const auto& actual = delivered[index];
            Check(actual.phase == expected.phase && actual.value.held == (expected.value.x != 0.0f) &&
                actual.sourceTime == expected.sourceTime && actual.effectiveTime == expected.effectiveTime &&
                actual.sequence == expected.sequence && actual.routingEpoch == expected.routingEpoch &&
                actual.deviceEpoch == expected.deviceEpoch && actual.user == expected.user &&
                actual.reason == expected.reason && actual.late == expected.late,
                "native typed subscription preserves every event field and exact event order");
        }
        Check(!registry.Dispatch(*frame) && delivered.size() == 6 && throwingCalls == 6,
            "duplicate dispatch is rejected without a second callback batch");
        bool wrongThreadAccepted = true;
        std::jthread foreign([&]
        {
            wrongThreadAccepted = registry.Dispatch(*frame);
        });
        foreign.join();
        Check(!wrongThreadAccepted && delivered.size() == 6, "foreign-thread dispatch fails without callbacks");
        Check(frame->GetEvents().size() == 6 && !State(*frame).held && State(*frame).pressed && State(*frame).released,
            "dispatch, callback reads, and exceptions do not mutate the sealed frame");
        InputSubscriptionScope invalidTarget(0, 1);
        auto invalid = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), invalidTarget, [](const SignalEvent<Button>&) {});
        Check(!invalid.IsValid(), "invalid target identity cannot subscribe");
        auto invalidKey = Key<Button>(*fixture.program);
        ++invalidKey.abiVersion;
        auto incompatible = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            invalidKey, target, [](const SignalEvent<Button>&) {});
        Check(!incompatible.IsValid(), "incompatible ABI cannot create a live subscription");
    }

    void NativeSubscriptionMutationDuringDispatch()
    {
        g_case = "unsubscribe and deferred subscribe within native dispatch";
        Fixture fixture(Graph());
        const auto first = fixture.Frame(1, 0, 100,
            { Edge(10, 1, true), Edge(20, 2, false), Edge(30, 3, true), Edge(40, 4, false) });
        InputSubscriptionRegistry registry;
        InputSubscriptionScope target(101, 1);
        InputSubscription controller;
        InputSubscription victim;
        InputSubscription added;
        std::size_t controllerCalls = 0;
        std::size_t victimCalls = 0;
        std::size_t addedCalls = 0;
        std::size_t observerCalls = 0;
        controller = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>&)
            {
                ++controllerCalls;
                victim.Reset();
                controller.Reset();
                added = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
                    Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>&)
                    {
                        ++addedCalls;
                    });
            });
        victim = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>&)
            {
                ++victimCalls;
            });
        auto observer = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>&)
            {
                ++observerCalls;
            });
        Check(controller.IsValid() && victim.IsValid() && observer.IsValid(), "initial callback order is established");
        Check(registry.Dispatch(*first), "dispatch batch whose first callback changes subscriptions");
        Check(controllerCalls == 1 && victimCalls == 0 && !controller.IsValid() && !victim.IsValid(),
            "self-reset and reset of the next subscriber suppress every subsequent invocation immediately");
        Check(added.IsValid() && addedCalls == 0 && observerCalls == 6,
            "new subscriber sees no tail of the current frame while existing observer receives all events");
        const auto second = fixture.Frame(2, 100, 200, { Edge(110, 5, true), Edge(120, 6, false) });
        Check(registry.Dispatch(*second), "next frame dispatch succeeds after subscriber mutation");
        Check(controllerCalls == 1 && victimCalls == 0 && addedCalls == 3 && observerCalls == 9,
            "deferred subscriber starts on exactly the next frame");
        target.Invalidate();
        Check(!added.IsValid() && !observer.IsValid(), "target disable invalidates all its subscriptions");
        const auto third = fixture.Frame(3, 200, 300, { Edge(210, 7, true), Edge(220, 8, false) });
        Check(registry.Dispatch(*third) && addedCalls == 3 && observerCalls == 9,
            "invalidated target cannot receive a later batch");
    }

    void NativeSubscriptionGenerations()
    {
        g_case = "native subscription session generation invalidation";
        const auto program = Compile(Graph());
        Fixture oldSession(program, kUser, { 55, 1 });
        Fixture newSession(program, kUser, { 55, 2 });
        const auto oldFrame = oldSession.Frame(1, 0, 100, { Edge(10, 1, true), Edge(20, 2, false) });
        const auto lateOldFrame = oldSession.Frame(2, 100, 200, { Edge(110, 3, true), Edge(120, 4, false) });
        const auto newFrame = newSession.Frame(1, 0, 100, { Edge(10, 1, true), Edge(20, 2, false) });
        InputSubscriptionRegistry registry;
        InputSubscriptionScope oldTarget(102, 1);
        InputSubscriptionScope newTarget(102, 2);
        std::size_t oldCalls = 0;
        std::size_t newCalls = 0;
        auto oldToken = registry.Subscribe(oldSession.session.GetHandle(), Domain::Game,
            Key<Button>(*program), oldTarget, [&](const SignalEvent<Button>&)
            {
                ++oldCalls;
            });
        Check(registry.Dispatch(*oldFrame) && oldCalls == 3, "old generation receives its initial valid batch");
        auto newToken = registry.Subscribe(newSession.session.GetHandle(), Domain::Game,
            Key<Button>(*program), newTarget, [&](const SignalEvent<Button>&)
            {
                ++newCalls;
            });
        Check(registry.Dispatch(*newFrame) && newCalls == 3 && oldCalls == 3,
            "new session generation can restart frame numbering without reviving old callbacks");
        Check(!oldToken.IsValid() && newToken.IsValid(), "generation transition retires only old subscriptions");
        Check(!registry.Dispatch(*lateOldFrame) && newToken.IsValid() && oldCalls == 3 && newCalls == 3,
            "late old-generation batch cannot roll registry backward or invalidate the live generation");
        const auto next = newSession.Frame(2, 100, 200, { Edge(110, 3, true), Edge(120, 4, false) });
        Check(registry.Dispatch(*next) && newCalls == 6, "live generation remains dispatchable after stale batch rejection");
        registry.InvalidateSession(newSession.session.GetHandle());
        Check(!newToken.IsValid(), "explicit session invalidation revokes its tokens");
        const auto afterInvalidation = newSession.Frame(3, 200, 300, { Edge(210, 5, true), Edge(220, 6, false) });
        registry.Dispatch(*afterInvalidation);
        Check(newCalls == 6 && oldCalls == 3, "late batch after session invalidation has no surviving callback");
        Check(oldFrame->GetSession().generation == 1 && newFrame->GetSession().generation == 2 &&
            oldFrame->GetEvents().size() == 3 && newFrame->GetEvents().size() == 3,
            "registry lifetime changes leave retained frame generations and events intact");
    }

    void NativeSubscriptionRAIILifetimes()
    {
        g_case = "native subscription RAII and token outliving registry";
        Fixture fixture(Graph());
        InputSubscriptionScope target(103, 1);
        InputSubscription survivesRegistry;
        std::size_t calls = 0;
        {
            InputSubscriptionRegistry registry;
            survivesRegistry = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
                Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>&)
                {
                    ++calls;
                });
            Check(survivesRegistry.IsValid(), "token is live before registry teardown");
        }
        Check(!survivesRegistry.IsValid() && target.IsValid(),
            "registry teardown invalidates an outliving token without destroying its target");
        survivesRegistry.Reset();
        survivesRegistry.Reset();
        Check(!survivesRegistry.IsValid() && calls == 0, "reset after registry teardown is safe and idempotent");

        InputSubscriptionRegistry registry;
        InputSubscription survivesTarget;
        {
            InputSubscriptionScope temporaryTarget(104, 1);
            survivesTarget = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
                Key<Button>(*fixture.program), temporaryTarget, [&](const SignalEvent<Button>&)
                {
                    ++calls;
                });
            Check(survivesTarget.IsValid(), "token is live while target scope exists");
        }
        Check(!survivesTarget.IsValid(), "target destruction invalidates its outliving token");
        auto movable = registry.Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), target, [&](const SignalEvent<Button>&)
            {
                ++calls;
            });
        InputSubscription moved(std::move(movable));
        Check(!movable.IsValid() && moved.IsValid(), "moving token transfers its sole RAII cancellation responsibility");
        const auto first = fixture.Frame(1, 0, 100, { Edge(10, 1, true), Edge(20, 2, false) });
        Check(registry.Dispatch(*first) && calls == 3, "moved subscription remains active and destroyed target stays silent");
        registry.InvalidateAll();
        Check(!moved.IsValid(), "registry-wide invalidation revokes the moved token");
        const auto second = fixture.Frame(2, 100, 200, { Edge(110, 3, true), Edge(120, 4, false) });
        registry.Dispatch(*second);
        Check(calls == 3, "registry-wide invalidation prevents later callbacks");
        moved.Reset();
        survivesTarget.Reset();

        auto destroysItself = std::make_unique<InputSubscriptionRegistry>();
        InputSubscriptionScope destructionTarget(105, 1);
        std::size_t destroyerCalls = 0;
        std::size_t suppressedCalls = 0;
        auto destroyer = destroysItself->Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), destructionTarget, [&](const SignalEvent<Button>&)
            {
                ++destroyerCalls;
                destroysItself.reset();
            });
        auto suppressed = destroysItself->Subscribe(fixture.session.GetHandle(), Domain::Game,
            Key<Button>(*fixture.program), destructionTarget, [&](const SignalEvent<Button>&)
            {
                ++suppressedCalls;
            });
        Check(destroyer.IsValid() && suppressed.IsValid(), "self-destructing registry starts with two live tokens");
        const auto terminal = fixture.Frame(3, 200, 300, { Edge(210, 5, true), Edge(220, 6, false) });
        const bool dispatched = destroysItself->Dispatch(*terminal);
        Check(dispatched && !destroysItself && destroyerCalls == 1 && suppressedCalls == 0,
            "registry destruction inside first callback safely terminates the remaining batch");
        Check(!destroyer.IsValid() && !suppressed.IsValid(),
            "tokens outliving a registry destroyed during dispatch are immediately invalid");
        destroyer.Reset();
        suppressed.Reset();
        Check(terminal->GetEvents().size() == 3 && !State(*terminal).held,
            "destructive callback lifetime changes cannot mutate its retained sealed frame");
    }
}

int main()
{
    SchemaValidation();
    HashesAndOverrides();
    SameTickTransitionsAndTypedReads();
    DeltaAndProcessorOrder();
    ThresholdsAndLateRecords();
    FocusCancellationAndNeutralGate();
    BoundaryOrderAndFutureRecords();
    PostCombineProcessorsAndStableTies();
    ChordOrderingAndExpiry();
    LayerClaims();
    ExplicitPersistentResume();
    SessionIsolation();
    RoutedHistoryAcrossDomains();
    DeviceEpochsAndResync();
    PauseAndTargetedDomainCancellation();
    RepeatDeadlinesAndOverload();
    MalformedBatchesAndGapRecovery();
    QueuedReloadAndRetainedFrames();
    NativeSubscriptionParityAndExceptions();
    NativeSubscriptionMutationDuringDispatch();
    NativeSubscriptionGenerations();
    NativeSubscriptionRAIILifetimes();
    std::printf("InputGraph contract PASS: %zu checks\n", g_checks);
    return EXIT_SUCCESS;
}
