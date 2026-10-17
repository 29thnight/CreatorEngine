#pragma once
#include "IRenderDeviceServices.h"
#include "../../Utility_Framework/StandardLibrary.h"

// One physical heap per device, with independent stable virtual buffers per asset.
// Only completed, unreferenced cache entries may offer their pages for eviction.
class RHISharedSparsePagePool : public std::enable_shared_from_this<RHISharedSparsePagePool>
{
  public:
    struct Allocation
    {
        std::shared_ptr<RHISharedSparsePagePool> pool;
        RHIBufferHandle buffer;
        uint64_t virtualBytes{}, revision{}, lastUse{};
        bool evictable{};
        std::map<uint32_t, uint32_t> tiles;
        std::map<uint32_t, uint32_t> priorities;
        ~Allocation()
        {
            if (pool)
            {
                pool->Release(*this);
            }
        }
    };
    struct Stats
    {
        uint64_t physicalBytes{}, mappedPages{}, evictions{}, reuses{};
    };
    explicit RHISharedSparsePagePool(IRenderDeviceServices& device, uint32_t capacity = 1024)
        : device_(device), capacity_(capacity), occupied_(capacity), usedBefore_(capacity)
    {
    }
    ~RHISharedSparsePagePool()
    {
        if (anchor_.IsValid())
        {
            device_.ReleaseBuffer(anchor_);
        }
    }
    static std::shared_ptr<RHISharedSparsePagePool> ForDevice(IRenderDeviceServices& device)
    {
        static std::mutex registryMutex;
        static std::map<IRenderDeviceServices*, std::weak_ptr<RHISharedSparsePagePool>> registry;
        std::lock_guard lock(registryMutex);
        auto result = registry[&device].lock();
        if (!result)
        {
            result = std::make_shared<RHISharedSparsePagePool>(device);
            registry[&device] = result;
        }
        return result;
    }
    std::shared_ptr<Allocation> Create(uint64_t virtualBytes, std::string& error)
    {
        std::lock_guard lock(mutex_);
        if (!anchor_.IsValid() &&
            !device_.CreateSparseBuffer({RHISparseTileBytes, capacity_, false, L"METEOR.SharedPhysicalPages"}, anchor_,
                                        error))
        {
            return {};
        }
        auto result = std::make_shared<Allocation>();
        if (!device_.CreateSparseBuffer({virtualBytes, capacity_, false, L"METEOR.AssetVirtualPages", anchor_},
                                        result->buffer, error))
        {
            return {};
        }
        result->pool = shared_from_this();
        result->virtualBytes = virtualBytes;
        allocations_.push_back(result);
        return result;
    }
    void SetEvictable(const std::shared_ptr<Allocation>& allocation, bool value)
    {
        std::lock_guard lock(mutex_);
        allocation->evictable = value;
        if (!value)
        {
            allocation->lastUse = ++clock_;
        }
    }
    uint64_t Revision(const std::shared_ptr<Allocation>& allocation) const
    {
        std::lock_guard lock(mutex_);
        return allocation->revision;
    }
    Stats GetStats() const
    {
        std::lock_guard lock(mutex_);
        return {anchor_.IsValid() ? uint64_t(capacity_) * RHISparseTileBytes : 0,
                uint64_t(std::count(occupied_.begin(), occupied_.end(), true)), evictions_, reuses_};
    }
    bool Prepare(const std::shared_ptr<Allocation>& allocation, const std::map<uint32_t, uint32_t>& wanted,
                 std::string& error)
    {
        std::lock_guard lock(mutex_);
        auto& target = *allocation;
        target.evictable = false;
        for (const auto& [tile, priority] : wanted)
        {
            if (uint64_t(tile) * RHISparseTileBytes >= target.virtualBytes)
            {
                error = "Shared sparse virtual page is outside its asset";
                return false;
            }
        }
        uint64_t missing{}, reclaim{};
        for (const auto& [tile, priority] : wanted)
        {
            missing += !target.tiles.contains(tile);
        }
        for (const auto& [tile, physical] : target.tiles)
        {
            reclaim += !wanted.contains(tile);
        }
        struct Victim
        {
            std::shared_ptr<Allocation> owner;
            uint32_t tile{}, priority{};
        };
        std::vector<Victim> candidates;
        for (auto it = allocations_.begin(); it != allocations_.end();)
        {
            auto owner = it->lock();
            if (!owner)
            {
                it = allocations_.erase(it);
                continue;
            }
            if (owner != allocation && owner->evictable)
            {
                for (const auto& [tile, physical] : owner->tiles)
                {
                    candidates.push_back({owner, tile, owner->priorities.at(tile)});
                }
            }
            ++it;
        }
        std::ranges::sort(candidates, [](const auto& a, const auto& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.owner->lastUse < b.owner->lastUse;
        });
        const uint64_t free = std::count(occupied_.begin(), occupied_.end(), false);
        if (missing > free + reclaim + candidates.size())
        {
            error = "Shared sparse budget exhausted by active pages; no completed eviction candidate";
            return false;
        }
        // Admission is checked before any mutation. An individual successful NULL
        // map is committed immediately, so a later queue rejection cannot invent residency.
        std::vector<uint32_t> obsolete;
        for (const auto& [tile, physical] : target.tiles)
        {
            if (!wanted.contains(tile))
            {
                obsolete.push_back(tile);
            }
        }
        for (auto tile : obsolete)
        {
            if (!Unmap(target, tile, error))
            {
                return false;
            }
        }
        uint64_t available = free + reclaim;
        for (const auto& victim : candidates)
        {
            if (available >= missing)
            {
                break;
            }
            if (!Unmap(*victim.owner, victim.tile, error))
            {
                return false;
            }
            ++evictions_;
            ++available;
        }
        std::vector<RHISparseTileMapping> maps;
        uint32_t cursor{};
        for (const auto& [tile, priority] : wanted)
        {
            if (target.tiles.contains(tile))
            {
                continue;
            }
            while (cursor < capacity_ && occupied_[cursor])
            {
                ++cursor;
            }
            maps.push_back({tile, cursor++, true});
        }
        if (!maps.empty() && !device_.UpdateSparseBufferMappings(target.buffer, maps, error))
        {
            return false;
        }
        for (const auto& map : maps)
        {
            occupied_[map.physicalTile] = true;
            reuses_ += usedBefore_[map.physicalTile];
            usedBefore_[map.physicalTile] = true;
            target.tiles.emplace(map.virtualTile, map.physicalTile);
        }
        if (!maps.empty())
        {
            ++target.revision;
        }
        target.priorities = wanted;
        target.lastUse = ++clock_;
        error.clear();
        return true;
    }

  private:
    bool Unmap(Allocation& owner, uint32_t tile, std::string& error)
    {
        const RHISparseTileMapping change{tile, 0, false};
        if (!device_.UpdateSparseBufferMappings(owner.buffer, std::span(&change, 1), error))
        {
            return false;
        }
        occupied_[owner.tiles.at(tile)] = false;
        owner.tiles.erase(tile);
        owner.priorities.erase(tile);
        ++owner.revision;
        return true;
    }
    void Release(Allocation& allocation)
    {
        std::lock_guard lock(mutex_);
        // The allocation owner retires only after the cache's completion lease.
        // Resource destruction removes virtual mappings; subsequent queue work
        // can reuse the physical tiles retained by the shared heap anchor.
        device_.ReleaseBuffer(allocation.buffer);
        for (const auto& [tile, physical] : allocation.tiles)
        {
            occupied_[physical] = false;
        }
    }
    IRenderDeviceServices& device_;
    uint32_t capacity_;
    RHIBufferHandle anchor_;
    mutable std::recursive_mutex mutex_;
    std::vector<bool> occupied_, usedBefore_;
    std::vector<std::weak_ptr<Allocation>> allocations_;
    uint64_t clock_{}, evictions_{}, reuses_{};
};
