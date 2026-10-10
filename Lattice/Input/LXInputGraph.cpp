#include "LXInputGraph.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <Windows.h>

namespace LX
{
    namespace
    {
        constexpr std::size_t kMaxArchiveBytes = 16 * 1024 * 1024;
        std::atomic_uint64_t saveSequence{};

        Pin Socket(const char* name, Direction direction, PinType type, bool multiple = false)
        {
            return {0, 0, name, direction, type, multiple, false, name, {}};
        }

        void Register(LXNodeDefinitionRegistry& registry, std::string type, std::string title,
                      std::vector<Pin> pins, std::map<std::string, std::string> properties = {})
        {
            std::string error;
            if (!registry.Register({"input", {std::move(type), std::move(title), std::move(pins),
                                             std::move(properties), {}}}, &error))
            {
                throw std::logic_error(error);
            }
        }

        bool Fail(std::string* error, const char* message)
        {
            if (error)
            {
                *error = message;
            }
            return false;
        }
    }

    const std::vector<std::string>& InputNodeTypes()
    {
        static const std::vector<std::string> types{
            "Input.Key", "Input.MouseButton", "Input.MouseDelta", "Input.PointerPosition", "Input.MouseWheel",
            "Input.GamepadButton", "Input.GamepadAxis", "Input.GamepadStick", "Input.Axis2D", "Input.Deadzone.Float",
            "Input.Deadzone.Vector2", "Input.Normalize.Vector2", "Input.Scale.Float", "Input.Scale.Vector2",
            "Input.Invert.Float", "Input.Invert.Vector2", "Input.Clamp.Float", "Input.Clamp.Vector2",
            "Input.Press", "Input.Hold", "Input.Tap", "Input.Chord", "Input.Press.Analog", "Input.Hold.Analog",
            "Input.Tap.Analog", "Input.Gate.Button", "Input.Gate.Float",
            "Input.Gate.Vector2", "Input.Combine.Button", "Input.Combine.Float", "Input.Combine.Vector2",
            "Input.Priority.Button", "Input.Priority.Float", "Input.Priority.Vector2",
            "Input.Signal.Button", "Input.Signal.Float", "Input.Signal.Vector2", "Input.Layer"};
        return types;
    }

    std::shared_ptr<const LXNodeDefinitionRegistry> CreateInputDefinitions()
    {
        auto registry = std::make_shared<LXNodeDefinitionRegistry>();
        registry->RegisterDomain("input", {PinType::Bool, PinType::Float, PinType::Vector2});
        for (const auto& type : {"Key", "MouseButton", "GamepadButton"})
        {
            Register(*registry, std::string("Input.") + type, type,
                     {Socket("Value", Direction::Output, PinType::Bool)}, {{"control", "0"}});
        }
        Register(*registry, "Input.GamepadAxis", "Gamepad Axis", {Socket("Value", Direction::Output, PinType::Float)},
                 {{"control", "2"}, {"space", "Normalized"}});
        Register(*registry, "Input.GamepadStick", "Gamepad Stick", {Socket("Value", Direction::Output, PinType::Vector2)},
                 {{"control", "0"}, {"space", "Normalized"}});
        Register(*registry, "Input.MouseWheel", "Mouse Wheel", {Socket("Value", Direction::Output, PinType::Float)},
                 {{"control", "0"}, {"space", "RelativeCounts"}});
        Register(*registry, "Input.MouseDelta", "Mouse Delta", {Socket("Value", Direction::Output, PinType::Vector2)},
                 {{"control", "0"}, {"space", "RelativeCounts"}});
        Register(*registry, "Input.PointerPosition", "Pointer Position",
                 {Socket("Value", Direction::Output, PinType::Vector2)}, {{"control", "0"}, {"space", "ClientPixels"}});
        Register(*registry, "Input.Axis2D", "Axis 2D",
                 {Socket("Left", Direction::Input, PinType::Bool), Socket("Right", Direction::Input, PinType::Bool),
                  Socket("Down", Direction::Input, PinType::Bool), Socket("Up", Direction::Input, PinType::Bool),
                  Socket("Value", Direction::Output, PinType::Vector2)});
        for (const auto& [suffix, pinType] : std::vector<std::pair<std::string, PinType>>{
                 {"Float", PinType::Float}, {"Vector2", PinType::Vector2}})
        {
            for (const auto& kind : {"Deadzone", "Scale", "Invert", "Clamp", "Normalize"})
            {
                if (suffix == "Float" && std::string_view(kind) == "Normalize")
                {
                    continue;
                }
                Pin parameter = Socket("Parameters", Direction::Input, PinType::Vector2);
                parameter.value = std::array<double, 2>{1.0, 1.0};
                if (std::string_view(kind) == "Deadzone")
                {
                    parameter.value = std::array<double, 2>{0.15, 1.0};
                }
                else if (std::string_view(kind) == "Clamp")
                {
                    parameter.value = std::array<double, 2>{-1.0, 1.0};
                }
                Register(*registry, "Input." + std::string(kind) + "." + suffix, std::string(kind) + " " + suffix,
                         {Socket("Value", Direction::Input, pinType), parameter,
                          Socket("Result", Direction::Output, pinType)});
            }
        }
        for (const auto& kind : {"Press", "Hold", "Tap", "Chord"})
        {
            std::vector<Pin> pins{Socket("Value", Direction::Input, PinType::Bool)};
            if (std::string_view(kind) == "Chord")
            {
                pins.push_back(Socket("Dependencies", Direction::Input, PinType::Bool, true));
            }
            pins.push_back(Socket("Result", Direction::Output, PinType::Bool));
            Register(*registry, std::string("Input.") + kind, kind, std::move(pins),
                     {{"duration", "0.2"}, {"repeat", "0"}, {"press", "0.5"}, {"release", "0.25"},
                      {"order", "Simultaneous"}, {"claimChord", "true"}});
        }
        for (const auto& kind : {"Press", "Hold", "Tap"})
        {
            Register(*registry, std::string("Input.") + kind + ".Analog", std::string(kind) + " Analog",
                     {Socket("Value", Direction::Input, PinType::Float), Socket("Result", Direction::Output, PinType::Bool)},
                     {{"duration", "0.2"}, {"repeat", "0"}, {"press", "0.5"}, {"release", "0.25"},
                      {"order", "Simultaneous"}, {"claimChord", "true"}});
        }
        for (const auto& [suffix, pinType] : std::vector<std::pair<std::string, PinType>>{
                 {"Button", PinType::Bool}, {"Float", PinType::Float}, {"Vector2", PinType::Vector2}})
        {
            Register(*registry, "Input.Gate." + suffix, "Gate " + suffix,
                     {Socket("Value", Direction::Input, pinType), Socket("Enable", Direction::Input, PinType::Bool),
                      Socket("Result", Direction::Output, pinType)});
            Register(*registry, "Input.Combine." + suffix, "Combine " + suffix,
                     {Socket("Values", Direction::Input, pinType, true), Socket("Result", Direction::Output, pinType)},
                     {{"policy", "MaximumMagnitude"}});
            Register(*registry, "Input.Priority." + suffix, "Binding Priority " + suffix,
                     {Socket("Value", Direction::Input, pinType), Socket("Result", Direction::Output, pinType)},
                     {{"priority", "0"}});
            Register(*registry, "Input.Signal." + suffix, "Signal " + suffix,
                     {Socket("Value", Direction::Input, pinType, true), Socket("Signal", Direction::Output, pinType)},
                     {{"name", "Signal"}, {"layer", "0"}, {"domain", "Game"}, {"lifetime", "Persistent"},
                      {"combine", "MaximumMagnitude"}, {"resumePersistentValue", "false"}});
        }
        Register(*registry, "Input.Layer", "Input Layer", {},
                 {{"name", "Gameplay"}, {"priority", "0"}, {"claim", "PassThrough"}, {"enabled", "true"}});
        return registry;
    }

    LXInputAsset::LXInputAsset() : graph("input", CreateInputDefinitions())
    {
    }

    std::string LXInputAsset::SemanticIdentity() const
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::quoted(graphId) << ' ' << LXInputArchive::SchemaVersion << '\n';
        std::vector<const Node*> nodes;
        for (const auto& node : graph.Nodes())
        {
            nodes.push_back(&node);
        }
        std::sort(nodes.begin(), nodes.end(), [](const Node* left, const Node* right) { return left->id < right->id; });
        for (const Node* node : nodes)
        {
            out << node->id << ' ' << std::quoted(node->type) << '\n';
            for (const auto& [key, value] : node->properties)
            {
                out << std::quoted(key) << ' ' << std::quoted(value) << '\n';
            }
            for (const auto& pin : node->pins)
            {
                out << pin.id << ' ' << static_cast<unsigned>(pin.type) << ' ' << pin.value.index() << ' ';
                std::visit([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::string>)
                    {
                        out << std::quoted(value);
                    }
                    else if constexpr (std::is_arithmetic_v<T>)
                    {
                        out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
                    }
                    else if constexpr (!std::is_same_v<T, std::monostate>)
                    {
                        for (double number : value)
                        {
                            out << std::setprecision(std::numeric_limits<double>::max_digits10) << number << ' ';
                        }
                    }
                }, pin.value);
                out << '\n';
            }
        }
        std::vector<Link> links = graph.Links();
        std::sort(links.begin(), links.end(), [](const Link& left, const Link& right) { return left.id < right.id; });
        for (const auto& link : links)
        {
            out << link.id << ' ' << link.output << ' ' << link.input << '\n';
        }
        return out.str();
    }

    std::string LXInputArchive::Write(const LXInputAsset& asset)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "LXINPUT " << SchemaVersion << ' ' << std::quoted(asset.graphId) << '\n';
        asset.graph.Write(out);
        return out.str();
    }

    std::optional<LXInputAsset> LXInputArchive::Read(std::string_view text, std::string* error)
    {
        if (text.size() > kMaxArchiveBytes)
        {
            Fail(error, "InputGraph archive exceeds the size limit");
            return std::nullopt;
        }
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        std::string magic, identity;
        std::uint32_t version{};
        if (!(in >> magic >> version >> std::quoted(identity)) || magic != "LXINPUT" || version != SchemaVersion)
        {
            Fail(error, "Unsupported InputGraph source schema; legacy input maps require explicit migration");
            return std::nullopt;
        }
        auto graph = LXGraph::LoadStream(in, error, CreateInputDefinitions(), 0);
        if (!graph || graph->Domain() != "input")
        {
            Fail(error, "Invalid input domain graph");
            return std::nullopt;
        }
        LXInputAsset result;
        result.graphId = std::move(identity);
        result.graph = std::move(*graph);
        return result;
    }

    std::optional<LXInputAsset> LXInputAsset::Load(const std::filesystem::path& path, std::string* error)
    {
        std::error_code code;
        const auto size = std::filesystem::file_size(path, code);
        if (code || size > kMaxArchiveBytes)
        {
            Fail(error, "Cannot read InputGraph source or source exceeds the size limit");
            return std::nullopt;
        }
        std::ifstream stream(path, std::ios::binary);
        std::string text(std::istreambuf_iterator<char>(stream), {});
        if (!stream || stream.bad())
        {
            Fail(error, "Cannot read InputGraph source");
            return std::nullopt;
        }
        return LXInputArchive::Read(text, error);
    }

    bool LXInputAsset::Save(const std::filesystem::path& path, std::string* error) const
    {
        const std::string text = LXInputArchive::Write(*this);
        auto staged = path;
        staged += ".tmp." + std::to_string(GetCurrentProcessId()) + "." + std::to_string(++saveSequence);
        {
            std::ofstream stream(staged, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            stream.flush();
            if (!stream)
            {
                std::error_code ignored;
                std::filesystem::remove(staged, ignored);
                return Fail(error, "Cannot stage InputGraph source");
            }
        }
        if (!MoveFileExW(staged.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code ignored;
            std::filesystem::remove(staged, ignored);
            return Fail(error, "Cannot atomically replace InputGraph source");
        }
        return true;
    }
}
