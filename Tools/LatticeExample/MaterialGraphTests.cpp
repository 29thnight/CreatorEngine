#include "MaterialGraphTests.h"
#include "../../Lattice/Material/LXMaterialIR.h"
#include "../../Lattice/Material/LXMaterialCompiler.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <stdexcept>
#include <Windows.h>

namespace
{
std::size_t checks = 0;

void Require(bool condition, const std::string& name)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(name);
    }
}

std::string Replace(std::string text, const std::string& from, const std::string& to)
{
    const auto position = text.find(from);
    Require(position != std::string::npos, "Find mutation field: " + from);
    text.replace(position, from.size(), to);
    return text;
}

void Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    Require(out.good(), "Write test document");
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

const LX::Pin& Socket(const LX::LXGraph& graph, LX::Id node, const std::string& identifier, LX::Direction direction)
{
    const auto& pins = graph.FindNode(node)->pins;
    const auto found = std::ranges::find_if(
        pins, [&](const auto& pin) { return pin.Identifier() == identifier && pin.direction == direction; });
    if (found == pins.end())
    {
        throw std::runtime_error("Missing socket " + identifier);
    }
    return *found;
}

LX::LXMaterialAsset Fixture(const LX::LXMaterialDefinitions& definitions)
{
    LX::LXMaterialAsset asset(definitions);
    const auto color = asset.CreateNode("ShaderNodeRGB", -220.5f, -41.25f);
    const auto principled = asset.CreateNode("ShaderNodeBsdfPrincipled", 40.25f, -20.5f);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 420.75f, 15.5f);
    Require(color && principled && output, "Create Material definitions");
    asset.activeOutput = output;
    Require(asset.graph
                .Connect(Socket(asset.graph, color, "Color", LX::Direction::Output).id,
                         Socket(asset.graph, principled, "Base Color", LX::Direction::Input).id)
                .has_value(),
            "Color to Principled");
    Require(asset.graph
                .Connect(Socket(asset.graph, principled, "BSDF", LX::Direction::Output).id,
                         Socket(asset.graph, output, "Surface", LX::Direction::Input).id)
                .has_value(),
            "Closure to Material Output");
    Require(asset.graph.SetSocketValue(Socket(asset.graph, color, "Color", LX::Direction::Output).id,
                                       std::array<double, 4>{0.12345678901234567, 1.0e-12, 11.5, 0.75}),
            "Exact double color values");
    Require(asset.graph.SetNodeCollapsed(color, true), "Collapsed node");
    Require(asset.graph.AddFrame("Material\n색상 \"test\"", -260.5f, -80.25f, 650.75f, 500.5f, {color, principled}) !=
                0,
            "Frame with UTF-8 and escaped text");
    Require(asset.graph.SetView({true, 100.25f, -24.5f, 0.875f}), "Saved view");
    asset.blackboard = {
        {1001, "roughness", "Roughness", LX::PinType::Float, 0.27, LX::LXColorSpace::Data, true},
        {1002, "texture", "Base Color Texture", LX::PinType::Texture, std::string("asset://texture/001"),
         LX::LXColorSpace::SRGB, true},
        {1003, "sampler", "Sampler", LX::PinType::Sampler, std::string("linear-repeat"), LX::LXColorSpace::Data, false},
        {1004, "bool", "Bool", LX::PinType::Bool, true, LX::LXColorSpace::Data, true},
        {1005, "int", "Integer", LX::PinType::Int, std::numeric_limits<std::int64_t>::min(), LX::LXColorSpace::Data,
         true},
        {1006, "vector", "Vector", LX::PinType::Vector, std::array<double, 3>{0.125, -2.7, 1.0e-8},
         LX::LXColorSpace::Data, true},
        {1007, "normal", "Normal", LX::PinType::Normal, std::array<double, 3>{0.0, 1.0, 0.0}, LX::LXColorSpace::Data,
         true},
        {1008, "color", "HDR Color", LX::PinType::Color, std::array<double, 4>{-1.5, 12.7, 0.25, 1.0},
         LX::LXColorSpace::Linear, true}};
    const auto parameter = asset.CreateNode("LXParameterFloat", -220.0f, 300.0f);
    Require(asset.graph.SetProperty(parameter, "parameter", "1001"), "Stable Blackboard reference");
    Require(asset.graph
                .Connect(Socket(asset.graph, parameter, "Value", LX::Direction::Output).id,
                         Socket(asset.graph, principled, "Roughness", LX::Direction::Input).id)
                .has_value(),
            "Parameter to Principled");
    return asset;
}

bool HasIssue(const std::vector<LX::Issue>& issues, const std::string& code)
{
    return std::ranges::any_of(issues, [&](const auto& issue) { return issue.code == code; });
}
} // namespace

bool RunMaterialGraphTests()
{
    try
    {
        checks = 0;
        const auto definitions = LX::CreateMaterialDefinitions();
        auto asset = Fixture(definitions);
        Require(asset.Validate().empty(), "Valid Material authoring fixture");
        const auto principled = definitions.nodes->Find("ShaderNodeBsdfPrincipled");
        Require(principled && principled->defaults.pins.size() == 32, "Blender Principled 31 inputs and BSDF output");
        const auto ir = LX::BuildMaterialIR(asset);
        Require(ir && ir->scopes.size() == 1 && ir->scopes.front().nodes.size() == 4,
                "Typed deterministic Material IR");
        Require(ir->scopes.front().links.back().type == LX::PinType::Float, "Typed Blackboard connection");
        Require(LX::BuildMaterialIR(asset) == ir, "Repeated lowering is deterministic");
        const auto originalText = LX::LXMaterialArchive::Write(asset);
        std::string error;
        const auto roundTrip = LX::LXMaterialArchive::Read(originalText, definitions, &error);
        Require(roundTrip && asset.Equals(*roundTrip), "Exact authoring round-trip: " + error);
        Require(roundTrip && LX::BuildMaterialIR(*roundTrip) == ir, "IR equivalence after round-trip");
        auto reordered = asset;
        std::ranges::reverse(reordered.blackboard);
        Require(LX::BuildMaterialIR(reordered) == ir, "Blackboard storage order does not affect IR");

        const auto directory =
            std::filesystem::path(L"Build/LatticeExample/MaterialTests") / std::to_wstring(GetCurrentProcessId());
        std::filesystem::create_directories(directory);
        const auto path = directory / L"머테리얼.shadergraph";
        Require(asset.Save(path, &error), "Atomic material save: " + error);
        Require(asset.graph.SetNodePosition(asset.graph.Nodes().front().id, 7.125f, 22.25f), "Move node");
        Require(asset.Save(path, &error), "Save second generation and backup: " + error);
        Require(LX::LXMaterialAsset::Load(path, definitions)->Equals(asset), "Close and reopen material file");
        Require(LX::BuildMaterialIR(asset) == ir, "Layout does not affect typed IR");
        LX::LXMaterialDocument document(asset);
        Require(document.PublishIR(), "Publish first valid IR");
        Require(document.Generation() == 1 && document.LastValidIR() == ir, "Published generation identity");
        const auto savedIR = document.LastValidIR();
        document.Asset().graph.SetNodeCollapsed(document.Asset().graph.Nodes().front().id, false);
        Require(document.PublishIR() && document.Generation() == 1, "Layout-only changes reuse IR generation");

        auto unknownText =
            Replace(originalText, "\"definition\":\"ShaderNodeRGB\"", "\"definition\":\"VendorFutureRGB\"");
        unknownText = Replace(unknownText, "\"title\":\"Color\"",
                              "\"title\":\"Color\",\"vendorPayload\":{\"array\":[1,\"한글\",true],\"revision\":99}");
        auto unknown = LX::LXMaterialArchive::Read(unknownText, definitions, &error);
        Require(unknown.has_value(), "Unknown node can open for preservation: " + error);
        const auto unknownId = unknown->graph.Nodes().front().id;
        Require(!unknown->graph.IsNodeEditable(unknownId), "Unknown node is read-only");
        const auto unknownPath = directory / L"unknown.shadergraph";
        Require(unknown->Save(unknownPath, &error), "Preserve unknown payload: " + error);
        auto unknownReloaded = LX::LXMaterialAsset::Load(unknownPath, definitions, &error);
        Require(unknownReloaded && unknown->Equals(*unknownReloaded), "Unknown complete payload round-trip");
        Require(Read(unknownPath).find("vendorPayload") != std::string::npos, "Unknown extension fields preserved");
        std::vector<LX::Issue> issues;
        Require(!LX::BuildMaterialIR(*unknown, &issues) && HasIssue(issues, "material_unknown_node"),
                "Unknown definition blocks IR");
        Require(document.Reload(unknownPath, &error) && !document.PublishIR(&issues),
                "Unknown authoring document opens without publishing");
        Require(document.LastValidIR() == savedIR && document.Generation() == 1, "Unknown node preserves last good IR");
        Require(document.Reload(path, &error), "Return to valid authoring asset");

        const auto future = Replace(originalText, "\"schemaVersion\":1", "\"schemaVersion\":99");
        Write(path, future);
        const auto beforeFailure = document.Asset();
        Require(!document.Reload(path, &error) && error.find("schema version") != std::string::npos,
                "Future schema rejected even with a valid backup");
        Require(document.Asset().Equals(beforeFailure) && document.LastValidIR() == savedIR,
                "Failed reload preserves authoring and IR");
        Require(!asset.Save(path, &error) && Read(path) == future,
                "Save cannot overwrite an unsupported future archive");
        auto revision = Replace(originalText, "\"schemaRevision\":1", "\"schemaRevision\":2");
        Write(path, revision);
        Require(!document.Reload(path, &error) && document.Asset().Equals(beforeFailure),
                "Node migration failure retains current document");
        Write(path, "{truncated");
        auto recovered = LX::LXMaterialAsset::Load(path, definitions, &error);
        Require(recovered && error.starts_with("Recovered backup"), "Truncated primary recovers verified backup");
        Require(recovered->Save(path, &error) && LX::LXMaterialAsset::Load(path, definitions)->Equals(*recovered),
                "Save repaired primary");
        const auto primaryBytes = Read(path);
        HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(locked != INVALID_HANDLE_VALUE, "Hold primary file open without write sharing");
        const bool lockedSave = asset.Save(path, &error);
        CloseHandle(locked);
        Require(!lockedSave && Read(path) == primaryBytes, "Atomic replacement failure preserves primary bytes");

        auto legacy = Replace(originalText, "\"schemaVersion\":1", "\"schemaVersion\":0");
        // Schema 0 predates explicit definition revision, identifier and color-space intent.
        const auto graphStart = legacy.find("\"graph\":");
        auto legacyGraph = legacy.substr(graphStart);
        legacyGraph = std::regex_replace(legacyGraph, std::regex("\\\"schemaRevision\\\":1,"), "");
        legacyGraph = std::regex_replace(legacyGraph, std::regex("\\\"identifier\\\":\\\"[^\\\"]*\\\","), "");
        legacyGraph = std::regex_replace(legacyGraph, std::regex(",\\\"colorSpace\\\":\\\"[^\\\"]*\\\""), "");
        legacy.replace(graphStart, legacy.size() - graphStart, legacyGraph);
        auto legacyTree = LX::LXMaterialArchive::Read(legacy, definitions, &error);
        auto expectedLegacy = *roundTrip;
        expectedLegacy.blackboard[1].colorSpace = LX::LXColorSpace::Data;
        Require(legacyTree && legacyTree->Equals(expectedLegacy),
                "Explicit schema 0 to 1 defaults migration: " + error);
        Require(LX::LXMaterialArchive::Write(*legacyTree).find("\"schemaVersion\":1") != std::string::npos,
                "Migration saves current schema");
        Require(!LX::LXMaterialArchive::Read(
                    Replace(originalText, "\"domain\":\"material\"", "\"domain\":\"animation\""), definitions),
                "Reject wrong domain");
        Require(!LX::LXMaterialArchive::Read(Replace(originalText, "\"graphId\":1", "\"graphId\":1,\"graphId\":2"),
                                             definitions),
                "Reject duplicate fields");
        Require(!LX::LXMaterialArchive::Read(Replace(originalText, "\"kind\":\"color\"", "\"kind\":\"float\""),
                                             definitions),
                "Reject wrong default value type");
        Require(!LX::LXMaterialArchive::Read(Replace(originalText, "\"value\":0.27", "\"value\":NaN"), definitions),
                "Reject non-finite values");
        auto invalid = asset;
        invalid.scopes[0]
            .sockets[Socket(invalid.graph, invalid.graph.Nodes()[1].id, "Base Color", LX::Direction::Input).id]
            .enabled = false;
        Require(!LX::BuildMaterialIR(invalid, &issues) && HasIssue(issues, "material_inactive_socket"),
                "Inactive socket blocks a live connection");
        invalid = asset;
        invalid.blackboard[0].type = LX::PinType::Color;
        Require(!LX::BuildMaterialIR(invalid, &issues), "Reject Blackboard type mismatch");
        invalid = asset;
        invalid.graph.SetProperty(invalid.graph.Nodes().back().id, "parameter", "7777");
        Require(!LX::BuildMaterialIR(invalid, &issues) && HasIssue(issues, "material_parameter"),
                "Missing parameter references diagnosed");

        auto grouped = asset;
        const auto oldColor = grouped.graph.Nodes().front().pins.front().id;
        grouped.scopes[0].sockets[oldColor] = {true, true, true, LX::LXColorSpace::SRGB};
        LX::LXMaterialDocument groupDocument(grouped);
        Require(groupDocument.PublishIR(), "Publish before Material group transaction");
        const auto beforeGroup = groupDocument.Asset();
        Require(groupDocument.Edit([&](auto& edited) {
            return edited.CollapseToGroup({edited.graph.Nodes().front().id}, "Transactional Tint") != 0;
        }),
                "Material asset transaction includes metadata");
        const auto afterGroup = groupDocument.Asset();
        Require(groupDocument.Undo() && groupDocument.Asset().Equals(beforeGroup),
                "Group undo restores node and socket metadata");
        Require(groupDocument.Redo() && groupDocument.Asset().Equals(afterGroup),
                "Group redo restores shared definition metadata");
        const auto beforeRejectedEdit = groupDocument.Asset();
        Require(!groupDocument.Edit([](auto& edited) {
            edited.blackboard[0].type = LX::PinType::Closure;
            return true;
        }) && groupDocument.Asset().Equals(beforeRejectedEdit),
                "Rejected transaction preserves asset");
        const auto groupInstance = grouped.CollapseToGroup({grouped.graph.Nodes().front().id}, "Shared Tint");
        Require(groupInstance != 0, "Create group from Material node");
        const auto groupId = grouped.graph.FindNode(groupInstance)->groupId;
        const auto& groupBody = *grouped.graph.FindGroup(groupId)->body;
        const auto& movedColor = groupBody.Nodes().front().pins.front();
        Require(grouped.SocketState(groupId, movedColor) ==
                    LX::LXMaterialSocketState{true, true, true, LX::LXColorSpace::SRGB},
                "Grouping remaps non-default socket metadata");
        Require(grouped.graph.CreateGroupInstance(groupId, -400.0f, 150.0f) != 0, "Reuse stable group definition");
        Require(grouped.Validate().empty(), "Material group interface validity");
        const auto groupIR = LX::BuildMaterialIR(grouped, &issues);
        Require(groupIR && groupIR->scopes.size() == 2, "Root and shared group lower once each");
        const auto groupPath = directory / L"group.shadergraph";
        Require(grouped.Save(groupPath, &error), "Save group and shared interface: " + error);
        auto groupReloaded = LX::LXMaterialAsset::Load(groupPath, definitions, &error);
        Require(groupReloaded && grouped.Equals(*groupReloaded) && LX::BuildMaterialIR(*groupReloaded) == groupIR,
                "Shared group exact authoring and IR round-trip");
        const auto secondGroup = grouped.CollapseToGroup({grouped.graph.Nodes().back().id}, "Nested Tint");
        Require(secondGroup != 0, "Nested group definition");
        const auto nestedIR = LX::BuildMaterialIR(grouped, &issues);
        Require(nestedIR && nestedIR->scopes.size() == 3, "Nested group lowering");
        const auto nestedRoundTrip =
            LX::LXMaterialArchive::Read(LX::LXMaterialArchive::Write(grouped), definitions, &error);
        Require(nestedRoundTrip && grouped.Equals(*nestedRoundTrip) &&
                    LX::BuildMaterialIR(*nestedRoundTrip) == nestedIR,
                "Nested group metadata and stable interface IDs round-trip");
        Require(grouped.Save(directory / L"nested-group.shadergraph", &error), "Persist nested group fixture");

        LX::LXMaterialAsset socketParameters(definitions);
        const auto socketBSDF = socketParameters.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
        const auto socketOutput = socketParameters.CreateNode("ShaderNodeOutputMaterial", 400, 0);
        socketParameters.activeOutput = socketOutput;
        Require(socketParameters.graph
                    .Connect(Socket(socketParameters.graph, socketBSDF, "BSDF", LX::Direction::Output).id,
                             Socket(socketParameters.graph, socketOutput, "Surface", LX::Direction::Input).id)
                    .has_value(),
                "Socket parameter fixture has a connected material output");
        socketParameters.blackboard.push_back(
            {1004, "roughness", "Roughness", LX::PinType::Float, 0.9, LX::LXColorSpace::Data, true});
        const auto scalar = socketParameters.CreateNode("LXParameterFloat", -300, 0, "Roughness");
        socketParameters.graph.SetProperty(scalar, "parameter", "1004");
        socketParameters.graph.SetProperty(scalar, "valueSource", "socket");
        const auto scalarPin = Socket(socketParameters.graph, scalar, "Value", LX::Direction::Output).id;
        socketParameters.graph.SetSocketValue(scalarPin, 0.27);
        Require(
            socketParameters.graph
                .Connect(scalarPin, Socket(socketParameters.graph, socketBSDF, "Roughness", LX::Direction::Input).id)
                .has_value(),
            "Model socket parameter connects to shader input");
        const auto defaultsIR = LX::BuildMaterialIR(socketParameters);
        Require(defaultsIR &&
                    std::get<double>(
                        std::ranges::find(defaultsIR->blackboard, 1004, &LX::LXMaterialParameter::id)->value) == 0.27,
                "Model socket default overrides import seed in IR");
        auto conflicting = socketParameters;
        const auto copies = conflicting.graph.PasteNodes(conflicting.graph.CopyNodes({scalar}), 100, 100);
        conflicting.graph.SetSocketValue(Socket(conflicting.graph, copies.front(), "Value", LX::Direction::Output).id,
                                         0.8);
        Require(!LX::BuildMaterialIR(conflicting, &issues) && HasIssue(issues, "material_parameter"),
                "Conflicting socket defaults fail without last-writer ambiguity");
        const auto parameterGroup = socketParameters.CollapseToGroup({scalar}, "Roughness Source");
        Require(parameterGroup != 0, "Group imported socket parameter");
        const auto parameterIR = LX::BuildMaterialIR(socketParameters);
        Require(parameterIR &&
                    std::get<double>(
                        std::ranges::find(parameterIR->blackboard, 1004, &LX::LXMaterialParameter::id)->value) == 0.27,
                "Grouped model parameter retains its authored default");
        const auto parameterReloaded =
            LX::LXMaterialArchive::Read(LX::LXMaterialArchive::Write(socketParameters), definitions);
        Require(parameterReloaded && LX::BuildMaterialIR(*parameterReloaded) == parameterIR,
                "Grouped socket defaults round-trip exactly");

        auto resources = asset;
        const auto image = resources.CreateNode("ShaderNodeTexImage", 0, 800);
        const auto normal = resources.CreateNode("ShaderNodeNormalMap", 220, 800);
        Require(resources.graph.SetProperty(image, "image", "asset://texture/normal") &&
                    resources.graph.SetProperty(image, "colorSpace", "data"),
                "Image asset reference and data intent");
        Require(resources.graph
                    .Connect(Socket(resources.graph, image, "Color", LX::Direction::Output).id,
                             Socket(resources.graph, normal, "Color", LX::Direction::Input).id)
                    .has_value(),
                "Image to Normal Map");
        Require(resources.graph
                    .Connect(Socket(resources.graph, normal, "Normal", LX::Direction::Output).id,
                             Socket(resources.graph, resources.graph.Nodes()[1].id, "Normal", LX::Direction::Input).id)
                    .has_value(),
                "Normal Map vector to Blender Principled normal");
        const auto imageRoundTrip = LX::LXMaterialArchive::Read(LX::LXMaterialArchive::Write(resources), definitions);
        Require(imageRoundTrip && imageRoundTrip->Equals(resources) &&
                    LX::BuildMaterialIR(*imageRoundTrip) == LX::BuildMaterialIR(resources),
                "Image and Normal Map structured round-trip");
        auto invalidEnum = resources;
        invalidEnum.graph.SetProperty(image, "interpolation", "NOT_SUPPORTED");
        Require(!LX::BuildMaterialIR(invalidEnum, &issues) && HasIssue(issues, "material_property"),
                "Unsupported enum rejected with node diagnostic");
        Require(!invalidEnum.Save(path, &error) && Read(path) == primaryBytes,
                "Invalid schema save preserves original file");
        const auto reroute = resources.CreateNode("LXRerouteColor", 400, 800);
        Require(resources.graph
                    .Connect(Socket(resources.graph, image, "Color", LX::Direction::Output).id,
                             Socket(resources.graph, reroute, "Input", LX::Direction::Input).id)
                    .has_value(),
                "Typed reroute connection");
        Require(!LX::LXMaterialArchive::Read(
                    Replace(originalText, "\"graphId\":1", "\"graphId\":1,\"futureField\":true"), definitions),
                "Unknown archive fields rejected without data loss");
        const auto texture = resources.CreateNode("LXParameterTexture", 0, 600);
        const auto sampler = resources.CreateNode("LXParameterSampler", 200, 600);
        resources.graph.SetProperty(texture, "parameter", "1002");
        resources.graph.SetProperty(sampler, "parameter", "1003");
        Require(LX::BuildMaterialIR(resources).has_value(), "Texture and sampler are distinct Material resource types");
        Require(resources.Save(directory / L"image-normal.shadergraph", &error),
                "Persist image/normal/resource fixture");
        Require(!resources.graph.Connect(
                    resources.graph.FindNode(texture)->pins.front().id,
                    Socket(resources.graph, resources.graph.Nodes()[1].id, "Base Color", LX::Direction::Input).id),
                "No implicit resource-to-color connection");
        const auto lxg = directory / L"resources.lxg";
        Require(resources.graph.Save(lxg.string(), &error), "LXG 9 supports appended resource types");
        const auto lxgRoundTrip = LX::LXGraph::Load(lxg.string(), &error, definitions.nodes);
        Require(lxgRoundTrip && resources.graph.Equals(*lxgRoundTrip), "LXG 9 resource types round-trip");
        auto unsupported = asset;
        unsupported.graph = LX::LXGraph("animation", definitions.nodes);
        Require(!LX::BuildMaterialIR(unsupported), "Animation graph cannot enter Material compiler");
        Require(!asset.graph.CreateNode("MissingDefinition", 0, 0), "Registry rejects unregistered creation");

        std::cout << "LX_MATERIAL_TEST_OK checks=" << checks << " artifacts=" << directory.string() << std::endl;
        return true;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "LX_MATERIAL_TEST_FAILED check=" << checks << " " << exception.what() << std::endl;
        return false;
    }
}

bool RunMaterialCompilerTests()
{
    try
    {
        checks = 0;
        const auto definitions = LX::CreateMaterialDefinitions();
        auto asset = Fixture(definitions);
        std::vector<LX::LXMaterialDiagnostic> diagnostics;
        const auto program = LX::GenerateMaterialSlang(asset, &diagnostics);
        Require(program && diagnostics.empty(), "Generate fixture Slang");
        Require(program->surface && !program->volume && program->features == 0x4F && program->parameters.size() == 1,
                "Feature mask and live Blackboard parameter");
        Require(program->resources.empty() && program->textureSamples == 0,
                "Unused texture/sampler Blackboard eliminated");
        Require(LX::GenerateMaterialSlang(asset)->semanticKey == program->semanticKey,
                "Repeated deterministic generation");
        asset.graph.SetNodePosition(asset.graph.Nodes()[0].id, 900, 400);
        asset.graph.SetNodeCollapsed(asset.graph.Nodes()[0].id, false);
        Require(LX::GenerateMaterialSlang(asset)->semanticKey == program->semanticKey, "Layout does not recompile");
        const auto invalidTemplate = asset;
        const auto group =
            asset.CollapseToGroup({asset.graph.Nodes()[0].id, asset.graph.Nodes()[1].id}, "Codegen group");
        Require(group != 0, "Collapse Material definition into group");
        const auto grouped = LX::GenerateMaterialSlang(asset, &diagnostics);
        Require(grouped && diagnostics.empty(), "Group boundary lowering");
        Require(std::ranges::any_of(
                    grouped->sourceMap,
                    [&](const auto& range) { return range.source.instances == std::vector<LX::Id>{group}; }),
                "Group source map identifies invocation");
        const auto restored = LX::LXMaterialArchive::Read(LX::LXMaterialArchive::Write(asset), definitions);
        Require(restored && LX::GenerateMaterialSlang(*restored)->semanticKey == grouped->semanticKey,
                "Saved group reopens with identical Slang identity");
        const auto& body = *asset.graph.FindGroup(asset.graph.FindNode(group)->groupId)->body;
        const auto surface =
            std::ranges::find(body.Nodes(), std::string("ShaderNodeBsdfPrincipled"), &LX::Node::type)->id;
        const auto roughness = Socket(body, surface, "Roughness", LX::Direction::Input).id;
        auto invalid = invalidTemplate;
        const auto rootSurface = invalid.graph.Nodes()[1].id;
        invalid.graph.SetSocketValue(Socket(invalid.graph, rootSurface, "Diffuse Roughness", LX::Direction::Input).id,
                                     .25);
        Require(!LX::GenerateMaterialSlang(invalid, &diagnostics) && diagnostics.front().source.node == rootSurface,
                "Unsupported socket reports graph owner");
        Require(!LX::GenerateMaterialSlang(asset, &diagnostics, {0}), "Complexity cap rejects generation");
        Require(std::ranges::any_of(grouped->sourceMap,
                                    [&](const auto& range) {
                                        return range.source.pin == roughness &&
                                               LX::FindMaterialSource(*grouped, range.firstLine) == range.source;
                                    }),
                "Generated assignment maps back to input pin");
        std::cout << "LX_MATERIAL_COMPILER_TEST_OK checks=" << checks << std::endl;
        return true;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "LX_MATERIAL_COMPILER_TEST_FAILED check=" << checks << " " << exception.what() << std::endl;
        return false;
    }
}
