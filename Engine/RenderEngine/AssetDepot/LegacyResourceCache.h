#pragma once

#include "../../Utility_Framework/Ownership.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <utility>

// Internal migration support for the existing synchronous loaders. Each
// DataSystem map entry owns its weak current index and bounded cache retention;
// consumers never depend on a cache reference to keep their generation alive.
namespace asset_cache_detail
{
    template<class T>
    struct Entry
    {
        using Resource = T;
        own::weak_owner<const T> current;
        own::shared_owner<const T> retained;
        std::uint64_t publication{};
        // Exact-generation/internal caches use the default revision. Current legacy
        // lookups always supply DataSystem's resolver revision, including zero.
        std::uint64_t resolverRevision{};
        std::uint64_t lastUse{};
        std::size_t retainedBytes{};
    };

    inline std::uint64_t NextUse() noexcept
    {
        static std::atomic<std::uint64_t> serial{};
        auto previous = serial.load(std::memory_order_relaxed);
        while (true)
        {
            if (previous == (std::numeric_limits<std::uint64_t>::max)())
            {
                std::terminate(); // Never reuse a publication/use serial.
            }
            if (serial.compare_exchange_weak(previous, previous + 1u,
                std::memory_order_relaxed, std::memory_order_relaxed))
            {
                return previous + 1u;
            }
        }
    }

    inline void AddCharge(std::size_t& total, std::size_t bytes) noexcept
    {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        total = bytes > maximum - total ? maximum : total + bytes;
    }

    template<class T>
    own::shared_owner<const T> Acquire(Entry<T>& entry)
    {
        // Caller holds the same cache mutex used by publication/invalidation.
        // The weak control block identifies precisely this entry's publication.
        auto owner = entry.retained ? entry.retained : entry.current.lock();
        if (owner)
        {
            entry.lastUse = NextUse();
        }
        return owner;
    }

    template<class T>
    own::shared_owner<const T> Acquire(Entry<T>& entry, std::uint64_t resolverRevision)
    {
        if (entry.resolverRevision != resolverRevision)
        {
            return {};
        }
        return Acquire(entry);
    }

    template<class Cache>
    void PruneExpired(Cache& cache)
    {
        // Caller holds the cache mutex and has finished using entry references.
        // Erasing the weak index also releases its otherwise surviving control block.
        for (auto entry = cache.begin(); entry != cache.end();)
        {
            if (!entry->second.retained && entry->second.current.expired())
            {
                entry = cache.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
    }

    template<class Cache>
    void Trim(Cache& cache, std::size_t budgetBytes, std::size_t budgetEntries)
    {
        PruneExpired(cache);
        std::size_t bytes{}, count{};
        for (const auto& [key, entry] : cache)
        {
            if (entry.retained)
            {
                AddCharge(bytes, entry.retainedBytes);
                ++count;
            }
        }
        while (bytes > budgetBytes || count > budgetEntries)
        {
            auto oldest = cache.end();
            for (auto candidate = cache.begin(); candidate != cache.end(); ++candidate)
            {
                if (candidate->second.retained &&
                    (oldest == cache.end() || candidate->second.lastUse < oldest->second.lastUse))
                {
                    oldest = candidate;
                }
            }
            if (oldest == cache.end())
            {
                break;
            }
            oldest->second.retained.reset();
            oldest->second.retainedBytes = 0u;
            // Recompute after saturated accounting instead of subtracting from
            // a saturated sum and accidentally accepting an over-budget cache.
            bytes = 0u;
            count = 0u;
            for (const auto& [key, entry] : cache)
            {
                if (entry.retained)
                {
                    AddCharge(bytes, entry.retainedBytes);
                    ++count;
                }
            }
        }
        PruneExpired(cache);
    }

    template<class Cache, class Key, class T>
    own::shared_owner<const T> Publish(Cache& cache, const Key& key,
        own::shared_owner<const T> owner, std::size_t bytes,
        std::size_t budgetBytes, std::size_t budgetEntries, std::uint64_t resolverRevision = 0u)
    {
        // Do not let a caller's key reference dangle if pruning erases its entry.
        const typename Cache::key_type publicationKey(key);
        PruneExpired(cache);
        {
            auto& entry = cache[publicationKey];
            if (entry.resolverRevision != resolverRevision)
            {
                // Logical invalidation drops only this cache's references.
                // Existing consumers continue owning the old immutable generation.
                entry = {};
            }
            if (auto current = Acquire(entry))
            {
                return current;
            }
            entry.current = own::weak_owner<const T>(owner);
            entry.publication = NextUse();
            entry.resolverRevision = resolverRevision;
            entry.lastUse = entry.publication;
            entry.retained = owner;
            entry.retainedBytes = bytes;
        }
        // Trim may erase entries. No reference/iterator into cache crosses it;
        // owner itself keeps the just-published generation alive even at budget 0.
        Trim(cache, budgetBytes, budgetEntries);
        return owner;
    }
}
