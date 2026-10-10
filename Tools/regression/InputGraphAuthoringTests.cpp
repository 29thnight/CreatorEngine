// Source-only regression probe. It is not executed as part of authoring or asset loading.
#include "../../Lattice/Input/LXInputCompiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void Require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    LX::Id Pin(const LX::LXGraph& graph, LX::Id node, const char* identifier, LX::Direction direction)
    {
        const auto* found = graph.FindNode(node);
        Require(found != nullptr, "Missing fixture node");
        for (const auto& pin : found->pins)
        {
            if (pin.Identifier() == identifier && pin.direction == direction)
            {
                return pin.id;
            }
        }
        throw std::runtime_error("Missing fixture pin");
    }

    LX::Id Connect(LX::LXGraph& graph, LX::Id source, const char* output, LX::Id target, const char* input)
    {
        const auto link = graph.Connect(Pin(graph, source, output, LX::Direction::Output),
                                        Pin(graph, target, input, LX::Direction::Input));
        Require(link.has_value(), "Fixture connection rejected");
        return *link;
    }

    LX::LXInputAsset ButtonGraph()
    {
        LX::LXInputAsset asset;
        asset.graphId = "a39d780c-2d1e-465f-b7e5-c44ce752ad96";
        const auto layer = asset.graph.CreateNode("Input.Layer", 0.0f, 0.0f);
        const auto source = asset.graph.CreateNode("Input.Key", 0.0f, 100.0f);
        const auto output = asset.graph.CreateNode("Input.Signal.Button", 400.0f, 100.0f);
        asset.graph.SetProperty(source, "control", "57");
        asset.graph.SetProperty(output, "name", "Jump");
        asset.graph.SetProperty(output, "layer", std::to_string(layer));
        Connect(asset.graph, source, "Value", output, "Value");
        return asset;
    }

    void ArchiveAndIdentity()
    {
        auto asset = ButtonGraph();
        const auto first = LX::CompileInputGraph(asset);
        Require(static_cast<bool>(first.program), "Button fixture did not compile");
        std::string error;
        const auto loaded = LX::LXInputArchive::Read(LX::LXInputArchive::Write(asset), &error);
        Require(loaded.has_value() && loaded->graph.Equals(asset.graph), "InputGraph source round-trip changed graph");
        Require(loaded->graphId == asset.graphId, "InputGraph identity changed");
        const auto identity = asset.SemanticIdentity();
        const auto source = asset.graph.Nodes()[1].id;
        asset.graph.SetNodePosition(source, 123.0f, 456.0f);
        asset.graph.SetNodeCollapsed(source, true);
        asset.graph.SetView({true, 80.0f, 90.0f, 1.5f});
        Require(identity == asset.SemanticIdentity(), "Layout entered input semantic identity");
        const auto moved = LX::CompileInputGraph(asset, 2);
        Require(moved.program && moved.program->GetSemanticHash() == first.program->GetSemanticHash(),
                "Layout changed executable semantic hash");
        Require(moved.program->GetInterfaceHash() == first.program->GetInterfaceHash(),
                "Layout changed accessor identity");
        asset.graph.SetProperty(source, "control", "58");
        const auto changed = LX::CompileInputGraph(asset, 3);
        Require(identity != asset.SemanticIdentity() && changed.program &&
                changed.program->GetSemanticHash() != first.program->GetSemanticHash(), "Source edit was ignored");
        Require(changed.program->GetInterfaceHash() == first.program->GetInterfaceHash(),
                "Binding change incorrectly invalidated generated accessors");

        const auto retired = asset.graph.CreateNode("Input.Signal.Float", 100.0f, 300.0f);
        Require(asset.graph.Undo(), "New signal could not be undone");
        const auto replacement = asset.graph.CreateNode("Input.Signal.Vector2", 100.0f, 300.0f);
        Require(replacement != retired, "New signal reused a retired output ID after Undo");
        asset.graph.RemoveNode(replacement);

        auto unknown = LX::LXInputArchive::Write(asset);
        const auto at = unknown.find("\"Input.Key\"");
        Require(at != std::string::npos, "Missing fixture definition");
        unknown.replace(at, std::string("\"Input.Key\"").size(), "\"Input.Unavailable\"");
        const auto preserved = LX::LXInputArchive::Read(unknown, &error);
        Require(preserved.has_value(), "Unknown node should remain inspectable in its source document");
        Require(!LX::CompileInputGraph(*preserved).program, "Unknown node schema was published");
        Require(!LX::LXInputArchive::Read("LXINPUT 999 \"bad\"", &error), "Unsupported source version accepted");
    }

    void TypedConnectionsAndPostCombine()
    {
        LX::LXInputAsset asset;
        asset.graphId = "a39d780c-2d1e-465f-b7e5-c44ce752ad96";
        const auto layer = asset.graph.CreateNode("Input.Layer", 0.0f, 0.0f);
        const auto left = asset.graph.CreateNode("Input.GamepadStick", 0.0f, 100.0f);
        const auto right = asset.graph.CreateNode("Input.GamepadStick", 0.0f, 300.0f);
        asset.graph.SetProperty(right, "control", "1");
        const auto sum = asset.graph.CreateNode("Input.Combine.Vector2", 300.0f, 100.0f);
        asset.graph.SetProperty(sum, "policy", "Sum");
        const auto normalize = asset.graph.CreateNode("Input.Normalize.Vector2", 600.0f, 100.0f);
        const auto output = asset.graph.CreateNode("Input.Signal.Vector2", 900.0f, 100.0f);
        asset.graph.SetProperty(output, "layer", std::to_string(layer));
        asset.graph.SetProperty(output, "combine", "Sum");
        Connect(asset.graph, left, "Value", sum, "Values");
        Connect(asset.graph, right, "Value", sum, "Values");
        Connect(asset.graph, sum, "Result", normalize, "Value");
        const auto outputLink = Connect(asset.graph, normalize, "Result", output, "Value");
        const auto result = LX::CompileInputGraph(asset);
        Require(result.program && result.definition.bindings.size() == 2, "Alternative binding lowering failed");
        Require(result.definition.signals.front().processors.size() == 1 &&
                result.definition.signals.front().processors.front().kind == Input::ProcessorKind::Normalize,
                "Normalize after Sum was not applied after combination");
        for (const auto& binding : result.definition.bindings)
        {
            Require(binding.processors.empty(), "Post-combine Normalize was incorrectly distributed to branches");
        }
        const auto parameter = Pin(asset.graph, normalize, "Parameters", LX::Direction::Input);
        Require(asset.graph.SetSocketValue(parameter, std::array<double, 2>{2.0, 3.0}), "Vector2 value edit failed");
        const auto loaded = LX::LXInputArchive::Read(LX::LXInputArchive::Write(asset));
        Require(loaded && std::get<std::array<double, 2>>(loaded->graph.FindPin(parameter)->value) ==
                std::array<double, 2>{2.0, 3.0}, "Vector2 wire tag did not round-trip");
        Require(asset.graph.Undo(), "Vector2 value edit did not enter Undo history");
        Require(std::get<std::array<double, 2>>(asset.graph.FindPin(parameter)->value) ==
                std::array<double, 2>{1.0, 1.0}, "Undo did not restore Vector2 value");
        Require(asset.graph.Redo(), "Vector2 value edit did not enter Redo history");
        Require(!asset.graph.Connect(Pin(asset.graph, normalize, "Result", LX::Direction::Output),
                                     Pin(asset.graph, normalize, "Value", LX::Direction::Input)), "Self-cycle accepted");
        const auto button = asset.graph.CreateNode("Input.Signal.Button", 900.0f, 400.0f);
        Require(!asset.graph.Connect(Pin(asset.graph, left, "Value", LX::Direction::Output),
                                     Pin(asset.graph, button, "Value", LX::Direction::Input)), "Vector2 to Button implicit cast accepted");
        asset.graph.RemoveNode(button);
        const auto outer = asset.graph.CreateNode("Input.Combine.Vector2", 800.0f, 300.0f);
        asset.graph.SetProperty(outer, "policy", "Sum");
        asset.graph.Disconnect(outputLink);
        Connect(asset.graph, normalize, "Result", outer, "Values");
        Connect(asset.graph, left, "Value", outer, "Values");
        Connect(asset.graph, outer, "Result", output, "Value");
        Require(!LX::CompileInputGraph(asset).program, "Processed subgroup was silently reassociated across outer combination");
    }

    void LegacyVector3()
    {
        const std::string legacy =
            "LXG 9 \"material\" 3\nN 1\n"
            "1 \"LegacyVector\" \"Vector\" 1 0 0 0 3 0 0 0\n"
            "2 \"Value\" \"Value\" 1 4 0 0 0 4 1.25 -2.5 3.75\n"
            "P 1\n1 0 0 0 0\nF 0\nV 0 0 0 1\nL 0\nG 0\n";
        const auto path = std::filesystem::temp_directory_path() / "CreatorEngine.InputGraph.LegacyVector3.lxg";
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output << legacy;
            Require(static_cast<bool>(output), "Could not write legacy fixture");
        }
        const auto name = path.u8string();
        std::string error;
        const auto loaded = LX::LXGraph::Load(std::string(name.begin(), name.end()), &error);
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        Require(loaded.has_value(), "Legacy LXG9 Vector3 did not load");
        const auto* pin = loaded->FindPin(2);
        Require(pin && pin->type == LX::PinType::Vector && pin->value.index() == 4 &&
                std::get<std::array<double, 3>>(pin->value) == std::array<double, 3>{1.25, -2.5, 3.75},
                "Legacy Vector3 enum/variant meaning shifted");
    }
}

int main()
{
    try
    {
        ArchiveAndIdentity();
        TypedConnectionsAndPostCombine();
        LegacyVector3();
        std::cout << "InputGraph authoring contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
