#include "MaterialGraphWindow.h"
#include "ProfileScope.h"
#include "MaterialGraphPresentation.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "EditorAssetDatabase.h"
#include "EditorObjectOperations.h"
#include "EditorImGuiTexture.h"
#include "EditorAssetDragPayload.h"
#include "Commands/CommandSupport.h"
#include "ReflectionUndo.h"
#include "SceneManager.h"
#include "Scene.h"
#include "MeshRenderer.h"
#include "Assets/ModelAssetGeneration.h"
#include "../ImGuiHelper/EditorIcons.h"
#include "Material.h"
#include "MaterialGraphProduct.h"
#include "MaterialGraphSceneInput.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "DataSystem.h"
#include "PathFinder.h"
#include "../../Lattice/Core/LXDocument.h"
#include "../../Lattice/Core/LXNodeDefinition.h"
#include <imgui_stdlib.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <mutex>
#include <fstream>
#include <limits>
#include <chrono>

namespace editor::material_editing
{
namespace
{
using namespace LX;
using namespace CommandCore;

struct Session
{
    EntityHandle target;
    std::size_t component = 0;
    FileGuid guid;
    std::filesystem::path path;
    std::filesystem::path recoveryPath;
    LXMaterialAsset asset;
    LXDocument document;
    LXStyleSheet styles;
    LXNodeItemRegistry items;
    CanvasState canvas;
    std::string name;
    std::string message;
    std::string search;
    std::string diskPayload;
    std::uint64_t uiFrames = 0;
    std::uint64_t analysisRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t analysisObservedRevision = std::numeric_limits<std::uint64_t>::max();
    double analysisChangedAt = 0.0;
    std::optional<LXMaterialProgram> analysisProgram;
    std::optional<material_graph::Selection> analysisSelection;
    std::vector<LXMaterialDiagnostic> analysisIssues;
    bool fit = true;
    bool diagnostics = false;
    bool materialDetails = true;
    bool confirmReload = false;
    bool previewPinned = false;
    std::chrono::steady_clock::time_point previewVisible{};
    std::shared_ptr<const material_graph::SceneMaterialSource> previewSource;
    std::uint64_t previewRevision = 0;

    Session(EntityHandle object, const MeshRenderer& renderer, FileGuid identity, std::filesystem::path source,
            LXMaterialAsset material, bool persisted)
        : target(object), component(renderer.GetInstanceID()), guid(identity), path(std::move(source)),
          asset(std::move(material)), document(asset.graph, {}, persisted), styles(BlenderStyles(asset.Definitions())),
          items(MaterialItems(asset.Definitions())),
          name(renderer.m_Material ? renderer.m_Material->m_name : "Material")
    {
        if (persisted)
        {
            std::ifstream sourceFile(path, std::ios::binary);
            diskPayload.assign(std::istreambuf_iterator<char>(sourceFile), {});
        }
        items.SetPinPredicates(
            [this](const Pin& pin) {
                const auto metadata = asset.SocketState(0, pin);
                return metadata.enabled && !metadata.hidden;
            },
            [this](const Pin& pin) {
                return std::ranges::any_of(document.Graph().Links(), [&](const Link& link) {
                    return link.input == pin.id || link.output == pin.id;
                });
            });
        items.SetTextureResolver([this](const std::string& reference) {
            ce::profile_scope profile{ce::marker<"MaterialNodeTextureResolve">()};
            Uuid::Uuid16 id;
            if (!Uuid::TryParse(reference, id))
            {
                return ImTextureID_Invalid;
            }
            if (const auto* renderer = Renderer())
            {
                const auto instance = renderer->m_Material ? renderer->m_Material->GetMaterialGraphInstance() : nullptr;
                if (instance)
                {
                    for (const auto& texture : instance->textures)
                    {
                        if (texture.assetId.value == id)
                        {
                            return EditorImGuiTexture::From(texture.owner);
                        }
                    }
                }
                if (renderer->m_modelGeneration && renderer->m_modelGeneration->FindTexture(id))
                {
                    return EditorImGuiTexture::From(
                        DataSystems->ResolveModelGenerationTexture(*renderer->m_modelGeneration, id));
                }
            }
            return static_cast<ImTextureID>(EditorImGuiTexture::From(DataSystems->LoadTextureGUID(FileGuid(id))));
        });
        items.SetResourceEditor([this]() -> std::optional<std::string> {
            std::optional<std::string> result;
            if (Editable() && ImGui::BeginDragDropTarget())
            {
                if (const auto* payload = ImGui::AcceptDragDropPayload("Texture"))
                {
                    const auto guid = DataSystems->GetFileGuid(asset_drag::path_of(*payload));
                    if (guid != FileGuid{})
                    {
                        result = guid.ToString();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            return result;
        });
        const auto stylePath = PathFinder::RuntimeDataPath("Editor/Styles/Material.lxstyle");
        if (std::filesystem::exists(stylePath))
        {
            if (auto loaded = LXStyleSheet::Load(stylePath.string(), &message))
            {
                if (loaded->SourceVersion() < 6)
                {
                    loaded->canvas.gridPattern = LXGridPattern::Dots;
                    loaded->canvas.gridDotRadius = styles.canvas.gridDotRadius;
                }
                styles = std::move(*loaded);
            }
        }
    }

    MeshRenderer* Renderer() const
    {
        auto* scene = SceneManagers->GetActiveScene();
        auto* object = scene ? scene->Resolve(target) : nullptr;
        auto* renderer = object ? object->GetComponent<MeshRenderer>() : nullptr;
        return renderer && renderer->GetInstanceID() == component ? renderer : nullptr;
    }

    LXMaterialAsset Snapshot() const
    {
        auto result = asset;
        result.graph = document.Graph();
        if (!result.graph.FindNode(result.activeOutput))
        {
            result.activeOutput = 0;
        }
        return result;
    }

    void Analyze()
    {
        ce::profile_scope profile{ce::marker<"MaterialNodeAnalysis">()};
        if (analysisObservedRevision != document.Revision())
        {
            analysisObservedRevision = document.Revision();
            analysisChangedAt = ImGui::GetTime();
        }
        if (analysisRevision == document.Revision())
            return;
        // LXDocument revisions also cover pan, zoom and layout edits. Wait for
        // interaction to settle instead of compiling the graph on every frame.
        if (analysisRevision != std::numeric_limits<std::uint64_t>::max() &&
            ImGui::GetTime() - analysisChangedAt < 0.25)
            return;
        analysisRevision = document.Revision();
        analysisIssues.clear();
        analysisSelection.reset();
        analysisProgram = GenerateMaterialSlang(Snapshot(), &analysisIssues);
        if (!analysisProgram)
            return;
        material_graph::Capabilities capabilities;
        capabilities.coreForward = capabilities.layeredLookup = capabilities.refraction =
            capabilities.subsurface = capabilities.volume = true;
        material_graph::Selection selection;
        if (material_graph::SelectRoute(*analysisProgram, capabilities, {}, selection, analysisIssues))
            analysisSelection = std::move(selection);
    }

    bool Editable() const
    {
        const auto* renderer = Renderer();
        return renderer && !EditorObjectOperations::IsEditLocked(renderer->GetOwner(), true) &&
               PathFinder::IsAssetAuthoringEnabled();
    }

    bool Save()
    {
        if (document.HasActivePreview())
        {
            message = "Finish the current drag before saving.";
            return false;
        }
        if (std::filesystem::exists(path))
        {
            std::ifstream sourceFile(path, std::ios::binary);
            const std::string current{std::istreambuf_iterator<char>(sourceFile), {}};
            if (current != diskPayload)
            {
                message = "The graph changed on disk. Reload it before saving.";
                return false;
            }
        }
        if (!Editable())
        {
            message = "The target is unavailable or locked.";
            return false;
        }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            message = error.message();
            return false;
        }
        const auto candidate = Snapshot();
        if (!candidate.Save(path, &message))
        {
            return false;
        }
        diskPayload = LXMaterialArchive::Write(candidate);
        if (EditorAssetDatabase::Get().CreateMeta(path, guid) != guid)
        {
            message = "Graph written, but its asset identity could not be registered. Save again before applying.";
            return false;
        }
        if (!document.AcceptSavedSnapshot())
        {
            message = "Finish the current drag before saving.";
            return false;
        }
        asset = candidate;
        message = "Saved " + path.filename().string();
        return true;
    }

    bool Apply()
    {
        auto* renderer = Renderer();
        if (!Editable() || !renderer)
        {
            message = "The target is unavailable or locked.";
            return false;
        }
        if (document.Dirty() || !std::filesystem::exists(path))
        {
            message = "Save the graph before applying it to the Scene.";
            return false;
        }
        const auto before = renderer->m_Material;
        const auto previousBase = renderer->m_materialBaseGuid;
        auto candidate = before ? own::make_shared<Material>(*before) : own::make_shared<Material>();
        material_graph::InstanceDescription description;
        if (before && before->HasMaterialGraph() &&
            before->GetMaterialGraphInstance()->description.graphId.value == guid.m_guid)
        {
            description = before->GetMaterialGraphInstance()->description;
        }
        description.graphId.value = guid.m_guid;
        candidate->m_name = name.empty() ? "Material" : name;
        if (!DataSystems->ConfigureMaterialGraphAuthoring(*candidate, Snapshot(), description, message))
        {
            message = "Apply failed. Scene and preview keep the last accepted material. " + message;
            return false;
        }
        const auto apply = [handle = target, identity = component](const own::shared_owner<Material>& material,
                                                                   FileGuid base) {
            auto* scene = SceneManagers->GetActiveScene();
            auto* object = scene ? scene->Resolve(handle) : nullptr;
            auto* component = object ? object->GetComponent<MeshRenderer>() : nullptr;
            if (component && component->GetInstanceID() == identity)
            {
                component->SetMaterial(material);
                component->m_materialBaseGuid = base;
            }
        };
        Meta::MakeCustomChangeCommand([apply, before, previousBase] { apply(before, previousBase); },
                                      [apply, candidate] { apply(candidate, {}); });
        message = "Applied to MeshRenderer. Save the Scene to retain this binding.";
        return true;
    }

    bool Reload(bool discard)
    {
        if (document.Dirty() && !discard)
        {
            message = "Reload requires confirmation while the graph has unsaved edits.";
            return false;
        }
        // Explicit reimport must validate the primary file. Load() may recover
        // an older .bak at startup, which must not silently replace this document.
        std::ifstream sourceFile(path, std::ios::binary);
        if (!sourceFile)
        {
            message = "Import failed. The open graph and applied material are preserved. Cannot open shadergraph.";
            return false;
        }
        std::string payload{std::istreambuf_iterator<char>(sourceFile), {}};
        if (sourceFile.bad())
        {
            message = "Import failed. The open graph and applied material are preserved. Cannot read shadergraph.";
            return false;
        }
        auto loaded = LXMaterialArchive::Read(payload, asset.Definitions(), &message);
        if (!loaded)
        {
            message = "Import failed. The open graph and applied material are preserved. " + message;
            return false;
        }
        asset = std::move(*loaded);
        diskPayload = std::move(payload);
        document = LXDocument(asset.graph, {}, true);
        analysisRevision = std::numeric_limits<std::uint64_t>::max();
        analysisObservedRevision = std::numeric_limits<std::uint64_t>::max();
        canvas = {};
        fit = !asset.graph.Layout().view.saved;
        message = "Reloaded " + path.filename().string();
        return true;
    }

    bool Execute(const LXCommand& command, std::uint64_t revision)
    {
        if (!Editable())
        {
            message = "The target is unavailable or locked.";
            return false;
        }
        const auto result = document.Execute(command, revision);
        message = result.message.empty() ? result.code : result.message;
        return result.applied;
    }

    void Fit()
    {
        const auto& graph = document.Graph();
        if (graph.Nodes().empty())
        {
            return;
        }
        ImVec2 minimum{FLT_MAX, FLT_MAX}, maximum{-FLT_MAX, -FLT_MAX};
        for (const auto& node : graph.Nodes())
        {
            const auto& layout = *graph.FindLayout(node.id);
            const auto size = MeasureNode(node, layout, styles.ForNode(node), items);
            minimum.x = std::min(minimum.x, layout.x);
            minimum.y = std::min(minimum.y, layout.y);
            maximum.x = std::max(maximum.x, layout.x + size.width);
            maximum.y = std::max(maximum.y, layout.y + size.height);
        }
        const float dpi = CanvasUiScale();
        const auto size = ImGui::GetContentRegionAvail();
        const float width = maximum.x - minimum.x + 90.0f;
        const float height = maximum.y - minimum.y + 80.0f;
        canvas.zoom =
            std::clamp(std::min(size.x / (width * dpi), size.y / (height * dpi)), styles.canvas.minZoom, 1.0f);
        canvas.pan = {size.x * 0.5f - (minimum.x + maximum.x) * 0.5f * canvas.zoom * dpi,
                      size.y * 0.5f - (minimum.y + maximum.y) * 0.5f * canvas.zoom * dpi};
        canvas.viewApplied = true;
        document.Execute(
            LXSetView{ViewLayout{true, (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, canvas.zoom}},
            document.Revision());
    }
};

std::mutex sessionMutex;
std::vector<std::unique_ptr<Session>> sessions;
Session* active = nullptr;
std::uint64_t previewSerial = 0;
std::shared_ptr<const material_graph::SceneMaterialSource> inspectorPreview;
std::uint64_t inspectorPreviewRevision{};
std::chrono::steady_clock::time_point inspectorPreviewVisible{};
std::uint32_t observedSceneId{};

void ReconcileSceneLocked()
{
    const auto* scene = SceneManagers->GetActiveScene();
    const auto sceneId = scene ? scene->GetSceneId() : 0u;
    if (sceneId != observedSceneId)
    {
        observedSceneId = sceneId;
        active = nullptr;
        inspectorPreview.reset();
        inspectorPreviewVisible = {};
        for (auto& session : sessions)
        {
            if (session->document.Dirty() && session->target.sceneId != sceneId)
            {
                // Scene/document IDs restart with the process. Do not overwrite
                // a recovery copy from an earlier Editor session.
                if (session->recoveryPath.empty())
                    session->recoveryPath = PathFinder::RelativeToBaseProject("Saved/Editor/MaterialDrafts") /
                        ("Scene_" + std::to_string(session->target.sceneId) + "_Document_" +
                         std::to_string(session->document.DocumentId()) + "_" +
                         FileGuid::CreateRandomV4().ToString() + ".shadergraph");
                const auto& recovery = session->recoveryPath;
                std::error_code error;
                std::filesystem::create_directories(recovery.parent_path(), error);
                if (error) session->message = "Draft recovery directory: " + error.message();
                else session->Snapshot().Save(recovery, &session->message);
            }
            session->previewPinned = false;
            session->previewSource.reset();
            session->canvas.selectedNodes.clear();
            session->canvas.selectedNode = 0;
        }
    }
    if (active && !active->Renderer()) active = nullptr;
    // Retain unsaved documents for explicit recovery; never bind them to a new Scene.
    std::erase_if(sessions, [](const auto& session) {
        return session.get() != active && !session->Renderer() && !session->document.Dirty();
    });
}

std::array<std::shared_ptr<const material_graph::SceneMaterialSource>, 2> previewFloor;
std::string previewFloorError;

bool PreparePreviewFloor()
{
    if (previewFloor[0] && previewFloor[1]) return true;
    if (!previewFloorError.empty()) return false;
    LXMaterialAsset asset;
    const Id surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    asset.activeOutput = asset.CreateNode("ShaderNodeOutputMaterial", 510, 50);
    const auto pin = [&](Id node, const char* name) {
        const auto& pins = asset.graph.FindNode(node)->pins;
        return std::ranges::find_if(pins, [&](const Pin& p) { return p.Identifier() == name; })->id;
    };
    asset.blackboard.push_back({1, "baseColor", "Base Color", PinType::Color,
                               std::array<double, 4>{.12, .12, .12, 1}, LXColorSpace::Linear, true});
    const Id color = asset.CreateNode("LXParameterColor", -300, 0);
    asset.graph.SetProperty(color, "parameter", "1");
    asset.graph.SetProperty(color, "valueSource", "socket");
    asset.graph.SetSocketValue(pin(color, "Value"), asset.blackboard.front().value);
    asset.graph.Connect(pin(color, "Value"), pin(surface, "Base Color"));
    asset.graph.SetSocketValue(pin(surface, "Roughness"), .85);
    asset.graph.Connect(pin(surface, "BSDF"), pin(asset.activeOutput, "Surface"));
    material_graph::InstanceDescription description;
    Uuid::TryParse("815d8195-c028-4819-8626-8d41f532fb42", description.graphId.value);
    Material dark;
    if (!DataSystems->ConfigureMaterialGraphAuthoring(dark, asset, description, previewFloorError)) return false;
    Material light(dark);
    if (!light.TrySetMaterialGraphParameter(1, std::array<double, 4>{.48, .48, .48, 1}, previewFloorError)) return false;
    previewFloor[0] = material_graph::SceneMaterialSource::Capture(dark);
    previewFloor[1] = material_graph::SceneMaterialSource::Capture(light);
    return true;
}

void RefreshPreview(Session& session)
{
    const auto* renderer = session.Renderer();
    auto source = renderer && renderer->m_Material
                      ? material_graph::SceneMaterialSource::Capture(*renderer->m_Material) : nullptr;
    if (!source || !source->instance) { session.previewSource.reset(); return; }
    if (!session.previewSource || session.previewSource->instance != source->instance ||
        session.previewSource->coverage.flags != source->coverage.flags)
    {
        session.previewSource = std::move(source);
        session.previewRevision = ++previewSerial;
    }
}

bool OpenLocked(MeshRenderer& renderer, std::string& error, bool create = false)
{
    ReconcileSceneLocked();
    auto* owner = renderer.GetOwner();
    if (!owner || !owner->GetScene() || !PathFinder::IsAssetAuthoringEnabled())
    {
        error = "A Material node editor requires an authoring Scene object.";
        return false;
    }
    const auto target = owner->GetScene()->HandleOf(owner->m_index);
    const auto instance = renderer.m_Material ? renderer.m_Material->GetMaterialGraphInstance() : nullptr;
    FileGuid guid = instance && !create ? FileGuid(instance->description.graphId.value) : FileGuid{};
    for (const auto& session : sessions)
    {
        if (!create && session->target == target && session->component == renderer.GetInstanceID() &&
            (guid == FileGuid{} || session->guid == guid))
        {
            active = session.get();
            queue_window_request(EditorWindowName::kMaterialGraph, window_request::focus);
            return true;
        }
    }
    LXMaterialAsset asset;
    std::filesystem::path path;
    const bool persisted = guid != FileGuid{};
    if (persisted)
    {
        path = DataSystems->GetMaterialGraphSourcePath(guid);
        auto loaded = LXMaterialAsset::Load(path, asset.Definitions(), &error);
        if (!loaded)
        {
            return false;
        }
        asset = std::move(*loaded);
    }
    else
    {
        guid = FileGuid::CreateRandomV4();
        path = PathFinder::RelativeToMaterial("") / ("Material_" + guid.ToString() + ".shadergraph");
        const Id surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0.0f, 0.0f);
        asset.activeOutput = asset.CreateNode("ShaderNodeOutputMaterial", 510.0f, 50.0f);
        const auto* bsdf = asset.graph.FindNode(surface);
        const auto* output = asset.graph.FindNode(asset.activeOutput);
        const auto find = [](const Node& node, const std::string& name) {
            return std::ranges::find_if(node.pins, [&](const Pin& pin) { return pin.Identifier() == name; })->id;
        };
        asset.graph.Connect(find(*bsdf, "BSDF"), find(*output, "Surface"));
    }
    sessions.push_back(std::make_unique<Session>(target, renderer, guid, path, std::move(asset), persisted));
    active = sessions.back().get();
    active->fit = !active->document.Graph().Layout().view.saved;
    queue_window_request(EditorWindowName::kMaterialGraph, window_request::focus);
    return true;
}

CommandData Describe(const Session& session)
{
    auto result = CommandData::Object();
    result.Set("target", CommandData::String(EditorObjectOperations::ObjectId(session.target)));
    result.Set("graph", CommandData::String(session.guid.ToString()));
    result.Set("path", CommandData::String(session.path.string()));
    result.Set("revision", CommandData::Int(session.document.Revision()));
    result.Set("document", CommandData::Int(session.document.DocumentId()));
    result.Set("dirty", CommandData::Bool(session.document.Dirty()));
    result.Set("uiFrames", CommandData::Int(session.uiFrames));
    result.Set("editable", CommandData::Bool(session.Editable()));
    const auto preview = EnhancedSceneRenderer::GetLiveDisplaySnapshot().Get(EnhancedLiveDisplayTarget::MaterialPreview);
    auto previewState = CommandData::Object();
    previewState.Set("revision", CommandData::Int(session.previewRevision));
    previewState.Set("completedRevision", CommandData::Int(preview.key.historyRevision));
    previewState.Set("completedFrame", CommandData::Int(preview.completedFrameId));
    previewState.Set("renders", CommandData::Int(preview.promotionCount));
    previewState.Set("pinned", CommandData::Bool(session.previewPinned));
    previewState.Set("ready", CommandData::Bool(preview.ready && preview.previewComplete &&
        preview.key.historyRevision == session.previewRevision));
    result.Set("preview", std::move(previewState));
    auto nodes = CommandData::Array();
    for (const auto& node : session.document.Graph().Nodes())
    {
        auto entry = CommandData::Object();
        entry.Set("id", CommandData::Int(node.id));
        entry.Set("type", CommandData::String(node.type));
        entry.Set("title", CommandData::String(node.title));
        const auto* layout = session.document.Graph().FindLayout(node.id);
        const auto geometry = MeasureNode(node, *layout, session.styles.ForNode(node), session.items);
        entry.Set("height", CommandData::Double(geometry.height));
        entry.Set("width", CommandData::Double(geometry.width));
        entry.Set("collapsed", CommandData::Bool(layout->collapsed));
        auto pins = CommandData::Array();
        for (const auto& pin : node.pins)
        {
            auto socket = CommandData::Object();
            socket.Set("id", CommandData::Int(pin.id));
            socket.Set("name", CommandData::String(pin.Identifier()));
            socket.Set("type", CommandData::String(PinTypeName(pin.type)));
            socket.Set("input", CommandData::Bool(pin.direction == Direction::Input));
            socket.Set("visible", CommandData::Bool(session.items.PinRow(node, pin).has_value()));
            const auto position = PinPosition(node, *layout, pin, session.styles.ForNode(node), &session.items);
            socket.Set("y", CommandData::Double(position.y));
            pins.Append(std::move(socket));
        }
        entry.Set("pins", std::move(pins));
        nodes.Append(std::move(entry));
    }
    result.Set("nodes", std::move(nodes));
    auto links = CommandData::Array();
    for (const auto& link : session.document.Graph().Links())
    {
        auto entry = CommandData::Object();
        entry.Set("id", CommandData::Int(link.id));
        entry.Set("output", CommandData::Int(link.output));
        entry.Set("input", CommandData::Int(link.input));
        links.Append(std::move(entry));
    }
    result.Set("links", std::move(links));
    return result;
}

void AddMenu(Session& session)
{
    if (const auto type = DrawMaterialNodeAddMenu(session.asset.Definitions(), session.search))
    {
        const auto size = session.canvas.canvasSize;
        const float scale = session.canvas.zoom * std::max(0.1f, session.canvas.viewDpi);
        session.Execute(LXCreateNode{*type, (size.x * 0.5f - session.canvas.pan.x) / scale,
                                     (size.y * 0.5f - session.canvas.pan.y) / scale},
                        session.document.Revision());
    }
}

void DrawSession(Session& session)
{
    ce::profile_scope profile{ce::marker<"MaterialNodeEditorDraw">()};
    MaterialBarAction action = MaterialBarAction::None;
    {
        const MaterialHeaderScope header(session.styles.canvas.background);
        ImGui::BeginChild("##material_editor", ImVec2{}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::BeginMenuBar())
        {
            DrawMaterialContextSelector();
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Frame All", "Home"))
                {
                    session.fit = true;
                }
                ImGui::MenuItem("Diagnostics", nullptr, &session.diagnostics);
                ImGui::MenuItem("Node Details", nullptr, &session.materialDetails);
                ImGui::MenuItem("Grid", nullptr, &session.styles.canvas.showGrid);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Select"))
            {
                if (ImGui::MenuItem("All"))
                {
                    session.canvas.selectedNodes.clear();
                    for (const auto& node : session.document.Graph().Nodes())
                    {
                        session.canvas.selectedNodes.push_back(node.id);
                    }
                }
                if (ImGui::MenuItem("None"))
                {
                    session.canvas.selectedNodes.clear();
                    session.canvas.selectedNode = 0;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add", session.Editable()))
            {
                AddMenu(session);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Node", session.Editable()))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z"))
                {
                    session.Execute(LXUndo{}, session.document.Revision());
                }
                if (ImGui::MenuItem("Redo", "Ctrl+Y"))
                {
                    session.Execute(LXRedo{}, session.document.Revision());
                }
                if (ImGui::MenuItem("Delete", "Delete"))
                {
                    auto selected = session.canvas.selectedNodes;
                    if (selected.empty() && session.canvas.selectedNode)
                    {
                        selected.push_back(session.canvas.selectedNode);
                    }
                    session.Execute(LXRemoveNodes{std::move(selected)}, session.document.Revision());
                    session.canvas.selectedNodes.clear();
                    session.canvas.selectedNode = 0;
                }
                ImGui::EndMenu();
            }
            const auto materialMenu = [&]() {
                ImGui::TextDisabled("Open Materials");
                for (const auto& document : sessions)
                {
                    if (!document->Renderer()) continue;
                    ImGui::PushID(document.get());
                    if (ImGui::Selectable(document->name.c_str(), document.get() == active))
                    {
                        active = document.get();
                    }
                    ImGui::PopID();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("New Material Graph", nullptr, false, session.Editable()))
                {
                    action = MaterialBarAction::NewGraph;
                }
                if (ImGui::MenuItem(EditorIcon::Label<EditorIcon::Save, " Save Graph">))
                {
                    action = MaterialBarAction::Save;
                }
                if (ImGui::MenuItem("Apply to MeshRenderer", nullptr, false, session.Editable()))
                {
                    action = MaterialBarAction::Apply;
                }
                if (ImGui::MenuItem("Reload Graph"))
                {
                    action = MaterialBarAction::Reload;
                }
            };
            const auto overlayMenu = [&]() {
                ImGui::MenuItem("Diagnostics", nullptr, &session.diagnostics);
                ImGui::Separator();
                if (ImGui::MenuItem("Reload Style"))
                {
                    if (auto style = LXStyleSheet::Load(
                            PathFinder::RuntimeDataPath("Editor/Styles/Material.lxstyle").string(), &session.message))
                    {
                        if (style->SourceVersion() < 6)
                        {
                            style->canvas.gridPattern = LXGridPattern::Dots;
                            style->canvas.gridDotRadius = 0.65f;
                        }
                        session.styles = std::move(*style);
                    }
                }
                if (ImGui::MenuItem("Export Style"))
                {
                    const auto path = PathFinder::RuntimeDataPath("Editor/Styles/Material.lxstyle");
                    std::filesystem::create_directories(path.parent_path());
                    session.styles.Save(path.string(), &session.message);
                }
            };
            const auto toolbarAction = DrawMaterialDataBar(session.name, session.document.Dirty(), session.Editable(),
                                                           session.styles.canvas.showGrid, session.canvas.snapToGrid,
                                                           materialMenu, overlayMenu);
            if (toolbarAction != MaterialBarAction::None)
            {
                action = toolbarAction;
            }
            ImGui::EndMenuBar();
        }
    }
    if (action == MaterialBarAction::NewGraph)
    {
        std::string error;
        if (auto* renderer = session.Renderer())
        {
            if (!OpenLocked(*renderer, error, true))
            {
                session.message = error;
            }
        }
    }
    else if (action == MaterialBarAction::Save)
    {
        session.Save();
    }
    else if (action == MaterialBarAction::Apply)
    {
        session.Apply();
    }
    else if (action == MaterialBarAction::Reload)
    {
        if (session.document.Dirty())
        {
            session.confirmReload = true;
        }
        else
        {
            session.Reload(false);
        }
    }
    else if (action == MaterialBarAction::FrameAll)
    {
        session.fit = true;
    }
    else if (action == MaterialBarAction::ToggleGrid)
    {
        session.styles.canvas.showGrid = !session.styles.canvas.showGrid;
    }
    else if (action == MaterialBarAction::ToggleSnap)
    {
        session.canvas.snapToGrid = !session.canvas.snapToGrid;
    }
    else if (action == MaterialBarAction::ClosePanel)
    {
        queue_window_request(EditorWindowName::kMaterialGraph, window_request::close);
    }
    const ImVec2 bodyOrigin = ImGui::GetCursorPos();
    const float bodyWidth = ImGui::GetContentRegionAvail().x;
    const bool showDetails = session.materialDetails && bodyWidth >= 400.f * CanvasUiScale();
    const float detailsWidth = showDetails ? std::min(300.f * CanvasUiScale(), bodyWidth * 0.4f) : 0.f;
    const float canvasWidth = showDetails ? bodyWidth - detailsWidth - ImGui::GetStyle().ItemSpacing.x : bodyWidth;
    if (showDetails)
    {
    ImGui::SetCursorPos(ImVec2{bodyOrigin.x + canvasWidth + ImGui::GetStyle().ItemSpacing.x, bodyOrigin.y});
    ImGui::BeginChild("##material_details", ImVec2{detailsWidth, 0.f}, ImGuiChildFlags_Borders);
    ImGui::PushTextWrapPos(0.f);
    ImGui::TextUnformatted("Node Details");
    ImGui::Separator();
    if (const auto* node = session.document.Graph().FindNode(session.canvas.selectedNode))
    {
        const LXNodeItemRegistry::RowCacheScope detailRows(session.items);
        ImGui::TextWrapped("%s", node->title.c_str());
        ImGui::TextDisabled("%s", node->type.c_str());
        if (node->type == "ShaderNodeBsdfPrincipled")
            ImGui::TextWrapped("Combines diffuse, metallic and specular reflection. Connect textures or values to its inputs, then connect BSDF to Surface.");
        else if (node->type == "ShaderNodeOutputMaterial")
            ImGui::TextWrapped("Defines the material's final surface. Connect a shader to Surface.");
        else if (node->type == "LXTextureSample")
            ImGui::TextWrapped("Samples a texture at the supplied coordinates. Color and channel outputs can drive shader inputs.");
        else if (node->type.starts_with("LXParameter"))
            ImGui::TextWrapped("Exposes a named material value. Each object can override exposed values in the Inspector.");
        else
            ImGui::TextWrapped("Connect inputs on the left to outputs on the right. Connected inputs use the incoming node's value.");
        ImGui::Spacing();
        for (const auto& pin : node->pins)
        {
            if (session.items.HasRows(*node) && !session.items.PinRow(*node, pin)) continue;
            ImGui::BulletText("%s: %s", pin.direction == Direction::Input ? "Input" : "Output", pin.Identifier().c_str());
        }
    }
    else
        ImGui::TextWrapped("Select a node to see its purpose and sockets. Material values and the preview are available in the Inspector.");
    if (!session.message.empty()) ImGui::TextWrapped("%s", session.message.c_str());
    if (session.diagnostics)
    {
        ImGui::BeginChild("##material_diagnostics", ImVec2{0.0f, 100.0f * CanvasUiScale()}, ImGuiChildFlags_Borders);
        session.Analyze();
        for (const auto& issue : session.analysisIssues)
        {
            ImGui::TextWrapped("Node %llu: %s", static_cast<unsigned long long>(issue.source.node),
                               issue.message.c_str());
        }
        ImGui::EndChild();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    }
    ImGui::SetCursorPos(bodyOrigin);
    if (session.confirmReload)
    {
        ImGui::OpenPopup("Discard edits?");
        session.confirmReload = false;
    }
    if (ImGui::BeginPopupModal("Discard edits?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Reload the saved graph and discard current edits?");
        if (ImGui::Button("Reload"))
        {
            session.Reload(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::BeginChild("##material_canvas", ImVec2{canvasWidth, 0.f}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto canvasOrigin = ImGui::GetCursorScreenPos();
    if (session.fit || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Home)))
    {
        session.Fit();
        session.fit = false;
    }
    ImGui::BeginDisabled(!session.Editable());
    {
        ce::profile_scope profile{ce::marker<"MaterialNodeCanvas">()};
        DrawCanvas(session.document.GraphForCanvas(), session.canvas, session.styles, session.items, &session.document);
    }
    ImGui::EndDisabled();
    const auto* renderer = session.Renderer();
    std::string mesh = "Mesh";
    if (renderer && renderer->m_modelGeneration &&
        renderer->m_modelMeshIndex < renderer->m_modelGeneration->Meshes().size())
    {
        mesh = renderer->m_modelGeneration->Meshes()[renderer->m_modelMeshIndex].name;
    }
    DrawMaterialBreadcrumb(canvasOrigin, session.canvas.canvasSize,
                           renderer ? std::string(renderer->GetOwner()->GetHashedName().data()) : "Unavailable object",
                           mesh, session.name, session.message);
    ImGui::EndChild();
    ImGui::EndChild();
    ++session.uiFrames;
}
} // namespace

bool Open(MeshRenderer& renderer, std::string& error)
{
    std::lock_guard lock(sessionMutex);
    return OpenLocked(renderer, error);
}

void OnActiveSceneChanged()
{
    std::lock_guard lock(sessionMutex);
    ReconcileSceneLocked();
}

void Draw()
{
    std::lock_guard lock(sessionMutex);
    ReconcileSceneLocked();
    if (!active)
    {
        ImGui::TextUnformatted("Open a material from MeshRenderer in the Inspector.");
        const auto drafts = std::count_if(sessions.begin(), sessions.end(), [](const auto& s) {
            return !s->Renderer() && s->document.Dirty();
        });
        if (drafts)
        {
            ImGui::TextDisabled("%zu unsaved documents retained from previous Scenes.", size_t(drafts));
            ImGui::TextWrapped("Recovery copies: Saved/Editor/MaterialDrafts. Previous Scene documents cannot be applied here.");
            for (const auto& session : sessions)
                if (!session->Renderer() && session->document.Dirty() && !session->message.empty())
                    ImGui::TextWrapped("%s", session->message.c_str());
        }
        return;
    }
    DrawSession(*active);
}

void DrawInspectorPreview(MeshRenderer& renderer)
{
    std::lock_guard lock(sessionMutex);
    ReconcileSceneLocked();
    if (!ImGui::CollapsingHeader("Preview"))
    {
        inspectorPreviewVisible = {};
        return;
    }
    inspectorPreviewVisible = std::chrono::steady_clock::now();
    if (!PreparePreviewFloor())
    {
        ImGui::TextWrapped("Preview floor: %s", previewFloorError.c_str());
        return;
    }
    auto source = renderer.m_Material ? material_graph::SceneMaterialSource::Capture(*renderer.m_Material) : nullptr;
    if (!source || !source->instance)
    {
        inspectorPreview.reset();
        ImGui::TextDisabled("Apply a Surface graph to preview this material.");
        return;
    }
    if (!inspectorPreview || inspectorPreview->instance != source->instance ||
        inspectorPreview->coverage.flags != source->coverage.flags)
    {
        inspectorPreview = std::move(source);
        inspectorPreviewRevision = ++previewSerial;
    }
    const auto state = EnhancedSceneRenderer::GetLiveDisplaySnapshot().Get(EnhancedLiveDisplayTarget::MaterialPreview);
    const auto texture = EnhancedSceneRenderer::GetLiveDisplayTexture(EnhancedLiveDisplayTarget::MaterialPreview);
    if (inspectorPreview->coverage.flags & EnhancedMaterialCoverage::Blended)
        ImGui::TextWrapped("Preview is available for Opaque and Masked surfaces.");
    else if (state.key.historyRevision != inspectorPreviewRevision || !state.previewComplete || !texture.textureId)
        ImGui::TextDisabled("Preparing material preview...");
    else
    {
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::Image(static_cast<ImTextureID>(texture.textureId),
                     ImVec2{width, width * float(texture.height) / std::max(texture.width, 1u)});
    }
    if (ImGui::SmallButton("Refresh")) inspectorPreviewRevision = ++previewSerial;
}

bool CapturePreviewRequest(EnhancedLiveViewRequest& request)
{
    ce::profile_scope profile{ce::marker<"MaterialPreviewRequest">()};
    std::unique_lock lock(sessionMutex, std::defer_lock);
    {
        ce::profile_scope wait{ce::marker<"MaterialPreviewRequestLockWait">()};
        lock.lock();
    }
    ReconcileSceneLocked();
    if (inspectorPreview && std::chrono::steady_clock::now() - inspectorPreviewVisible <= std::chrono::milliseconds(250))
    {
        if (inspectorPreview->coverage.flags & EnhancedMaterialCoverage::Blended) return false;
        request = {};
        request.key = {3, inspectorPreviewRevision};
        request.displayTarget = EnhancedLiveDisplayTarget::MaterialPreview;
        request.viewFlags = EnhancedLiveViewFlags::None;
        request.materialPreview = inspectorPreview;
        request.materialPreviewFloor = previewFloor;
        return true;
    }
    if (!active || !active->previewPinned) return false;
    RefreshPreview(*active);
    if (!active->previewSource) return false;
    // Transparent coverage has no installed Scene product replacement. Keep
    // the exact coverage rather than silently showing an opaque substitute.
    if (active->previewSource->coverage.flags & EnhancedMaterialCoverage::Blended) return false;
    request = {};
    request.key = {3, active->previewRevision};
    request.displayTarget = EnhancedLiveDisplayTarget::MaterialPreview;
    request.viewFlags = EnhancedLiveViewFlags::None;
    if (!PreparePreviewFloor()) return false;
    request.materialPreview = active->previewSource;
    request.materialPreviewFloor = previewFloor;
    return true;
}

CommandResult Command(const std::vector<std::string>& parts)
{
    using namespace ConsoleCmd;
    std::lock_guard lock(sessionMutex);
    ReconcileSceneLocked();
    if (parts.size() == 2 && parts[1] == "sessions")
    {
        auto data = CommandData::Object();
        data.Set("scene", CommandData::Int(observedSceneId));
        data.Set("open", CommandData::Bool(active != nullptr));
        auto visible = CommandData::Array();
        auto retained = CommandData::Array();
        for (const auto& session : sessions)
        {
            if (session->Renderer()) visible.Append(Describe(*session));
            else if (session->document.Dirty())
            {
                auto draft = CommandData::Object();
                draft.Set("graph", CommandData::String(session->guid.ToString()));
                draft.Set("document", CommandData::Int(session->document.DocumentId()));
                draft.Set("scene", CommandData::Int(session->target.sceneId));
                retained.Append(std::move(draft));
            }
        }
        data.Set("visible", std::move(visible));
        data.Set("retained", std::move(retained));
        return Ok({}, std::move(data));
    }
    if (parts.size() == 3 && (parts[1] == "open" || parts[1] == "new"))
    {
        EntityHandle target;
        auto resolved = EditorObjectOperations::ResolveTarget(parts[2], target);
        if (!resolved.IsSuccess())
        {
            return resolved;
        }
        auto* scene = SceneManagers->GetActiveScene();
        auto* object = scene ? scene->Resolve(target) : nullptr;
        auto* renderer = object ? object->GetComponent<MeshRenderer>() : nullptr;
        std::string error;
        if (!renderer || !OpenLocked(*renderer, error, parts[1] == "new"))
        {
            return Fail("material.editor.open", error.empty() ? "MeshRenderer not found" : error);
        }
        return Ok({}, Describe(*active));
    }
    if (!active)
    {
        return PreconditionFailed("material.editor.closed", "Open a MeshRenderer material first");
    }
    if (parts.size() == 3 && parts[1] == "preview" && (parts[2] == "on" || parts[2] == "off" || parts[2] == "refresh"))
    {
        active->previewPinned = parts[2] != "off";
        RefreshPreview(*active);
        if (parts[2] == "refresh") active->previewRevision = ++previewSerial;
        return Ok({}, Describe(*active));
    }
    if (parts.size() == 2 && parts[1] == "state")
    {
        return Ok({}, Describe(*active));
    }
    if (parts.size() < 4)
    {
        return InvalidArguments(
            "material.editor open <object> | state | <operation> <document-id> <revision> <arguments...>");
    }
    Id documentId{};
    if (!ParseNumber(parts[2], documentId) || documentId != active->document.DocumentId())
    {
        return PreconditionFailed("material.editor.document",
                                  "The active document changed; read its identity before editing");
    }
    std::uint64_t revision{};
    if (!ParseNumber(parts[3], revision) || revision != active->document.Revision())
    {
        return PreconditionFailed("material.editor.revision", "Read the current document revision before editing");
    }
    bool applied = false;
    Id first{}, second{};
    if (parts.size() == 4)
    {
        if (parts[1] == "save")
        {
            applied = active->Save();
        }
        else if (parts[1] == "apply")
        {
            applied = active->Apply();
        }
        else if (parts[1] == "reload")
        {
            applied = active->Reload(false);
        }
        else if (parts[1] == "undo")
        {
            applied = active->Execute(LXUndo{}, revision);
        }
        else if (parts[1] == "redo")
        {
            applied = active->Execute(LXRedo{}, revision);
        }
    }
    else if (parts[1] == "add" && parts.size() == 5)
    {
        applied = active->Execute(LXCreateNode{parts[4], 100.0f, 100.0f}, revision);
    }
    else if (parts.size() == 6 && ParseNumber(parts[4], first) && ParseNumber(parts[5], second) &&
             parts[1] == "connect")
    {
        applied = active->Execute(LXConnectPins{first, second}, revision);
    }
    else if (parts.size() == 5 && ParseNumber(parts[4], first))
    {
        if (parts[1] == "disconnect")
        {
            applied = active->Execute(LXDisconnectLink{first}, revision);
        }
        else if (parts[1] == "delete")
        {
            applied = active->Execute(LXRemoveNode{first}, revision);
        }
    }
    else if (parts.size() == 6 && ParseNumber(parts[4], first) && parts[1] == "collapse")
    {
        if (parts[5] != "true" && parts[5] != "false")
        {
            return InvalidArguments("Collapse value must be true or false");
        }
        applied = active->Execute(LXSetNodeCollapsed{first, parts[5] == "true"}, revision);
    }
    else if (parts.size() == 7 && ParseNumber(parts[4], first) && parts[1] == "property")
    {
        applied = active->Execute(LXSetProperty{first, parts[5], parts[6]}, revision);
    }
    else if (parts.size() >= 6 && ParseNumber(parts[4], first) && parts[1] == "value")
    {
        const auto* pin = active->document.Graph().FindPin(first);
        if (!pin)
        {
            return InvalidArguments("Pin not found");
        }
        LXSocketValue value;
        std::array<double, 4> numbers{};
        const std::size_t count = pin->type == PinType::Color                                    ? 4
                                  : pin->type == PinType::Vector || pin->type == PinType::Normal ? 3
                                                                                                 : 1;
        if (parts.size() != count + 5)
        {
            return InvalidArguments("Socket component count does not match its type");
        }
        if (pin->type == PinType::Bool)
        {
            if (parts[5] != "true" && parts[5] != "false")
            {
                return InvalidArguments("Boolean value must be true or false");
            }
            value = parts[5] == "true";
        }
        else if (pin->type == PinType::Int)
        {
            std::int64_t integer{};
            if (!ParseNumber(parts[5], integer))
            {
                return InvalidArguments("Invalid integer");
            }
            value = integer;
        }
        else
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                if (!ParseNumber(parts[5 + index], numbers[index]) || !std::isfinite(numbers[index]))
                {
                    return InvalidArguments("Finite socket values required");
                }
            }
            value = count == 4   ? LXSocketValue{numbers}
                    : count == 3 ? LXSocketValue{std::array<double, 3>{numbers[0], numbers[1], numbers[2]}}
                                 : LXSocketValue{numbers[0]};
        }
        applied = active->Execute(LXSetSocketValue{first, value}, revision);
    }
    else
    {
        return InvalidArguments("Unknown material editor operation or arguments");
    }
    return applied ? Ok(active->message, Describe(*active)) : Fail("material.editor.rejected", active->message);
}
} // namespace editor::material_editing

namespace editor::windows
{
void draw_material_graph()
{
    material_editing::Draw();
}
} // namespace editor::windows
