#include "EditorModelPlacement.h"

#include "DataSystem.h"
#include "ModelSceneInstantiation.h"
#include "ReflectionUndo.h"
#include "Scene.h"
#include "SceneManager.h"
#include "imgui.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace Editor
{
    struct ModelPlacement::Request
    {
        std::uint32_t sceneId{};
        std::string path;
        std::optional<math::vector3> position;
        bool gameMode{};
        std::atomic<bool> cancelled{ false };
        std::atomic<bool> ready{ false };
        std::atomic<bool> finished{ false };
        std::atomic<std::size_t> completedSteps{ 0 };
        std::atomic<std::size_t> totalSteps{ 0 };
        // Worker writes these before ready.store(release); only GT reads afterwards.
        std::unique_ptr<ModelSceneInstantiation::PendingInstance> prepared;
        std::string error;
        // GT only, including undo cleanup. UI only posts cancellation.
        EntityHandle root;
        bool positionApplied{ false };
        std::size_t applyFrames{};
        std::size_t loadingFrames{};
        double prepareMs{};
        long long maxApplyUs{};
    };

    struct ModelPlacement::Impl
    {
        std::mutex mutex;
        std::condition_variable wake;
        std::thread worker;
        bool stopping{ true };
        std::deque<std::shared_ptr<Request>> work;
        std::vector<std::shared_ptr<Request>> incoming;
        std::vector<std::shared_ptr<Request>> cancellations;
        std::vector<std::shared_ptr<Request>> visible;
        std::vector<std::shared_ptr<Request>> pending; // GT only
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
        std::shared_ptr<Request> m_request;
    };

    ModelPlacement::ModelPlacement() : m_impl(std::make_unique<Impl>()) {}
    ModelPlacement::~ModelPlacement() { Shutdown(); }
    ModelPlacement& ModelPlacement::Get() { static ModelPlacement instance; return instance; }

    void ModelPlacement::Initialize()
    {
        auto& state = *m_impl;
        std::lock_guard lock(state.mutex);
        if (!state.stopping) return;
        state.stopping = false;
        state.worker = std::thread([this]
        {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            SetThreadDescription(GetCurrentThread(), L"Model asset preparation");
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            auto& state = *m_impl;
            for (;;)
            {
                std::shared_ptr<Request> request;
                {
                    std::unique_lock lock(state.mutex);
                    state.wake.wait(lock, [&] { return state.stopping || !state.work.empty(); });
                    if (state.stopping) break;
                    request = std::move(state.work.front());
                    state.work.pop_front();
                }
                if (request->cancelled.load(std::memory_order_acquire)) continue;
                const auto prepareStart = std::chrono::steady_clock::now();
                try
                {
                    if (FAILED(com)) request->error = "Model image decoder initialization failed";
                    else
                    {
                        auto generation = DataSystems->LoadModelAssetGenerationByPath(request->path);
                        if (!generation) request->error = "Model asset load failed: " + request->path;
                        else if (!request->cancelled.load(std::memory_order_acquire))
                        {
                            ModelSceneInstantiation::Options options;
                            options.createMeshCollider = DataSystems->ReadModelCreateMeshCollider(
                                FileGuid(generation->Identity().modelId));
                            request->prepared = ModelSceneInstantiation::PendingInstance::Prepare(
                                std::move(generation), options);
                            if (!request->prepared) request->error = "Model preparation failed: " + request->path;
                        }
                    }
                }
                catch (const std::exception& error) { request->error = error.what(); }
                catch (...) { request->error = "Unexpected model preparation failure"; }
                request->prepareMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - prepareStart).count();
                request->ready.store(true, std::memory_order_release);
            }
            if (SUCCEEDED(com)) CoUninitialize();
        });
    }

    void ModelPlacement::Execute(std::uint32_t sceneId, std::string path,
        std::optional<math::vector3> position)
    {
        Meta::UndoManager::GetInstance()->Execute(
            std::make_unique<Command>(sceneId, std::move(path), position));
    }

    std::shared_ptr<ModelPlacement::Request> ModelPlacement::Enqueue(
        std::uint32_t sceneId, const std::string& path,
        const std::optional<math::vector3>& position, bool gameMode)
    {
        auto request = std::make_shared<Request>();
        request->sceneId = sceneId;
        request->path = path;
        request->position = position;
        request->gameMode = gameMode;
        {
            std::lock_guard lock(m_impl->mutex);
            if (m_impl->stopping) return {};
            m_impl->incoming.push_back(request);
            m_impl->visible.push_back(request);
            m_impl->work.push_back(request);
        }
        m_impl->wake.notify_one();
        return request;
    }

    void ModelPlacement::Cancel(const std::shared_ptr<Request>& request)
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
        std::vector<std::shared_ptr<Request>> cancellations;
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
            if (!request.finished.exchange(true, std::memory_order_acq_rel)) ++state.cancelled;
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
                ++request->loadingFrames;
                continue;
            }
            if (!request->prepared)
            {
                Debug->LogError(request->error);
                ++state.failed;
                request->finished.store(true, std::memory_order_release);
                continue;
            }
            // 전체 요청에 2ms를 배정하고 한 프레임에는 한 인스턴스만 진행한다.
            if (advanced || std::chrono::steady_clock::now() >= deadline) continue;
            advanced = true;
            request->totalSteps.store(request->prepared->TotalSteps(), std::memory_order_relaxed);
            ModelSceneInstantiation::PendingInstance::Status result;
            const auto applyStart = std::chrono::steady_clock::now();
            try
            {
                result = request->prepared->Advance(*active, 16,
                    std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now()));
            }
            catch (const std::exception& error)
            {
                Debug->LogError(error.what());
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
    }

    void ModelPlacement::DrawStatus()
    {
        std::vector<std::shared_ptr<Request>> visible;
        { std::lock_guard lock(m_impl->mutex); visible = m_impl->visible; }
        if (visible.empty()) return;
        if (ImGui::Begin("Model loading", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
        {
            for (const auto& request : visible)
            {
                ImGui::PushID(request.get());
                ImGui::TextUnformatted(file::path(request->path).filename().string().c_str());
                const auto total = request->totalSteps.load(std::memory_order_relaxed);
                if (total) ImGui::ProgressBar(static_cast<float>(request->completedSteps.load(std::memory_order_relaxed)) / total);
                else ImGui::TextUnformatted("Preparing model...");
                if (ImGui::Button("Cancel")) Cancel(request);
                ImGui::PopID();
            }
        }
        ImGui::End();
    }

    void ModelPlacement::Shutdown()
    {
        auto& state = *m_impl;
        {
            std::lock_guard lock(state.mutex);
            if (state.stopping) return;
            state.stopping = true;
            for (const auto& request : state.visible) request->cancelled.store(true, std::memory_order_release);
            state.work.clear();
        }
        state.wake.notify_all();
        if (state.worker.joinable()) state.worker.join();
        Tick(); // presentation has stopped; scenes and DataSystem are still alive.
        std::lock_guard lock(state.mutex);
        state.pending.clear();
        state.visible.clear();
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
