#pragma once

#include "InputGraphTypes.h"
#include "Ownership.h"

#include <string>
#include <vector>

namespace Input
{
    struct InputProcessor final
    {
        ProcessorKind kind{ ProcessorKind::Scale };
        float x{ 1.0f }; // Scale, minimum for Clamp, inner radius for Deadzone.
        float y{ 1.0f }; // Y scale, maximum for Clamp, outer radius for Deadzone.
    };

    struct InputSource final
    {
        ControlID control{};
        float scaleX{ 1.0f };
        float scaleY{}; // Scalar sources can contribute to either component of Axis2D.
        CoordinateSpace space{ CoordinateSpace::None };
    };

    struct InputInteraction final
    {
        InteractionKind kind{ InteractionKind::Press };
        Timestamp duration{ 200'000'000 };
        Timestamp repeatInterval{}; // Zero means one Performed per hold.
        float pressThreshold{ 0.5f };
        float releaseThreshold{ 0.25f };
        std::vector<SignalID> chord;
        ChordOrder chordOrder{ ChordOrder::Simultaneous };
        bool claimChordControls{ true };
    };

    struct InputLayer final
    {
        LayerID id{};
        std::string name;
        std::int32_t priority{};
        ClaimPolicy claim{ ClaimPolicy::PassThrough };
        bool enabled{ true };
    };

    struct InputSignalDefinition final
    {
        SignalID id{};
        LayerID layer{};
        std::string name;
        ValueType type{ ValueType::Button };
        ValueLifetime lifetime{ ValueLifetime::Persistent };
        Domain domain{ Domain::Game };
        CombinePolicy combine{ CombinePolicy::MaximumMagnitude };
        InputInteraction interaction{};
        SignalID gate{}; // Optional same-layer Button dependency. Invalid means always enabled.
        bool resumePersistentValue{}; // Explicit opt-in for positions/axes; never allowed for Button/charge.
    };

    struct InputBinding final
    {
        BindingID id{};
        SignalID signal{};
        std::int32_t priority{};
        std::vector<InputSource> sources; // A direct source or a sum/composite such as WASD.
        std::vector<InputProcessor> processors; // Stored order is evaluation order.
    };

    struct InputGraph final
    {
        GraphID id{};
        std::uint32_t schemaVersion{ kInputSchemaVersion };
        std::uint32_t compilerVersion{ kInputCompilerVersion };
        std::uint32_t abiVersion{ kInputABIVersion };
        std::uint64_t generation{ 1 };
        std::vector<InputLayer> layers;
        std::vector<InputSignalDefinition> signals;
        std::vector<InputBinding> bindings;
    };

    struct InputDiagnostic final
    {
        SignalID signal{};
        BindingID binding{};
        std::string message;
    };

    struct PreparedInputSignal final
    {
        std::uint32_t definitionIndex{};
        std::uint32_t layerIndex{};
        std::uint32_t gateIndex{ kInvalidSlot };
        std::vector<std::uint32_t> bindingIndices;
        std::vector<std::uint32_t> chordIndices;
    };

    struct PreparedControlLookup final
    {
        ControlID control{};
        std::vector<std::uint32_t> bindingIndices;
    };

    // Prepared once, owned jointly by an asset generation and its live sessions.
    // Only the compiler can construct it; publication exposes const ownership.
    class InputGraphProgram final
    {
    public:
        const InputGraph& GetDefinition() const noexcept { return m_definition; }
        std::span<const PreparedInputSignal> GetSignals() const noexcept { return m_signals; }
        std::span<const std::uint32_t> GetEvaluationOrder() const noexcept { return m_evaluationOrder; }
        std::span<const PreparedControlLookup> GetControls() const noexcept { return m_controls; }
        std::uint64_t GetSemanticHash() const noexcept { return m_semanticHash; }
        std::uint64_t GetInterfaceHash() const noexcept { return m_interfaceHash; }
        std::uint32_t FindSignal(SignalID signal, ValueType type) const noexcept;
        std::uint32_t FindBinding(BindingID binding) const noexcept;
        std::uint32_t FindLayer(LayerID layer) const noexcept;

    private:
        friend own::shared_owner<const InputGraphProgram> CompileInputGraph(
            const InputGraph&, std::vector<InputDiagnostic>&);
        InputGraph m_definition;
        std::vector<PreparedInputSignal> m_signals;
        std::vector<std::uint32_t> m_evaluationOrder;
        std::vector<PreparedControlLookup> m_controls;
        std::uint64_t m_semanticHash{};
        std::uint64_t m_interfaceHash{};
    };

    [[nodiscard]] own::shared_owner<const InputGraphProgram> CompileInputGraph(
        const InputGraph& graph, std::vector<InputDiagnostic>& diagnostics);

    struct InputBindingOverride final
    {
        GraphID graph{};
        BindingID binding{};
        std::uint32_t schemaVersion{ kInputSchemaVersion };
        std::vector<InputSource> sources;
    };

    // Returns no program on any invalid/orphan override. The original is unchanged.
    [[nodiscard]] own::shared_owner<const InputGraphProgram> ApplyInputOverrides(
        const InputGraphProgram& program, std::span<const InputBindingOverride> overrides,
        std::vector<InputDiagnostic>& diagnostics);
}
