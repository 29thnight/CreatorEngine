#include "Tasks/WorkerPoolSelfTest.h"
#include "DataSystem.h"
#include "JobScheduler.h"
#include "FoliageComponent.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace RenderTest
{
    bool RunWorkerPoolSelfTest(const std::string& modelPath, std::string& log)
    {
        if (!ce::get_job_scheduler().is_running()) { log = "Job scheduler is not running"; return false; }
        try
        {
            // Use the real bundle path alongside an external producer (the same
            // submission shape as Presentation's thumbnail reader).
            std::atomic<unsigned> reads{0}, inlineReads{0};
            job_handle external;
            struct Drain
            {
                job_handle& handle;
                ~Drain() { try { handle.wait(); } catch (...) {} }
            } drain{external};
            std::jthread producer([&]
            {
                const auto submitter = std::this_thread::get_id();
                job_group jobs;
                for (int i = 0; i < 64; ++i)
                    jobs.add([&, submitter, modelPath]
                    {
                        if (submitter == std::this_thread::get_id()) ++inlineReads;
                        std::ifstream input(modelPath, std::ios::binary);
                        char magic[4]{};
                        input.read(magic, 4);
                        if (input && std::string_view(magic, 4) == "glTF") ++reads;
                    });
                external = ce::get_job_scheduler().submit(std::move(jobs));
            });
            AssetBundle bundle;
            for (int i = 0; i < 32; ++i)
                bundle.AddAsset(AssetEntry(ManagedAssetType::Model, file::path(modelPath)));
            const auto loaded = DataSystems->LoadAssetBundle(bundle);
            const bool bundleBarrier = loaded.submitted == 32 && loaded.completed == 32;
            producer.join();
            external.wait();
            if (!bundleBarrier) throw std::runtime_error("Bundle returned before all 32 loader callbacks completed");
            if (reads != 64 || inlineReads != 0) throw std::runtime_error("External producer completion/thread mismatch");
            const auto generation = DataSystems->LoadModelAssetGenerationByPath(modelPath);
            if (!generation || !generation->Skeleton() || generation->Animations().size() < 2)
                throw std::runtime_error("Real model generation missing after bundle load");
            FoliageComponent foliage;
            FoliageType type;
            type.m_modelName = generation->Name();
            foliage.AddFoliageType(type);
            if (!foliage.GetFoliageTypes().front().m_modelGeneration)
                throw std::runtime_error("Foliage model generation not bound");
            // Start culled: every real range callback must update its own entry.
            for (int i = 0; i < 73; ++i)
            {
                FoliageInstance instance;
                instance.m_position.x = static_cast<float>(i);
                instance.m_isCulled = true;
                foliage.AddFoliageInstance(instance);
            }
            foliage.UpdateFoliageCullingData(std::nullopt);
            for (const auto& instance : foliage.GetFoliageInstances())
                if (instance.m_isCulled) throw std::runtime_error("Foliage range barrier missed an entry");
            log = "WORKER_PRODUCT_OK bundleSubmitted=32 bundleCompleted=32 externalReads=64 inlineReads=0 model="
                + generation->Name() + " foliageCompleted=73";
            return true;
        }
        catch (const std::exception& e)
        {
            log = std::string("WORKER_PRODUCT_FAILED ") + e.what();
            return false;
        }
    }
}
