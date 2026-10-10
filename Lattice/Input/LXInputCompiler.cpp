#include "LXInputCompiler.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

namespace LX
{
    namespace
    {
        struct Recipe
        {
            Id identity{};
            std::int32_t priority{};
            std::vector<Input::InputSource> sources;
            std::vector<Input::InputProcessor> processors;
            std::vector<Input::InputProcessor> signalProcessors;
            Id postCombineScope{};
            const Node* interaction{};
            const Node* gate{};
            std::optional<Input::CombinePolicy> combine;
        };

        class Compiler final
        {
        public:
            explicit Compiler(const LXInputAsset& asset) : m_asset(asset)
            {
                m_result.diagnostics = asset.graph.Validate();
            }

            LXInputCompileResult Run(std::uint64_t generation)
            {
                auto& definition = m_result.definition;
                if (!ReadGraphID(m_asset.graphId, definition.id))
                {
                    Error(0, "identity", "Graph identity must be a canonical, nonzero UUID");
                }
                definition.generation = generation;
                if (m_asset.graph.Domain() != "input" || !m_asset.graph.Groups().empty())
                {
                    Error(0, "domain", "Input requires the input domain; stateful group expansion is unsupported");
                }
                const auto definitions = CreateInputDefinitions();
                for (const auto& node : m_asset.graph.Nodes())
                {
                    if (!definitions->Find(node.type) || !definitions->MatchesNode("input", node))
                    {
                        Error(node.id, "schema", "Unsupported input node schema cannot be published");
                    }
                    if (node.type == "Input.Layer")
                    {
                        Input::InputLayer layer;
                        layer.id = Layer(node.id);
                        layer.name = Property(node, "name");
                        layer.priority = Integer<std::int32_t>(node, "priority");
                        layer.enabled = Boolean(node, "enabled");
                        layer.claim = Choice<Input::ClaimPolicy>(node, "claim",
                            {{"OnPress", Input::ClaimPolicy::OnPress}, {"OnPerformed", Input::ClaimPolicy::OnPerformed},
                             {"PassThrough", Input::ClaimPolicy::PassThrough}});
                        definition.layers.push_back(std::move(layer));
                    }
                }
                for (const auto& node : m_asset.graph.Nodes())
                {
                    if (node.type.starts_with("Input.Signal."))
                    {
                        CompileSignal(node);
                    }
                }
                if (definition.signals.empty())
                {
                    Error(0, "output", "InputGraph requires at least one typed signal output");
                }
                if (!HasErrors())
                {
                    const auto byIdentity = [](const auto& left, const auto& right) { return left.id < right.id; };
                    std::sort(definition.layers.begin(), definition.layers.end(), byIdentity);
                    std::sort(definition.signals.begin(), definition.signals.end(), byIdentity);
                    std::sort(definition.bindings.begin(), definition.bindings.end(), byIdentity);
                    std::vector<Input::InputDiagnostic> diagnostics;
                    m_result.program = Input::CompileInputGraph(definition, diagnostics);
                    for (const auto& diagnostic : diagnostics)
                    {
                        Error(diagnostic.signal.low, "runtime", diagnostic.message);
                    }
                }
                return std::move(m_result);
            }

        private:
            static bool ReadGraphID(std::string_view text, Input::GraphID& result)
            {
                if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
                {
                    return false;
                }
                if ((text[14] != '4' && text[14] != '8') ||
                    (text[19] != '8' && text[19] != '9' && text[19] != 'a' && text[19] != 'b'))
                {
                    return false;
                }
                std::string digits;
                for (char character : text)
                {
                    if (character != '-')
                    {
                        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
                        {
                            return false;
                        }
                        digits.push_back(character);
                    }
                }
                if (digits.size() != 32)
                {
                    return false;
                }
                const auto high = std::from_chars(digits.data(), digits.data() + 16, result.high, 16);
                const auto low = std::from_chars(digits.data() + 16, digits.data() + 32, result.low, 16);
                return high.ec == std::errc{} && high.ptr == digits.data() + 16 && low.ec == std::errc{} &&
                       low.ptr == digits.data() + 32 && result.IsValid();
            }

            Input::SignalID Signal(Id id) const
            {
                return {m_result.definition.id.high ^ m_result.definition.id.low, id};
            }

            Input::LayerID Layer(Id id) const
            {
                return {m_result.definition.id.high ^ m_result.definition.id.low, id};
            }

            void Error(Id node, std::string code, std::string message)
            {
                m_result.diagnostics.push_back({std::move(code), std::move(message), node});
            }

            bool HasErrors() const
            {
                return std::any_of(m_result.diagnostics.begin(), m_result.diagnostics.end(), [](const Issue& issue) {
                    return issue.severity == Issue::Severity::Error;
                });
            }

            std::string Property(const Node& node, const char* key)
            {
                const auto found = node.properties.find(key);
                if (found == node.properties.end())
                {
                    Error(node.id, "property", std::string("Missing property: ") + key);
                    return {};
                }
                return found->second;
            }

            template<class T>
            T Integer(const Node& node, const char* key)
            {
                const auto text = Property(node, key);
                T result{};
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                {
                    Error(node.id, "number", std::string("Invalid integer: ") + key);
                }
                return result;
            }

            double Number(const Node& node, const char* key)
            {
                const auto text = Property(node, key);
                double result{};
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(result))
                {
                    Error(node.id, "number", std::string("Invalid finite number: ") + key);
                    return 0.0;
                }
                return result;
            }

            Input::Timestamp Duration(const Node& node, const char* key)
            {
                const double seconds = Number(node, key);
                constexpr double maximum = static_cast<double>(std::numeric_limits<Input::Timestamp>::max()) / 1e9;
                if (seconds < 0.0 || seconds >= maximum)
                {
                    Error(node.id, "duration", "Interaction duration must be nonnegative and representable");
                    return 0;
                }
                return static_cast<Input::Timestamp>(seconds * 1e9);
            }

            bool Boolean(const Node& node, const char* key)
            {
                const auto value = Property(node, key);
                if (value != "true" && value != "false")
                {
                    Error(node.id, "boolean", std::string("Invalid boolean: ") + key);
                }
                return value == "true";
            }

            template<class T>
            T Choice(const Node& node, const char* key, std::initializer_list<std::pair<std::string_view, T>> choices)
            {
                const auto value = Property(node, key);
                for (const auto& [name, item] : choices)
                {
                    if (value == name)
                    {
                        return item;
                    }
                }
                Error(node.id, "choice", std::string("Unsupported ") + key + ": " + value);
                return choices.begin()->second;
            }

            Input::CombinePolicy Combine(const Node& node, const char* key)
            {
                return Choice<Input::CombinePolicy>(node, key,
                    {{"Sum", Input::CombinePolicy::Sum}, {"MaximumMagnitude", Input::CombinePolicy::MaximumMagnitude},
                     {"MostRecent", Input::CombinePolicy::MostRecent}, {"Priority", Input::CombinePolicy::Priority}});
            }

            std::vector<const Link*> Incoming(const Node& node, const char* identifier)
            {
                std::vector<const Link*> result;
                for (const auto& pin : node.pins)
                {
                    if (pin.direction != Direction::Input || pin.Identifier() != identifier)
                    {
                        continue;
                    }
                    for (const auto& link : m_asset.graph.Links())
                    {
                        if (link.input == pin.id)
                        {
                            result.push_back(&link);
                        }
                    }
                }
                std::sort(result.begin(), result.end(), [](const Link* left, const Link* right) {
                    return left->id < right->id;
                });
                return result;
            }

            const Node* Upstream(const Link& link)
            {
                const auto* pin = m_asset.graph.FindPin(link.output);
                return pin ? m_asset.graph.FindNode(pin->node) : nullptr;
            }

            const Node* SignalReference(const Node& node, const char* input)
            {
                const auto links = Incoming(node, input);
                const Node* target = links.size() == 1 ? Upstream(*links.front()) : nullptr;
                if (!target || target->type != "Input.Signal.Button")
                {
                    Error(node.id, "dependency", std::string(input) + " requires exactly one Button signal output");
                    return nullptr;
                }
                return target;
            }

            std::optional<Input::InputSource> Source(const Node& node)
            {
                static const std::map<std::string, Input::SourceKind> kinds{
                    {"Input.Key", Input::SourceKind::Key}, {"Input.MouseButton", Input::SourceKind::MouseButton},
                    {"Input.MouseDelta", Input::SourceKind::MouseDelta},
                    {"Input.PointerPosition", Input::SourceKind::PointerPosition},
                    {"Input.MouseWheel", Input::SourceKind::MouseWheel},
                    {"Input.GamepadButton", Input::SourceKind::GamepadButton},
                    {"Input.GamepadAxis", Input::SourceKind::GamepadAxis},
                    {"Input.GamepadStick", Input::SourceKind::GamepadAxis}};
                const auto kind = kinds.find(node.type);
                if (kind == kinds.end())
                {
                    return std::nullopt;
                }
                Input::InputSource source;
                source.control = {kind->second, Integer<std::uint32_t>(node, "control")};
                if ((source.control.kind == Input::SourceKind::Key && source.control.code == 0) ||
                    (source.control.kind == Input::SourceKind::GamepadButton && source.control.code >= 14) ||
                    ((source.control.kind == Input::SourceKind::MouseDelta ||
                      source.control.kind == Input::SourceKind::PointerPosition ||
                      source.control.kind == Input::SourceKind::MouseWheel) && source.control.code != 0))
                {
                    Error(node.id, "capability", "Physical control is outside the supported source capability");
                }
                if ((node.type == "Input.GamepadAxis" && (source.control.code < 2 || source.control.code > 3)) ||
                    (node.type == "Input.GamepadStick" && source.control.code > 1))
                {
                    Error(node.id, "capability", "Stick controls are 0/1; scalar trigger controls are 2/3");
                }
                if (node.properties.contains("space"))
                {
                    source.space = Choice<Input::CoordinateSpace>(node, "space",
                        {{"None", Input::CoordinateSpace::None}, {"ClientPixels", Input::CoordinateSpace::ClientPixels},
                         {"RelativeCounts", Input::CoordinateSpace::RelativeCounts},
                         {"Normalized", Input::CoordinateSpace::Normalized}});
                }
                if (kind->second == Input::SourceKind::MouseDelta || kind->second == Input::SourceKind::PointerPosition ||
                    node.type == "Input.GamepadStick")
                {
                    source.scaleY = 1.0f;
                }
                return source;
            }

            std::vector<Recipe> Follow(const Link& link, std::set<Id> path)
            {
                if (++m_expansionCount > 100000 || path.size() > 128)
                {
                    if (m_expansionCount == 100001 || path.size() > 128)
                    {
                        Error(0, "budget", "Input dependency expansion exceeds the bounded compile budget");
                    }
                    return {};
                }
                const Node* node = Upstream(link);
                if (!node || !path.insert(node->id).second)
                {
                    Error(node ? node->id : 0, "cycle", "Missing source or cyclic input dependency");
                    return {};
                }
                if (const auto source = Source(*node))
                {
                    Recipe recipe;
                    recipe.identity = link.id;
                    recipe.sources.push_back(*source);
                    return {recipe};
                }
                if (node->type == "Input.Axis2D")
                {
                    Recipe recipe;
                    recipe.identity = link.id;
                    const char* names[]{"Left", "Right", "Down", "Up"};
                    constexpr float x[]{-1, 1, 0, 0};
                    constexpr float y[]{0, 0, -1, 1};
                    for (std::size_t index = 0; index < 4; ++index)
                    {
                        const auto incoming = Incoming(*node, names[index]);
                        if (incoming.size() != 1)
                        {
                            Error(node->id, "axis", "Axis2D requires four connected physical Button sources");
                            continue;
                        }
                        const Node* sourceNode = Upstream(*incoming.front());
                        auto source = sourceNode ? Source(*sourceNode) : std::nullopt;
                        if (!source)
                        {
                            Error(node->id, "axis", "Axis2D inputs must be direct physical Button sources");
                            continue;
                        }
                        source->scaleX = x[index];
                        source->scaleY = y[index];
                        recipe.sources.push_back(*source);
                    }
                    return {recipe};
                }
                const bool combination = node->type.starts_with("Input.Combine.");
                auto links = Incoming(*node, combination ? "Values" : "Value");
                if (links.empty() || (!combination && links.size() != 1))
                {
                    Error(node->id, "connection", "Node requires connected value input(s)");
                    return {};
                }
                std::vector<Recipe> recipes;
                for (const Link* incoming : links)
                {
                    auto branch = Follow(*incoming, path);
                    for (auto& recipe : branch)
                    {
                        if (combination)
                        {
                            if (!recipe.signalProcessors.empty())
                            {
                                Error(node->id, "processor-order", "A processed combination cannot be nested inside another combination");
                            }
                            const auto policy = Combine(*node, "policy");
                            if (recipe.combine && *recipe.combine != policy)
                            {
                                Error(node->id, "combine", "Nested combination policies must agree");
                            }
                            recipe.combine = policy;
                            if (branch.size() == 1)
                            {
                                recipe.identity = incoming->id;
                            }
                        }
                        recipes.push_back(std::move(recipe));
                    }
                }
                if (combination)
                {
                    return recipes;
                }
                if (node->type.starts_with("Input.Priority."))
                {
                    for (auto& recipe : recipes)
                    {
                        recipe.priority = Integer<std::int32_t>(*node, "priority");
                    }
                    return recipes;
                }
                if (node->type == "Input.Press" || node->type == "Input.Hold" || node->type == "Input.Tap" ||
                    node->type == "Input.Chord" || node->type.ends_with(".Analog"))
                {
                    for (auto& recipe : recipes)
                    {
                        if (recipe.interaction)
                        {
                            Error(node->id, "interaction", "A binding may have only one interaction");
                        }
                        recipe.interaction = node;
                    }
                    return recipes;
                }
                if (node->type.starts_with("Input.Gate."))
                {
                    const Node* gate = SignalReference(*node, "Enable");
                    for (auto& recipe : recipes)
                    {
                        if (recipe.gate && recipe.gate != gate)
                        {
                            Error(node->id, "gate", "Multiple distinct gates require an explicit Button signal");
                        }
                        recipe.gate = gate;
                    }
                    return recipes;
                }
                static const std::map<std::string, Input::ProcessorKind> kinds{
                    {"Deadzone", Input::ProcessorKind::Deadzone}, {"Normalize", Input::ProcessorKind::Normalize},
                    {"Scale", Input::ProcessorKind::Scale}, {"Invert", Input::ProcessorKind::Invert},
                    {"Clamp", Input::ProcessorKind::Clamp}};
                const auto end = node->type.find('.', 6);
                const auto kind = kinds.find(node->type.substr(6, end - 6));
                if (kind == kinds.end())
                {
                    Error(node->id, "node", "Unsupported value node; signal outputs are references only for gates/chords");
                    return {};
                }
                Input::InputProcessor processor;
                processor.kind = kind->second;
                for (const auto& pin : node->pins)
                {
                    if (pin.Identifier() == "Parameters")
                    {
                        if (!Incoming(*node, "Parameters").empty())
                        {
                            Error(node->id, "parameter", "Processor parameters must be finite authoring constants");
                        }
                        if (const auto* value = std::get_if<std::array<double, 2>>(&pin.value))
                        {
                            processor.x = static_cast<float>((*value)[0]);
                            processor.y = static_cast<float>((*value)[1]);
                        }
                        else
                        {
                            Error(node->id, "parameter", "Processor requires Vector2 parameters");
                        }
                    }
                }
                for (auto& recipe : recipes)
                {
                    if (recipes.size() > 1)
                    {
                        recipe.signalProcessors.push_back(processor);
                        if (!recipe.postCombineScope)
                        {
                            recipe.postCombineScope = node->id;
                        }
                    }
                    else
                    {
                        recipe.processors.push_back(processor);
                    }
                }
                return recipes;
            }

            Input::InputInteraction Interaction(const Node* node)
            {
                Input::InputInteraction interaction;
                if (!node)
                {
                    return interaction;
                }
                if (node->type.starts_with("Input.Hold"))
                {
                    interaction.kind = Input::InteractionKind::Hold;
                }
                else if (node->type.starts_with("Input.Tap"))
                {
                    interaction.kind = Input::InteractionKind::Tap;
                }
                else if (node->type == "Input.Chord")
                {
                    interaction.kind = Input::InteractionKind::Chord;
                }
                interaction.duration = Duration(*node, "duration");
                interaction.repeatInterval = Duration(*node, "repeat");
                interaction.pressThreshold = static_cast<float>(Number(*node, "press"));
                interaction.releaseThreshold = static_cast<float>(Number(*node, "release"));
                interaction.chordOrder = Choice<Input::ChordOrder>(*node, "order",
                    {{"Simultaneous", Input::ChordOrder::Simultaneous}, {"Sequential", Input::ChordOrder::Sequential}});
                interaction.claimChordControls = Boolean(*node, "claimChord");
                for (const Link* link : Incoming(*node, "Dependencies"))
                {
                    const Node* target = Upstream(*link);
                    if (!target || target->type != "Input.Signal.Button")
                    {
                        Error(node->id, "chord", "Chord dependencies must reference Button signal outputs");
                    }
                    else
                    {
                        interaction.chord.push_back(Signal(target->id));
                    }
                }
                return interaction;
            }

            void CompileSignal(const Node& node)
            {
                Input::InputSignalDefinition signal;
                signal.id = Signal(node.id);
                signal.name = Property(node, "name");
                Id layer = Integer<Id>(node, "layer");
                if (!layer && m_result.definition.layers.size() == 1)
                {
                    signal.layer = m_result.definition.layers.front().id;
                }
                else
                {
                    signal.layer = Layer(layer);
                }
                signal.type = node.type.ends_with("Button") ? Input::ValueType::Button
                            : node.type.ends_with("Vector2") ? Input::ValueType::Vector2 : Input::ValueType::Float;
                signal.domain = Choice<Input::Domain>(node, "domain", {{"Game", Input::Domain::Game}, {"UI", Input::Domain::UI}});
                signal.lifetime = Choice<Input::ValueLifetime>(node, "lifetime",
                    {{"Persistent", Input::ValueLifetime::Persistent}, {"Delta", Input::ValueLifetime::Delta}});
                signal.combine = Combine(node, "combine");
                signal.resumePersistentValue = Boolean(node, "resumePersistentValue");
                std::vector<Recipe> recipes;
                for (const Link* link : Incoming(node, "Value"))
                {
                    auto branch = Follow(*link, {node.id});
                    for (auto& recipe : branch)
                    {
                        if (branch.size() == 1)
                        {
                            recipe.identity = link->id;
                        }
                        recipes.push_back(std::move(recipe));
                    }
                }
                if (recipes.empty())
                {
                    Error(node.id, "binding", "Signal output requires a source binding");
                }
                const Node* interaction = recipes.empty() ? nullptr : recipes.front().interaction;
                const Node* gate = recipes.empty() ? nullptr : recipes.front().gate;
                signal.interaction = Interaction(interaction);
                signal.gate = gate ? Signal(gate->id) : Input::SignalID{};
                const Id postCombineScope = recipes.empty() ? 0 : recipes.front().postCombineScope;
                signal.processors = recipes.empty() ? std::vector<Input::InputProcessor>{}
                                                    : recipes.front().signalProcessors;
                for (const auto& recipe : recipes)
                {
                    const bool sameProcessors = recipe.signalProcessors.size() == signal.processors.size() &&
                        std::equal(recipe.signalProcessors.begin(), recipe.signalProcessors.end(), signal.processors.begin(),
                            [](const auto& left, const auto& right) {
                                return left.kind == right.kind && left.x == right.x && left.y == right.y;
                            });
                    if (recipe.postCombineScope != postCombineScope || !sameProcessors)
                    {
                        Error(node.id, "processor-order", "Post-combination processors must operate on the complete signal combination");
                    }
                    if (recipe.interaction != interaction || recipe.gate != gate)
                    {
                        Error(node.id, "policy", "All alternatives of a signal must share its interaction and gate");
                    }
                    if (recipe.combine && *recipe.combine != signal.combine)
                    {
                        Error(node.id, "combine", "Combine node and signal output policies must agree");
                    }
                    for (const auto& source : recipe.sources)
                    {
                        const bool delta = source.control.kind == Input::SourceKind::MouseDelta ||
                                           source.control.kind == Input::SourceKind::MouseWheel;
                        if (delta != (signal.lifetime == Input::ValueLifetime::Delta))
                        {
                            Error(node.id, "lifetime", "Persistent values and deltas cannot be mixed; set the output lifetime explicitly");
                        }
                    }
                    Input::InputBinding binding;
                    binding.id = {node.id, recipe.identity};
                    binding.signal = signal.id;
                    binding.priority = recipe.priority;
                    binding.sources = recipe.sources;
                    binding.processors = recipe.processors;
                    m_result.definition.bindings.push_back(std::move(binding));
                }
                m_result.definition.signals.push_back(std::move(signal));
            }

            const LXInputAsset& m_asset;
            LXInputCompileResult m_result;
            std::size_t m_expansionCount{};
        };
    }

    LXInputCompileResult CompileInputGraph(const LXInputAsset& asset, std::uint64_t generation)
    {
        return Compiler(asset).Run(generation);
    }
}
