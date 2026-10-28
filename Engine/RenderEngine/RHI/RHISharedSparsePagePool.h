#include "../../Containers/ContainerAPI.h"
#pragma once
#include "../../Utility_Framework/Foundation/EngineOrderedMap.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.DynamicBitset.h"
#else
import Creator.Core.DynamicBitset;
#endif
#include "IRenderDeviceServices.h"
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

// One slab pool per device, with independent stable virtual buffers per asset.
// Only completed, unreferenced cache entries may offer their pages for eviction.
class RHISharedSparsePagePool : public own::enable_owner_from_this<RHISharedSparsePagePool>
{
  public:
    struct Allocation
    {
        own::shared_owner<RHISharedSparsePagePool> pool;
        RHIBufferHandle buffer;
        uint64_t virtualBytes{}, revision{}, lastUse{};
        bool evictable{};
        ce::engine_ordered_map<uint32_t, uint32_t> tiles;
        ce::engine_ordered_map<uint32_t, uint32_t> priorities;
        ~Allocation()
        {
            if (pool)
            {
                pool->Release(*this);
            }
        }
    };
    struct Policy
    {
        uint32_t minimumTiles{256}, growthTiles{256}, maximumTiles{8192};
        uint64_t idleFrames{120};
    };
    struct Stats
    {
        uint64_t physicalBytes{}, mappedPages{}, evictions{}, reuses{};
        uint64_t slabs{}, growths{}, shrinks{}, growthDenials{}, deferredShrinks{};
    };
    explicit RHISharedSparsePagePool(IRenderDeviceServices& device, uint32_t capacity = 1024)
        : RHISharedSparsePagePool(device, Policy{capacity, capacity, capacity, 120})
    {
    }
    RHISharedSparsePagePool(IRenderDeviceServices& device, Policy policy)
        : device_(device), policy_(policy), capacity_(policy.minimumTiles), occupied_(policy.minimumTiles),
          usedBefore_(policy.minimumTiles)
    {
        if (!policy.minimumTiles || !policy.growthTiles || policy.maximumTiles < policy.minimumTiles)
        {
            throw std::invalid_argument("Invalid sparse slab pool policy");
        }
    }
    ~RHISharedSparsePagePool()
    {
        for (const auto& slab : slabs_)
        {
            device_.ReleaseBuffer(slab.owner);
        }
    }
    static own::shared_owner<RHISharedSparsePagePool> ForDevice(IRenderDeviceServices& device)
    {
        static std::mutex registryMutex;
        static ce::engine_ordered_map<IRenderDeviceServices*, own::weak_owner<RHISharedSparsePagePool>> registry;
        std::lock_guard lock(registryMutex);
        auto result = registry[&device].lock();
        if (!result)
        {
            result = own::make_shared<RHISharedSparsePagePool>(device, Policy{});
            registry[&device] = result;
        }
        return result;
    }
    own::shared_owner<Allocation> Create(uint64_t virtualBytes, std::string& error)
    {
        std::lock_guard lock(mutex_);
        if (!anchor_.IsValid() &&
            !device_.CreateSparseBuffer({RHISparseTileBytes, capacity_, false, L"METEOR.SharedPhysicalPages"}, anchor_,
                                        error))
        {
            return {};
        }
        if (slabs_.empty())
        {
            slabs_.push_back({anchor_, 0, policy_.minimumTiles, tick_});
        }
        auto result = own::make_shared<Allocation>();
        if (!device_.CreateSparseBuffer(
                {virtualBytes, policy_.minimumTiles, false, L"METEOR.AssetVirtualPages", anchor_}, result->buffer,
                error))
        {
            return {};
        }
        result->pool = shared_from_this();
        result->virtualBytes = virtualBytes;
        allocations_.push_back(result);
        return result;
    }
    void SetEvictable(const own::shared_owner<Allocation>& allocation, bool value)
    {
        std::lock_guard lock(mutex_);
        allocation->evictable = value;
        if (!value)
        {
            allocation->lastUse = ++clock_;
            Touch(*allocation);
        }
    }
    uint64_t Revision(const own::shared_owner<Allocation>& allocation) const
    {
        std::lock_guard lock(mutex_);
        return allocation->revision;
    }
    Stats GetStats() const
    {
        std::lock_guard lock(mutex_);
        return {anchor_.IsValid() ? uint64_t(capacity_) * RHISparseTileBytes : 0,
                uint64_t(occupied_.count()),
                evictions_,
                reuses_,
                slabs_.size(),
                growths_,
                shrinks_,
                growthDenials_,
                deferredShrinks_};
    }
    bool CanGrow() const
    {
        std::lock_guard lock(mutex_);
        const auto budget = device_.GetStreamingMemoryBudget();
        return capacity_ < policy_.maximumTiles && !budget.memoryPressure &&
               budget.availableGrowthBytes >= RHISparseTileBytes;
    }
    // Called once per upload recording after the cache has classified every owner.
    bool Tick(uint64_t recording, std::string& error)
    {
        std::lock_guard lock(mutex_);
        if (lastRecording_ == recording)
        {
            return true;
        }
        lastRecording_ = recording;
        ++tick_;
        const auto budget = device_.GetStreamingMemoryBudget();
        uint64_t released{};
        while (slabs_.size() > 1)
        {
            const auto& tail = slabs_.back();
            if (!budget.memoryPressure && tick_ - tail.lastUse < policy_.idleFrames)
            {
                break;
            }
            ce::dynamic_array<own::shared_owner<Allocation>> owners;
            for (const auto& weak : allocations_)
            {
                auto owner = weak.lock();
                if (!owner)
                {
                    continue;
                }
                const bool usesTail =
                    std::ranges::any_of(owner->tiles, [&](const auto& item) { return item.second >= tail.base; });
                if (!usesTail)
                {
                    continue;
                }
                if (!owner->evictable)
                {
                    ++deferredShrinks_;
                    return true;
                }
                owners.push_back(std::move(owner));
            }
            for (const auto& owner : owners)
            {
                auto found = owner->tiles.begin();
                while (found != owner->tiles.end())
                {
                    const auto tile = found->first;
                    const auto physical = found->second;
                    ++found;
                    if (physical >= tail.base)
                    {
                        if (!Unmap(*owner, tile, error))
                        {
                            return false;
                        }
                        ++evictions_;
                    }
                }
            }
            const auto bytes = uint64_t(tail.count) * RHISparseTileBytes;
            capacity_ = tail.base;
            // NULL mapping retains the slab and budget until its GPU fence completes.
            device_.ReleaseBuffer(tail.owner);
            slabs_.pop_back();
            occupied_.resize(capacity_);
            usedBefore_.resize(capacity_);
            ++shrinks_;
            released += bytes;
            if (budget.memoryPressure && budget.targetReleaseBytes && released >= budget.targetReleaseBytes)
            {
                break;
            }
        }
        error.clear();
        return true;
    }
    // A target may retain its own tiles. Only idle, completed other assets offer
    // pages; active allocations are never counted as available refinement space.
    uint64_t AdmissibleTiles(const own::shared_owner<Allocation>& target = {}) const
    {
        std::lock_guard lock(mutex_);
        uint64_t available = occupied_.size() - occupied_.count();
        for (const auto& weak : allocations_)
        {
            const auto allocation = weak.lock();
            if (allocation && ((target && allocation.operator->() == target.operator->()) || allocation->evictable))
            {
                available += allocation->tiles.size();
            }
        }
        return available;
    }
    template<typename Plan>
    bool WithAdmissionBudget(const own::shared_owner<Allocation>& target, Plan&& plan, uint64_t wanted = 0)
    {
        std::lock_guard lock(mutex_);
        // Planning and mapping share the lock; another asset cannot consume the
        // selected budget between a coarse-cut decision and tile admission.
        Grow(target, wanted);
        return std::forward<Plan>(plan)(AdmissibleTiles(target));
    }
    bool Prepare(const own::shared_owner<Allocation>& allocation, const ce::engine_ordered_map<uint32_t, uint32_t>& wanted,
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
        Grow(allocation, wanted.size());
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
            own::shared_owner<Allocation> owner;
            uint32_t tile{}, priority{};
        };
        ce::dynamic_array<Victim> candidates;
        for (auto it = allocations_.begin(); it != allocations_.end();)
        {
            auto owner = it->lock();
            if (!owner)
            {
                it = allocations_.erase(it);
                continue;
            }
            if (owner.operator->() != allocation.operator->() && owner->evictable)
            {
                for (const auto& [tile, physical] : owner->tiles)
                {
                    candidates.push_back({owner, tile, owner->priorities.at(tile)});
                }
            }
            ++it;
        }
        std::ranges::sort(
            candidates, [](const auto& a, const auto& b)
            { return a.priority != b.priority ? a.priority < b.priority : a.owner->lastUse < b.owner->lastUse; });
        const uint64_t free = (occupied_.size() - occupied_.count());
        if (missing > free + reclaim + candidates.size())
        {
            error = "Shared sparse budget exhausted by active pages; no completed eviction candidate";
            return false;
        }
        // Admission is checked before any mutation. An individual successful NULL
        // map is committed immediately, so a later queue rejection cannot invent residency.
        ce::dynamic_array<uint32_t> obsolete;
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
        ce::dynamic_array<RHISparseTileMapping> maps;
        uint32_t cursor{};
        for (const auto& [tile, priority] : wanted)
        {
            if (target.tiles.contains(tile))
            {
                continue;
            }
            while (cursor < capacity_ && occupied_.test(cursor))
            {
                ++cursor;
            }
            const auto& slab = SlabFor(cursor);
            maps.push_back({tile, cursor - slab.base, true, slab.owner});
            ++cursor;
        }
        if (!maps.empty() && !device_.UpdateSparseBufferMappings(target.buffer, maps, error))
        {
            return false;
        }
        for (const auto& map : maps)
        {
            const auto slab = std::ranges::find(slabs_, map.physicalPoolOwner, &Slab::owner);
            const auto physical = slab->base + map.physicalTile;
            occupied_.set(physical, true);
            reuses_ += usedBefore_.test(physical);
            usedBefore_.set(physical, true);
            target.tiles.emplace(map.virtualTile, physical);
        }
        if (!maps.empty())
        {
            ++target.revision;
        }
        target.priorities = wanted;
        target.lastUse = ++clock_;
        Touch(target);
        error.clear();
        return true;
    }

  private:
    struct Slab
    {
        RHIBufferHandle owner;
        uint32_t base{}, count{};
        uint64_t lastUse{};
    };
    const Slab& SlabFor(uint32_t physical) const
    {
        const auto found = std::ranges::find_if(slabs_, [&](const auto& slab)
                                                { return physical >= slab.base && physical - slab.base < slab.count; });
        if (found == slabs_.end())
        {
            throw std::out_of_range("Sparse physical slab index");
        }
        return *found;
    }
    void Touch(const Allocation& allocation)
    {
        for (auto& slab : slabs_)
        {
            // tiles is keyed by virtual tile; physical indices can be in any order.
            if (std::ranges::any_of(allocation.tiles, [&](const auto& item)
                                    { return item.second >= slab.base && item.second - slab.base < slab.count; }))
            {
                slab.lastUse = tick_;
            }
        }
    }
    void Grow(const own::shared_owner<Allocation>& target, uint64_t wanted)
    {
        if (wanted > AdmissibleTiles(target) && !anchor_.IsValid())
        {
            std::string error;
            if (!device_.CreateSparseBuffer(
                    {RHISparseTileBytes, policy_.minimumTiles, false, L"METEOR.SharedPhysicalPages"}, anchor_, error))
            {
                ++growthDenials_;
                return;
            }
            slabs_.push_back({anchor_, 0, policy_.minimumTiles, tick_});
        }
        while (wanted > AdmissibleTiles(target) && capacity_ < policy_.maximumTiles)
        {
            const auto budget = device_.GetStreamingMemoryBudget();
            const auto pages =
                (std::min)(uint64_t(policy_.growthTiles), (std::min)(uint64_t(policy_.maximumTiles - capacity_),
                                                                     budget.availableGrowthBytes / RHISparseTileBytes));
            if (budget.memoryPressure || !pages)
            {
                ++growthDenials_;
                break;
            }
            RHIBufferHandle owner;
            std::string error;
            if (!device_.CreateSparseBuffer({RHISparseTileBytes, uint32_t(pages), false, L"METEOR.SharedPhysicalSlab"},
                                            owner, error))
            {
                ++growthDenials_;
                break;
            }
            slabs_.push_back({owner, capacity_, uint32_t(pages), tick_});
            capacity_ += uint32_t(pages);
            occupied_.resize(capacity_);
            usedBefore_.resize(capacity_);
            ++growths_;
        }
    }
    bool Unmap(Allocation& owner, uint32_t tile, std::string& error)
    {
        const auto& slab = SlabFor(owner.tiles.at(tile));
        const RHISparseTileMapping change{tile, 0, false, slab.owner};
        if (!device_.UpdateSparseBufferMappings(owner.buffer, std::span(&change, 1), error))
        {
            return false;
        }
        occupied_.set(owner.tiles.at(tile), false);
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
            occupied_.set(physical, false);
        }
    }
    IRenderDeviceServices& device_;
    Policy policy_;
    uint32_t capacity_;
    ce::dynamic_array<Slab> slabs_;
    uint64_t tick_{}, lastRecording_{UINT64_MAX};
    uint64_t growths_{}, shrinks_{}, growthDenials_{}, deferredShrinks_{};
    RHIBufferHandle anchor_;
    mutable std::recursive_mutex mutex_;
    ce::dynamic_bitset occupied_, usedBefore_;
    ce::dynamic_array<own::weak_owner<Allocation>> allocations_;
    uint64_t clock_{}, evictions_{}, reuses_{};
};
