#include "../../../Tools/regression/gcce_probe_cleanup.h"
#include "Tasks/WorkerPoolSelfTest.h"
#include "DataSystem.h"
#include "AssetDepot/LegacyResourceCache.h"
#include <map>
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
            // Source-only regression coverage for bounded cache ownership. A
            // logical eviction releases retention, never another consumer's pin.
            std::map<std::string, asset_cache_detail::Entry<int>> cache;
            auto first = asset_cache_detail::Publish(cache, std::string("first"),
                own::make_shared<const int>(1), sizeof(int), sizeof(int), 1u);
            const own::weak_owner<const int> firstLifetime(first);
            auto second = asset_cache_detail::Publish(cache, std::string("second"),
                own::make_shared<const int>(2), sizeof(int), sizeof(int), 1u);
            if (cache.at("first").retained || firstLifetime.expired())
            {
                throw std::runtime_error("Cache budget eviction lost a live consumer or retained too much");
            }
            auto oldGeneration = asset_cache_detail::Acquire(cache.at("first"));
            cache.erase("first");
            auto replacement = asset_cache_detail::Publish(cache, std::string("first"),
                own::make_shared<const int>(3), sizeof(int), sizeof(int), 1u);
            if (!oldGeneration || *oldGeneration != 1 || *replacement != 3)
            {
                throw std::runtime_error("Cache replacement changed an already pinned generation");
            }
            first.reset();
            oldGeneration.reset();
            if (!firstLifetime.expired())
            {
                throw std::runtime_error("Old generation survived its final consumer without a cache root");
            }
            asset_cache_detail::Trim(cache, 0u, 0u);
            if (cache.at("first").retained || !replacement || *replacement != 3)
            {
                throw std::runtime_error("Zero cache budget destroyed a consumer pin");
            }

            replacement.reset();
            second.reset();
            asset_cache_detail::Trim(cache, 0u, 0u);
            if (!cache.empty())
            {
                throw std::runtime_error("Expired weak cache entries survived pruning");
            }
            auto revisionOne = asset_cache_detail::Publish(cache, std::string("revision"),
                own::make_shared<const int>(10), sizeof(int), sizeof(int), 1u, 1u);
            if (asset_cache_detail::Acquire(cache.at("revision"), 2u))
            {
                throw std::runtime_error("A current lookup returned a prior resolver revision");
            }
            auto revisionTwo = asset_cache_detail::Publish(cache, std::string("revision"),
                own::make_shared<const int>(20), sizeof(int), sizeof(int), 1u, 2u);
            const auto currentRevision = asset_cache_detail::Acquire(cache.at("revision"), 2u);
            if (*revisionOne != 10 || !currentRevision || *currentRevision != 20 || *revisionTwo != 20)
            {
                throw std::runtime_error("Resolver replacement changed an old pin or reused stale cache state");
            }

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
            const bool bundleBarrier = loaded.submitted == 32 && loaded.completed == 32
                && loaded.status == AssetDepot::AssetRequestStatus::Ready
                && loaded.models.size() == 32;
            auto asyncBundle = DataSystems->LoadAssetBundleAsync(bundle);
            const own::weak_owner<DataSystem::AssetBundlePreparation> asyncLifetime(asyncBundle);
            asyncBundle->Completion().wait();
            const auto asyncResult = asyncBundle->Snapshot();
            if (asyncResult.status != AssetDepot::AssetRequestStatus::Ready
                || asyncResult.completed != 32 || asyncResult.models.size() != 32)
            {
                throw std::runtime_error("Async bundle failed to hand off actual generation owners");
            }
            asyncBundle.reset();
            if (!asyncLifetime.expired())
            {
                throw std::runtime_error("Completed bundle request leaked through a strong registry");
            }
            producer.join();
            external.wait();
            if (!bundleBarrier) throw std::runtime_error("Bundle returned before all 32 loader callbacks completed");
            if (reads != 64 || inlineReads != 0) throw std::runtime_error("External producer completion/thread mismatch");
            const auto generation = DataSystems->LoadModelAssetGenerationByPath(modelPath);
            if (!generation || !generation->Skeleton() || generation->Animations().size() < 2)
                throw std::runtime_error("Real model generation missing after bundle load");
            gc::domain foliageDomain;
            auto foliageRoot = Component::CreateManaged<FoliageComponent>(foliageDomain);
            FoliageComponent& foliage = *foliageRoot;
            gcce_probe::cleanup foliageCleanup(foliage);
            FoliageType type;
            type.m_modelName = generation->Name();
            type.m_modelGuid = FileGuid(generation->Identity().modelId);
            type.m_allowLegacySource = true;
            foliage.AddFoliageType(type);
            // Only this explicit regression harness may wait. Product binding
            // polls on the foliage tick without blocking or decoding on the UI.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (foliage.GetAssetBindingStatus(0u) == AssetDepot::AssetRequestStatus::Pending
                && std::chrono::steady_clock::now() < deadline)
            {
                foliage.PollAssetBindings();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (foliage.GetAssetBindingStatus(0u) != AssetDepot::AssetRequestStatus::Ready
                || !foliage.GetFoliageTypes().front().m_modelGeneration)
            {
                throw std::runtime_error("Asynchronous legacy foliage generation/material did not become ready");
            }
            // Bounds-only culling updates all instances without queued raw captures.
            for (int i = 0; i < 73; ++i)
            {
                FoliageInstance instance;
                instance.m_position.x = static_cast<float>(i);
                instance.m_isCulled = true;
                foliage.AddFoliageInstance(instance);
            }
            foliage.UpdateFoliageCullingData(std::nullopt);
            for (const auto& instance : foliage.GetFoliageInstances())
                if (instance.m_isCulled) throw std::runtime_error("Foliage culling missed an entry");
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
