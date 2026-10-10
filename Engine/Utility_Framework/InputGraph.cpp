#include "InputGraph.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace Input
{
    namespace
    {
        template<class T>
        bool IsEnum(T value, T maximum)
        {
            return static_cast<std::uint32_t>(value) <= static_cast<std::uint32_t>(maximum);
        }

        template<class T>
        bool HasDuplicateID(const std::vector<T>& items)
        {
            for (std::size_t i = 1; i < items.size(); ++i)
            {
                if (items[i - 1].id == items[i].id)
                {
                    return true;
                }
            }
            return false;
        }

        template<class T, class ID>
        std::uint32_t FindID(const std::vector<T>& items, ID id)
        {
            const auto found = std::lower_bound(items.begin(), items.end(), id,
                [](const T& item, ID key) { return item.id < key; });
            if (found == items.end() || found->id != id)
            {
                return kInvalidSlot;
            }
            return static_cast<std::uint32_t>(found - items.begin());
        }

        bool IsDelta(SourceKind kind)
        {
            return kind == SourceKind::MouseDelta || kind == SourceKind::MouseWheel;
        }

        bool IsVectorSource(ControlID control)
        {
            return control.kind == SourceKind::MouseDelta || control.kind == SourceKind::PointerPosition ||
                (control.kind == SourceKind::GamepadAxis && control.code < 2);
        }

        // Explicit byte order and float bits make this independent of struct padding,
        // native endianness, editor layout, names, and publication generation.
        class SemanticHasher final
        {
        public:
            void Add(std::uint64_t value)
            {
                for (unsigned shift = 0; shift < 64; shift += 8)
                {
                    m_hash ^= (value >> shift) & 0xff;
                    m_hash *= 1099511628211ull;
                }
            }
            void AddFloat(float value) { Add(std::bit_cast<std::uint32_t>(value)); }
            template<class Tag>
            void AddID(StableID<Tag> id)
            {
                Add(id.high);
                Add(id.low);
            }
            std::uint64_t Get() const noexcept { return m_hash; }

        private:
            std::uint64_t m_hash{ 14695981039346656037ull };
        };

        std::uint64_t HashDefinition(const InputGraph& graph)
        {
            SemanticHasher hash;
            hash.AddID(graph.id);
            hash.Add(graph.schemaVersion);
            hash.Add(graph.compilerVersion);
            hash.Add(graph.abiVersion);
            hash.Add(graph.layers.size());
            for (const auto& layer : graph.layers)
            {
                hash.AddID(layer.id);
                hash.Add(static_cast<std::uint32_t>(layer.priority));
                hash.Add(static_cast<std::uint8_t>(layer.claim));
                hash.Add(layer.enabled);
            }
            hash.Add(graph.signals.size());
            for (const auto& signal : graph.signals)
            {
                hash.AddID(signal.id);
                hash.AddID(signal.layer);
                hash.Add(static_cast<std::uint8_t>(signal.type));
                hash.Add(static_cast<std::uint8_t>(signal.lifetime));
                hash.Add(static_cast<std::uint8_t>(signal.domain));
                hash.Add(static_cast<std::uint8_t>(signal.combine));
                hash.AddID(signal.gate);
                hash.Add(signal.resumePersistentValue);
                hash.Add(signal.processors.size());
                for (const auto& processor : signal.processors)
                {
                    hash.Add(static_cast<std::uint8_t>(processor.kind));
                    hash.AddFloat(processor.x);
                    hash.AddFloat(processor.y);
                }
                const auto& interaction = signal.interaction;
                hash.Add(static_cast<std::uint8_t>(interaction.kind));
                hash.Add(static_cast<std::uint64_t>(interaction.duration));
                hash.Add(static_cast<std::uint64_t>(interaction.repeatInterval));
                hash.AddFloat(interaction.pressThreshold);
                hash.AddFloat(interaction.releaseThreshold);
                hash.Add(static_cast<std::uint8_t>(interaction.chordOrder));
                hash.Add(interaction.claimChordControls);
                hash.Add(interaction.chord.size());
                for (const auto id : interaction.chord)
                {
                    hash.AddID(id);
                }
            }
            hash.Add(graph.bindings.size());
            for (const auto& binding : graph.bindings)
            {
                hash.AddID(binding.id);
                hash.AddID(binding.signal);
                hash.Add(static_cast<std::uint32_t>(binding.priority));
                hash.Add(binding.sources.size());
                for (const auto& source : binding.sources)
                {
                    hash.Add(static_cast<std::uint8_t>(source.control.kind));
                    hash.Add(source.control.code);
                    hash.AddFloat(source.scaleX);
                    hash.AddFloat(source.scaleY);
                    hash.Add(static_cast<std::uint8_t>(source.space));
                }
                hash.Add(binding.processors.size());
                for (const auto& processor : binding.processors)
                {
                    hash.Add(static_cast<std::uint8_t>(processor.kind));
                    hash.AddFloat(processor.x);
                    hash.AddFloat(processor.y);
                }
            }
            return hash.Get();
        }
    }

    std::uint32_t InputGraphProgram::FindSignal(SignalID signal, ValueType type) const noexcept
    {
        const auto slot = FindID(m_definition.signals, signal);
        if (slot == kInvalidSlot || m_definition.signals[slot].type != type)
        {
            return kInvalidSlot;
        }
        return slot;
    }

    std::uint32_t InputGraphProgram::FindBinding(BindingID binding) const noexcept
    {
        return FindID(m_definition.bindings, binding);
    }

    std::uint32_t InputGraphProgram::FindLayer(LayerID layer) const noexcept
    {
        return FindID(m_definition.layers, layer);
    }

    own::shared_owner<const InputGraphProgram> CompileInputGraph(
        const InputGraph& source, std::vector<InputDiagnostic>& diagnostics)
    {
        diagnostics.clear();
        const auto reject = [&](std::string message, SignalID signal = {}, BindingID binding = {})
        {
            diagnostics.push_back({ signal, binding, std::move(message) });
        };
        if (!source.id.IsValid() || source.generation == 0 || source.schemaVersion != kInputSchemaVersion ||
            source.compilerVersion != kInputCompilerVersion || source.abiVersion != kInputABIVersion)
        {
            reject("Invalid graph identity/generation or unsupported schema, compiler, or ABI version.");
            return {};
        }
        // Bound preparation and runtime state before accepting untrusted/cooked input.
        constexpr std::size_t kMaximumDefinitions = 65'536;
        if (source.layers.empty() || source.signals.empty() || source.layers.size() > 1'024 ||
            source.signals.size() > kMaximumDefinitions || source.bindings.size() > kMaximumDefinitions)
        {
            reject("Input graph needs layers and outputs and must fit the bounded definition limits.");
            return {};
        }
        constexpr std::size_t kMaximumElements = 262'144;
        std::size_t elementCount = 0;
        for (const auto& signal : source.signals)
        {
            if (signal.interaction.chord.size() > 64 || signal.processors.size() > 64 || signal.name.size() > 4'096)
            {
                reject("Signal dependency or name size exceeds the preparation limit.", signal.id);
                return {};
            }
            elementCount += signal.interaction.chord.size() + signal.processors.size() + (signal.gate.IsValid() ? 1 : 0);
        }
        for (const auto& binding : source.bindings)
        {
            if (binding.sources.size() > 64 || binding.processors.size() > 64)
            {
                reject("Binding source/processor count exceeds the preparation limit.", {}, binding.id);
                return {};
            }
            elementCount += binding.sources.size() + binding.processors.size();
        }
        for (const auto& layer : source.layers)
        {
            if (layer.name.size() > 4'096)
            {
                reject("Layer name exceeds the preparation limit.");
                return {};
            }
        }
        if (elementCount > kMaximumElements)
        {
            reject("Total source, processor, and dependency count exceeds the preparation limit.");
            return {};
        }

        auto program = own::make_shared<InputGraphProgram>(InputGraphProgram::ConstructionKey{});
        auto& graph = program->m_definition;
        graph = source;
        const auto byID = [](const auto& left, const auto& right) { return left.id < right.id; };
        std::sort(graph.layers.begin(), graph.layers.end(), byID);
        std::sort(graph.signals.begin(), graph.signals.end(), byID);
        std::sort(graph.bindings.begin(), graph.bindings.end(), byID);
        if (HasDuplicateID(graph.layers) || HasDuplicateID(graph.signals) || HasDuplicateID(graph.bindings))
        {
            reject("Duplicate stable layer, signal, or binding ID.");
            return {};
        }
        for (const auto& layer : graph.layers)
        {
            if (!layer.id.IsValid() || !IsEnum(layer.claim, ClaimPolicy::PassThrough))
            {
                reject("Invalid layer identity or claim policy.");
            }
        }
        program->m_signals.resize(graph.signals.size());
        for (std::uint32_t index = 0; index < graph.signals.size(); ++index)
        {
            const auto& signal = graph.signals[index];
            auto& prepared = program->m_signals[index];
            prepared.definitionIndex = index;
            prepared.layerIndex = FindID(graph.layers, signal.layer);
            if (!signal.id.IsValid() || prepared.layerIndex == kInvalidSlot ||
                !IsEnum(signal.type, ValueType::Vector2) || !IsEnum(signal.lifetime, ValueLifetime::Delta) ||
                !IsEnum(signal.domain, Domain::UI) || !IsEnum(signal.combine, CombinePolicy::Priority))
            {
                reject("Invalid signal identity, layer, type, domain, lifetime, or combine policy.", signal.id);
            }
            const auto& interaction = signal.interaction;
            if (!IsEnum(interaction.kind, InteractionKind::Chord) ||
                !IsEnum(interaction.chordOrder, ChordOrder::Sequential) ||
                !std::isfinite(interaction.pressThreshold) || !std::isfinite(interaction.releaseThreshold) ||
                interaction.pressThreshold <= 0.0f || interaction.releaseThreshold < 0.0f ||
                interaction.releaseThreshold >= interaction.pressThreshold || interaction.duration < 0 ||
                interaction.repeatInterval < 0 || interaction.chord.size() > 64 ||
                (interaction.kind != InteractionKind::Press && signal.type != ValueType::Button) ||
                (signal.type == ValueType::Button && signal.lifetime == ValueLifetime::Delta) ||
                (signal.resumePersistentValue && (signal.type == ValueType::Button ||
                    signal.lifetime != ValueLifetime::Persistent)) ||
                (interaction.kind != InteractionKind::Hold && interaction.repeatInterval != 0))
            {
                reject("Invalid interaction type, thresholds, duration, repeat interval, or chord size.", signal.id);
            }
            if ((interaction.kind == InteractionKind::Chord && interaction.chord.empty()) ||
                (interaction.kind != InteractionKind::Chord && !interaction.chord.empty()))
            {
                reject("Chord participants must be present only on a Chord interaction.", signal.id);
            }
            const auto resolveDependency = [&](SignalID id)
            {
                const auto dependency = FindID(graph.signals, id);
                if (dependency == kInvalidSlot || dependency == index ||
                    graph.signals[dependency].type != ValueType::Button ||
                    graph.signals[dependency].layer != signal.layer || graph.signals[dependency].domain != signal.domain)
                {
                    reject("Gate/chord dependencies must be other Button signals in the same layer and domain.", signal.id);
                    return kInvalidSlot;
                }
                return dependency;
            };
            if (signal.gate.IsValid())
            {
                prepared.gateIndex = resolveDependency(signal.gate);
            }
            for (const auto dependency : interaction.chord)
            {
                const auto slot = resolveDependency(dependency);
                if (std::find(prepared.chordIndices.begin(), prepared.chordIndices.end(), slot) !=
                    prepared.chordIndices.end())
                {
                    reject("Duplicate chord participant.", signal.id);
                }
                prepared.chordIndices.push_back(slot);
            }
            for (const auto& processor : signal.processors)
            {
                if (!IsEnum(processor.kind, ProcessorKind::Clamp) || !std::isfinite(processor.x) ||
                    !std::isfinite(processor.y) ||
                    (processor.kind == ProcessorKind::Deadzone && (processor.x < 0.0f || processor.y <= processor.x)) ||
                    (processor.kind == ProcessorKind::Clamp && processor.x > processor.y) ||
                    interaction.kind == InteractionKind::Chord)
                {
                    reject("Invalid post-combine processor, or a processor attached after a Chord interaction.", signal.id);
                }
            }
        }
        std::vector<std::pair<ControlID, std::uint32_t>> controlBindings;
        program->m_bindingSignals.resize(graph.bindings.size(), kInvalidSlot);
        for (std::uint32_t index = 0; index < graph.bindings.size(); ++index)
        {
            const auto& binding = graph.bindings[index];
            const auto signalIndex = FindID(graph.signals, binding.signal);
            if (!binding.id.IsValid() || signalIndex == kInvalidSlot || binding.sources.empty() ||
                binding.sources.size() > 64 || binding.processors.size() > 64)
            {
                reject("Binding needs a stable ID, a known output, and a bounded nonempty source list.", {}, binding.id);
                continue;
            }
            const auto& signal = graph.signals[signalIndex];
            program->m_bindingSignals[index] = signalIndex;
            program->m_signals[signalIndex].bindingIndices.push_back(index);
            for (const auto& input : binding.sources)
            {
                if (!IsEnum(input.control.kind, SourceKind::GamepadAxis) ||
                    !IsEnum(input.space, CoordinateSpace::Normalized) || !std::isfinite(input.scaleX) ||
                    !std::isfinite(input.scaleY) ||
                    (IsDelta(input.control.kind) != (signal.lifetime == ValueLifetime::Delta)) ||
                    (IsVectorSource(input.control) && signal.type == ValueType::Float) ||
                    (signal.type != ValueType::Vector2 && input.scaleY != 0.0f) ||
                    (input.control.kind == SourceKind::MouseDelta && input.space != CoordinateSpace::RelativeCounts) ||
                    (input.control.kind == SourceKind::PointerPosition && input.space != CoordinateSpace::ClientPixels) ||
                    (input.control.kind == SourceKind::GamepadAxis && input.control.code > 3))
                {
                    reject("Unsupported source capability, coordinate space, value dimension, or delta lifetime.",
                        signal.id, binding.id);
                }
                controlBindings.emplace_back(input.control, index);
            }
            for (const auto& processor : binding.processors)
            {
                if (!IsEnum(processor.kind, ProcessorKind::Clamp) || !std::isfinite(processor.x) ||
                    !std::isfinite(processor.y) ||
                    (processor.kind == ProcessorKind::Deadzone && (processor.x < 0.0f || processor.y <= processor.x)) ||
                    (processor.kind == ProcessorKind::Clamp && processor.x > processor.y))
                {
                    reject("Invalid processor kind, finite parameter, deadzone radius, or clamp range.",
                        signal.id, binding.id);
                }
            }
        }
        for (std::uint32_t index = 0; index < graph.signals.size(); ++index)
        {
            if (program->m_signals[index].bindingIndices.empty() &&
                graph.signals[index].interaction.kind != InteractionKind::Chord)
            {
                reject("Every non-Chord output requires at least one binding.", graph.signals[index].id);
            }
        }
        if (!diagnostics.empty())
        {
            return {};
        }
        std::sort(controlBindings.begin(), controlBindings.end());
        controlBindings.erase(std::unique(controlBindings.begin(), controlBindings.end()), controlBindings.end());
        for (const auto& [control, binding] : controlBindings)
        {
            if (program->m_controls.empty() || program->m_controls.back().control != control)
            {
                program->m_controls.push_back({ control, {} });
            }
            program->m_controls.back().bindingIndices.push_back(binding);
        }

        // Kahn ordering simultaneously enforces the dependency DAG and stable layer
        // priority. Cross-layer stateful edges were rejected above.
        std::vector<std::uint32_t> incoming(graph.signals.size());
        std::vector<std::vector<std::uint32_t>> dependents(graph.signals.size());
        for (std::uint32_t index = 0; index < graph.signals.size(); ++index)
        {
            auto dependencies = program->m_signals[index].chordIndices;
            const auto gate = program->m_signals[index].gateIndex;
            if (gate != kInvalidSlot && std::find(dependencies.begin(), dependencies.end(), gate) == dependencies.end())
            {
                dependencies.push_back(gate);
            }
            incoming[index] = static_cast<std::uint32_t>(dependencies.size());
            for (const auto dependency : dependencies)
            {
                dependents[dependency].push_back(index);
            }
        }
        const auto lowerPriority = [&](std::uint32_t left, std::uint32_t right)
        {
            const auto leftPriority = graph.layers[program->m_signals[left].layerIndex].priority;
            const auto rightPriority = graph.layers[program->m_signals[right].layerIndex].priority;
            return leftPriority != rightPriority ? leftPriority < rightPriority : graph.signals[left].id > graph.signals[right].id;
        };
        std::priority_queue<std::uint32_t, std::vector<std::uint32_t>, decltype(lowerPriority)> ready(lowerPriority);
        for (std::uint32_t index = 0; index < incoming.size(); ++index)
        {
            if (incoming[index] == 0)
            {
                ready.push(index);
            }
        }
        while (!ready.empty())
        {
            const auto best = ready.top();
            ready.pop();
            program->m_evaluationOrder.push_back(best);
            for (const auto dependent : dependents[best])
            {
                if (--incoming[dependent] == 0)
                {
                    ready.push(dependent);
                }
            }
        }
        if (program->m_evaluationOrder.size() != graph.signals.size())
        {
            reject("Cyclic gate or Chord signal dependency.");
            return {};
        }
        program->m_semanticHash = HashDefinition(graph);
        SemanticHasher interfaceHash;
        interfaceHash.AddID(graph.id);
        interfaceHash.Add(graph.schemaVersion);
        interfaceHash.Add(graph.abiVersion);
        interfaceHash.Add(graph.layers.size());
        for (const auto& layer : graph.layers)
        {
            interfaceHash.AddID(layer.id);
        }
        interfaceHash.Add(graph.signals.size());
        for (const auto& signal : graph.signals)
        {
            interfaceHash.AddID(signal.id);
            interfaceHash.Add(static_cast<std::uint8_t>(signal.type));
        }
        program->m_interfaceHash = interfaceHash.Get();
        return program;
    }

    own::shared_owner<const InputGraphProgram> ApplyInputOverrides(const InputGraphProgram& program,
        std::span<const InputBindingOverride> overrides, std::vector<InputDiagnostic>& diagnostics)
    {
        diagnostics.clear();
        if (overrides.size() > program.GetDefinition().bindings.size())
        {
            diagnostics.push_back({ {}, {}, "Override count exceeds the graph binding count." });
            return {};
        }
        std::size_t sourceCount = 0;
        for (const auto& override : overrides)
        {
            if (override.sources.size() > 64)
            {
                diagnostics.push_back({ {}, override.binding, "Override source count exceeds the preparation limit." });
                return {};
            }
            sourceCount += override.sources.size();
        }
        if (sourceCount > 262'144)
        {
            diagnostics.push_back({ {}, {}, "Total override source count exceeds the preparation limit." });
            return {};
        }
        auto graph = program.GetDefinition();
        std::vector<bool> replaced(graph.bindings.size());
        for (const auto& override : overrides)
        {
            const auto binding = program.FindBinding(override.binding);
            if (override.graph != graph.id || override.schemaVersion != graph.schemaVersion ||
                binding == kInvalidSlot || override.sources.empty() ||
                (binding != kInvalidSlot && replaced[binding]))
            {
                diagnostics.push_back({ {}, override.binding,
                    "Rebind override has an incompatible graph/schema, orphan binding, empty source, or duplicate target." });
                return {};
            }
            replaced[binding] = true;
            graph.bindings[binding].sources = override.sources;
        }
        if (graph.generation == (std::numeric_limits<std::uint64_t>::max)())
        {
            diagnostics.push_back({ {}, {}, "Definition generation exhausted." });
            return {};
        }
        ++graph.generation;
        return CompileInputGraph(graph, diagnostics);
    }
}
