#include "CookedInputGraph.h"
#include "../../Assets/AssetIdentityProfile.h"

#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace experiment::cooked
{
    namespace InputGraphWire
    {
        constexpr std::uint32_t kMagic = 0x47494543u; // CEIG
        constexpr std::uint32_t kMaxElements = 65536u;
        constexpr std::uint32_t kMaxNameBytes = 4096u;

        class Writer final
        {
        public:
            std::vector<std::byte> bytes;
            template<class T>
            void Number(T value)
            {
                using U = std::make_unsigned_t<T>;
                const auto bits = static_cast<U>(value);
                for (std::size_t index = 0; index < sizeof(T); ++index)
                {
                    bytes.push_back(static_cast<std::byte>((bits >> (index * 8u)) & 0xffu));
                }
                Check();
            }
            template<class T>
            void ID(T value)
            {
                Number(value.high);
                Number(value.low);
            }
            template<class T>
            void Enum(T value) { Number(static_cast<std::uint8_t>(value)); }
            void Float(float value) { Number(std::bit_cast<std::uint32_t>(value)); }
            void Count(std::size_t value)
            {
                if (value > kMaxElements)
                {
                    throw std::runtime_error("InputGraph array exceeds the format limit.");
                }
                Number(static_cast<std::uint32_t>(value));
            }
            void String(const std::string& value)
            {
                if (value.size() > kMaxNameBytes)
                {
                    throw std::runtime_error("InputGraph name exceeds the format limit.");
                }
                Count(value.size());
                for (const char ch : value)
                {
                    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
                }
                Check();
            }
            void Check() const
            {
                if (bytes.size() > kInputGraphMaxBytes)
                {
                    throw std::runtime_error("InputGraph artifact exceeds the byte limit.");
                }
            }
        };

        class Reader final
        {
        public:
            explicit Reader(std::span<const std::byte> bytes) : m_bytes(bytes) {}
            template<class T>
            T Number()
            {
                if (m_bytes.size() - m_offset < sizeof(T))
                {
                    throw std::runtime_error("InputGraph artifact is truncated.");
                }
                using U = std::make_unsigned_t<T>;
                U value{};
                for (std::size_t index = 0; index < sizeof(T); ++index)
                {
                    value |= static_cast<U>(std::to_integer<std::uint8_t>(m_bytes[m_offset++])) << (index * 8u);
                }
                return std::bit_cast<T>(value);
            }
            template<class T>
            T ID() { return { Number<std::uint64_t>(), Number<std::uint64_t>() }; }
            template<class T>
            T Enum(T last)
            {
                const auto value = Number<std::uint8_t>();
                if (value > static_cast<std::uint8_t>(last))
                {
                    throw std::runtime_error("InputGraph artifact contains an unsupported enum tag.");
                }
                return static_cast<T>(value);
            }
            bool Boolean()
            {
                const auto value = Number<std::uint8_t>();
                if (value > 1u)
                {
                    throw std::runtime_error("InputGraph artifact contains an invalid Boolean.");
                }
                return value != 0u;
            }
            float Float() { return std::bit_cast<float>(Number<std::uint32_t>()); }
            std::uint32_t Count(std::size_t minimumBytes)
            {
                const auto count = Number<std::uint32_t>();
                if (count > kMaxElements || count > (m_bytes.size() - m_offset) / minimumBytes)
                {
                    throw std::runtime_error("InputGraph artifact has an invalid array bound.");
                }
                return count;
            }
            std::string String()
            {
                const auto count = Count(1u);
                if (count > kMaxNameBytes)
                {
                    throw std::runtime_error("InputGraph name exceeds the format limit.");
                }
                std::string result(reinterpret_cast<const char*>(m_bytes.data() + m_offset), count);
                m_offset += count;
                return result;
            }
            bool AtEnd() const { return m_offset == m_bytes.size(); }

        private:
            std::span<const std::byte> m_bytes;
            std::size_t m_offset{};
        };
    }

    Input::GraphID InputGraphIdentity(const AssetId& asset) noexcept
    {
        Input::GraphID result;
        for (std::size_t index = 0; index < 8u; ++index)
        {
            result.high = (result.high << 8u) | asset.value.data[index];
            result.low = (result.low << 8u) | asset.value.data[index + 8u];
        }
        return result;
    }

    bool WriteInputGraphArtifact(const Input::InputGraphProgram& program,
        std::vector<std::byte>& bytes, std::string& failure)
    {
        try
        {
            const auto& graph = program.GetDefinition();
            InputGraphWire::Writer writer;
            writer.Number(InputGraphWire::kMagic);
            writer.Number(kInputGraphArtifactVersion);
            writer.Number(graph.schemaVersion);
            writer.Number(graph.compilerVersion);
            writer.Number(graph.abiVersion);
            writer.Number<std::uint32_t>(0u); // Required capabilities; unknown bits fail closed.
            writer.ID(graph.id);
            writer.Number(program.GetSemanticHash());
            // Generation is a publication property, not semantic/cooked identity.
            writer.Count(graph.layers.size());
            writer.Count(graph.signals.size());
            writer.Count(graph.bindings.size());
            for (const auto& layer : graph.layers)
            {
                writer.ID(layer.id);
                writer.String(layer.name);
                writer.Number(layer.priority);
                writer.Enum(layer.claim);
                writer.Number<std::uint8_t>(layer.enabled ? 1u : 0u);
            }
            for (const auto& signal : graph.signals)
            {
                writer.ID(signal.id);
                writer.ID(signal.layer);
                writer.String(signal.name);
                writer.Enum(signal.type);
                writer.Enum(signal.lifetime);
                writer.Enum(signal.domain);
                writer.Enum(signal.combine);
                writer.ID(signal.gate);
                writer.Number<std::uint8_t>(signal.resumePersistentValue ? 1u : 0u);
                const auto& interaction = signal.interaction;
                writer.Enum(interaction.kind);
                writer.Number(interaction.duration);
                writer.Number(interaction.repeatInterval);
                writer.Float(interaction.pressThreshold);
                writer.Float(interaction.releaseThreshold);
                writer.Enum(interaction.chordOrder);
                writer.Number<std::uint8_t>(interaction.claimChordControls ? 1u : 0u);
                writer.Count(interaction.chord.size());
                for (const auto id : interaction.chord)
                {
                    writer.ID(id);
                }
                writer.Count(signal.processors.size());
                for (const auto& processor : signal.processors)
                {
                    writer.Enum(processor.kind);
                    writer.Float(processor.x);
                    writer.Float(processor.y);
                }
            }
            for (const auto& binding : graph.bindings)
            {
                writer.ID(binding.id);
                writer.ID(binding.signal);
                writer.Number(binding.priority);
                writer.Count(binding.sources.size());
                for (const auto& source : binding.sources)
                {
                    writer.Enum(source.control.kind);
                    writer.Number(source.control.code);
                    writer.Float(source.scaleX);
                    writer.Float(source.scaleY);
                    writer.Enum(source.space);
                }
                writer.Count(binding.processors.size());
                for (const auto& processor : binding.processors)
                {
                    writer.Enum(processor.kind);
                    writer.Float(processor.x);
                    writer.Float(processor.y);
                }
            }
            bytes = std::move(writer.bytes);
            failure.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return false;
        }
    }

    bool ReadInputGraphArtifact(std::span<const std::byte> bytes, const AssetId& expectedAsset,
        own::shared_owner<const Input::InputGraphProgram>& result, std::string& failure, std::uint64_t generation)
    {
        using namespace Input;
        try
        {
            if (bytes.size() > kInputGraphMaxBytes ||
                (!IsAssetIdV4(expectedAsset) && !assets::IsUuidV8(expectedAsset.value)))
            {
                throw std::runtime_error("InputGraph artifact size or expected identity is invalid.");
            }
            InputGraphWire::Reader reader(bytes);
            if (reader.Number<std::uint32_t>() != InputGraphWire::kMagic ||
                reader.Number<std::uint32_t>() != kInputGraphArtifactVersion)
            {
                throw std::runtime_error("Unsupported InputGraph artifact format; recook this asset.");
            }
            InputGraph graph;
            graph.generation = generation;
            graph.schemaVersion = reader.Number<std::uint32_t>();
            graph.compilerVersion = reader.Number<std::uint32_t>();
            graph.abiVersion = reader.Number<std::uint32_t>();
            if (graph.schemaVersion != kInputSchemaVersion || graph.compilerVersion != kInputCompilerVersion ||
                graph.abiVersion != kInputABIVersion || reader.Number<std::uint32_t>() != 0u)
            {
                throw std::runtime_error("InputGraph schema/compiler/API/capability mismatch; recook this asset.");
            }
            graph.id = reader.ID<GraphID>();
            const auto semanticHash = reader.Number<std::uint64_t>();
            if (graph.id != InputGraphIdentity(expectedAsset))
            {
                throw std::runtime_error("InputGraph artifact identity differs from its AssetDepot link.");
            }
            const auto layers = reader.Count(26u);
            const auto signals = reader.Count(83u);
            const auto bindings = reader.Count(44u);
            for (std::uint32_t index = 0; index < layers; ++index)
            {
                InputLayer layer;
                layer.id = reader.ID<LayerID>();
                layer.name = reader.String();
                layer.priority = reader.Number<std::int32_t>();
                layer.claim = reader.Enum(ClaimPolicy::PassThrough);
                layer.enabled = reader.Boolean();
                graph.layers.push_back(std::move(layer));
            }
            for (std::uint32_t index = 0; index < signals; ++index)
            {
                InputSignalDefinition signal;
                signal.id = reader.ID<SignalID>();
                signal.layer = reader.ID<LayerID>();
                signal.name = reader.String();
                signal.type = reader.Enum(ValueType::Vector2);
                signal.lifetime = reader.Enum(ValueLifetime::Delta);
                signal.domain = reader.Enum(Domain::UI);
                signal.combine = reader.Enum(CombinePolicy::Priority);
                signal.gate = reader.ID<SignalID>();
                signal.resumePersistentValue = reader.Boolean();
                signal.interaction.kind = reader.Enum(InteractionKind::Chord);
                signal.interaction.duration = reader.Number<Timestamp>();
                signal.interaction.repeatInterval = reader.Number<Timestamp>();
                signal.interaction.pressThreshold = reader.Float();
                signal.interaction.releaseThreshold = reader.Float();
                signal.interaction.chordOrder = reader.Enum(ChordOrder::Sequential);
                signal.interaction.claimChordControls = reader.Boolean();
                const auto chords = reader.Count(16u);
                for (std::uint32_t chord = 0; chord < chords; ++chord)
                {
                    signal.interaction.chord.push_back(reader.ID<SignalID>());
                }
                const auto processors = reader.Count(9u);
                for (std::uint32_t processorIndex = 0; processorIndex < processors; ++processorIndex)
                {
                    InputProcessor processor;
                    processor.kind = reader.Enum(ProcessorKind::Clamp);
                    processor.x = reader.Float();
                    processor.y = reader.Float();
                    signal.processors.push_back(processor);
                }
                graph.signals.push_back(std::move(signal));
            }
            for (std::uint32_t index = 0; index < bindings; ++index)
            {
                InputBinding binding;
                binding.id = reader.ID<BindingID>();
                binding.signal = reader.ID<SignalID>();
                binding.priority = reader.Number<std::int32_t>();
                const auto sources = reader.Count(14u);
                for (std::uint32_t sourceIndex = 0; sourceIndex < sources; ++sourceIndex)
                {
                    InputSource source;
                    source.control.kind = reader.Enum(SourceKind::GamepadAxis);
                    source.control.code = reader.Number<std::uint32_t>();
                    source.scaleX = reader.Float();
                    source.scaleY = reader.Float();
                    source.space = reader.Enum(CoordinateSpace::Normalized);
                    binding.sources.push_back(source);
                }
                const auto processors = reader.Count(9u);
                for (std::uint32_t processorIndex = 0; processorIndex < processors; ++processorIndex)
                {
                    InputProcessor processor;
                    processor.kind = reader.Enum(ProcessorKind::Clamp);
                    processor.x = reader.Float();
                    processor.y = reader.Float();
                    binding.processors.push_back(processor);
                }
                graph.bindings.push_back(std::move(binding));
            }
            if (!reader.AtEnd())
            {
                throw std::runtime_error("InputGraph artifact has trailing bytes.");
            }
            std::vector<InputDiagnostic> diagnostics;
            auto candidate = CompileInputGraph(graph, diagnostics);
            if (!candidate || candidate->GetSemanticHash() != semanticHash)
            {
                throw std::runtime_error(diagnostics.empty() ? "InputGraph semantic hash mismatch." : diagnostics.front().message);
            }
            result = std::move(candidate);
            failure.clear();
            return true;
        }
        catch (const std::exception& error)
        {
            failure = error.what();
            return false;
        }
    }
}
