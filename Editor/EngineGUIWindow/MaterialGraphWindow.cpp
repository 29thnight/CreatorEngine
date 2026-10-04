#include "MaterialGraphWindow.h"
#include "ProfileScope.h"
#include "JobScheduler.h"
#include "AuthoringParsedDocument.h"
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
#include <set>

namespace editor::material_editing
{
    namespace
    {
        using namespace LX;
        using namespace CommandCore;

        struct PreviewWork
        {
            LXMaterialAsset graph;
            FileGuid graphGuid;
            std::filesystem::path shaderDirectory;
            std::filesystem::path cacheDirectory;
            std::shared_ptr<const material_graph::Generation> generation;
            std::string error;
        };

        std::uint64_t previewSerial = 0;

        bool ReadPayload(const std::filesystem::path& path, std::string& payload)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return false;
            }
            payload.assign(std::istreambuf_iterator<char>(input), {});
            return !input.bad();
        }

        bool ValidMaterialName(const std::string& name)
        {
            if (name.empty() || name.size() > 240 || name == "." || name == ".." ||
                name.find_first_of("<>:\"/\\|?*") != std::string::npos || name.back() == '.' || name.back() == ' ' ||
                std::ranges::any_of(name, [](unsigned char value) { return value < 32; }))
            {
                return false;
            }
            auto stem = name.substr(0, name.find('.'));
            for (auto& character : stem)
            {
                if (character >= 'a' && character <= 'z')
                {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            const bool numberedDevice = stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
                                        stem[3] >= '1' && stem[3] <= '9';
            return stem != "CON" && stem != "PRN" && stem != "AUX" && stem != "NUL" && !numberedDevice;
        }

        struct Session
        {
            EntityHandle target;
            std::size_t component = 0;
            FileGuid guid;
            FileGuid materialGuid;
            std::filesystem::path materialPath;
            std::string materialDiskPayload;
            std::shared_ptr<Material> sourceMaterial;
            std::shared_ptr<Material> acceptedMaterial;
            std::shared_ptr<Material> preparedMaterial;
            material_graph::InstanceDescription defaults;
            std::uint64_t settingsRevision = 0;
            std::uint64_t savedSettingsRevision = 0;
            std::uint64_t appliedRevision = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t appliedDocument = 0;
            std::uint64_t preparedRevision = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t preparedDocument = 0;
            std::uint64_t observedRevision = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t attemptedRevision = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t workRevision = 0;
            std::uint64_t workDocument = 0;
            double changedAt = 0.0;
            MaterialRenderingMode renderingMode = MaterialRenderingMode::Opaque;
            bool doubleSided = false;
            bool persistedMaterial = false;
            bool persistedGraph = false;
            bool previewPending = true;
            std::string previewError;
            std::shared_ptr<PreviewWork> previewWork;
            job_handle previewJob;
            std::filesystem::path path;
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

            Session(FileGuid identity, std::filesystem::path source, LXMaterialAsset material,
                    std::shared_ptr<Material> base, bool graphOnDisk, bool materialOnDisk)
                : guid(identity), materialGuid(base->m_fileGuid), sourceMaterial(base),
                    persistedMaterial(materialOnDisk),
                  persistedGraph(graphOnDisk), path(std::move(source)), asset(std::move(material)),
                  document(asset.graph, {}, graphOnDisk), styles(BlenderStyles(asset.Definitions())),
                  items(MaterialItems(asset.Definitions())), name(base->m_name)
            {
                materialPath = materialOnDisk ? DataSystems->GetFilePath(materialGuid)
                                             : PathFinder::RelativeToMaterial("") / (name + ".asset");
                if (graphOnDisk)
                {
                    ReadPayload(path, diskPayload);
                }
                if (materialOnDisk)
                {
                    ReadPayload(materialPath, materialDiskPayload);
                    acceptedMaterial = sourceMaterial;
                    // Disk and cached runtime graph versions can differ. Only
                    // explicit Apply marks the opened draft as accepted.
                }
                if (const auto instance = sourceMaterial->GetMaterialGraphInstance())
                {
                    defaults = instance->description;
                    previewSource = material_graph::SceneMaterialSource::Capture(*sourceMaterial);
                    previewRevision = ++previewSerial;
                }
                defaults.graphId.value = guid.m_guid;
                renderingMode = sourceMaterial->m_renderingMode;
                doubleSided = sourceMaterial->m_doubleSided;
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
                        const auto instance =
                            renderer->m_Material ? renderer->m_Material->GetMaterialGraphInstance() : nullptr;
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
                    const auto texture = DataSystems->LoadTextureGUID(FileGuid(id));
                    return static_cast<ImTextureID>(EditorImGuiTexture::From(texture));
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
                if (analysisObservedRevision != Revision())
                {
                    analysisObservedRevision = Revision();
                    analysisChangedAt = ImGui::GetTime();
                }
                if (analysisRevision == Revision())
                {
                    return;
                }
                // LXDocument revisions also cover pan, zoom and layout edits. Wait for
                // interaction to settle instead of compiling the graph on every frame.
                if (analysisRevision != std::numeric_limits<std::uint64_t>::max() &&
                    ImGui::GetTime() - analysisChangedAt < 0.25)
                {
                    return;
                }
                analysisRevision = Revision();
                analysisIssues.clear();
                analysisSelection.reset();
                analysisProgram = GenerateMaterialSlang(Snapshot(), &analysisIssues);
                if (!analysisProgram)
                {
                    return;
                }
                material_graph::Capabilities capabilities;
                capabilities.coreForward = capabilities.layeredLookup = capabilities.refraction =
                    capabilities.subsurface = capabilities.volume = true;
                material_graph::Selection selection;
                if (material_graph::SelectRoute(*analysisProgram, capabilities, {}, selection, analysisIssues))
                {
                    analysisSelection = std::move(selection);
                }
            }

            std::uint64_t Revision() const
            {
                return document.Revision() + settingsRevision;
            }

            bool Dirty() const
            {
                return !persistedMaterial ||
                    !persistedGraph || document.Dirty() || settingsRevision != savedSettingsRevision;
            }

            bool Editable() const
            {
                return PathFinder::IsAssetAuthoringEnabled();
            }

            bool CheckDisk()
            {
                const auto unchanged = [&](const std::filesystem::path& file, const std::string& before, bool existed) {
                    std::error_code error;
                    const bool exists = std::filesystem::exists(file, error);
                    if (error || exists != existed)
                    {
                        return false;
                    }
                    std::string current;
                    return !exists || (ReadPayload(file, current) && current == before);
                };
                if (!unchanged(path, diskPayload, persistedGraph) ||
                    !unchanged(materialPath, materialDiskPayload, persistedMaterial))
                {
                    message = "The material or graph changed or was deleted on disk. Reload or create a copy "
                        "before saving.";
                    return false;
                }
                return true;
            }

            bool Save()
            {
                if (!Editable() || document.HasActivePreview())
                {
                    message = "Finish the current edit before saving.";
                    return false;
                }
                if (!acceptedMaterial || appliedDocument != document.DocumentId() || appliedRevision != Revision())
                {
                    message = "Apply the current draft first. Save persists the accepted material and graph.";
                    return false;
                }
                if (DataSystems->FindCachedMaterial(materialGuid) != acceptedMaterial)
                {
                    message = "The accepted asset changed. Reload its saved draft before saving.";
                    return false;
                }
                if (!CheckDisk())
                {
                    return false;
                }
                if (!EditorAssetDatabase::Get().SaveMaterialGraph(*acceptedMaterial, Snapshot(), path, guid, message))
                {
                    return false;
                }
                if (!ReadPayload(path, diskPayload) || !ReadPayload(materialPath, materialDiskPayload))
                {
                    message = "Saved files could not be verified. Reload before saving again.";
                    return false;
                }
                document.AcceptSavedSnapshot();
                persistedGraph = persistedMaterial = true;
                savedSettingsRevision = settingsRevision;
                message = "Saved material asset and graph. Mesh assignment is separate.";
                return true;
            }

            bool Apply()
            {
                if (!Editable() || document.HasActivePreview())
                {
                    message = "Finish the current edit before applying.";
                    return false;
                }
                if (!acceptedMaterial && !ValidMaterialName(name))
                {
                    message = "Enter a valid material asset name.";
                    return false;
                }
                if (acceptedMaterial && name != acceptedMaterial->m_name)
                {
                    message = "Use Save As Copy to create a material with a different name.";
                    return false;
                }
                if (!preparedMaterial || preparedDocument != document.DocumentId() || preparedRevision != Revision())
                {
                    message = previewError.empty() ? "Draft compilation is pending. Apply when the draft is ready."
                              : "Apply failed; the accepted material is unchanged. " + previewError;
                    return false;
                }
                if (preparedMaterial == acceptedMaterial &&
                    DataSystems->FindCachedMaterial(materialGuid) == acceptedMaterial)
                {
                    message = "This draft is already applied. Save to retain it on disk.";
                    return true;
                }
                if (!persistedMaterial)
                {
                    materialPath = PathFinder::RelativeToMaterial("") / (name + ".asset");
                }
                if (!CheckDisk() || !DataSystems->PublishMaterialAsset(preparedMaterial, acceptedMaterial, message))
                {
                    return false;
                }
                acceptedMaterial = preparedMaterial;
                sourceMaterial = acceptedMaterial;
                appliedRevision = Revision();
                appliedDocument = document.DocumentId();
                message = "Applied to the material asset and its users. Save to retain it on disk.";
                return true;
            }

            bool Assign(MeshRenderer& renderer)
            {
                if (!acceptedMaterial || !persistedMaterial || appliedDocument != document.DocumentId() ||
                    appliedRevision != Revision() || Dirty())
                {
                    message = "Apply and Save this draft before assigning its asset reference.";
                    return false;
                }
                auto* owner = renderer.GetOwner();
                if (!owner || !owner->GetScene() || EditorObjectOperations::IsEditLocked(owner, true))
                {
                    message = "The selected MeshRenderer is unavailable or locked.";
                    return false;
                }
                const auto before = renderer.m_Material;
                const auto beforeGuid = renderer.m_materialBaseGuid;
                const auto beforeBase = renderer.GetMaterialAssetBase();
                const auto beforeState = renderer.GetMaterialAssetReferenceState();
                const auto candidate = std::make_shared<Material>(*acceptedMaterial);
                const auto handle = owner->GetScene()->HandleOf(owner->m_index);
                const auto apply = [handle,
                    identity = renderer.GetInstanceID()](const std::shared_ptr<Material>& material,
                                      FileGuid baseGuid, const std::shared_ptr<const Material>& base,
                                      const std::shared_ptr<const MaterialAssetReferenceState>& state) {
                    auto* scene = SceneManagers->GetActiveScene();
                    auto* object = scene ? scene->Resolve(handle) : nullptr;
                    auto* component = object ? object->GetComponent<MeshRenderer>() : nullptr;
                    if (component && component->GetInstanceID() == identity)
                    {
                        component->SetMaterialAssetReference(material, baseGuid, base, state);
                    }
                };
                Meta::MakeCustomChangeCommand(
                    [apply, before, beforeGuid, beforeBase, beforeState] {
                        apply(before, beforeGuid, beforeBase, beforeState);
                    },
                    [apply, candidate, base = acceptedMaterial, id = materialGuid] {
                        apply(candidate, id, base, {});
                    });
                message = "Assigned to the selected MeshRenderer. Save the Scene to retain the reference.";
                return true;
            }

            void TickPreview()
            {
                const auto revision = Revision();
                if (observedRevision != revision)
                {
                    observedRevision = revision;
                    changedAt = ImGui::GetTime();
                    previewPending = true;
                    previewError.clear();
                }
                if (previewJob.valid())
                {
                    if (!previewJob.is_complete())
                    {
                        return;
                    }
                    try
                    {
                        previewJob.wait();
                    }
                    catch (const std::exception& failure)
                    {
                        previewWork->error = failure.what();
                    }
                    if (workDocument == document.DocumentId() && workRevision == revision)
                    {
                        auto candidate = std::make_shared<Material>(*sourceMaterial);
                        candidate->m_name = name;
                        candidate->m_fileGuid = materialGuid;
                        candidate->m_doubleSided = doubleSided;
                        candidate->m_renderingMode = renderingMode;
                        std::string error = previewWork->error;
                        auto generation = previewWork->generation;
                        if (generation && preparedMaterial)
                        {
                            const auto& previous = preparedMaterial->GetMaterialGraphInstance();
                            if (previous && previous->generation->assetId == generation->assetId &&
                                previous->generation->contentDigest == generation->contentDigest)
                            {
                                // Layout and asset-default edits can share the
                                // program without repeating GPU preparation.
                                generation = previous->generation;
                            }
                        }
                        if (generation && DataSystems->ConfigureMaterialGraph(*candidate, generation, defaults, error))
                        {
                            preparedMaterial = std::move(candidate);
                            preparedRevision = revision;
                            preparedDocument = document.DocumentId();
                            previewSource = material_graph::SceneMaterialSource::Capture(*preparedMaterial);
                            previewRevision = ++previewSerial;
                            previewError.clear();
                        }
                        else
                        {
                            previewError = error.empty() ? "Draft compilation failed." : std::move(error);
                        }
                        previewPending = false;
                        attemptedRevision = revision;
                    }
                    previewJob = {};
                    previewWork.reset();
                }
                if (document.HasActivePreview() || attemptedRevision == revision || ImGui::GetTime() - changedAt < 0.25)
                {
                    return;
                }
                auto work = std::make_shared<PreviewWork>();
                work->graph = Snapshot();
                work->graphGuid = guid;
                work->shaderDirectory = PathFinder::RelativeToShader("DefaultPassShader");
                work->cacheDirectory = PathFinder::CachePath("Lattice/EditorDrafts");
                workRevision = revision;
                workDocument = document.DocumentId();
                try
                {
                    previewJob = ce::get_job_scheduler().submit([work] {
                        work->generation = DataSystem::CompileMaterialGraphAuthoring(
                            work->graph, work->graphGuid, work->shaderDirectory, work->cacheDirectory, work->error);
                    });
                    previewWork = std::move(work);
                }
                catch (const std::exception& failure)
                {
                    previewError = failure.what();
                    previewPending = false;
                    attemptedRevision = revision;
                }
            }

            bool Reload(bool discard)
            {
                if (Dirty() && !discard)
                {
                    message = "Reload requires confirmation while the material has unsaved edits.";
                    return false;
                }
                if (!persistedGraph || !persistedMaterial)
                {
                    message = "This new material has no saved version. Create a new draft to start over.";
                    return false;
                }
                std::string graphPayload, materialPayload;
                if (!ReadPayload(materialPath, materialPayload))
                {
                    message = "Reload failed. The open draft and last good material are preserved.";
                    return false;
                }
                const auto saved = Authoring::ParsedDocument::ParseText(materialPayload, message);
                material_graph::InstanceDocument materialDocument;
                if (!saved || !material_graph::ReadInstanceDocument(saved.Root(), materialDocument, message) ||
                    materialDocument.materialId.value != materialGuid.m_guid)
                {
                    message = "Reload requires the same graph-backed material asset identity. " + message;
                    return false;
                }
                const FileGuid graphGuid(materialDocument.description.graphId.value);
                const auto graphPath = DataSystems->GetMaterialGraphSourcePath(graphGuid);
                if (!ReadPayload(graphPath, graphPayload))
                {
                    message = "Reload cannot read the referenced graph. The draft is preserved.";
                    return false;
                }
                auto loaded = LXMaterialArchive::Read(graphPayload, asset.Definitions(), &message);
                if (!loaded)
                {
                    message = "Reload failed. " + message;
                    return false;
                }
                // Reload replaces only the editor draft. It never implicitly
                // publishes unsaved changes or compiles under the session lock.
                acceptedMaterial = DataSystems->FindCachedMaterial(materialGuid);
                if (acceptedMaterial)
                {
                    sourceMaterial = acceptedMaterial;
                }
                defaults = std::move(materialDocument.description);
                name = sourceMaterial->m_name;
                doubleSided = materialDocument.doubleSided;
                renderingMode = materialDocument.blendMode == "transparent" ? MaterialRenderingMode::Transparent :
                                materialDocument.blendMode == "masked" ? MaterialRenderingMode::Masked :
                                                                        MaterialRenderingMode::Opaque;
                guid = graphGuid;
                path = graphPath;
                asset = std::move(*loaded);
                diskPayload = std::move(graphPayload);
                materialDiskPayload = std::move(materialPayload);
                document = LXDocument(asset.graph, {}, true);
                settingsRevision = savedSettingsRevision = 0;
                appliedDocument = 0;
                appliedRevision = std::numeric_limits<std::uint64_t>::max();
                preparedMaterial.reset();
                observedRevision = attemptedRevision = std::numeric_limits<std::uint64_t>::max();
                analysisRevision = analysisObservedRevision = std::numeric_limits<std::uint64_t>::max();
                canvas = {};
                fit = !asset.graph.Layout().view.saved;
                message = "Reloaded the saved draft. Apply explicitly to replace the accepted asset.";
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
                    LXSetView{ViewLayout{true, (minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f,
                        canvas.zoom}},
                    document.Revision());
            }
        };

        std::mutex sessionMutex;
        std::vector<std::unique_ptr<Session>> sessions;
        Session* active = nullptr;
        std::shared_ptr<const material_graph::SceneMaterialSource> inspectorPreview;
        std::uint64_t inspectorPreviewRevision{};
        std::chrono::steady_clock::time_point inspectorPreviewVisible{};
        std::uint32_t observedSceneId{};
        bool shuttingDown = false;

        void ReconcileSceneLocked()
        {
            const auto* scene = SceneManagers->GetActiveScene();
            const auto sceneId = scene ? scene->GetSceneId() : 0u;
            if (sceneId != observedSceneId)
            {
                observedSceneId = sceneId;
                inspectorPreview.reset();
                inspectorPreviewVisible = {};
                // Material drafts are asset-owned. Scene changes must not discard or
                // silently retarget them; assignment resolves the current selection.
                for (auto& session : sessions)
                {
                    session->target = {};
                    session->component = 0;
                }
            }
        }

        std::array<std::shared_ptr<const material_graph::SceneMaterialSource>, 2> previewFloor;
        std::string previewFloorError;
        std::shared_ptr<PreviewWork> previewFloorWork;
        job_handle previewFloorJob;

        bool PreparePreviewFloor()
        {
            if (previewFloor[0] && previewFloor[1])
            {
                return true;
            }
            if (!previewFloorError.empty())
            {
                return false;
            }
            if (previewFloorJob.valid())
            {
                if (!previewFloorJob.is_complete())
                {
                    return false;
                }
                try
                {
                    previewFloorJob.wait();
                }
                catch (const std::exception& failure)
                {
                    previewFloorWork->error = failure.what();
                }
                previewFloorError = previewFloorWork->error;
                material_graph::InstanceDescription description;
                description.graphId.value = previewFloorWork->graphGuid.m_guid;
                Material dark;
                if (!previewFloorWork->generation ||
                    !DataSystems->ConfigureMaterialGraph(dark, previewFloorWork->generation, description,
                                                          previewFloorError))
                {
                    if (previewFloorError.empty())
                    {
                        previewFloorError = "Preview floor compilation failed.";
                    }
                    previewFloorJob = {};
                    previewFloorWork.reset();
                    return false;
                }
                Material light(dark);
                if (!light.TrySetMaterialGraphParameter(1, std::array<double, 4>{.48, .48, .48, 1}, previewFloorError))
                {
                    return false;
                }
                previewFloor[0] = material_graph::SceneMaterialSource::Capture(dark);
                previewFloor[1] = material_graph::SceneMaterialSource::Capture(light);
                previewFloorJob = {};
                previewFloorWork.reset();
                return true;
            }
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
            auto work = std::make_shared<PreviewWork>();
            work->graph = std::move(asset);
            work->graphGuid = FileGuid(description.graphId.value);
            work->shaderDirectory = PathFinder::RelativeToShader("DefaultPassShader");
            work->cacheDirectory = PathFinder::CachePath("Lattice/EditorDrafts");
            try
            {
                previewFloorJob = ce::get_job_scheduler().submit([work] {
                    work->generation = DataSystem::CompileMaterialGraphAuthoring(
                        work->graph, work->graphGuid, work->shaderDirectory, work->cacheDirectory, work->error);
                });
                previewFloorWork = std::move(work);
            }
            catch (const std::exception& failure)
            {
                previewFloorError = failure.what();
            }
            return false;
        }

        void RefreshPreview(Session& session)
        {
            session.TickPreview();
        }

        LXMaterialAsset NewGraph()
        {
            LXMaterialAsset asset;
            const Id surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0.0f, 0.0f);
            asset.activeOutput = asset.CreateNode("ShaderNodeOutputMaterial", 510.0f, 50.0f);
            const auto find = [](const Node& node, const std::string& name) {
                return std::ranges::find_if(node.pins, [&](const Pin& pin) { return pin.Identifier() == name; })->id;
            };
            asset.graph.Connect(find(*asset.graph.FindNode(surface), "BSDF"),
                                find(*asset.graph.FindNode(asset.activeOutput), "Surface"));
            return asset;
        }

        bool OpenAssetLocked(const std::shared_ptr<Material>& material, std::string& error, bool create = false,
                             const LXMaterialAsset* draft = nullptr)
        {
            if (shuttingDown || !PathFinder::IsAssetAuthoringEnabled())
            {
                error = "Material authoring requires an active Editor project.";
                return false;
            }
            for (const auto& session : sessions)
            {
                if (!create && material && session->materialGuid == material->m_fileGuid)
                {
                    active = session.get();
                    queue_window_request(EditorWindowName::kMaterialGraph, window_request::focus);
                    return true;
                }
            }
            auto base = material ? material : std::make_shared<Material>();
            LXMaterialAsset graph;
            const auto instance = base->GetMaterialGraphInstance();
            FileGuid graphGuid = instance ? FileGuid(instance->description.graphId.value) : FileGuid{};
            std::filesystem::path graphPath;
            if (draft)
            {
                graph = *draft;
            }
            else if (instance)
            {
                graphPath = DataSystems->GetMaterialGraphSourcePath(graphGuid);
                auto loaded = LXMaterialAsset::Load(graphPath, graph.Definitions(), &error);
                if (!loaded)
                {
                    return false;
                }
                graph = std::move(*loaded);
            }
            else
            {
                graph = NewGraph();
                create = true;
            }
            if (create)
            {
                base = std::make_shared<Material>(*base);
                base->m_fileGuid = FileGuid::CreateRandomV4();
                if (base->m_name.empty())
                {
                    base->m_name = "Material_" + base->m_fileGuid.ToString().substr(0, 8);
                }
                else
                {
                    base->m_name = std::filesystem::path(base->m_name).filename().string() + " Copy";
                }
                graphGuid = FileGuid::CreateRandomV4();
                graphPath = PathFinder::RelativeToMaterial("") / ("Material_" + graphGuid.ToString() + ".shadergraph");
            }
            if (!create && (!instance || base->m_fileGuid == FileGuid{}))
            {
                error = "Choose a graph-backed material asset, or create a new material.";
                return false;
            }
            sessions.push_back(std::make_unique<Session>(graphGuid, graphPath, std::move(graph), base, !create,
                !create));
            active = sessions.back().get();
            active->fit = !active->document.Graph().Layout().view.saved;
            queue_window_request(EditorWindowName::kMaterialGraph, window_request::focus);
            return true;
        }

        bool OpenLocked(MeshRenderer& renderer, std::string& error, bool create = false)
        {
            ReconcileSceneLocked();
            std::shared_ptr<Material> material;
            if (!create && renderer.m_materialBaseGuid != FileGuid{})
            {
                material = DataSystems->LoadMaterialShared(renderer.m_materialBaseGuid);
                if (!material)
                {
                    error = "The referenced material asset is unavailable. The mesh keeps its last good material.";
                    return false;
                }
            }
            else
            {
                material = renderer.m_Material;
                create = true;
            }
            if (!OpenAssetLocked(material, error, create))
            {
                return false;
            }
            if (auto* owner = renderer.GetOwner(); owner && owner->GetScene())
            {
                active->target = owner->GetScene()->HandleOf(owner->m_index);
                active->component = renderer.GetInstanceID();
            }
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
            result.Set("dirty", CommandData::Bool(session.Dirty()));
            result.Set("material", CommandData::String(session.materialGuid.ToString()));
            result.Set("materialPath", CommandData::String(session.materialPath.string()));
            result.Set("draftRevision", CommandData::Int(session.Revision()));
            result.Set("applied", CommandData::Bool(session.appliedDocument == session.document.DocumentId() &&
                                                   session.appliedRevision == session.Revision()));
            result.Set("uiFrames", CommandData::Int(session.uiFrames));
            result.Set("editable", CommandData::Bool(session.Editable()));
            const auto preview =
                EnhancedSceneRenderer::GetLiveDisplaySnapshot().Get(EnhancedLiveDisplayTarget::MaterialPreview);
            auto previewState = CommandData::Object();
            previewState.Set("revision", CommandData::Int(session.previewRevision));
            previewState.Set("completedRevision", CommandData::Int(preview.key.historyRevision));
            previewState.Set("completedFrame", CommandData::Int(preview.completedFrameId));
            previewState.Set("renders", CommandData::Int(preview.promotionCount));
            previewState.Set("pinned", CommandData::Bool(session.previewPinned));
            previewState.Set("pending", CommandData::Bool(session.previewPending));
            previewState.Set("error", CommandData::String(session.previewError));
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

        void DrawAssetPicker(std::string& error)
        {
            if (ImGui::Button("New Material Asset"))
            {
                OpenAssetLocked({}, error, true);
            }
            ImGui::SameLine();
            if (ImGui::BeginCombo("##open_material_asset", "Open Material Asset"))
            {
                static std::vector<std::filesystem::path> paths;
                static ImGuiTextFilter filter;
                if (ImGui::IsWindowAppearing())
                {
                    paths.clear();
                    std::error_code failure;
                    for (std::filesystem::recursive_directory_iterator it(PathFinder::RelativeToMaterial(""),
                        failure), end;
                         it != end && !failure; it.increment(failure))
                    {
                        if (it->is_regular_file(failure) && it->path().extension() == ".asset")
                        {
                            paths.push_back(it->path());
                        }
                    }
                    std::ranges::sort(paths);
                }
                filter.Draw("Search");
                for (const auto& path : paths)
                {
                    const auto caption = path.lexically_relative(PathFinder::RelativeToMaterial("")).generic_string();
                    if (!filter.PassFilter(caption.c_str()))
                    {
                        continue;
                    }
                    if (ImGui::Selectable(caption.c_str()))
                    {
                        try
                        {
                            auto material = DataSystems->LoadMaterialShared(DataSystems->GetFileGuid(path));
                            if (!material)
                            {
                                error = "Cannot load material asset: " + caption;
                            }
                            else
                            {
                                OpenAssetLocked(material, error);
                            }
                        }
                        catch (const std::exception& failure)
                        {
                            error = failure.what();
                        }
                    }
                }
                if (paths.empty())
                {
                    ImGui::TextDisabled("No saved material assets.");
                }
                ImGui::EndCombo();
            }
        }

        bool DrawParameterValue(LXSocketValue& value, PinType type)
        {
            if (type == PinType::Texture)
            {
                std::string reference;
                if (const auto* current = std::get_if<std::string>(&value))
                {
                    reference = *current;
                }
                ImGui::InputText("Texture GUID", &reference, ImGuiInputTextFlags_ReadOnly);
                bool changed = false;
                if (ImGui::BeginDragDropTarget())
                {
                    if (const auto* payload = ImGui::AcceptDragDropPayload("Texture"))
                    {
                        const auto identity = DataSystems->GetFileGuid(asset_drag::path_of(*payload));
                        if (identity != FileGuid{})
                        {
                            value = identity.ToString();
                            changed = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                return changed;
            }
            if (auto* scalar = std::get_if<double>(&value))
            {
                return ImGui::InputDouble("Default", scalar, 0.01, 0.1, "%.4f");
            }
            if (auto* integer = std::get_if<std::int64_t>(&value))
            {
                return ImGui::InputScalar("Default", ImGuiDataType_S64, integer);
            }
            if (auto* boolean = std::get_if<bool>(&value))
            {
                return ImGui::Checkbox("Default", boolean);
            }
            if (auto* vector = std::get_if<std::array<double, 3>>(&value))
            {
                float numbers[]{float((*vector)[0]), float((*vector)[1]), float((*vector)[2])};
                if (ImGui::DragFloat3("Default", numbers, 0.01f))
                {
                    value = std::array<double, 3>{numbers[0], numbers[1], numbers[2]};
                    return true;
                }
            }
            if (auto* color = std::get_if<std::array<double, 4>>(&value))
            {
                float numbers[]{float((*color)[0]), float((*color)[1]), float((*color)[2]), float((*color)[3])};
                if (ImGui::ColorEdit4("Default", numbers, ImGuiColorEditFlags_Float))
                {
                    value = std::array<double, 4>{numbers[0], numbers[1], numbers[2], numbers[3]};
                    return true;
                }
            }
            if (auto* text = std::get_if<std::string>(&value))
            {
                return ImGui::InputText("Default", text);
            }
            return false;
        }

        LXSocketValue DefaultParameterValue(PinType type)
        {
            switch (type)
            {
            case PinType::Bool:
                return false;
            case PinType::Int:
                return std::int64_t{0};
            case PinType::Vector:
                return std::array<double, 3>{0, 0, 0};
            case PinType::Normal:
                return std::array<double, 3>{0, 0, 1};
            case PinType::Color:
                return std::array<double, 4>{1, 1, 1, 1};
            case PinType::Texture:
                return std::string{};
            case PinType::Sampler:
                return std::string{"linear-repeat"};
            default:
                return 0.0;
            }
        }

        void DrawParameterDeclaration(Session& session, const Node& node)
        {
            if (!node.type.starts_with("LXParameter") || node.pins.empty())
            {
                return;
            }
            Id parameterId{};
            const auto property = node.properties.find("parameter");
            if (property != node.properties.end())
            {
                const auto& text = property->second;
                const auto parsed = std::from_chars(text.data(), text.data() + text.size(), parameterId);
                if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                {
                    parameterId = 0;
                }
            }
            auto found = std::ranges::find(session.asset.blackboard, parameterId, &LXMaterialParameter::id);
            if (found == session.asset.blackboard.end())
            {
                ImGui::TextWrapped("This node needs a graph parameter declaration.");
                if (ImGui::Button("Declare Parameter"))
                {
                    Id identity = 1;
                    for (const auto& parameter : session.asset.blackboard)
                    {
                        identity = std::max(identity, parameter.id + 1);
                    }
                    const auto type = node.pins.front().type;
                    LXMaterialParameter parameter;
                    parameter.id = identity;
                    parameter.identifier = "parameter_" + std::to_string(identity);
                    parameter.name = "Parameter " + std::to_string(identity);
                    parameter.type = type;
                    parameter.value = DefaultParameterValue(type);
                    parameter.colorSpace = type == PinType::Color ? LXColorSpace::Linear : LXColorSpace::Data;
                    if (session.Execute(LXSetProperty{node.id, "parameter", std::to_string(identity)},
                                        session.document.Revision()))
                    {
                        session.asset.blackboard.push_back(std::move(parameter));
                        ++session.settingsRevision;
                    }
                }
                return;
            }
            ImGui::TextUnformatted("Graph declaration");
            bool changed = ImGui::InputText("Identifier", &found->identifier);
            changed |= ImGui::InputText("Label", &found->name);
            if (ImGui::Checkbox("Expose", &found->exposed))
            {
                changed = true;
                if (!found->exposed)
                {
                    std::erase_if(session.defaults.parameters,
                                  [&](const auto& item) { return item.id == found->id; });
                    std::erase_if(session.defaults.textures,
                                  [&](const auto& item) { return item.parameter == found->id; });
                }
            }
            ImGui::TextDisabled("%s, stable ID %llu", PinTypeName(found->type),
                                static_cast<unsigned long long>(found->id));
            const auto valueSource = node.properties.find("valueSource");
            if (valueSource != node.properties.end() && valueSource->second == "socket")
            {
                auto value = node.pins.front().value;
                ImGui::TextDisabled("Graph default comes from this node's output socket.");
                if (DrawParameterValue(value, found->type))
                {
                    session.Execute(LXSetSocketValue{node.pins.front().id, std::move(value)},
                                    session.document.Revision());
                }
            }
            else
            {
                changed |= DrawParameterValue(found->value, found->type);
            }
            if (changed)
            {
                ++session.settingsRevision;
            }
        }

        LXSocketValue EffectiveParameterDefault(const Session& session, const LXMaterialParameter& parameter)
        {
            auto value = parameter.value;
            std::set<Id> visited;
            const auto visit = [&](auto&& self, const LXGraph& graph) -> void {
                for (const auto& node : graph.Nodes())
                {
                    const auto source = node.properties.find("valueSource");
                    const auto reference = node.properties.find("parameter");
                    if (node.type.starts_with("LXParameter") && !node.pins.empty() &&
                        source != node.properties.end() && source->second == "socket" &&
                        reference != node.properties.end())
                    {
                        Id identity{};
                        const auto& text = reference->second;
                        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), identity);
                        if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                            identity == parameter.id)
                        {
                            value = node.pins.front().value;
                        }
                    }
                }
                for (const auto& [id, group] : graph.Groups())
                {
                    if (group.body && visited.insert(id).second)
                    {
                        self(self, *group.body);
                    }
                }
            };
            visit(visit, session.document.Graph());
            return value;
        }

        void DrawMaterialDefaults(Session& session)
        {
            if (!ImGui::CollapsingHeader("Material Asset Defaults", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }
            if (ImGui::Checkbox("Double Sided", &session.doubleSided))
            {
                ++session.settingsRevision;
            }
            for (const auto& parameter : session.asset.blackboard)
            {
                if (!parameter.exposed || parameter.type == PinType::Sampler)
                {
                    continue;
                }
                ImGui::PushID(std::to_string(parameter.id).c_str());
                ImGui::TextUnformatted(parameter.name.c_str());
                LXSocketValue value = EffectiveParameterDefault(session, parameter);
                const auto numeric = std::ranges::find(session.defaults.parameters, parameter.id,
                                                       &material_graph::ParameterOverride::id);
                const auto texture = std::ranges::find(session.defaults.textures, parameter.id,
                                                       &material_graph::TextureOverride::parameter);
                if (numeric != session.defaults.parameters.end())
                {
                    value = numeric->value;
                }
                if (texture != session.defaults.textures.end())
                {
                    value = FileGuid(texture->assetId.value).ToString();
                }
                if (DrawParameterValue(value, parameter.type))
                {
                    if (parameter.type == PinType::Texture)
                    {
                        Uuid::Uuid16 identity;
                        const auto* reference = std::get_if<std::string>(&value);
                        if (reference && Uuid::TryParse(*reference, identity))
                        {
                            if (texture == session.defaults.textures.end())
                            {
                                session.defaults.textures.push_back({parameter.id, experiment::AssetId{identity}});
                            }
                            else
                            {
                                texture->assetId.value = identity;
                            }
                        }
                    }
                    else if (numeric == session.defaults.parameters.end())
                    {
                        session.defaults.parameters.push_back({parameter.id, std::move(value)});
                    }
                    else
                    {
                        numeric->value = std::move(value);
                    }
                    ++session.settingsRevision;
                }
                if (ImGui::SmallButton("Use Graph Default"))
                {
                    std::erase_if(session.defaults.parameters,
                        [&](const auto& item) { return item.id == parameter.id; });
                    std::erase_if(session.defaults.textures,
                        [&](const auto& item) { return item.parameter == parameter.id; });
                    ++session.settingsRevision;
                }
                ImGui::PopID();
            }
            if (session.asset.blackboard.empty())
            {
                ImGui::TextWrapped("Add a Parameter node and use Declare Parameter in its details to expose an "
                    "asset default.");
            }
        }

        void DrawDraftPreview(Session& session)
        {
            if (!ImGui::CollapsingHeader("Draft Preview", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }
            session.previewVisible = std::chrono::steady_clock::now();
            if (session.previewPending)
            {
                ImGui::TextDisabled("Compiling latest draft... Last good preview is retained.");
            }
            if (!session.previewError.empty())
            {
                ImGui::TextWrapped("Draft error: %s", session.previewError.c_str());
            }
            if (!session.previewSource)
            {
                ImGui::TextDisabled("Waiting for the first valid draft.");
                return;
            }
            if (session.previewSource->coverage.flags & EnhancedMaterialCoverage::Blended)
            {
                ImGui::TextWrapped("Transparent draft compiled. This preview renderer supports Opaque and Masked "
                    "surfaces.");
                return;
            }
            const auto state =
                EnhancedSceneRenderer::GetLiveDisplaySnapshot().Get(EnhancedLiveDisplayTarget::MaterialPreview);
            const auto texture =
                EnhancedSceneRenderer::GetLiveDisplayTexture(EnhancedLiveDisplayTarget::MaterialPreview);
            if (state.key.historyRevision == session.previewRevision && state.previewComplete && texture.textureId)
            {
                const float width = ImGui::GetContentRegionAvail().x;
                ImGui::Image(static_cast<ImTextureID>(texture.textureId),
                             ImVec2{width, width * float(texture.height) / std::max(texture.width, 1u)});
            }
            else
            {
                ImGui::TextDisabled("Preparing draft GPU preview...");
            }
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
            ImGui::PushID(&session);
            const auto previousName = session.name;
            MaterialBarAction action = MaterialBarAction::None;
            {
                const MaterialHeaderScope header(session.styles.canvas.background);
                ImGui::BeginChild("##material_editor", ImVec2{}, ImGuiChildFlags_None,
                                  ImGuiWindowFlags_MenuBar |
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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

                            ImGui::PushID(document.get());
                            if (ImGui::Selectable(document->name.c_str(), document.get() == active))
                            {
                                active = document.get();
                            }
                            ImGui::PopID();
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("New Material Asset", nullptr, false, session.Editable()))
                        {
                            action = MaterialBarAction::NewGraph;
                        }
                        if (ImGui::MenuItem(EditorIcon::Label<EditorIcon::Save, " Save Material Asset">))
                        {
                            action = MaterialBarAction::Save;
                        }
                        if (ImGui::MenuItem("Apply to Material Asset", nullptr, false, session.Editable()))
                        {
                            action = MaterialBarAction::Apply;
                        }
                        if (ImGui::MenuItem("Save As Copy"))
                        {
                            action = MaterialBarAction::SaveAs;
                        }
                        if (ImGui::MenuItem("Assign Asset to Selected Mesh"))
                        {
                            action = MaterialBarAction::Assign;
                        }
                        if (ImGui::MenuItem("Reload Saved Draft"))
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
                                    PathFinder::RuntimeDataPath("Editor/Styles/Material.lxstyle").string(),
                                        &session.message))
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
                    const auto toolbarAction = DrawMaterialDataBar(session.name, session.Dirty(), session.Editable(),
                                                                   session.styles.canvas.showGrid,
                                                                       session.canvas.snapToGrid,
                                                                   materialMenu, overlayMenu);
                    if (toolbarAction != MaterialBarAction::None)
                    {
                        action = toolbarAction;
                    }
                    ImGui::EndMenuBar();
                }
            }
            if (previousName != session.name)
            {
                ++session.settingsRevision;
            }
            if (action == MaterialBarAction::NewGraph)
            {
                OpenAssetLocked({}, session.message, true);
            }
            else if (action == MaterialBarAction::SaveAs)
            {
                auto source = std::make_shared<Material>(*session.sourceMaterial);
                source->m_name = session.name;
                const auto graph = session.Snapshot();
                if (OpenAssetLocked(source, session.message, true, &graph))
                {
                    active->defaults = session.defaults;
                    active->defaults.graphId.value = active->guid.m_guid;
                    active->doubleSided = session.doubleSided;
                    active->renderingMode = session.renderingMode;
                    active->message = "Independent copy created. Set its name, then Apply and Save. Nothing was "
                        "assigned.";
                }
            }
            else if (action == MaterialBarAction::Assign)
            {
                auto* scene = SceneManagers->GetActiveScene();
                auto* selected = scene ? scene->GetSelectedEntity() : nullptr;
                auto* renderer = selected ? selected->GetComponent<MeshRenderer>() : nullptr;
                if (renderer)
                {
                    session.Assign(*renderer);
                }
                else
                {
                    session.message = "Select a MeshRenderer to assign this asset.";
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
                if (session.Dirty())
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
            if (active != &session)
            {
                ImGui::EndChild();
                ImGui::PopID();
                return;
            }
            const ImVec2 bodyOrigin = ImGui::GetCursorPos();
            const float bodyWidth = ImGui::GetContentRegionAvail().x;
            const bool showDetails = session.materialDetails && bodyWidth >= 400.f * CanvasUiScale();
            const float detailsWidth = showDetails ? std::min(300.f * CanvasUiScale(), bodyWidth * 0.4f) : 0.f;
            const float canvasWidth =
                showDetails ? bodyWidth - detailsWidth - ImGui::GetStyle().ItemSpacing.x : bodyWidth;
            if (showDetails)
            {
                ImGui::SetCursorPos(ImVec2{bodyOrigin.x + canvasWidth + ImGui::GetStyle().ItemSpacing.x, bodyOrigin.y});
                ImGui::BeginChild("##material_details", ImVec2{detailsWidth, 0.f}, ImGuiChildFlags_Borders);
                ImGui::PushTextWrapPos(0.f);
                ImGui::BeginDisabled(!session.Editable());
                DrawMaterialDefaults(session);
                ImGui::EndDisabled();
                DrawDraftPreview(session);
                ImGui::Separator();
                ImGui::TextUnformatted("Node Details");
                ImGui::Separator();
                if (const auto* node = session.document.Graph().FindNode(session.canvas.selectedNode))
                {
                    const LXNodeItemRegistry::RowCacheScope detailRows(session.items);
                    ImGui::TextWrapped("%s", node->title.c_str());
                    ImGui::TextDisabled("%s", node->type.c_str());
                    ImGui::BeginDisabled(!session.Editable());
                    DrawParameterDeclaration(session, *node);
                    ImGui::EndDisabled();
                    ImGui::Spacing();
                    const auto* currentNode = session.document.Graph().FindNode(session.canvas.selectedNode);
                    for (const auto& pin : currentNode->pins)
                    {
                        if (session.items.HasRows(*currentNode) && !session.items.PinRow(*currentNode, pin))
                        {
                            continue;
                        }
                        ImGui::BulletText("%s: %s", pin.direction == Direction::Input ? "Input" : "Output",
                            pin.Identifier().c_str());
                    }
                }
                else
                {
                    ImGui::TextWrapped("Select a node to edit its declaration and inspect sockets.");
                }
                if (!session.message.empty())
                {
                    ImGui::TextWrapped("%s", session.message.c_str());
                }
                if (session.diagnostics)
                {
                    ImGui::BeginChild("##material_diagnostics", ImVec2{0.0f, 100.0f * CanvasUiScale()},
                        ImGuiChildFlags_Borders);
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
                ImGui::TextUnformatted("Reload the saved material and graph, discarding the current draft?");
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
                DrawCanvas(session.document.GraphForCanvas(), session.canvas, session.styles, session.items,
                    &session.document);
            }
            ImGui::EndDisabled();
            DrawMaterialBreadcrumb(canvasOrigin, session.canvas.canvasSize,
                                   "Material Asset", "Draft", session.name, session.message);
            ImGui::EndChild();
            ImGui::EndChild();
            session.TickPreview();
            ++session.uiFrames;
            ImGui::PopID();
        }
    } // namespace

    bool Open(MeshRenderer& renderer, std::string& error)
    {
        std::lock_guard lock(sessionMutex);
        try
        {
            return OpenLocked(renderer, error);
        }
        catch (const std::exception& failure)
        {
            error = failure.what();
            return false;
        }
    }

    void OnActiveSceneChanged()
    {
        std::lock_guard lock(sessionMutex);
        ReconcileSceneLocked();
    }

    void Draw()
    {
        std::lock_guard lock(sessionMutex);
        if (shuttingDown)
        {
            return;
        }
        ReconcileSceneLocked();
        static std::string error;
        ImGui::BeginDisabled(!PathFinder::IsAssetAuthoringEnabled());
        DrawAssetPicker(error);
        ImGui::EndDisabled();
        if (!error.empty())
        {
            ImGui::TextWrapped("%s", error.c_str());
        }
        if (!active)
        {
            ImGui::TextWrapped("Create or open a material asset. No Scene object or MeshRenderer selection is "
                "required.");
            return;
        }
        PreparePreviewFloor();
        DrawSession(*active);
    }

    void Shutdown()
    {
        std::vector<job_handle> pending;
        {
            std::lock_guard lock(sessionMutex);
            shuttingDown = true;
            if (previewFloorJob.valid())
            {
                pending.push_back(previewFloorJob);
            }
            for (const auto& session : sessions)
            {
                if (session->previewJob.valid())
                {
                    pending.push_back(session->previewJob);
                }
            }
        }
        // Jobs own their inputs and never access a Session or DataSystem. Drain
        // before the compiler, runtime paths or editor texture owners are destroyed.
        for (const auto& job : pending)
        {
            try
            {
                job.wait();
            }
            catch (const std::exception&)
            {
                // Failed drafts already preserve the accepted material.
            }
        }
        std::lock_guard lock(sessionMutex);
        active = nullptr;
        sessions.clear();
        inspectorPreview.reset();
        previewFloor = {};
        previewFloorJob = {};
        previewFloorWork.reset();
    }

    void DrawInspectorPreview(MeshRenderer& renderer)
    {
        std::lock_guard lock(sessionMutex);
        if (shuttingDown)
        {
            return;
        }
        ReconcileSceneLocked();
        if (!ImGui::CollapsingHeader("Preview"))
        {
            inspectorPreviewVisible = {};
            return;
        }
        inspectorPreviewVisible = std::chrono::steady_clock::now();
        if (!PreparePreviewFloor())
        {
            ImGui::TextWrapped("%s", previewFloorError.empty() ? "Preparing preview environment..."
                                                               : previewFloorError.c_str());
            return;
        }
        auto source =
            renderer.m_Material ? material_graph::SceneMaterialSource::Capture(*renderer.m_Material) : nullptr;
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
        const auto state =
            EnhancedSceneRenderer::GetLiveDisplaySnapshot().Get(EnhancedLiveDisplayTarget::MaterialPreview);
        const auto texture = EnhancedSceneRenderer::GetLiveDisplayTexture(EnhancedLiveDisplayTarget::MaterialPreview);
        if (inspectorPreview->coverage.flags & EnhancedMaterialCoverage::Blended)
        {
            ImGui::TextWrapped("Preview is available for Opaque and Masked surfaces.");
        }
        else if (state.key.historyRevision != inspectorPreviewRevision || !state.previewComplete || !texture.textureId)
        {
            ImGui::TextDisabled("Preparing material preview...");
        }
        else
        {
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::Image(static_cast<ImTextureID>(texture.textureId),
                         ImVec2{width, width * float(texture.height) / std::max(texture.width, 1u)});
        }
        if (ImGui::SmallButton("Refresh"))
        {
            inspectorPreviewRevision = ++previewSerial;
        }
    }

    bool CapturePreviewRequest(EnhancedLiveViewRequest& request)
    {
        ce::profile_scope profile{ce::marker<"MaterialPreviewRequest">()};
        std::unique_lock lock(sessionMutex, std::defer_lock);
        {
            ce::profile_scope wait{ce::marker<"MaterialPreviewRequestLockWait">()};
            lock.lock();
        }
        if (shuttingDown || !previewFloor[0] || !previewFloor[1])
        {
            return false;
        }
        ReconcileSceneLocked();
        const auto now = std::chrono::steady_clock::now();
        if (active && (active->previewPinned || now - active->previewVisible <= std::chrono::milliseconds(250)) &&
            active->previewSource && !(active->previewSource->coverage.flags & EnhancedMaterialCoverage::Blended))
        {
            request = {};
            request.key = {3, active->previewRevision};
            request.displayTarget = EnhancedLiveDisplayTarget::MaterialPreview;
            request.viewFlags = EnhancedLiveViewFlags::None;
            request.materialPreview = active->previewSource;
            request.materialPreviewFloor = previewFloor;
            return true;
        }
        if (inspectorPreview && now - inspectorPreviewVisible <= std::chrono::milliseconds(250) &&
            !(inspectorPreview->coverage.flags & EnhancedMaterialCoverage::Blended))
        {
            request = {};
            request.key = {3, inspectorPreviewRevision};
            request.displayTarget = EnhancedLiveDisplayTarget::MaterialPreview;
            request.viewFlags = EnhancedLiveViewFlags::None;
            request.materialPreview = inspectorPreview;
            request.materialPreviewFloor = previewFloor;
            return true;
        }
        return false;
    }

    CommandResult Command(const std::vector<std::string>& parts)
    {
        using namespace ConsoleCmd;
        std::lock_guard lock(sessionMutex);
        if (shuttingDown)
        {
            return PreconditionFailed("material.editor.shutdown", "The Editor is shutting down");
        }
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
                visible.Append(Describe(*session));
                if (session->Dirty())
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
        if (parts.size() == 2 && parts[1] == "new")
        {
            std::string error;
            return OpenAssetLocked({}, error, true) ? Ok({}, Describe(*active)) : Fail("material.editor.new", error);
        }
        if (parts.size() == 3 && parts[1] == "asset")
        {
            Uuid::Uuid16 identity;
            if (!Uuid::TryParse(parts[2], identity))
            {
                return InvalidArguments("Expected a material asset GUID");
            }
            std::string error;
            try
            {
                auto material = DataSystems->LoadMaterialShared(FileGuid(identity));
                return material && OpenAssetLocked(material, error) ? Ok({}, Describe(*active))
                                                                   : Fail("material.editor.asset", error);
            }
            catch (const std::exception& failure)
            {
                return Fail("material.editor.asset", failure.what());
            }
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
            return PreconditionFailed("material.editor.closed", "Create or open a material asset first");
        }
        if (parts.size() == 3 && parts[1] == "preview" && (parts[2] == "on" ||
            parts[2] == "off" || parts[2] == "refresh"))
        {
            active->previewPinned = parts[2] != "off";
            if (parts[2] == "refresh")
            {
                active->attemptedRevision = std::numeric_limits<std::uint64_t>::max();
                active->previewPending = true;
            }
            RefreshPreview(*active);
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
