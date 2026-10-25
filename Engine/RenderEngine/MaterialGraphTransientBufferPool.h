#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "RHI/IRenderDeviceServices.h"
#include "../Utility_Framework/Foundation/StandardLibrary.h"
#include "../EngineDiagnostics/ProfileScope.h"

namespace material_graph
{
    // Reuses per-frame scene buffers instead of creating a committed resource every
    // frame. Owners return a buffer from their destructor, which runs only after the
    // GPU completed the recording or when it was never submitted, so a returned
    // buffer has no pending GPU use. Every pooled use rewrites the buffer through its
    // own initialize/copy pass; callers must not rely on creation-time contents.
    struct TransientBufferPool
    {
        explicit TransientBufferPool(IRenderDeviceServices& owner) : device(owner) {}
        ~TransientBufferPool() { Close(); }
        TransientBufferPool(const TransientBufferPool&) = delete;
        TransientBufferPool& operator=(const TransientBufferPool&) = delete;

        struct Key
        {
            std::uint64_t bytes{};
            bool unordered{}, indirect{};
            RHIResourceState initialState{};
        };
        struct Statistics
        {
            std::uint64_t requests{}, hits{}, creates{}, failures{}, returns{}, releases{};
            std::uint64_t capacityMisses{}, unavailableMisses{}, budgetReleases{}, unknownReturns{};
            std::uint64_t cachedBytes{}, cachedCount{}, ownedCount{}, peakOwnedCount{};
        };
        Statistics Snapshot()
        {
            std::lock_guard lock(mutex);
            auto result = statistics;
            result.cachedBytes = cachedBytes;
            result.cachedCount = free.size();
            result.ownedCount = owned.size();
            return result;
        }

        void PublishStatistics(std::uint32_t engineFrame)
        {
            if (!ce::profiler().counter_enabled(ce::counter_category::render))
            {
                return;
            }
            static const std::array names{"Scene pool requests",        "Scene pool hits",
                                          "Scene pool creates",         "Scene pool failures",
                                          "Scene pool returns",         "Scene pool releases",
                                          "Scene pool capacity misses", "Scene pool unavailable misses",
                                          "Scene pool budget releases", "Scene pool unknown returns",
                                          "Scene pool cached bytes",    "Scene pool cached count",
                                          "Scene pool owned count",     "Scene pool peak owned count"};
            static const auto ids = []
            {
                std::array<ce::profile_counter_id, names.size()> result{};
                for (std::size_t index = 0; index < names.size(); ++index)
                {
                    result[index] =
                        ce::register_counter(names[index], index == 10 ? "B" : "count", ce::counter_category::render);
                }
                return result;
            }();
            const auto value = Snapshot();
            const std::array values{
                value.requests,       value.hits,           value.creates,        value.failures,
                value.returns,        value.releases,       value.capacityMisses, value.unavailableMisses,
                value.budgetReleases, value.unknownReturns, value.cachedBytes,    value.cachedCount,
                value.ownedCount,     value.peakOwnedCount};
            std::array<ce::profile_counter_sample, names.size()> samples{};
            for (std::size_t index = 0; index < samples.size(); ++index)
            {
                samples[index] = {ids[index], static_cast<double>(values[index])};
#if !CE_SHIPPING
                samples[index].cpu = ce::current_cpu_context;
#endif
            }
            // One batch per prepared view, never a global counter publication per acquire.
            ce::profiler().publish_counters(engineFrame, ce::counter_category::render, samples);
        }

        bool Acquire(const RHIBufferDesc& desc, RHIBufferHandle& handle, std::string& error)
        {
            const std::array descriptions{desc};
            std::array<RHIBufferHandle, 1> handles{};
            const bool success = AcquireBatch(descriptions, handles, error);
            handle = handles[0];
            return success;
        }

        template<std::size_t Count>
        bool AcquireBatch(const std::array<RHIBufferDesc, Count>& descriptions,
                          std::array<RHIBufferHandle, Count>& handles, std::string& error)
        {
            static_assert(Count > 0);
            ce::profile_scope acquire{ce::marker<"SceneBufferPoolAcquire">()};
            handles.fill({});
            std::array<Key, Count> keys{};
            std::bitset<Count> missing, capacityMiss;
            {
                ce::profile_scope lookup{ce::marker<"SceneBufferPoolLookup">()};
                std::unique_lock lock(mutex, std::defer_lock);
                {
                    ce::profile_scope wait{ce::marker<"SceneBufferPoolAcquireLock">()};
                    lock.lock();
                }
                statistics.requests += Count;
                if (closed)
                {
                    ++statistics.failures;
                    error = "Scene buffer pool is closed.";
                    return false;
                }
                // Validate the complete request before removing any cached owner.
                for (std::size_t index = 0; index < Count; ++index)
                {
                    const auto& desc = descriptions[index];
                    if (!desc.bytes)
                    {
                        ++statistics.failures;
                        error = "Scene buffer size is zero.";
                        return false;
                    }
                    keys[index] = {desc.bytes, desc.allowUnorderedAccess, desc.allowIndirectArguments,
                                   desc.initialState};
                }
                for (std::size_t request = 0; request < Count; ++request)
                {
                    const auto& key = keys[request];
                    auto best = free.size();
                    bool compatible = false;
                    for (std::size_t index = 0; index < free.size(); ++index)
                    {
                        const auto& candidate = free[index].key;
                        if (candidate.unordered != key.unordered || candidate.indirect != key.indirect ||
                            candidate.initialState != key.initialState)
                        {
                            continue;
                        }
                        compatible = true;
                        if (candidate.bytes >= key.bytes &&
                            (best == free.size() || candidate.bytes < free[best].key.bytes))
                        {
                            best = index;
                        }
                    }
                    if (best != free.size())
                    {
                        handles[request] = free[best].handle;
                        cachedBytes -= free[best].key.bytes;
                        free[best] = std::move(free.back());
                        free.pop_back();
                        ++statistics.hits;
                    }
                    else
                    {
                        missing.set(request);
                        if (compatible)
                        {
                            capacityMiss.set(request);
                            ++statistics.capacityMisses;
                        }
                        else
                        {
                            ++statistics.unavailableMisses;
                        }
                    }
                }
            }
            // Publishing trace events is outside the pool's critical section.
            for (std::size_t index = 0; index < Count; ++index)
            {
                if (!missing.test(index))
                {
                    ce::profile_instant(ce::marker<"SceneBufferPoolReuse">());
                }
                else if (capacityMiss.test(index))
                {
                    ce::profile_instant(ce::marker<"SceneBufferPoolMissCapacity">());
                }
                else
                {
                    ce::profile_instant(ce::marker<"SceneBufferPoolMissUnavailable">());
                }
            }
            try
            {
                for (std::size_t index = 0; index < Count; ++index)
                {
                    if (!missing.test(index))
                    {
                        continue;
                    }
                    bool success;
                    {
                        ce::profile_scope create{ce::marker<"SceneBufferPoolCreateResource">()};
                        success = device.CreateBuffer(descriptions[index], handles[index], error);
                    }
                    if (!success)
                    {
                        {
                            std::lock_guard lock(mutex);
                            ++statistics.failures;
                        }
                        // Failed batches return every already selected/created owner.
                        for (const auto handle : handles)
                        {
                            Return(handle);
                        }
                        handles.fill({});
                        return false;
                    }
                    std::lock_guard lock(mutex);
                    owned.emplace(handles[index].id, keys[index]);
                    ++statistics.creates;
                    statistics.peakOwnedCount = std::max<std::uint64_t>(statistics.peakOwnedCount, owned.size());
                }
            }
            catch (...)
            {
                for (const auto handle : handles)
                {
                    Return(handle);
                }
                handles.fill({});
                throw;
            }
            return true;
        }

        void Return(RHIBufferHandle handle)
        {
            ce::profile_scope returned{ce::marker<"SceneBufferPoolReturn">()};
            if (!handle.IsValid())
            {
                return;
            }
            {
                std::unique_lock lock(mutex, std::defer_lock);
                {
                    ce::profile_scope wait{ce::marker<"SceneBufferPoolReturnLock">()};
                    lock.lock();
                }
                ++statistics.returns;
                const auto found = owned.find(handle.id);
                if (found == owned.end())
                {
                    ++statistics.unknownReturns;
                    ce::profile_instant(ce::marker<"SceneBufferPoolReturnUnknown">());
                }
                else if (!closed && found->second.bytes <= kMaxBytes - cachedBytes)
                {
                    // Bound memory, not the number of small per-pass packet buffers.
                    cachedBytes += found->second.bytes;
                    free.push_back({found->second, handle});
                    return;
                }
                else
                {
                    if (!closed)
                    {
                        ++statistics.budgetReleases;
                        ce::profile_instant(ce::marker<"SceneBufferPoolReleaseBudget">());
                    }
                    else
                    {
                        ce::profile_instant(ce::marker<"SceneBufferPoolReleaseClosed">());
                    }
                    owned.erase(found);
                }
                ++statistics.releases;
            }
            // Driver calls must not serialize unrelated pool users under the mutex.
            ce::profile_scope release{ce::marker<"SceneBufferPoolReleaseResource">()};
            device.ReleaseBuffer(handle);
        }

        void Close()
        {
            ce::profile_scope close{ce::marker<"SceneBufferPoolClose">()};
            std::vector<Entry> retired;
            {
                std::lock_guard lock(mutex);
                closed = true;
                retired.swap(free);
                for (const auto& entry : retired)
                {
                    owned.erase(entry.handle.id);
                }
                statistics.releases += retired.size();
                cachedBytes = 0;
            }
            for (const auto& entry : retired)
            {
                device.ReleaseBuffer(entry.handle);
            }
        }

      private:
        static constexpr std::uint64_t kMaxBytes = 64ull << 20;
        struct Entry
        {
            Key key;
            RHIBufferHandle handle;
        };
        IRenderDeviceServices& device;
        std::mutex mutex;
        std::vector<Entry> free;
        std::unordered_map<std::uint32_t, Key> owned;
        std::uint64_t cachedBytes{};
        bool closed{};
        Statistics statistics;
    };

    // Owners call these so a missing pool keeps the original create/release path.
    inline bool CreateSceneBuffer(IRenderDeviceServices& device, TransientBufferPool* pool, const RHIBufferDesc& desc,
                                  RHIBufferHandle& handle, std::string& error)
    {
        return pool ? pool->Acquire(desc, handle, error) : device.CreateBuffer(desc, handle, error);
    }

    inline void ReleaseSceneBuffer(IRenderDeviceServices& device, const own::shared_owner<TransientBufferPool>& pool,
                                   RHIBufferHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }
        if (pool)
        {
            pool->Return(handle);
        }
        else
        {
            device.ReleaseBuffer(handle);
        }
    }
} // namespace material_graph
