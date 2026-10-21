#include "EditorModelPlacement.h"
#include "EditorAssetDatabase.h"
#include "../../Engine/RenderEngine/Assets/PreparedClusterPages.h"
#include "../../Engine/RenderEngine/Assets/ModelMaterialGraph.h"
#include "../../Engine/RenderEngine/MaterialGraphRuntime.h"
#include "../../Engine/RenderEngine/RHI/RHIShaderCompiler.h"
#include "../../Engine/Utility_Framework/WindowsApartment.h"
#include "EditorModelCollisionGeometry.h"
#include "../../Engine/EngineDiagnostics/ProfileScope.h"

#include "DataSystem.h"
#include "Material.h"
#include "TypeTrait.h"
#include "ModelSceneInstantiation.h"
#include "JobScheduler.h"
#include "ReflectionUndo.h"
#include "Scene.h"
#include "SceneManager.h"
#include "imgui.h"
#include "EditorWindowNames.h"
#include "Windows/EditorToolboxWindows.h"

#include "../../Engine/Utility_Framework/StandardLibrary.h"

namespace Editor
{
    struct ModelPlacement::Request
    {
        HashedGuid requestId{ TypeTrait::MakeRuntimeResourceId() };
        std::uint32_t sceneId{};
        std::string path;
        std::optional<math::vector3> position;
        bool gameMode{};
        file::path assetRoot;
        std::weak_ptr<ProjectLayerSettings> project;
        DataSystem::ModelPreparation modelPreparation;
        job_handle preparation;
        job_handle materialPreparation;
        assets::ModelAssetGeneration::Shared sourceGeneration;
        std::vector<own::shared_owner<Material>> finalMaterials;
        std::vector<std::string> materialErrors;
        bool deferredMaterials{};
        bool sceneApplied{};
        bool materialStarted{};
        std::chrono::steady_clock::time_point sceneAppliedAt;
        std::shared_ptr<std::atomic<bool>> previewSubmitted{std::make_shared<std::atomic<bool>>(false)};
        std::atomic<bool> cancelled{ false };
        std::atomic<bool> ready{ false };
        std::atomic<bool> finished{ false };
        std::atomic<std::size_t> completedSteps{ 0 };
        std::atomic<std::size_t> totalSteps{ 0 };
        // Worker writes these before ready.store(release); only GT reads afterwards.
        own::unique_owner<ModelSceneInstantiation::PendingInstance> prepared;
        std::string error;
        // GT only, including undo cleanup. UI only posts cancellation.
        EntityHandle root;
        bool modelPublished{ false };
        bool preparationStarted{ false };
        bool materialWarmupStarted{ false };
        bool recipeDispatched{ false };
        std::vector<own::shared_owner<const material_graph::Generation>> materialWarmOwners;
        double admissionWaitMs{};
        bool positionApplied{ false };
        unsigned preparationRetries{};
        std::chrono::steady_clock::time_point retryDeadline{};
        std::chrono::steady_clock::time_point nextRetryAt{};
        std::size_t applyFrames{};
        std::size_t loadingFrames{};
        double prepareMs{};
        long long maxApplyUs{};
        std::chrono::steady_clock::time_point enqueuedAt{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point previousTick{};
        double maxTickGapMs{};
        double publishMs{};
    };

    struct ModelPlacement::Impl
    {
        std::mutex mutex;
        bool stopping{ true };
        job_handle preparationTail;
        // Cancelled requests remain here until their scheduler jobs release all captures.
        std::vector<own::shared_owner<Request>> preparations;
        std::vector<own::shared_owner<Request>> incoming;
        std::vector<own::shared_owner<Request>> cancellations;
        std::vector<own::shared_owner<Request>> visible;
        std::vector<own::shared_owner<Request>> pending; // GT only
        std::uint64_t completed{};
        std::uint64_t failed{};
        std::uint64_t cancelled{};
    };

    class ModelPlacement::Command final : public Meta::IUndoableCommand
    {
    public:
        Command(std::uint32_t sceneId, std::string path, std::optional<math::vector3> position)
            : m_sceneId(sceneId), m_path(std::move(path)), m_position(position),
              m_gameMode(SceneManagers->IsGameStart()) {}
        ~Command() override
        {
            if (m_request && !m_request->finished.load(std::memory_order_acquire))
                ModelPlacement::Get().Cancel(m_request);
        }
        void Redo() override
        { m_request = ModelPlacement::Get().Enqueue(m_sceneId, m_path, m_position, m_gameMode); }
        void Undo() override { ModelPlacement::Get().Cancel(m_request); }
    private:
        std::uint32_t m_sceneId;
        std::string m_path;
        std::optional<math::vector3> m_position;
        bool m_gameMode;
        own::shared_owner<Request> m_request;
    };

    ModelPlacement::ModelPlacement() : m_impl(std::make_unique<Impl>()) {}
    ModelPlacement::~ModelPlacement() { Shutdown(); }
    ModelPlacement& ModelPlacement::Get() { static ModelPlacement instance; return instance; }

    void ModelPlacement::Initialize()
    {
        // 창 본문을 이름에 건다. 표시 여부는 선언의 존재 술어가 정한다.
        m_statusBody = editor::windows::bind_window_body(
            EditorWindowName::kModelLoading,
            []() { ModelPlacement::Get().DrawStatus(); });

        auto& state = *m_impl;
        std::lock_guard lock(state.mutex);
        if (!state.stopping)
        {
            return;
        }
        state.stopping = false;
        state.preparationTail = {};
    }

    namespace
    {
        own::shared_owner<Material> PreparePlacementPreview(std::string& error, const file::path& packageDirectory = {})
        {
            auto graph = assets::BuildDefaultMaterialGraph(error);
            if (!graph) return {};
            // Neutral emission avoids spending the preview's first-use budget
            // on specular/diffuse integration. Geometry and coverage stay real.
            for (const auto& node : graph->graph.Nodes())
                if (node.type == "ShaderNodeBsdfPrincipled")
                    for (const auto& pin : node.pins)
                    {
                        if (pin.Identifier() == "Base Color") graph->graph.SetSocketValue(pin.id, std::array<double, 4>{0, 0, 0, 1});
                        if (pin.Identifier() == "Specular IOR Level") graph->graph.SetSocketValue(pin.id, 0.0);
                        if (pin.Identifier() == "Emission Color") graph->graph.SetSocketValue(pin.id, std::array<double, 4>{.35, .35, .35, 1});
                        if (pin.Identifier() == "Emission Strength") graph->graph.SetSocketValue(pin.id, 1.0);
                    }
            // Stable transient identity; never written into the asset catalog.
            const auto guid = FileGuid(assets::DeriveIdentity(
                {"editor-preview", Uuid::Uuid16{}.data, "shadergraph", "placement-default-v1"}).uuid);
            const auto generation = DataSystem::CompileEditorPreviewMaterial(*graph, guid,
                PathFinder::RelativeToShader("DefaultPassShader"),
                packageDirectory.empty() ? PathFinder::CachePath("LatticePreview") : packageDirectory, error, !packageDirectory.empty());
            if (!generation) return {};
            auto material = own::make_shared<Material>();
            material_graph::InstanceDescription description;
            description.graphId.value = guid.m_guid;
            if (!DataSystems->ConfigureMaterialGraph(*material, generation, description, error)) return {};
            material->m_name = "Preparing source material";
            material->MarkEditorPreview();
            return material;
        }

        template<class RequestOwner>
        job_handle StartPlacementOriginalMaterials(const RequestOwner& request)
        {
            const auto model = request->sourceGeneration;
            const auto count = model->Materials().size();
            request->finalMaterials.resize(count);
            request->materialErrors.resize(count);
            std::set<Uuid::Uuid16> meshes, used;
            for (const auto& node : model->Nodes())
                for (const auto& mesh : node.meshes) meshes.insert(mesh);
            for (const auto& mesh : model->Meshes())
                if (meshes.contains(mesh.meshId)) used.insert(mesh.materialId);
            auto indices = std::make_shared<std::vector<std::size_t>>();
            for (std::size_t i = 0; i < count; ++i)
                if (used.contains(model->Materials()[i].materialId)) indices->push_back(i);
            const auto next = std::make_shared<std::atomic<std::size_t>>(0);
            job_group work;
            for (std::size_t worker = 0; worker < (std::min)(indices->size(), RHIShaderCompiler::MaxParallelCompiles()); ++worker)
                work.add([request, model, indices, next] {
                    const ce::platform::windows::apartment_scope apartment{ce::platform::windows::apartment_model::mta};
                    for (auto n = next->fetch_add(1); n < indices->size(); n = next->fetch_add(1))
                    {
                        if (request->cancelled.load(std::memory_order_acquire)) return;
                        const auto index = (*indices)[n];
                        try
                        {
                            request->finalMaterials[index] = ModelSceneInstantiation::PendingInstance::PrepareMaterial(
                                model, index, request->materialErrors[index]);
                            if (request->finalMaterials[index]) request->finalMaterials[index]->TagEditorPlacement(
                                request->requestId.m_ID_Data,
                                std::chrono::duration<double, std::milli>(request->enqueuedAt.time_since_epoch()).count());
                            if (!request->finalMaterials[index] && request->materialErrors[index].empty())
                                request->materialErrors[index] = "Source material preparation returned no owner.";
                        }
                        catch (const std::exception& e) { request->materialErrors[index] = e.what(); }
                    }
                });
            return ce::get_job_scheduler().submit(std::move(work));
        }

        template<class RequestOwner>
        job_handle StartPlacementMaterialWarmup(const RequestOwner& request)
        {
            // GT discovery only after the model job completes. Typed inputs already
            // carry prepared materials. Legacy recipe construction used to compile
            // each graph serially on one worker, even when compiler slots were idle.
            if (DataSystems->HasPreparedModelScene(request->modelPreparation)) return {};
            std::string error;
            const auto model = DataSystems->ReadPreparedModel(request->modelPreparation, error);
            if (!model) return {}; // Preserve the normal recipe's failure/retry path.
            std::set<Uuid::Uuid16> meshes, materials;
            for (const auto& node : model->Nodes())
                for (const auto& mesh : node.meshes) meshes.insert(mesh);
            for (const auto& mesh : model->Meshes())
                if (meshes.contains(mesh.meshId)) materials.insert(mesh.materialId);
            std::vector<FileGuid> graphs;
            for (const auto& material : model->Materials())
                if (materials.contains(material.materialId))
                    graphs.emplace_back(assets::ModelMaterialGraphId(material.materialId));
            request->materialWarmOwners.resize(graphs.size());
            if (graphs.empty()) return {};
            const auto next = std::make_shared<std::atomic<std::size_t>>(0);
            const auto inputs = std::make_shared<const std::vector<FileGuid>>(std::move(graphs));
            job_group work;
            const auto workers = (std::min)(inputs->size(), RHIShaderCompiler::MaxParallelCompiles());
            for (std::size_t worker = 0; worker < workers; ++worker)
                work.add([request, inputs, next] {
                    ce::profile_scope profile{ce::marker<"ModelPlacement.MaterialWarmup">()};
                    for (auto index = next->fetch_add(1); index < inputs->size(); index = next->fetch_add(1))
                    {
                        if (request->cancelled.load(std::memory_order_acquire)) return;
                        std::string failure;
                        request->materialWarmOwners[index] = DataSystems->LoadMaterialGraphGeneration((*inputs)[index], failure);
                        // Missing/invalid graphs retain the existing editable recovery
                        // path in Prepare; this stage never invents a replacement.
                    }
                });
            return ce::get_job_scheduler().submit(std::move(work));
        }

        template<class RequestOwner>
        void PreparePlacementRootPages(const RequestOwner& request)
        {
            ce::profile_scope roots{ce::marker<"ModelPlacement.PrepareRootPages">()};
            if (request->cancelled.load(std::memory_order_acquire)) return;
            std::string error;
            if (DataSystems->HasPreparedModelScene(request->modelPreparation))
            {
                assets::ModelSceneAssetInputs inputs;
                if (!DataSystems->ReadPreparedModelScene(request->modelPreparation, inputs, error))
                    throw std::runtime_error(error);
                std::set<Uuid::Uuid16> usedMeshes;
                for (const auto& node : inputs.descriptor->summary.nodes)
                    for (const auto& id : node.meshAssetIds) usedMeshes.insert(id.value);
                for (const auto& mesh : inputs.meshes)
                    if (mesh && usedMeshes.contains(mesh->meshId) && mesh->streamMetadata && mesh->streamMetadata->archive)
                        assets::cluster::PrepareRootPages(*mesh->streamMetadata->archive, &request->cancelled);
            }
            else
            {
                const auto generation = DataSystems->ReadPreparedModel(request->modelPreparation, error);
                if (!generation) throw std::runtime_error(error);
                std::set<Uuid::Uuid16> usedMeshes;
                for (const auto& node : generation->Nodes()) usedMeshes.insert(node.meshes.begin(), node.meshes.end());
                for (const auto& mesh : generation->Meshes())
                    if (usedMeshes.contains(mesh.meshId) && mesh.streamDescriptor && mesh.streamDescriptor->streamMetadata && mesh.streamDescriptor->streamMetadata->archive)
                        assets::cluster::PrepareRootPages(*mesh.streamDescriptor->streamMetadata->archive, &request->cancelled);
            }
        }
    }

    void ModelPlacement::Prepare(const own::shared_owner<Request>& request)
    {
        ce::profile_scope timing{ ce::marker<"ModelPlacement.PrepareRecipe">() };
        if (request->cancelled.load(std::memory_order_acquire))
        {
            request->ready.store(true, std::memory_order_release);
            return;
        }
        if (DataSystems->HasPreparedModelScene(request->modelPreparation))
        {
            const auto prepareStart = std::chrono::steady_clock::now();
            try
            {
                assets::ModelSceneAssetInputs inputs;
                if (DataSystems->ReadPreparedModelScene(request->modelPreparation, inputs, request->error) &&
                    !request->cancelled.load(std::memory_order_acquire))
                {
                    if (request->cancelled.load(std::memory_order_acquire))
                    {
                        request->ready.store(true, std::memory_order_release);
                        return;
                    }
                    ModelSceneInstantiation::Options options;
                    options.createMeshCollider = inputs.createMeshCollider;
                    if (options.createMeshCollider)
                    {
                        const auto assetRoot = request->assetRoot;
                        const auto expectedProject = request->project;
                        const auto model = inputs.descriptor->summary.modelAssetId.value;
                        std::vector<Uuid::Uuid16> meshes;
                        for (const auto& mesh : inputs.descriptor->summary.meshes)
                        {
                            meshes.push_back(mesh.meshAssetId.value);
                        }
                        options.collisionGeometry = [assetRoot, expectedProject, model, meshes = std::move(meshes)](
                            Scene& scene, std::uint32_t meshIndex, const ce::physics::triangle_mesh_source& source)
                            -> ce::physics::result<ce::physics::geometry_asset_key>
                        {
                            const auto current = expectedProject.lock();
                            if (!current || current != SceneManagers->ProjectLayers() ||
                                assetRoot != PathFinder::Relative() || SceneManagers->IsPlayCommitted() ||
                                meshIndex >= meshes.size())
                            {
                                return std::unexpected(ce::physics::error{ ce::physics::error_code::wrong_phase, 0,
                                    "Model collision project changed or Play is active" });
                            }
                            return PublishModelCollisionGeometry(scene, assetRoot, model, meshes[meshIndex], source);
                        };
                    }
                    request->prepared = ModelSceneInstantiation::PendingInstance::Prepare(std::move(inputs), options);
                    if (!request->prepared)
                    {
                        request->error = "Typed model hierarchy/material preparation failed: " + request->path;
                    }
                }
            }
            catch (const std::exception& error)
            {
                request->error = error.what();
            }
            catch (...)
            {
                request->error = "Typed model scene preparation failed.";
            }
            request->prepareMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - prepareStart).count();
            request->ready.store(true, std::memory_order_release);
            return;
        }
        // The shared pool requires an explicit apartment scope for COM/WinRT jobs.
        // Keep it local without changing a shared worker's name or priority.
        const ce::platform::windows::apartment_scope apartmentScope{ ce::platform::windows::apartment_model::mta };
        const HRESULT com = apartmentScope.result;
        const auto prepareStart = std::chrono::steady_clock::now();
        try
        {
            if (FAILED(com))
            {
                request->error = "Model image decoder initialization failed";
            }
            else
            {
                auto generation = DataSystems->ReadPreparedModel(request->modelPreparation, request->error);
                if (!generation)
                {
                    if (request->error.empty())
                    {
                        request->error = "Model asset preparation failed: " + request->path;
                    }
                }
                else if (!request->cancelled.load(std::memory_order_acquire))
                {
                    if (request->cancelled.load(std::memory_order_acquire))
                    {
                        request->ready.store(true, std::memory_order_release);
                        return;
                    }
                    ModelSceneInstantiation::Options options;
                    options.editorPlacementId = request->requestId.m_ID_Data;
                    options.editorPlacementStartMs = std::chrono::duration<double, std::milli>(request->enqueuedAt.time_since_epoch()).count();
                    options.editorPreviewSubmitted = request->previewSubmitted;
                    if (request->deferredMaterials)
                    {
                        request->sourceGeneration = generation;
                        options.previewMaterial = PreparePlacementPreview(request->error);
                        if (!options.previewMaterial) throw std::runtime_error(request->error);
                    }
                    options.createMeshCollider = DataSystems->ReadModelCreateMeshCollider(
                        FileGuid(generation->Identity().modelId));
                    if (options.createMeshCollider)
                    {
                        const auto assetRoot = request->assetRoot;
                        const auto expectedProject = request->project;
                        const auto model = generation->Identity().modelId;
                        std::vector<Uuid::Uuid16> meshes;
                        for (const auto& mesh : generation->Meshes())
                        {
                            meshes.push_back(mesh.meshId);
                        }
                        options.collisionGeometry = [assetRoot, expectedProject, model, meshes = std::move(meshes)](
                            Scene& scene, std::uint32_t meshIndex, const ce::physics::triangle_mesh_source& source)
                            -> ce::physics::result<ce::physics::geometry_asset_key>
                        {
                            const auto current = expectedProject.lock();
                            if (!current || current != SceneManagers->ProjectLayers() ||
                                assetRoot != PathFinder::Relative() || SceneManagers->IsPlayCommitted() ||
                                meshIndex >= meshes.size())
                            {
                                return std::unexpected(ce::physics::error{ce::physics::error_code::wrong_phase, 0,
                                    "Model collision authoring project changed or Play is active"});
                            }
                            return PublishModelCollisionGeometry(scene, assetRoot, model, meshes[meshIndex], source);
                        };
                    }
                    request->prepared = ModelSceneInstantiation::PendingInstance::Prepare(std::move(generation), options);
                    if (!request->prepared)
                    {
                        request->error = "Model preparation failed: " + request->path;
                    }
                }
            }
        }
        catch (const std::exception& error)
        {
            request->error = error.what();
        }
        catch (...)
        {
            request->error = "Unexpected model preparation failure";
        }
        request->prepareMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - prepareStart).count();
        request->ready.store(true, std::memory_order_release);
    }

    void ModelPlacement::Execute(std::uint32_t sceneId, std::string path,
        std::optional<math::vector3> position)
    {
        Meta::UndoManager::GetInstance()->Execute(
            std::make_unique<Command>(sceneId, std::move(path), position));
    }

    own::shared_owner<ModelPlacement::Request> ModelPlacement::Enqueue(
        std::uint32_t sceneId, const std::string& path,
        const std::optional<math::vector3>& position, bool gameMode)
    {
        ce::profile_scope timing{ ce::marker<"ModelPlacement.Enqueue">() };
        auto request = own::make_shared<Request>();
        request->sceneId = sceneId;
        request->path = path;
        request->position = position;
        request->gameMode = gameMode;
        char* preview{};
        std::size_t previewLength{};
        _dupenv_s(&preview, &previewLength, "CE_EDITOR_MODEL_PREVIEW");
        request->deferredMaterials = !gameMode && PathFinder::IsAssetAuthoringEnabled() &&
            (!preview || std::string_view(preview) != "0");
        std::free(preview);
        request->assetRoot = PathFinder::Relative();
        request->project = SceneManagers->ProjectLayers();
        {
            std::lock_guard lock(m_impl->mutex);
            if (m_impl->stopping)
            {
                return {};
            }
            m_impl->incoming.push_back(request);
            m_impl->visible.push_back(request);
            m_impl->preparations.push_back(request);

        }
        return request;
    }

    void ModelPlacement::Cancel(const own::shared_owner<Request>& request)
    {
        if (!request || request->cancelled.exchange(true, std::memory_order_acq_rel)) return;
        std::lock_guard lock(m_impl->mutex);
        if (!m_impl->stopping) m_impl->cancellations.push_back(request);
    }

    namespace
    {
        Scene* PlacementScene(std::uint32_t sceneId)
        {
            for (Scene* scene : SceneManagers->GetScenes())
                if (scene && scene->GetSceneId() == sceneId) return scene;
            return nullptr;
        }
    }

    bool ModelPlacement::RetryInvalidatedPreparation(const own::shared_owner<Request>& request)
    {
        // No scene entities exist before publication. A derived-data rebuild or
        // generated material watcher may invalidate this ticket during Prepare.
        // Retry the source request, never publish the obsolete generation.
        if (request->root.IsValid() || request->modelPublished
            || request->cancelled.load(std::memory_order_acquire)
            || request->assetRoot != PathFinder::Relative()
            || request->project.lock() != SceneManagers->ProjectLayers()
            || !DataSystems->IsModelPreparationInvalidated(request->modelPreparation)) return false;
        const auto now = std::chrono::steady_clock::now();
        if (request->retryDeadline == std::chrono::steady_clock::time_point{})
        {
            request->retryDeadline = now + std::chrono::seconds(30);
            request->nextRetryAt = now + std::chrono::milliseconds(100);
        }
        if (now >= request->retryDeadline) return false;
        // Watcher bursts and an in-progress publication are temporary admission
        // failures. Poll on later frames instead of spending the entire retry
        // allowance before the new snapshot can be admitted.
        if (now < request->nextRetryAt || EditorAssetDatabase::Get().HasPendingTexturePublications()) return true;
        request->nextRetryAt = now + std::chrono::milliseconds(100);
        const auto previousRecipe = request->preparation;
        auto preparation = DataSystems->PrepareModelAssetByPath(request->path);
        if (!preparation) return true;
        request->prepared.reset();
        request->materialWarmOwners.clear();
        request->modelPreparation = std::move(preparation);
        request->error.clear();
        request->ready.store(false, std::memory_order_release);
        ++request->preparationRetries;
        job_group work;
        work.add([request] { Prepare(request); });
        work.add([request] { PreparePlacementRootPages(request); });
        std::array<job_handle, 2> dependencies;
        std::size_t dependencyCount = 0;
        const auto modelWork = DataSystems->ModelPreparationCompletion(request->modelPreparation);
        if (modelWork.valid()) dependencies[dependencyCount++] = modelWork;
        if (previousRecipe.valid()) dependencies[dependencyCount++] = previousRecipe;
        try
        {
            request->preparation = ce::get_job_scheduler().submit_after(
                std::span<const job_handle>(dependencies.data(), dependencyCount), std::move(work));
        }
        catch (const std::exception& error)
        {
            request->error = error.what();
            request->ready.store(true, std::memory_order_release);
            return false;
        }
        std::printf("[model.async] retry invalidated preparation path=%s attempt=%u\n",
            request->path.c_str(), request->preparationRetries);
        return true;
    }

    void ModelPlacement::Tick()
    {
        ce::profile_scope timing{ ce::marker<"ModelPlacement.Tick">() };
        auto& state = *m_impl;
        std::vector<own::shared_owner<Request>> cancellations;
        {
            std::lock_guard lock(state.mutex);
            state.pending.insert(state.pending.end(), state.incoming.begin(), state.incoming.end());
            state.incoming.clear();
            cancellations.swap(state.cancellations);
        }
        const auto cleanup = [&state](Request& request)
        {
            if (Scene* scene = PlacementScene(request.sceneId))
            {
                if (request.root.IsValid())
                {
                    resetSelectedObjectEvent.Broadcast();
                    if (request.prepared) request.prepared->Cancel(*scene);
                    else if (auto* root = scene->Resolve(request.root); root && !root->IsDestroyMark())
                        scene->DestroyEntity(root->m_index);
                    request.root = {};
                    // Play snapshot/scene switching follows this pump. Remove cancelled
                    // entities now so a pending destroy mark cannot enter that snapshot.
                    scene->EndFramePass();
                }
            }
            if (request.preparation.is_complete() && request.materialPreparation.is_complete())
            {
                request.prepared.reset();
                request.modelPreparation.reset();
                request.materialWarmOwners.clear();
                request.sourceGeneration.reset();
                request.finalMaterials.clear();
            }
            if (!request.finished.exchange(true, std::memory_order_acq_rel))
            {
                ++state.cancelled;
            }
        };
        for (const auto& request : cancellations) cleanup(*request);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2);
        bool advanced = false;
        for (const auto& request : state.pending)
        {
            const auto observedTick = std::chrono::steady_clock::now();
            if (request->previousTick != std::chrono::steady_clock::time_point{})
                request->maxTickGapMs = (std::max)(request->maxTickGapMs,
                    std::chrono::duration<double, std::milli>(observedTick - request->previousTick).count());
            request->previousTick = observedTick;
            Scene* active = SceneManagers->GetActiveScene();
            if (!active || active->GetSceneId() != request->sceneId
                || SceneManagers->IsGameStart() != request->gameMode || SceneManagers->IsSceneLoading()
                || request->assetRoot != PathFinder::Relative()
                || request->project.lock() != SceneManagers->ProjectLayers())
                request->cancelled.store(true, std::memory_order_release);
            if (request->cancelled.load(std::memory_order_acquire)) { cleanup(*request); continue; }
            if (!request->preparationStarted)
            {
                // A known resolver publication wave would cancel this work anyway.
                // Poll before dispatch rather than repeatedly decoding/compiling it.
                if (EditorAssetDatabase::Get().HasPendingTexturePublications())
                {
                    ++request->loadingFrames;
                    continue;
                }
                request->preparationStarted = true;
                request->admissionWaitMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - request->enqueuedAt).count();
                std::lock_guard lock(state.mutex);
                try
                {
                    request->modelPreparation = DataSystems->PrepareModelAssetByPath(request->path);
                    if (!request->modelPreparation)
                    {
                        request->error = "Cannot queue model asset preparation: " + request->path;
                        request->ready.store(true, std::memory_order_release);
                    }
                }
                catch (const std::exception& error)
                {
                    request->error = error.what();
                    request->ready.store(true, std::memory_order_release);
                }
            }
            if (request->modelPreparation && !request->recipeDispatched)
            {
                const auto modelWork = DataSystems->ModelPreparationCompletion(request->modelPreparation);
                if (modelWork.valid() && !modelWork.is_complete())
                {
                    ++request->loadingFrames;
                    continue;
                }
                try
                {
                    if (modelWork.valid()) modelWork.wait(); // Complete-only, never block GT.
                    if (!request->materialWarmupStarted)
                    {
                        if (DataSystems->HasPreparedModelScene(request->modelPreparation)) request->deferredMaterials = false;
                        request->preparation = request->deferredMaterials ? job_handle{} : StartPlacementMaterialWarmup(request);
                        request->materialWarmupStarted = true;
                    }
                    if (!request->preparation.is_complete())
                    {
                        ++request->loadingFrames;
                        continue;
                    }
                    request->preparation.wait(); // Join the whole bounded batch before using its owners.
                    request->recipeDispatched = true;
                    std::lock_guard lock(state.mutex);
                    {
                        job_group work;
                        work.add([request] { Prepare(request); });
                        work.add([request] { PreparePlacementRootPages(request); });
                        std::array<job_handle, 2> dependencies;
                        std::size_t dependencyCount = 0;
                        // Shared asset preparation deduplicates scene/drop requests.
                        // Serialize only recipe construction; waiting consumes no worker.
                        if (m_impl->preparationTail.valid() && !m_impl->preparationTail.is_complete())
                        {
                            dependencies[dependencyCount++] = m_impl->preparationTail;
                        }
                        request->preparation = ce::get_job_scheduler().submit_after(
                            std::span<const job_handle>(dependencies.data(), dependencyCount), std::move(work));
                        m_impl->preparationTail = request->preparation;
                    }
                }
                catch (const std::exception& error)
                {
                    request->recipeDispatched = true;
                    request->error = error.what();
                    request->ready.store(true, std::memory_order_release);
                }
            }
            if (!request->preparation.is_complete())
            {
                ++request->loadingFrames;
                continue;
            }
            try
            {
                // Already complete: observe errors from either parallel producer.
                request->preparation.wait();
            }
            catch (const std::exception& error)
            {
                request->prepared.reset();
                request->error = error.what();
                request->ready.store(true, std::memory_order_release);
            }
            request->materialWarmOwners.clear(); // Recipe/material instances now retain their exact generations.
            if (!request->ready.load(std::memory_order_acquire))
            {
                if (!request->preparation.is_complete())
                {
                    ++request->loadingFrames;
                    continue;
                }
                // A scheduler dispatch failure can complete without entering Prepare.
                // Poll only: the presentation/game loop never waits for unfinished work.
                if (!request->ready.load(std::memory_order_acquire))
                {
                    std::string assetError;
                    if (DataSystems->HasPreparedModelScene(request->modelPreparation))
                    {
                        assets::ModelSceneAssetInputs ignored;
                        (void)DataSystems->ReadPreparedModelScene(request->modelPreparation, ignored, assetError);
                    }
                    else
                    {
                        (void)DataSystems->ReadPreparedModel(request->modelPreparation, assetError);
                    }
                    request->error = assetError.empty()
                        ? "Model preparation job completed without a result" : std::move(assetError);
                    request->ready.store(true, std::memory_order_release);
                }
            }
            if (!request->prepared)
            {
                if (RetryInvalidatedPreparation(request)) continue;
                Debug::PrintLog(spdlog::level::err, request->error);
                ++state.failed;
                request->modelPreparation.reset();
                request->finished.store(true, std::memory_order_release);
                continue;
            }
            if (request->sceneApplied)
            {
                if (!request->materialStarted)
                {
                    // Let the cold first frame finish its shared render helpers
                    // before CPU-heavy source compilation competes for the same cores.
                    // Culled/hidden placements still make bounded background progress.
                    if (!request->previewSubmitted->load(std::memory_order_acquire) &&
                        std::chrono::steady_clock::now() - request->sceneAppliedAt < std::chrono::seconds(30)) continue;
                    request->materialPreparation = StartPlacementOriginalMaterials(request);
                    request->materialStarted = true;
                    continue;
                }
                if (!request->materialPreparation.is_complete()) continue;
                try
                {
                    request->materialPreparation.wait(); // Completed only, never block the editor.
                    if (DataSystems->IsModelPreparationInvalidated(request->modelPreparation))
                        throw std::runtime_error("Model snapshot changed during source material preparation; late result rejected.");
                    const auto failure = std::ranges::find_if(request->materialErrors, [](const auto& e) { return !e.empty(); });
                    if (failure != request->materialErrors.end()) throw std::runtime_error(*failure);
                    if (!request->prepared->ApplyMaterials(*active, request->finalMaterials))
                        throw std::runtime_error("Placement was removed before its source materials were applied.");
                    std::printf("[model.materials] path=%s appliedMs=%.3f\n", request->path.c_str(),
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - request->enqueuedAt).count());
                    ++state.completed;
                }
                catch (const std::exception& e)
                {
                    // Keep the editable preview, report failure; do not mislabel it as source completion.
                    Debug::PrintLog(spdlog::level::err, std::string("Source material preparation failed: ") + e.what());
                    ++state.failed;
                }
                request->prepared.reset();
                request->sourceGeneration.reset();
                request->finalMaterials.clear();
                request->modelPreparation.reset();
                request->finished.store(true, std::memory_order_release);
                continue;
            }
            // 전체 요청에 2ms를 배정하고 한 프레임에는 한 인스턴스만 진행한다.
            if (advanced || std::chrono::steady_clock::now() >= deadline) continue;
            advanced = true;
            if (!request->modelPublished)
            {
                const auto publishStart = std::chrono::steady_clock::now();
                std::string error;
                if (!DataSystems->PublishPreparedModel(request->modelPreparation, error))
                {
                    if (RetryInvalidatedPreparation(request)) continue;
                    Debug::PrintLog(spdlog::level::err, error);
                    ++state.failed;
                    request->prepared.reset();
                    request->modelPreparation.reset();
                    request->finished.store(true, std::memory_order_release);
                    continue;
                }
                request->modelPublished = true;
                request->publishMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - publishStart).count();
                if (!request->deferredMaterials) request->modelPreparation.reset();
            }
            request->totalSteps.store(request->prepared->TotalSteps(), std::memory_order_relaxed);
            if (std::chrono::steady_clock::now() >= deadline)
            {
                continue;
            }
            ModelSceneInstantiation::PendingInstance::Status result;
            const auto applyStart = std::chrono::steady_clock::now();
            try
            {
                result = request->prepared->Advance(*active, 16,
                    std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now()));
            }
            catch (const std::exception& error)
            {
                Debug::PrintLog(spdlog::level::err, error.what());
                request->prepared->Cancel(*active);
                result = ModelSceneInstantiation::PendingInstance::Status::Failed;
            }
            request->root = request->prepared->Root();
            ++request->applyFrames;
            request->maxApplyUs = (std::max)(request->maxApplyUs,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - applyStart).count()));
            request->completedSteps.store(request->prepared->CompletedSteps(), std::memory_order_relaxed);
            if (!request->positionApplied && request->root.IsValid())
            {
                if (auto* root = active->Resolve(request->root); root && request->position)
                    root->Transform_().SetPosition(*request->position, TransformWriteReason::ModelImport);
                request->positionApplied = true;
            }
            if (result == ModelSceneInstantiation::PendingInstance::Status::Failed)
            {
                ++state.failed;
                request->finished.store(true, std::memory_order_release);
                cleanup(*request);
            }
            if (result == ModelSceneInstantiation::PendingInstance::Status::Complete)
            {
                std::printf("[model.async] ready path=%s steps=%zu frames=%zu loadingFrames=%zu prepareMs=%.3f maxApplyUs=%lld\n",
                    request->path.c_str(), request->prepared->TotalSteps(), request->applyFrames,
                    request->loadingFrames, request->prepareMs, request->maxApplyUs);
                std::printf("[model.timing] path=%s totalMs=%.3f publishMs=%.3f maxTickGapMs=%.3f retries=%u admissionWaitMs=%.3f\n",
                    request->path.c_str(), std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - request->enqueuedAt).count(),
                    request->publishMs, request->maxTickGapMs, request->preparationRetries, request->admissionWaitMs);
                if (request->deferredMaterials)
                {
                    request->sceneApplied = true;
                    request->sceneAppliedAt = std::chrono::steady_clock::now();
                }
                else
                {
                    ++state.completed;
                    request->prepared.reset();
                    request->finished.store(true, std::memory_order_release);
                }
            }
        }
        std::erase_if(state.pending, [](const auto& request) { return request->finished.load(std::memory_order_acquire); });
        std::lock_guard lock(state.mutex);
        std::erase_if(state.visible, [](const auto& request) { return request->finished.load(std::memory_order_acquire); });
        std::erase_if(state.preparations, [](const auto& request)
        {
            if (!request->finished.load(std::memory_order_acquire)) return false;
            if (!request->preparationStarted && !request->finished.load(std::memory_order_acquire)) return false;
            if (!request->preparation.is_complete() || !request->materialPreparation.is_complete())
            {
                return false;
            }
            if (request->cancelled.load(std::memory_order_acquire))
            {
                // Cancellation may have removed the visible request before its
                // worker published ready. Do not retain its recipe in undo history.
                request->prepared.reset();
                request->modelPreparation.reset();
                request->materialWarmOwners.clear();
                request->sourceGeneration.reset();
                request->finalMaterials.clear();
            }
            return true;
        });
    }

    ModelPlacement::Progress ModelPlacement::GetProgress() const
    {
        Progress progress;
        std::lock_guard lock(m_impl->mutex);
        for (const auto& request : m_impl->visible)
        {
            if (request->cancelled.load(std::memory_order_acquire) ||
                request->finished.load(std::memory_order_acquire))
            {
                continue;
            }
            ++progress.activeRequests;
            if (progress.activeRequests == 1)
            {
                progress.path = request->path;
                progress.totalSteps = request->totalSteps.load(std::memory_order_relaxed);
                progress.completedSteps = std::min(progress.totalSteps,
                    request->completedSteps.load(std::memory_order_relaxed));
            }
        }
        return progress;
    }

    // PHASE 21 M4 3단계: 프레임은 셸이 연다. 큐가 비면 창 자체가 존재하지
    // 않으므로(existence predicate), 여기 첫 두 줄이 하던 조기 반환은
    // HasVisible이 대신 답한다.
    void ModelPlacement::DrawStatus()
    {
        std::vector<own::shared_owner<Request>> visible;
        { std::lock_guard lock(m_impl->mutex); visible = m_impl->visible; }
        for (const auto& request : visible)
        {
            const std::string requestId = std::to_string(request->requestId.m_ID_Data);
            ImGui::PushID(requestId.c_str());
            ImGui::TextUnformatted(file::path(request->path).filename().string().c_str());
            const auto total = request->totalSteps.load(std::memory_order_relaxed);
            if (request->sceneApplied && !request->previewSubmitted->load(std::memory_order_acquire))
                ImGui::TextUnformatted("Preparing model preview...");
            else if (request->sceneApplied) ImGui::TextUnformatted("Model is editable. Preparing source materials...");
            else if (total) ImGui::ProgressBar(static_cast<float>(request->completedSteps.load(std::memory_order_relaxed)) / total);
            else ImGui::TextUnformatted("Preparing model...");
            if (ImGui::Button("Cancel")) Cancel(request);
            ImGui::PopID();
        }
    }

    bool ModelPlacement::HasVisible() const
    {
        std::lock_guard lock(m_impl->mutex);
        return !m_impl->visible.empty();
    }

    void ModelPlacement::Shutdown()
    {
        // 이 싱글톤도 `Shutdown` 뒤 `Initialize` 가 다시 온다(PHASE 21 W3).
        m_statusBody.reset();

        auto& state = *m_impl;
        std::vector<own::shared_owner<Request>> preparations;
        {
            std::lock_guard lock(state.mutex);
            if (state.stopping)
            {
                return;
            }
            state.stopping = true;
            for (const auto& request : state.visible)
            {
                request->cancelled.store(true, std::memory_order_release);
            }
            preparations = state.preparations;
            for (const auto& request : preparations)
            {
                request->cancelled.store(true, std::memory_order_release);
            }
        }
        // Lifecycle-only drain, outside the queue lock. Even requests removed from
        // the visible list must finish before DataSystem and its caches disappear.
        for (const auto& request : preparations)
        {
            try
            {
                request->preparation.wait();
                request->materialPreparation.wait();
            }
            catch (const std::exception& error)
            {
                Debug::PrintLog(spdlog::level::err, error.what());
            }
            catch (...)
            {
                Debug::PrintLog(spdlog::level::err, "Model preparation job failed during shutdown");
            }
        }
        Tick(); // presentation has stopped; scenes and DataSystem are still alive.
        std::lock_guard lock(state.mutex);
        state.pending.clear();
        state.visible.clear();
        state.preparations.clear();
        state.preparationTail = {};
    }

    void ModelPlacement::PrintStatus() const
    {
        // CLI and Tick both run on GT; the queue counts also include UI requests.
        std::lock_guard lock(m_impl->mutex);
        std::printf("[model.async] pending=%zu completed=%llu failed=%llu cancelled=%llu\n",
            m_impl->visible.size(), static_cast<unsigned long long>(m_impl->completed),
            static_cast<unsigned long long>(m_impl->failed), static_cast<unsigned long long>(m_impl->cancelled));
    }

    bool ModelPlacement::BakePreviewMaterial(const std::string& directory, std::string& error)
    {
        return !directory.empty() && bool(PreparePlacementPreview(error, directory));
    }

    bool ModelPlacement::IsIdle() const
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->visible.empty();
    }
}

namespace editor::windows
{
    /// 큐가 비어 있지 않은가. 창이 존재할 조건이고, 큐를 든 것이 이 TU라
    /// 답도 여기서 한다.
    bool model_loading_has_visible()
    {
        return Editor::ModelPlacement::Get().HasVisible();
    }
}
