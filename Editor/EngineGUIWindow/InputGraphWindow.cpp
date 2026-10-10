#include "InputGraphWindow.h"
#include "EditorAssetDatabase.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "EditorPropertyRow.h"
#include "DataSystem.h"
#include "PathFinder.h"
#include "InputSessionComponent.h"
#include "InputSession.h"
#include "InputAccessorGenerator.h"
#include "../../Lattice/Input/LXInputCompiler.h"
#include "../../Lattice/Core/LXDocument.h"
#include "../../Lattice/ImGui/LXCanvas.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace editor::input_editing
{
    namespace
    {
        struct Session final
        {
            LX::LXInputAsset asset;
            LX::LXDocument document;
            LX::CanvasState canvas;
            LX::LXStyleSheet styles;
            LX::LXNodeItemRegistry items;
            LX::LXInputCompileResult compiled;
            std::filesystem::path path;
            std::string pathText;
            std::string semantic;
            std::string message;
            std::string accessorName{"GameplayInputs"};
            std::uint64_t generation{};
            std::uint64_t observedRevision{};
            bool attempted{};

            Session(LX::LXInputAsset input, std::filesystem::path source, bool saved)
                : asset(std::move(input)), document(asset.graph, {}, saved), path(std::move(source)),
                  pathText(path.generic_string())
            {
                styles.defaultNode.width = 250.0f;
                LX::LXPinStyle vector;
                vector.fill = {0.38f, 0.62f, 0.92f, 1.0f};
                styles.SetPinTypeStyle(LX::PinType::Vector2, vector);
                for (const auto& type : LX::InputNodeTypes())
                {
                    const auto* definition = asset.graph.Definitions()->Find(type);
                    if (!definition)
                    {
                        continue;
                    }
                    LX::LXNodeStyle style = styles.defaultNode;
                    if (type.starts_with("Input.Signal."))
                    {
                        style.header = {0.18f, 0.40f, 0.26f, 1.0f};
                    }
                    else if (type == "Input.Layer")
                    {
                        style.header = {0.37f, 0.26f, 0.48f, 1.0f};
                    }
                    styles.SetTypeStyle(type, style);
                    for (const auto& [key, value] : definition->defaults.properties)
                    {
                        LX::LXNodeItemSpec item;
                        item.key = key;
                        item.label = key;
                        item.kind = LX::LXNodeItemKind::String;
                        if (key == "domain")
                        {
                            item.choices = {"Game", "UI"};
                        }
                        else if (key == "lifetime")
                        {
                            item.choices = {"Persistent", "Delta"};
                        }
                        else if (key == "combine" || key == "policy")
                        {
                            item.choices = {"Sum", "MaximumMagnitude", "MostRecent", "Priority"};
                        }
                        else if (key == "claim")
                        {
                            item.choices = {"OnPress", "OnPerformed", "PassThrough"};
                        }
                        else if (key == "order")
                        {
                            item.choices = {"Simultaneous", "Sequential"};
                        }
                        else if (key == "space")
                        {
                            item.choices = {"None", "ClientPixels", "RelativeCounts", "Normalized"};
                        }
                        else if (key == "enabled" || key == "claimChord" || key == "resumePersistentValue")
                        {
                            item.choices = {"true", "false"};
                        }
                        if (!item.choices.empty())
                        {
                            item.kind = LX::LXNodeItemKind::Choice;
                        }
                        items.Register(type, std::move(item));
                    }
                    for (const auto& pin : definition->defaults.pins)
                    {
                        if (pin.Identifier() == "Parameters")
                        {
                            LX::LXNodeItemSpec item;
                            item.key = "Parameters";
                            item.label = "Parameters";
                            item.kind = LX::LXNodeItemKind::Vector2;
                            item.valuePin = "Parameters";
                            items.Register(type, std::move(item));
                        }
                    }
                }
            }

            void Compile()
            {
                if (attempted && observedRevision == document.Revision())
                {
                    return;
                }
                observedRevision = document.Revision();
                asset.graph = document.Graph();
                const auto identity = asset.SemanticIdentity();
                if (attempted && identity == semantic)
                {
                    return;
                }
                semantic = identity;
                attempted = true;
                compiled = LX::CompileInputGraph(asset, ++generation);
                for (const auto& node : asset.graph.Nodes())
                {
                    styles.ClearNodeStyle(node.id);
                }
                for (const auto& issue : compiled.diagnostics)
                {
                    const auto* node = asset.graph.FindNode(issue.node);
                    if (node && issue.severity == LX::Issue::Severity::Error)
                    {
                        auto style = styles.ForNodeType(node->type);
                        style.header = {0.60f, 0.16f, 0.13f, 1.0f};
                        style.border = {0.95f, 0.25f, 0.16f, 1.0f};
                        styles.SetNodeStyle(node->id, style);
                    }
                }
            }
        };

        std::unique_ptr<Session> active;
        std::optional<std::filesystem::path> pendingOpen;
        bool pendingNew{};

        const char* PhaseName(Input::EventPhase phase)
        {
            switch (phase)
            {
            case Input::EventPhase::Started: return "Started";
            case Input::EventPhase::Performed: return "Performed";
            case Input::EventPhase::Completed: return "Completed";
            case Input::EventPhase::Canceled: return "Canceled";
            }
            return "Unknown";
        }

        const char* CancelName(Input::CancelReason reason)
        {
            switch (reason)
            {
            case Input::CancelReason::None: return "None";
            case Input::CancelReason::LayerBlocked: return "Layer blocked";
            case Input::CancelReason::UIOwnership: return "UI ownership";
            case Input::CancelReason::FocusLost: return "Focus lost";
            case Input::CancelReason::Paused: return "Paused";
            case Input::CancelReason::DeviceDisconnected: return "Device disconnected";
            case Input::CancelReason::DeviceReassigned: return "Device reassigned";
            case Input::CancelReason::DefinitionChanged: return "Definition changed";
            case Input::CancelReason::Rebound: return "Binding overridden";
            case Input::CancelReason::HistoryGap: return "History gap";
            case Input::CancelReason::ClockDiscontinuity: return "Clock discontinuity";
            case Input::CancelReason::SessionShutdown: return "Session shutdown";
            case Input::CancelReason::ScriptReload: return "Script reload";
            }
            return "Unknown";
        }

        std::vector<std::filesystem::path> Sources()
        {
            std::vector<std::filesystem::path> paths;
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator iterator(InternalPath::GetInstance()->DataPath,
                     std::filesystem::directory_options::skip_permission_denied, error), end;
                 iterator != end && !error; iterator.increment(error))
            {
                if (iterator->is_regular_file(error) && iterator->path().extension() == ".inputgraph")
                {
                    paths.push_back(iterator->path());
                }
            }
            std::sort(paths.begin(), paths.end());
            return paths;
        }

        void NewDocument()
        {
            LX::LXInputAsset asset;
            asset.graphId = FileGuid::CreateRandomV4().ToString();
            const auto layer = asset.graph.CreateNode("Input.Layer", 20.0f, 20.0f);
            const auto source = asset.graph.CreateNode("Input.Key", 20.0f, 220.0f);
            const auto output = asset.graph.CreateNode("Input.Signal.Button", 400.0f, 220.0f);
            asset.graph.SetProperty(source, "control", "57");
            asset.graph.SetProperty(output, "name", "Jump");
            asset.graph.SetProperty(output, "layer", std::to_string(layer));
            const auto sourcePin = asset.graph.FindNode(source)->pins.front().id;
            const auto outputPin = asset.graph.FindNode(output)->pins.front().id;
            asset.graph.Connect(sourcePin, outputPin);
            active = std::make_unique<Session>(std::move(asset),
                InternalPath::GetInstance()->DataPath / "InputGraph" / "NewInput.inputgraph", false);
        }

        bool Load(const std::filesystem::path& path, std::string& error)
        {
            auto asset = LX::LXInputAsset::Load(path, &error);
            if (!asset)
            {
                return false;
            }
            active = std::make_unique<Session>(std::move(*asset), path, true);
            return true;
        }

        void RequestOpen(const std::filesystem::path& path)
        {
            if (active && active->document.Dirty())
            {
                pendingOpen = path;
            }
            else
            {
                std::string error;
                if (!Load(path, error) && active)
                {
                    active->message = std::move(error);
                }
            }
        }

        void ExportAccessors(Session& session)
        {
            if (!session.compiled.program)
            {
                session.message = "Resolve validation errors before exporting accessors";
                return;
            }
            Input::InputAccessorSources sources;
            if (!Input::GenerateInputAccessors(*session.compiled.program, session.accessorName, sources, session.message))
            {
                return;
            }
            auto header = session.path.parent_path() / (session.accessorName + ".generated.h");
            auto managed = session.path.parent_path() / (session.accessorName + ".generated.cs");
            std::ofstream cpp(header, std::ios::binary | std::ios::trunc);
            std::ofstream csharp(managed, std::ios::binary | std::ios::trunc);
            cpp << sources.cpp;
            csharp << sources.csharp;
            cpp.flush();
            csharp.flush();
            session.message = cpp && csharp ? "Exported C++ and C# accessors beside the InputGraph source"
                                           : "Could not write both accessor files";
        }

        void DrawDiagnostics(Session& session)
        {
            ImGui::TextUnformatted("Validation");
            for (const auto& issue : session.compiled.diagnostics)
            {
                const std::string text = "[" + issue.code + "] " + issue.message;
                if (ImGui::Selectable(text.c_str(), session.canvas.selectedNode == issue.node))
                {
                    session.canvas.selectedNode = issue.node;
                    session.canvas.selectedNodes = {issue.node};
                }
            }
            if (session.compiled.program)
            {
                const auto& definition = session.compiled.program->GetDefinition();
                ImGui::Text("%zu signals / %zu bindings / %zu layers", definition.signals.size(),
                            definition.bindings.size(), definition.layers.size());
                ImGui::Text("Semantic: %016llx", static_cast<unsigned long long>(session.compiled.program->GetSemanticHash()));
            }
            const auto* selected = session.document.Graph().FindNode(session.canvas.selectedNode);
            if (selected && selected->type.starts_with("Input.Signal."))
            {
                ImGui::Separator();
                ImGui::Text("Stable signal node: %llu", static_cast<unsigned long long>(selected->id));
                const auto found = selected->properties.find("layer");
                const std::string current = found == selected->properties.end() ? "0" : found->second;
                if (ImGui::BeginCombo("Layer", current.c_str()))
                {
                    for (const auto& layer : session.document.Graph().Nodes())
                    {
                        if (layer.type != "Input.Layer")
                        {
                            continue;
                        }
                        const auto id = std::to_string(layer.id);
                        const auto name = layer.properties.find("name");
                        const auto label = (name == layer.properties.end() ? "Layer" : name->second) + " (" + id + ")";
                        if (ImGui::Selectable(label.c_str(), current == id))
                        {
                            session.document.Execute(LX::LXSetProperty{selected->id, "layer", id}, session.document.Revision());
                            break;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }
    }

    bool Open(const std::filesystem::path& path, std::string& error)
    {
        editor::open_window(EditorWindowName::kInputGraphs);
        if (active && active->document.Dirty())
        {
            pendingOpen = path;
            return true;
        }
        return Load(path, error);
    }

    void Draw()
    {
        if (!active)
        {
            NewDocument();
        }
        if (pendingOpen || pendingNew)
        {
            ImGui::OpenPopup("Discard unsaved InputGraph changes?");
        }
        if (ImGui::BeginPopupModal("Discard unsaved InputGraph changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("The current document has unsaved changes.");
            if (ImGui::Button("Discard and continue"))
            {
                const auto path = pendingOpen;
                pendingOpen.reset();
                const bool create = pendingNew;
                pendingNew = false;
                if (create)
                {
                    NewDocument();
                }
                else if (path)
                {
                    std::string error;
                    if (!Load(*path, error))
                    {
                        active->message = std::move(error);
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                pendingOpen.reset();
                pendingNew = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button("New"))
        {
            if (active->document.Dirty())
            {
                pendingNew = true;
            }
            else
            {
                NewDocument();
            }
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("Open", "Select InputGraph"))
        {
            static std::vector<std::filesystem::path> paths;
            if (ImGui::IsWindowAppearing())
            {
                paths = Sources();
            }
            for (const auto& path : paths)
            {
                if (ImGui::Selectable(path.filename().string().c_str()))
                {
                    RequestOpen(path);
                    break;
                }
            }
            ImGui::EndCombo();
        }
        auto& session = *active;
        ImGui::InputText("Source path", &session.pathText);
        session.Compile();
        ImGui::BeginDisabled(!session.compiled.program || session.document.HasActivePreview());
        if (ImGui::Button("Save and publish"))
        {
            const std::filesystem::path path(session.pathText);
            if (path.extension() != ".inputgraph")
            {
                session.message = "InputGraph source must use the .inputgraph extension";
            }
            else if (EditorAssetDatabase::Get().SaveInputGraph(session.asset, path, session.message))
            {
                session.path = path;
                session.document.AcceptSavedSnapshot();
                session.message = "Saved InputGraph; validated runtime definition is ready for the next input boundary";
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170.0f);
        ImGui::InputText("Accessor class", &session.accessorName);
        ImGui::SameLine();
        if (ImGui::Button("Export C++ / C#"))
        {
            ExportAccessors(session);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Undo"))
        {
            session.document.Execute(LX::LXUndo{}, session.document.Revision());
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo"))
        {
            session.document.Execute(LX::LXRedo{}, session.document.Revision());
        }
        ImGui::TextWrapped("%s", session.message.c_str());
        ImGui::Separator();
        ImGui::BeginChild("InputNodePalette", ImVec2(220.0f, 0.0f), true);
        ImGui::TextUnformatted("Add node");
        ImGui::TextWrapped("Key controls use physical scan codes. Gamepad sticks: 0/1; triggers: 2/3.");
        for (const auto& type : LX::InputNodeTypes())
        {
            const auto* definition = session.asset.graph.Definitions()->Find(type);
            if (ImGui::Selectable(definition->defaults.title.c_str()))
            {
                const auto result = session.document.Execute(LX::LXCreateNode{type, 80.0f, 80.0f}, session.document.Revision());
                session.canvas.selectedNode = result.created;
                session.canvas.selectedNodes = {result.created};
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("InputGraphCanvas", ImVec2((std::max)(200.0f, ImGui::GetContentRegionAvail().x - 310.0f), 0.0f), true);
        LX::DrawCanvas(session.document.GraphForCanvas(), session.canvas, session.styles, session.items, &session.document);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("InputGraphValidation", ImVec2(0.0f, 0.0f), true);
        DrawDiagnostics(session);
        ImGui::EndChild();
    }

    void DrawInspector(InputSessionComponent& component)
    {
        ImGui::PushID(&component);
        static editor::widgets::property_layout_state configurationLayout;
        const editor::widgets::property_sheet configurationSheet(configurationLayout,
            {"User", "Gamepad index", "Share keyboard / mouse", "Evaluate UI domain", "InputGraph"});
        auto configuration = component.GetConfiguration();
        const float userWidth = configurationSheet.line("User");
        ImGui::SetNextItemWidth(userWidth);
        editor::widgets::track_property_input("##User", userWidth);
        bool changed = ImGui::InputScalar("##User", ImGuiDataType_U64, &configuration.user);
        const float gamepadWidth = configurationSheet.line("Gamepad index");
        ImGui::SetNextItemWidth(gamepadWidth);
        editor::widgets::track_property_input("##GamepadIndex", gamepadWidth);
        changed |= ImGui::InputInt("##GamepadIndex", &configuration.controllerIndex);
        configurationSheet.line("Share keyboard / mouse");
        changed |= ImGui::Checkbox("##ShareKeyboardMouse", &configuration.shareKeyboard);
        configurationSheet.line("Evaluate UI domain");
        changed |= ImGui::Checkbox("##EvaluateUIDomain", &configuration.evaluateUI);
        if (changed)
        {
            component.RequestConfiguration(configuration);
        }
        const auto graphPath = DataSystems->GetFilePath(configuration.graph);
        const std::string caption = graphPath.empty() ? "Select InputGraph" : graphPath.filename().string();
        const float graphWidth = configurationSheet.line("InputGraph");
        ImGui::SetNextItemWidth(graphWidth);
        editor::widgets::track_property_input("##InputGraph", graphWidth);
        if (ImGui::BeginCombo("##InputGraph", caption.c_str()))
        {
            static std::vector<std::filesystem::path> paths;
            if (ImGui::IsWindowAppearing())
            {
                paths = Sources();
            }
            for (const auto& path : paths)
            {
                const auto guid = DataSystems->GetFileGuid(path);
                if (ImGui::Selectable(path.filename().string().c_str(), guid == configuration.graph))
                {
                    configuration.graph = guid;
                    component.RequestConfiguration(configuration);
                }
            }
            ImGui::EndCombo();
        }
        if (!graphPath.empty() && ImGui::Button("Open InputGraph editor"))
        {
            std::string error;
            Open(graphPath, error);
        }
        const auto program = component.GetDiagnosticProgram();
        if (!program)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("No live input session. Enter Play with a valid InputGraph.");
            ImGui::PopTextWrapPos();
            ImGui::PopID();
            return;
        }
        if (ImGui::CollapsingHeader("Live input diagnostics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const auto domain : {Input::Domain::Game, Input::Domain::UI})
            {
                const auto frame = component.GetDiagnosticFrame(domain);
                if (!frame)
                {
                    continue;
                }
                ImGui::Text("%s: sequence %llu, generation %llu%s", domain == Input::Domain::Game ? "Game" : "UI",
                    static_cast<unsigned long long>(frame->GetBoundary().sequence),
                    static_cast<unsigned long long>(frame->GetDefinitionGeneration()),
                    frame->HasHistoryGap() ? " [HISTORY GAP]" : "");
                for (const auto& state : frame->GetStates())
                {
                    const auto& definitions = program->GetDefinition().signals;
                    const auto found = std::find_if(definitions.begin(), definitions.end(), [&](const auto& signal) {
                        return signal.id == state.signal;
                    });
                    const char* name = found == definitions.end() ? "Unloaded signal" : found->name.c_str();
                    ImGui::Text("%s = (%.3f, %.3f) held:%d pressed:%d released:%d", name,
                                state.value.x, state.value.y, state.held, state.pressed, state.released);
                }
                for (const auto& event : frame->GetEvents())
                {
                    ImGui::Text("Signal %llu %s / %s at %lld%s",
                        static_cast<unsigned long long>(event.signal.low), PhaseName(event.phase),
                        CancelName(event.reason), static_cast<long long>(event.effectiveTime),
                        event.late ? " [LATE]" : "");
                    ImGui::TextDisabled("Source %lld, sequence %llu, routing %llu, device generation %llu",
                        static_cast<long long>(event.sourceTime), static_cast<unsigned long long>(event.sequence),
                        static_cast<unsigned long long>(event.routingEpoch),
                        static_cast<unsigned long long>(event.deviceEpoch));
                }
            }
        }
        if (ImGui::CollapsingHeader("Rebind this user"))
        {
            struct RebindEditor
            {
                const InputSessionComponent* component{};
                Input::BindingID binding{};
                int sourceIndex{};
                Input::SourceKind kind{Input::SourceKind::Key};
                std::uint32_t control{};
                std::string message;
            };
            static RebindEditor rebind;
            if (rebind.component != &component)
            {
                rebind = {};
                rebind.component = &component;
            }
            const auto& definition = program->GetDefinition();
            const auto current = std::find_if(definition.bindings.begin(), definition.bindings.end(), [&](const auto& binding) {
                return binding.id == rebind.binding;
            });
            const std::string bindingCaption = current == definition.bindings.end() ? "Select binding"
                : std::to_string(current->id.high) + ":" + std::to_string(current->id.low);
            if (ImGui::BeginCombo("Stable binding", bindingCaption.c_str()))
            {
                for (const auto& binding : definition.bindings)
                {
                    const std::string label = std::to_string(binding.id.high) + ":" + std::to_string(binding.id.low);
                    if (ImGui::Selectable(label.c_str(), binding.id == rebind.binding))
                    {
                        rebind.binding = binding.id;
                        rebind.sourceIndex = 0;
                        if (!binding.sources.empty())
                        {
                            rebind.kind = binding.sources.front().control.kind;
                            rebind.control = binding.sources.front().control.code;
                        }
                        component.CancelRebindCapture();
                    }
                }
                ImGui::EndCombo();
            }
            // Re-find after selection; never keep a pointer into a mutable session definition.
            const auto binding = std::find_if(definition.bindings.begin(), definition.bindings.end(), [&](const auto& item) {
                return item.id == rebind.binding;
            });
            if (binding != definition.bindings.end() && !binding->sources.empty())
            {
                ImGui::SliderInt("Composite source", &rebind.sourceIndex, 0, static_cast<int>(binding->sources.size() - 1));
                rebind.sourceIndex = std::clamp(rebind.sourceIndex, 0, static_cast<int>(binding->sources.size() - 1));
                const char* kinds[]{"Key", "MouseButton", "MouseDelta", "PointerPosition", "MouseWheel", "GamepadButton", "GamepadAxis"};
                int kind = static_cast<int>(rebind.kind);
                if (ImGui::Combo("Source kind", &kind, kinds, static_cast<int>(std::size(kinds))))
                {
                    rebind.kind = static_cast<Input::SourceKind>(kind);
                }
                ImGui::InputScalar("Physical control", ImGuiDataType_U32, &rebind.control);
                if (ImGui::Button("Listen for control"))
                {
                    component.BeginRebindCapture();
                    rebind.message = "Release held controls, then press the replacement control";
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel capture"))
                {
                    component.CancelRebindCapture();
                    rebind.message = "Capture canceled; binding unchanged";
                }
                if (component.IsRebindCaptureActive())
                {
                    ImGui::TextDisabled("Listening... capture stops after 10 seconds or disconnect");
                }
                if (const auto candidate = component.GetRebindCandidate())
                {
                    ImGui::Text("Captured %s control %u (device %llu)", kinds[static_cast<int>(candidate->control.kind)],
                        candidate->control.code, static_cast<unsigned long long>(candidate->device));
                    if (ImGui::Button("Use captured candidate"))
                    {
                        rebind.kind = candidate->control.kind;
                        rebind.control = candidate->control.code;
                        component.CancelRebindCapture();
                    }
                }
                if (ImGui::Button("Validate and apply override"))
                {
                    Input::InputBindingOverride override;
                    override.graph = definition.id;
                    override.binding = binding->id;
                    override.sources = binding->sources;
                    auto& source = override.sources[static_cast<std::size_t>(rebind.sourceIndex)];
                    const auto previous = source.control;
                    const auto category = [](Input::ControlID value) {
                        switch (value.kind)
                        {
                        case Input::SourceKind::MouseDelta: return 1;
                        case Input::SourceKind::PointerPosition: return 2;
                        case Input::SourceKind::MouseWheel: return 3;
                        case Input::SourceKind::GamepadAxis: return value.code < 2 ? 5 : 4;
                        default: return 0;
                        }
                    };
                    source.control = {rebind.kind, rebind.control};
                    bool conflict = false;
                    for (const auto& other : definition.bindings)
                    {
                        for (std::size_t index = 0; index < other.sources.size(); ++index)
                        {
                            if (other.id == binding->id && index == static_cast<std::size_t>(rebind.sourceIndex))
                            {
                                continue;
                            }
                            conflict |= other.sources[index].control == source.control;
                        }
                    }
                    std::vector<Input::InputDiagnostic> diagnostics;
                    if (category(previous) != category(source.control))
                    {
                        rebind.message = "Replacement must preserve source type, delta lifetime and coordinate meaning";
                    }
                    else if (conflict)
                    {
                        rebind.message = "Control conflicts with another binding. Choose another control";
                    }
                    else if (!Input::ApplyInputOverrides(*program, std::span(&override, 1), diagnostics))
                    {
                        rebind.message = diagnostics.empty() ? "Override rejected" : diagnostics.front().message;
                    }
                    else
                    {
                        component.RequestRebind({std::move(override)});
                        component.CancelRebindCapture();
                        rebind.message = "Validated override queued for the next input boundary";
                    }
                }
            }
            ImGui::TextWrapped("%s", rebind.message.c_str());
            if (ImGui::Button("Reset saved user overrides"))
            {
                ImGui::OpenPopup("Reset this user's overrides?");
            }
            if (ImGui::BeginPopupModal("Reset this user's overrides?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("Restore this graph's original bindings for this user, including an incompatible saved profile?");
                if (ImGui::Button("Restore original bindings"))
                {
                    component.RequestResetOverrides();
                    component.CancelRebindCapture();
                    rebind.message = "Reset queued; the empty override profile is saved after the input boundary";
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Keep overrides"))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            for (const auto& diagnostic : component.GetRebindDiagnostics())
            {
                ImGui::TextWrapped("%s", diagnostic.message.c_str());
            }
        }
        ImGui::PopID();
    }
}
