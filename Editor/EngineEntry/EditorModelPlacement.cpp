#include "EditorModelPlacement.h"
#include "EditorModelCollisionGeometry.h"

#include "DataSystem.h"
#include "TypeTrait.h"
#include "ModelSceneInstantiation.h"
#include "JobScheduler.h"
#include "ReflectionUndo.h"
#include "Scene.h"
#include "SceneManager.h"
#include "imgui.h"
#include "EditorWindowNames.h"
#include "Windows/EditorToolboxWindows.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

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
        bool positionApplied{ false };
        std::size_t applyFrames{};
        std::size_t loadingFrames{};
        double prepareMs{};
        long long maxApplyUs{};
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

    void ModelPlacement::Prepare(const own::shared_owner<Request>& request)
    {
        if (request->cancelled.load(std::memory_order_acquire))
        {
            request->ready.store(true, std::memory_order_release);
            return;
        }
        // The shared pool does not promise a COM apartment; balance this job's
        // initialization without changing a shared worker's name or priority.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
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
                    ModelSceneInstantiation::Options options;
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
        if (SUCCEEDED(com))
        {
            CoUninitialize();
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
        auto request = own::make_shared<Request>();
        request->sceneId = sceneId;
        request->path = path;
        request->position = position;
        request->gameMode = gameMode;
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
            try
            {
                request->modelPreparation = DataSystems->PrepareModelAssetByPath(request->path);
                if (!request->modelPreparation)
                {
                    request->error = "Cannot queue model asset preparation: " + request->path;
                    request->ready.store(true, std::memory_order_release);
                }
                else
                {
                    job_group work;
                    work.add([request] { Prepare(request); });
                    std::array<job_handle, 2> dependencies;
                    std::size_t dependencyCount = 0;
                    const auto modelWork = DataSystems->ModelPreparationCompletion(request->modelPreparation);
                    if (modelWork.valid() && !modelWork.is_complete())
                    {
                        dependencies[dependencyCount++] = modelWork;
                    }
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
                request->error = error.what();
                request->ready.store(true, std::memory_order_release);
            }
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

    void ModelPlacement::Tick()
    {
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
            if (request.ready.load(std::memory_order_acquire))
            {
                request.prepared.reset();
                request.modelPreparation.reset();
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
            Scene* active = SceneManagers->GetActiveScene();
            if (!active || active->GetSceneId() != request->sceneId
                || SceneManagers->IsGameStart() != request->gameMode || SceneManagers->IsSceneLoading())
                request->cancelled.store(true, std::memory_order_release);
            if (request->cancelled.load(std::memory_order_acquire)) { cleanup(*request); continue; }
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
                    (void)DataSystems->ReadPreparedModel(request->modelPreparation, assetError);
                    request->error = assetError.empty()
                        ? "Model preparation job completed without a result" : std::move(assetError);
                    request->ready.store(true, std::memory_order_release);
                }
            }
            if (!request->prepared)
            {
                Debug::PrintLog(spdlog::level::err, request->error);
                ++state.failed;
                request->modelPreparation.reset();
                request->finished.store(true, std::memory_order_release);
                continue;
            }
            // 전체 요청에 2ms를 배정하고 한 프레임에는 한 인스턴스만 진행한다.
            if (advanced || std::chrono::steady_clock::now() >= deadline) continue;
            advanced = true;
            if (!request->modelPublished)
            {
                std::string error;
                if (!DataSystems->PublishPreparedModel(request->modelPreparation, error))
                {
                    Debug::PrintLog(spdlog::level::err, error);
                    ++state.failed;
                    request->prepared.reset();
                    request->modelPreparation.reset();
                    request->finished.store(true, std::memory_order_release);
                    continue;
                }
                request->modelPublished = true;
                request->modelPreparation.reset();
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
                ++state.completed;
                std::printf("[model.async] ready path=%s steps=%zu frames=%zu loadingFrames=%zu prepareMs=%.3f maxApplyUs=%lld\n",
                    request->path.c_str(), request->prepared->TotalSteps(), request->applyFrames,
                    request->loadingFrames, request->prepareMs, request->maxApplyUs);
                request->prepared.reset();
                request->finished.store(true, std::memory_order_release);
            }
        }
        std::erase_if(state.pending, [](const auto& request) { return request->finished.load(std::memory_order_acquire); });
        std::lock_guard lock(state.mutex);
        std::erase_if(state.visible, [](const auto& request) { return request->finished.load(std::memory_order_acquire); });
        std::erase_if(state.preparations, [](const auto& request)
        {
            if (!request->preparation.is_complete())
            {
                return false;
            }
            if (request->cancelled.load(std::memory_order_acquire))
            {
                // Cancellation may have removed the visible request before its
                // worker published ready. Do not retain its recipe in undo history.
                request->prepared.reset();
                request->modelPreparation.reset();
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
            if (total) ImGui::ProgressBar(static_cast<float>(request->completedSteps.load(std::memory_order_relaxed)) / total);
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
