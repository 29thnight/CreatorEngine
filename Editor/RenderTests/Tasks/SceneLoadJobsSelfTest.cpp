#include "Tasks/SceneLoadJobsSelfTest.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Entity.h"
#include "Transform.h"
#include "DataSystem.h"
#include "JobScheduler.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace
{
    void Check(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    Entity* Find(Scene* scene, std::string_view name)
    {
        if (scene)
            for (const auto& entity : scene->m_Entities)
                if (entity && entity->m_name.ToString() == name) return entity.get();
        return nullptr;
    }

    // Occupy every persistent worker to make abandonment/cancellation deterministic.
    // RAII always releases them before an assertion can unwind into engine shutdown.
    class WorkerGate
    {
    public:
        WorkerGate()
        {
            const auto count = ce::get_thread_pool().size();
            try
            {
                // One handoff, then N stealable items. Separate external submissions
                // can be pinned behind a blocked worker and cannot form this gate.
                job_group group;
                for (size_t i = 0; i < count; ++i)
                    group.add([this]
                    {
                        std::unique_lock lock(m_mutex);
                        ++m_entered;
                        m_ready.notify_all();
                        m_ready.wait(lock, [this] { return m_release; });
                    });
                m_jobs.push_back(ce::get_job_scheduler().submit(std::move(group)));
                std::unique_lock lock(m_mutex);
                if (!m_ready.wait_for(lock, std::chrono::seconds(10), [&] { return m_entered == count; }))
                    throw std::runtime_error("Worker gate did not occupy every engine worker");
            }
            catch (...) { Release(); throw; }
        }
        ~WorkerGate() { Release(); }
        void Release()
        {
            { std::lock_guard lock(m_mutex); m_release = true; }
            m_ready.notify_all();
            for (const auto& job : m_jobs) job.wait();
            m_jobs.clear();
        }
    private:
        std::mutex m_mutex;
        std::condition_variable m_ready;
        std::vector<job_handle> m_jobs;
        size_t m_entered = 0;
        bool m_release = false;
    };
}

namespace RenderTest
{
    bool RunSceneLoadJobsSelfTest(const std::string& directory, const std::string& modelPath, std::string& log)
    {
        try
        {
            const auto first = (file::path(directory) / "SceneJobsA.creator").string();
            const auto second = (file::path(directory) / "SceneJobsB.creator").string();
            Check(!file::exists(first) && !file::exists(second), "Fixture paths already exist");
            Scene* original = SceneManagers->CreateScene("SceneJobsFixture");
            Check(original != nullptr, "Fixture scene creation failed");
            auto* child = original->CreateEntity("SceneJobsChild");
            Check(child && child->GetComponent<Transform>(), "Fixture transform missing");
            original->m_requiredLoadAssetsBundle.AddAsset(AssetEntry(ManagedAssetType::Model, file::path(modelPath)));
            Check(SceneManagers->SaveScene(first) != nullptr, "First fixture save failed");
            auto* serializedDdol = original->CreateEntity("SceneJobsSerializedDdol");
            Object::SetDontDestroyOnLoad(serializedDdol);
            Check(SceneManagers->SaveScene(second) != nullptr, "DDOL fixture save failed");
            SceneManagers->RemoveDontDestroyOnLoad(serializedDdol);
            serializedDdol->Destroy();
            original->EndFramePass();
            auto* persistent = original->CreateEntity("SceneJobsPersistent");
            Object::SetDontDestroyOnLoad(persistent);
            const auto persistentId = persistent->GetInstanceID();
            const auto before = SceneManagers->GetScenes().size();
            const auto hasModel = [&]
            {
                const auto generations = DataSystems->SnapshotCurrentModelAssetGenerations();
                return std::ranges::any_of(generations, [&](const auto& generation)
                {
                    return generation && generation->SourcePath().filename() == file::path(modelPath).filename();
                });
            };
            Check(!hasModel(), "Fixture model is already cached; run in a fresh isolated process");

            std::future<Scene*> loaded;
            std::future<Scene*> missing;
            {
                WorkerGate gate;
                // Destroy the caller's path and a returned future while all workers
                // are blocked: neither may own execution or force a blocking join.
                std::string temporary = first;
                loaded = SceneManagers->LoadSceneAsync(temporary);
                temporary.assign(temporary.size(), 'x');
                { auto abandoned = SceneManagers->LoadSceneAsync(first); }
                SceneManagers->LoadSceneAsyncAndWaitCallback(first);
                SceneManagers->LoadSceneAsyncAndWaitCallback(second);
                missing = SceneManagers->LoadSceneAsync(first + ".missing");
                Check(SceneManagers->IsSceneLoading(), "Pending preparation not reported");
                Check(loaded.wait_for(std::chrono::seconds(0)) == std::future_status::timeout,
                    "Result completed before owner-thread construction");
                Check(SceneManagers->GetScenes().size() == before, "Worker changed scene ownership");
            }
            SceneManagers->WaitForSceneLoad();
            Scene* prepared = loaded.get();
            Check(prepared && Find(prepared, "SceneJobsChild"), "Prepared entity missing");
            Check(Find(prepared, "SceneJobsChild")->GetComponent<Transform>() != nullptr, "Component load missing");
            Check(missing.get() == nullptr, "Missing document did not resolve to nullptr");
            Check(SceneManagers->GetActiveScene() == original, "Wait activated outside frame boundary");
            Check(SceneManagers->GetScenes().size() == before + 3,
                "Abandoned result ownership or replaced callback count mismatch");
            Check(prepared->m_requiredLoadAssetsBundle.assets.size() == 1, "Asset bundle not preserved");
            Check(hasModel(), "Model asset preparation did not publish a generation before scene completion");

            // The latest callback activates at the same boundary the hosts use.
            SceneManagers->ApplyPendingSceneStructureChange();
            Scene* active = SceneManagers->GetActiveScene();
            Check(active && active->m_sceneName.ToString() == "SceneJobsB", "Latest callback did not win");
            Check(Find(active, "SceneJobsPersistent") == persistent && persistent->GetInstanceID() == persistentId,
                "Existing DDOL ownership/identity changed");
            auto* restoredDdol = Find(active, "SceneJobsSerializedDdol");
            Check(restoredDdol && restoredDdol->IsDontDestroyOnLoad(), "Serialized DDOL was not transferred");
            Check(std::ranges::count_if(active->m_Entities, [&](const auto& entity)
            {
                return entity && entity->GetInstanceID() == restoredDdol->GetInstanceID();
            }) == 1, "Serialized DDOL identity was instantiated twice");
            Check(!SceneManagers->IsSceneLoading(), "Loading flag remained set after activation");

            // Cancel accepted work without publishing a scene, then verify admission
            // remains open for a normal subsequent request.
            auto cancelled = SceneManagers->LoadSceneAsync(first);
            SceneManagers->DrainSceneLoads();
            Check(cancelled.get() == nullptr && !SceneManagers->IsSceneLoading(), "Drain published cancelled load");
            auto superseded = SceneManagers->LoadSceneAsync(first);
            Check(SceneManagers->LoadScene(first) != nullptr, "Synchronous scene load failed");
            Check(superseded.get() == nullptr, "Synchronous load did not cancel queued async work");

            bool workerRejected = false;
            ce::get_job_scheduler().submit([&]
            {
                try { (void)SceneManagers->LoadSceneAsync(first); }
                catch (const std::logic_error&) { workerRejected = true; }
            }).wait();
            Check(workerRejected, "Scene mutation admitted from a worker");

            // Finish is polled by the commandlet on later frames. This request must
            // progress through the real host pump, without an explicit wait here.
            SceneManagers->LoadSceneAsyncAndWaitCallback(first);
            log = "SCENE_LOAD_JOBS_PREPARED ownedResults=3 coldAsset=1 latestCallback=1 missing=1 cancellation=1 ddol=2 workerRejected=1";
            return true;
        }
        catch (const std::exception& e)
        {
            log = std::string("SCENE_LOAD_JOBS_FAILED ") + e.what();
            return false;
        }
    }

    bool FinishSceneLoadJobsSelfTest(const std::string& directory, std::string& log)
    {
        try
        {
            Scene* active = SceneManagers->GetActiveScene();
            Check(active && active->m_sceneName.ToString() == "SceneJobsA", "Normal frame pump did not activate");
            Check(Find(active, "SceneJobsPersistent") && Find(active, "SceneJobsSerializedDdol"), "DDOL lost on second transition");
            const auto path = (file::path(directory) / "SceneJobsA.creator").string();
            auto accepted = SceneManagers->LoadSceneAsync(path);
            SceneManagers->SetDecommissioning();
            auto rejected = SceneManagers->LoadSceneAsync(path);
            Check(rejected.wait_for(std::chrono::seconds(0)) == std::future_status::ready && rejected.get() == nullptr,
                "Teardown accepted a new load");
            SceneManagers->DrainSceneLoads();
            Check(accepted.get() == nullptr && !SceneManagers->IsSceneLoading(), "Teardown did not settle accepted request");
            log = "SCENE_LOAD_JOBS_OK ownedResults=3 coldAsset=1 latestCallback=1 missing=1 cancellation=1 ddol=2 workerRejected=1 framePump=1 shutdown=1";
            return true;
        }
        catch (const std::exception& e)
        {
            log = std::string("SCENE_LOAD_JOBS_FAILED ") + e.what();
            return false;
        }
    }
}
