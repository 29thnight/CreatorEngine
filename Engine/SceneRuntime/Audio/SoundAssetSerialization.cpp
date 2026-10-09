#include "SoundAssetSerialization.h"

#include "../../Utility_Framework/AuthoringCookedDocument.h"
#include "../../Utility_Framework/AuthoringParsedDocument.h"
#include "../../Utility_Framework/AuthoringWriteNode.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace wave
{
    namespace
    {
        using Authoring::ReadNode;
        using Authoring::WriteNode;
        constexpr std::size_t kMaximumDocumentBytes = 4u * 1024u * 1024u;
        constexpr const char* kNodeNames[]{ "Clip", "Random", "Switch", "Layer", "GainPitch", "Parameter", "Output" };
        constexpr const char* kTypeNames[]{ "Boolean", "Integer", "Float", "String" };
        constexpr const char* kSourceNames[]{ "Clip", "Preset", "Graph" };

        void Require(bool valid, const char* message)
        {
            if (!valid)
            {
                throw std::runtime_error(message);
            }
        }

        void CheckFields(const ReadNode& node, std::initializer_list<std::string_view> allowed)
        {
            Require(node.IsMap(), "Expected a sound asset object");
            std::unordered_set<std::string> seen;
            for (const auto item : node.Map())
            {
                const auto name = item.key.AsString();
                Require(std::ranges::find(allowed, name) != allowed.end(), "Unknown sound asset field");
                Require(seen.insert(name).second, "Duplicate sound asset field");
            }
        }

        template<std::size_t Size>
        std::size_t EnumIndex(const ReadNode& node, const char* const (&names)[Size])
        {
            for (std::size_t index = 0; index < Size; ++index)
            {
                if (node.Scalar() == names[index])
                {
                    return index;
                }
            }
            throw std::runtime_error("Unknown sound asset enum value");
        }

        ClipKey ReadKey(const ReadNode& node, bool optional = false)
        {
            if (!node && optional)
            {
                return {};
            }
            Require(node.IsScalar(), "Sound asset reference must be a scalar GUID or empty draft");
            if (optional && node.Scalar().empty())
            {
                return {};
            }
            Uuid::Uuid16 id;
            Require(node.IsScalar() && Uuid::TryParse(node.Scalar(), id)
                && !id.IsNil() && Uuid::ToString(id) == node.Scalar()
                && (id.data[6] & 0xf0u) == 0x40u && (id.data[8] & 0xc0u) == 0x80u,
                "Sound asset reference must be a canonical UUIDv4");
            return ClipKey::FromGuid(id);
        }

        float Finite(const ReadNode& node)
        {
            const float value = node.As<float>();
            Require(std::isfinite(value), "Sound asset float is not finite");
            return value;
        }

        ParameterValue ReadValue(const ReadNode& node)
        {
            CheckFields(node, { "name", "type", "value" });
            Require(node["value"].IsScalar(), "Expected a typed parameter value");
            switch (EnumIndex(node["type"], kTypeNames))
            {
            case 0: return node["value"].As<bool>();
            case 1: return node["value"].As<std::int32_t>();
            case 2: return Finite(node["value"]);
            case 3: return node["value"].AsString();
            default: throw std::runtime_error("Unknown parameter type");
            }
        }

        void WriteValue(WriteNode node, const ParameterValue& value)
        {
            node.SetMap();
            node.Child("type").SetScalar(kTypeNames[static_cast<std::size_t>(TypeOfParameter(value))]);
            std::visit([&](const auto& scalar)
            {
                if constexpr (std::is_same_v<std::decay_t<decltype(scalar)>, std::string>)
                {
                    node.Child("value").SetString(scalar);
                }
                else
                {
                    node.Child("value").SetScalar(scalar);
                }
            }, value);
        }

        void WriteParameters(WriteNode node, const ParameterMap& values)
        {
            node.SetMap();
            std::vector<std::string> names;
            for (const auto& [name, value] : values)
            {
                names.push_back(name);
            }
            std::ranges::sort(names);
            for (const auto& name : names)
            {
                WriteValue(node.Child(name), values.at(name));
            }
        }

        SoundGraphDefinition ReadGraphRoot(const ReadNode& root)
        {
            Require(root.IsMap() && root["assetType"].Scalar() == "SoundGraph"
                && root["schemaVersion"].As<std::uint32_t>() == 1u, "Unsupported SoundGraph schema");
            CheckFields(root, { "assetType", "schemaVersion", "output", "maximumVoices", "parameters", "nodes" });
            SoundGraphDefinition graph;
            graph.output = root["output"].As<std::uint32_t>();
            graph.maximumVoices = root["maximumVoices"].As<std::uint32_t>();
            const auto parameters = root["parameters"];
            Require(parameters.IsSequence() && parameters.Size() <= 256u, "Invalid SoundGraph parameter list");
            for (const auto parameter : parameters)
            {
                ParameterDefinition definition;
                definition.name = parameter["name"].AsStringChecked();
                definition.defaultValue = ReadValue(parameter);
                definition.type = TypeOfParameter(definition.defaultValue);
                graph.parameters.push_back(std::move(definition));
            }
            const auto nodes = root["nodes"];
            Require(nodes.IsSequence() && nodes.Size() <= 4096u, "Invalid SoundGraph node list");
            for (const auto item : nodes)
            {
                CheckFields(item, { "id", "kind", "inputs", "clip", "gain", "pitch", "parameter",
                    "gainParameter", "pitchParameter", "weights", "cases" });
                SoundNode node;
                node.id = item["id"].As<std::uint32_t>();
                node.kind = static_cast<SoundNodeKind>(EnumIndex(item["kind"], kNodeNames));
                const auto inputs = item["inputs"];
                Require(inputs.IsSequence() && inputs.Size() <= 4096u, "Invalid SoundGraph input list");
                for (const auto input : inputs)
                {
                    node.inputs.push_back(input.As<std::uint32_t>());
                }
                node.clip = ReadKey(item["clip"], true);
                node.gain = item["gain"] ? Finite(item["gain"]) : 1.0f;
                node.pitch = item["pitch"] ? Finite(item["pitch"]) : 1.0f;
                node.parameter = item["parameter"].AsString();
                node.gainParameter = item["gainParameter"].AsString();
                node.pitchParameter = item["pitchParameter"].AsString();
                if (const auto weights = item["weights"])
                {
                    Require(weights.IsSequence() && weights.Size() <= 4096u, "Invalid Random weights");
                    for (const auto weight : weights)
                    {
                        node.weights.push_back(Finite(weight));
                    }
                }
                if (const auto cases = item["cases"])
                {
                    Require(cases.IsSequence() && cases.Size() <= 4096u, "Invalid Switch cases");
                    for (const auto value : cases)
                    {
                        node.cases.push_back(ReadValue(value));
                    }
                }
                graph.nodes.push_back(std::move(node));
            }
            return graph;
        }

        Authoring::WriteDocument GraphDocument(const SoundGraphDefinition& graph)
        {
            Authoring::WriteDocument document;
            const auto root = document.Root();
            root.SetMap();
            root.Child("assetType").SetScalar("SoundGraph");
            root.Child("schemaVersion").SetScalar(graph.schemaVersion);
            root.Child("output").SetScalar(graph.output);
            root.Child("maximumVoices").SetScalar(graph.maximumVoices);
            const auto parameters = root.Child("parameters");
            parameters.SetSequence();
            for (const auto& parameter : graph.parameters)
            {
                Require(parameter.type == TypeOfParameter(parameter.defaultValue), "Parameter declaration/default type mismatch");
                const auto item = parameters.Append();
                WriteValue(item, parameter.defaultValue);
                item.Child("name").SetString(parameter.name);
            }
            const auto nodes = root.Child("nodes");
            nodes.SetSequence();
            for (const auto& node : graph.nodes)
            {
                Require(static_cast<std::size_t>(node.kind) < std::size(kNodeNames), "Unknown SoundGraph node kind");
                const auto item = nodes.Append();
                item.Child("id").SetScalar(node.id);
                item.Child("kind").SetScalar(kNodeNames[static_cast<std::size_t>(node.kind)]);
                const auto inputs = item.Child("inputs");
                inputs.SetSequence(true);
                for (const auto input : node.inputs)
                {
                    inputs.Append().SetScalar(input);
                }
                item.Child("clip").SetString(node.clip.Text());
                item.Child("gain").SetScalar(node.gain);
                item.Child("pitch").SetScalar(node.pitch);
                item.Child("parameter").SetString(node.parameter);
                item.Child("gainParameter").SetString(node.gainParameter);
                item.Child("pitchParameter").SetString(node.pitchParameter);
                const auto weights = item.Child("weights");
                weights.SetSequence(true);
                for (const float weight : node.weights)
                {
                    weights.Append().SetScalar(weight);
                }
                const auto cases = item.Child("cases");
                cases.SetSequence();
                for (const auto& value : node.cases)
                {
                    WriteValue(cases.Append(), value);
                }
            }
            return document;
        }

        void ReadSettings(const ReadNode& node, PlayRequest& settings)
        {
            CheckFields(node, { "volume", "pitch", "priority", "bus", "loop", "spatialBlend", "minimumDistance",
                "maximumDistance", "rolloff", "concurrencyGroup", "preemptSameClip", "persistent", "allowVirtualization",
                "useReverbSend", "reverbSendDecibels", "reverbBus", "customRolloff" });
            settings.volume = Finite(node["volume"]);
            settings.pitch = Finite(node["pitch"]);
            settings.priority = node["priority"].As<int>();
            settings.bus.value = node["bus"].As<std::uint16_t>();
            settings.loop = node["loop"].As<bool>();
            settings.spatialBlend = Finite(node["spatialBlend"]);
            settings.minimumDistance = Finite(node["minimumDistance"]);
            settings.maximumDistance = Finite(node["maximumDistance"]);
            const auto rolloff = node["rolloff"].As<std::uint32_t>();
            Require(rolloff <= 2u, "Invalid SoundPreset rolloff");
            settings.rolloff = static_cast<RolloffKind>(rolloff);
            settings.concurrencyGroup.value = node["concurrencyGroup"].As<std::uint32_t>();
            settings.preemptSameClip = node["preemptSameClip"].As<bool>();
            settings.persistent = node["persistent"].As<bool>();
            settings.allowVirtualization = node["allowVirtualization"].As<bool>();
            settings.useReverbSend = node["useReverbSend"].As<bool>();
            settings.reverbSendDecibels = Finite(node["reverbSendDecibels"]);
            settings.reverbBus.value = node["reverbBus"].As<std::uint16_t>();
            Require(settings.volume >= 0.0f && settings.pitch > 0.0f
                && settings.spatialBlend >= 0.0f && settings.spatialBlend <= 1.0f
                && settings.minimumDistance > 0.0f && settings.maximumDistance >= settings.minimumDistance
                && settings.priority >= 0 && settings.priority <= 256 && settings.bus.IsValid(),
                "SoundPreset settings are out of range");
            const auto curve = node["customRolloff"];
            Require(curve.IsSequence() && curve.Size() <= 1024u, "Invalid custom rolloff curve");
            for (const auto point : curve)
            {
                CheckFields(point, { "distance", "gain" });
                RolloffPoint value{ Finite(point["distance"]), Finite(point["gain"]) };
                Require(value.distance >= 0.0f && value.gain >= 0.0f, "Invalid custom rolloff point");
                settings.customRolloff.push_back(value);
            }
        }

        void WriteSettings(WriteNode node, const PlayRequest& settings)
        {
            node.SetMap();
            node.Child("volume").SetScalar(settings.volume);
            node.Child("pitch").SetScalar(settings.pitch);
            node.Child("priority").SetScalar(settings.priority);
            node.Child("bus").SetScalar(settings.bus.value);
            node.Child("loop").SetScalar(settings.loop);
            node.Child("spatialBlend").SetScalar(settings.spatialBlend);
            node.Child("minimumDistance").SetScalar(settings.minimumDistance);
            node.Child("maximumDistance").SetScalar(settings.maximumDistance);
            node.Child("rolloff").SetScalar(static_cast<std::uint32_t>(settings.rolloff));
            node.Child("concurrencyGroup").SetScalar(settings.concurrencyGroup.value);
            node.Child("preemptSameClip").SetScalar(settings.preemptSameClip);
            node.Child("persistent").SetScalar(settings.persistent);
            node.Child("allowVirtualization").SetScalar(settings.allowVirtualization);
            node.Child("useReverbSend").SetScalar(settings.useReverbSend);
            node.Child("reverbSendDecibels").SetScalar(settings.reverbSendDecibels);
            node.Child("reverbBus").SetScalar(settings.reverbBus.value);
            const auto curve = node.Child("customRolloff");
            curve.SetSequence();
            for (const auto& point : settings.customRolloff)
            {
                const auto item = curve.Append();
                item.Child("distance").SetScalar(point.distance);
                item.Child("gain").SetScalar(point.gain);
            }
        }

        SoundPreset ReadPresetRoot(const ReadNode& root)
        {
            Require(root.IsMap() && root["assetType"].Scalar() == "SoundPreset"
                && root["schemaVersion"].As<std::uint32_t>() == 1u, "Unsupported SoundPreset schema");
            CheckFields(root, { "assetType", "schemaVersion", "source", "settings", "parameters" });
            CheckFields(root["source"], { "kind", "asset" });
            SoundPreset preset;
            preset.source.kind = static_cast<SoundSourceKind>(EnumIndex(root["source"]["kind"], kSourceNames));
            Require(preset.source.kind != SoundSourceKind::Preset, "Nested SoundPresets are not supported");
            preset.source.asset = ReadKey(root["source"]["asset"], true);
            ReadSettings(root["settings"], preset.defaults);
            const auto parameters = root["parameters"];
            Require(parameters.IsMap() && parameters.Size() <= 256u, "Invalid SoundPreset parameter map");
            for (const auto item : parameters.Map())
            {
                Require(preset.parameters.emplace(item.key.AsString(), ReadValue(item.value)).second,
                    "Duplicate SoundPreset parameter");
            }
            return preset;
        }

        Authoring::WriteDocument PresetDocument(const SoundPreset& preset)
        {
            Require(static_cast<std::size_t>(preset.source.kind) < std::size(kSourceNames), "Unknown SoundPreset source kind");
            Authoring::WriteDocument document;
            const auto root = document.Root();
            root.Child("assetType").SetScalar("SoundPreset");
            root.Child("schemaVersion").SetScalar(1u);
            root.Child("source").Child("kind").SetScalar(kSourceNames[static_cast<std::size_t>(preset.source.kind)]);
            root.Child("source").Child("asset").SetString(preset.source.asset.Text());
            WriteSettings(root.Child("settings"), preset.defaults);
            WriteParameters(root.Child("parameters"), preset.parameters);
            return document;
        }

        template<class Value, class Reader>
        bool ReadDocument(const Authoring::ParsedDocument& document, Value& out, std::string& error, Reader reader)
        {
            if (!document)
            {
                return false;
            }
            try
            {
                auto value = reader(document.Root());
                out = std::move(value);
                error.clear();
                return true;
            }
            catch (const std::exception& exception)
            {
                error = exception.what();
                return false;
            }
        }
    }

    bool ReadSoundGraph(std::string_view text, SoundGraphDefinition& out, std::string& error)
    {
        if (text.size() > kMaximumDocumentBytes)
        {
            error = "SoundGraph exceeds document size limit";
            return false;
        }
        return ReadDocument(Authoring::ParsedDocument::ParseText(std::string(text), error), out, error, ReadGraphRoot);
    }

    std::string WriteSoundGraph(const SoundGraphDefinition& graph)
    {
        return GraphDocument(graph).Dump();
    }

    bool ReadSoundPreset(std::string_view text, SoundPreset& out, std::string& error)
    {
        if (text.size() > kMaximumDocumentBytes)
        {
            error = "SoundPreset exceeds document size limit";
            return false;
        }
        return ReadDocument(Authoring::ParsedDocument::ParseText(std::string(text), error), out, error, ReadPresetRoot);
    }

    std::string WriteSoundPreset(const SoundPreset& preset)
    {
        return PresetDocument(preset).Dump();
    }

    bool CookSoundGraph(const SoundGraphDefinition& graph, std::vector<std::byte>& out, std::string& error)
    {
        if (!CompileSoundGraph(graph, [](const ClipKey& key) { return key.IsGuid(); }, error))
        {
            return false;
        }
        try
        {
            const auto document = GraphDocument(graph);
            (void)ReadGraphRoot(document.Root());
            if (!Authoring::EncodeCookedDocument(document.Root(), out, error))
            {
                return false;
            }
            if (out.size() > kMaximumDocumentBytes)
            {
                out.clear();
                error = "Cooked sound asset exceeds 4 MiB size limit";
                return false;
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool CookSoundPreset(const SoundPreset& preset, std::vector<std::byte>& out, std::string& error)
    {
        if (!preset.source.asset.IsGuid() || preset.source.kind == SoundSourceKind::Preset)
        {
            error = "Cooked SoundPreset requires a GUID clip or graph source";
            return false;
        }
        try
        {
            const auto document = PresetDocument(preset);
            // Validate the exact serialized shape without entering the text parser.
            // Runtime-only transforms/owners are intentionally not preset defaults.
            (void)ReadPresetRoot(document.Root());
            if (!Authoring::EncodeCookedDocument(document.Root(), out, error))
            {
                return false;
            }
            if (out.size() > kMaximumDocumentBytes)
            {
                out.clear();
                error = "Cooked sound asset exceeds 4 MiB size limit";
                return false;
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
            return false;
        }
    }

    bool ReadCookedSoundGraph(std::span<const std::byte> bytes, SoundGraphDefinition& out, std::string& error)
    {
        if (bytes.size() > kMaximumDocumentBytes)
        {
            error = "Cooked SoundGraph exceeds size limit";
            return false;
        }
        SoundGraphDefinition graph;
        if (!ReadDocument(Authoring::ParsedDocument::ParseCooked(bytes, error), graph, error, ReadGraphRoot)
            || !CompileSoundGraph(graph, [](const ClipKey& key) { return key.IsGuid(); }, error))
        {
            return false;
        }
        out = std::move(graph);
        return true;
    }

    bool ReadCookedSoundPreset(std::span<const std::byte> bytes, SoundPreset& out, std::string& error)
    {
        if (bytes.size() > kMaximumDocumentBytes)
        {
            error = "Cooked SoundPreset exceeds size limit";
            return false;
        }
        SoundPreset preset;
        if (!ReadDocument(Authoring::ParsedDocument::ParseCooked(bytes, error), preset, error, ReadPresetRoot))
        {
            return false;
        }
        if (!preset.source.asset.IsGuid())
        {
            error = "Cooked SoundPreset source is empty";
            return false;
        }
        out = std::move(preset);
        return true;
    }
}
