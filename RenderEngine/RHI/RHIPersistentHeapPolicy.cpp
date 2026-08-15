#include "RHIPersistentHeapPolicy.h"

#include <algorithm>

RHIPersistentHeapPolicy::RHIPersistentHeapPolicy(
    const RHIPersistentHeapConfig& config)
    : m_config(config)
{
}

void RHIPersistentHeapPolicy::Reset(const RHIPersistentHeapConfig& config)
{
    std::lock_guard lock(m_mutex);
    m_config = config;
    m_segments.clear();
    m_freeSegmentSlots.clear();
    m_stats = {};
}

bool RHIPersistentHeapPolicy::AlignUp(uint64_t value, uint64_t alignment,
    uint64_t& outAligned)
{
    if (0 == alignment) alignment = 1;
    const uint64_t remainder = value % alignment;
    if (0 == remainder)
    {
        outAligned = value;
        return true;
    }

    const uint64_t delta = alignment - remainder;
    if (value > (std::numeric_limits<uint64_t>::max)() - delta) return false;
    outAligned = value + delta;
    return true;
}

void RHIPersistentHeapPolicy::AddFreeBlock(Segment& segment, uint64_t offset,
    uint64_t bytes)
{
    if (0 == bytes) return;
    segment.freeByOffset.emplace(offset, bytes);
    segment.freeBySize.emplace(bytes, offset);
}

bool RHIPersistentHeapPolicy::RemoveFreeBlock(Segment& segment, uint64_t offset,
    uint64_t bytes)
{
    const auto byOffset = segment.freeByOffset.find(offset);
    if (byOffset == segment.freeByOffset.end() || byOffset->second != bytes) return false;
    segment.freeByOffset.erase(byOffset);

    const auto range = segment.freeBySize.equal_range(bytes);
    for (auto it = range.first; it != range.second; ++it)
    {
        if (it->second != offset) continue;
        segment.freeBySize.erase(it);
        return true;
    }
    return false;
}

RHIPersistentHeapSegmentHandle RHIPersistentHeapPolicy::AddSegment(
    uint64_t compatibilityKey, uint64_t bytes)
{
    if (0 == bytes) return {};
    std::lock_guard lock(m_mutex);

    uint32_t slot = 0;
    if (!m_freeSegmentSlots.empty())
    {
        slot = m_freeSegmentSlots.back();
        m_freeSegmentSlots.pop_back();
        ++m_stats.segmentSlotReuses;
    }
    else
    {
        slot = static_cast<uint32_t>(m_segments.size());
        m_segments.emplace_back();
    }

    Segment& segment = m_segments[slot];
    segment.compatibilityKey = compatibilityKey;
    segment.bytes = bytes;
    segment.alive = true;
    segment.everAllocated = false;
    segment.freeByOffset.clear();
    segment.freeBySize.clear();
    segment.allocatedByOffset.clear();
    AddFreeBlock(segment, 0, bytes);

    ++m_stats.activeSegments;
    ++m_stats.emptySegments;
    ++m_stats.segmentCreates;
    m_stats.segmentBytes += bytes;
    m_stats.freeBytes += bytes;
    return RHIPersistentHeapSegmentHandle{ slot, segment.generation };
}

RHIPersistentHeapAllocation RHIPersistentHeapPolicy::Allocate(
    uint64_t compatibilityKey, uint64_t bytes, uint64_t alignment)
{
    if (0 == bytes) return {};
    if (0 == alignment) alignment = 1;
    std::lock_guard lock(m_mutex);

    uint32_t bestSlot = RHIPersistentHeapSegmentHandle::kInvalidSlot;
    uint64_t bestBlockOffset = 0;
    uint64_t bestBlockBytes = 0;
    uint64_t bestAlignedOffset = 0;
    uint64_t bestWaste = (std::numeric_limits<uint64_t>::max)();

    for (uint32_t slot = 0; slot < m_segments.size(); ++slot)
    {
        Segment& segment = m_segments[slot];
        if (!segment.alive || segment.compatibilityKey != compatibilityKey) continue;

        for (auto candidate = segment.freeBySize.lower_bound(bytes);
            candidate != segment.freeBySize.end(); ++candidate)
        {
            const uint64_t blockBytes = candidate->first;
            const uint64_t blockOffset = candidate->second;
            uint64_t alignedOffset = 0;
            if (!AlignUp(blockOffset, alignment, alignedOffset)) continue;
            if (alignedOffset < blockOffset || alignedOffset - blockOffset > blockBytes) continue;
            const uint64_t prefix = alignedOffset - blockOffset;
            if (bytes > blockBytes - prefix) continue;

            const uint64_t waste = blockBytes - bytes;
            if (waste < bestWaste)
            {
                bestSlot = slot;
                bestBlockOffset = blockOffset;
                bestBlockBytes = blockBytes;
                bestAlignedOffset = alignedOffset;
                bestWaste = waste;
            }
            break; // 이 segment의 size index에서는 첫 적합 블록이 best-fit이다.
        }
    }

    if (RHIPersistentHeapSegmentHandle::kInvalidSlot == bestSlot) return {};

    Segment& segment = m_segments[bestSlot];
    const bool wasEmpty = segment.allocatedByOffset.empty();
    if (!RemoveFreeBlock(segment, bestBlockOffset, bestBlockBytes)) return {};

    const uint64_t prefix = bestAlignedOffset - bestBlockOffset;
    const uint64_t suffixOffset = bestAlignedOffset + bytes;
    const uint64_t suffix = bestBlockBytes - prefix - bytes;
    AddFreeBlock(segment, bestBlockOffset, prefix);
    AddFreeBlock(segment, suffixOffset, suffix);
    segment.allocatedByOffset.emplace(bestAlignedOffset, bytes);

    if (wasEmpty) --m_stats.emptySegments;
    const bool aliasing = segment.everAllocated;
    if (aliasing) ++m_stats.blockReuses;
    segment.everAllocated = true;
    ++m_stats.livePooledAllocations;
    ++m_stats.pooledAllocations;
    m_stats.allocatedBytes += bytes;
    m_stats.freeBytes -= bytes;

    return RHIPersistentHeapAllocation{
        RHIPersistentHeapSegmentHandle{ bestSlot, segment.generation },
        compatibilityKey, bestAlignedOffset, bytes, alignment, aliasing };
}

bool RHIPersistentHeapPolicy::IsLiveHandle(
    const RHIPersistentHeapSegmentHandle& handle) const
{
    if (!handle.IsValid() || handle.slot >= m_segments.size()) return false;
    const Segment& segment = m_segments[handle.slot];
    return segment.alive && segment.generation == handle.generation;
}

bool RHIPersistentHeapPolicy::Release(
    const RHIPersistentHeapAllocation& allocation)
{
    if (!allocation.IsValid()) return false;
    std::lock_guard lock(m_mutex);
    if (!IsLiveHandle(allocation.segment)) return false;

    Segment& segment = m_segments[allocation.segment.slot];
    if (segment.compatibilityKey != allocation.compatibilityKey) return false;
    const auto live = segment.allocatedByOffset.find(allocation.offset);
    if (live == segment.allocatedByOffset.end() || live->second != allocation.size) return false;
    segment.allocatedByOffset.erase(live);

    uint64_t mergedOffset = allocation.offset;
    uint64_t mergedBytes = allocation.size;
    auto next = segment.freeByOffset.lower_bound(mergedOffset);
    if (next != segment.freeByOffset.begin())
    {
        const auto previous = std::prev(next);
        if (previous->first + previous->second == mergedOffset)
        {
            mergedOffset = previous->first;
            mergedBytes += previous->second;
            RemoveFreeBlock(segment, previous->first, previous->second);
            ++m_stats.coalesces;
        }
    }

    next = segment.freeByOffset.lower_bound(mergedOffset);
    if (next != segment.freeByOffset.end() &&
        mergedOffset + mergedBytes == next->first)
    {
        mergedBytes += next->second;
        RemoveFreeBlock(segment, next->first, next->second);
        ++m_stats.coalesces;
    }
    AddFreeBlock(segment, mergedOffset, mergedBytes);

    --m_stats.livePooledAllocations;
    m_stats.allocatedBytes -= allocation.size;
    m_stats.freeBytes += allocation.size;
    if (segment.allocatedByOffset.empty()) ++m_stats.emptySegments;
    return true;
}

std::vector<RHIPersistentHeapSegmentHandle>
RHIPersistentHeapPolicy::TrimEmptySegments(uint32_t keepPerKey)
{
    std::lock_guard lock(m_mutex);
    std::vector<RHIPersistentHeapSegmentHandle> removed;
    std::unordered_map<uint64_t, uint32_t> kept;

    for (uint32_t slot = 0; slot < m_segments.size(); ++slot)
    {
        Segment& segment = m_segments[slot];
        if (!segment.alive || !segment.allocatedByOffset.empty()) continue;
        uint32_t& keptForKey = kept[segment.compatibilityKey];
        if (keptForKey < keepPerKey)
        {
            ++keptForKey;
            continue;
        }

        removed.push_back({ slot, segment.generation });
        --m_stats.activeSegments;
        --m_stats.emptySegments;
        m_stats.segmentBytes -= segment.bytes;
        m_stats.freeBytes -= segment.bytes;
        ++m_stats.trimmedSegments;
        m_stats.trimmedBytes += segment.bytes;

        segment.alive = false;
        segment.compatibilityKey = 0;
        segment.bytes = 0;
        segment.everAllocated = false;
        segment.freeByOffset.clear();
        segment.freeBySize.clear();
        segment.allocatedByOffset.clear();
        ++segment.generation;
        if (0 == segment.generation) ++segment.generation;
        m_freeSegmentSlots.push_back(slot);
    }
    return removed;
}

bool RHIPersistentHeapPolicy::ShouldUseDedicated(uint64_t requiredBytes) const
{
    std::lock_guard lock(m_mutex);
    return 0 != m_config.dedicatedThresholdBytes &&
        requiredBytes >= m_config.dedicatedThresholdBytes;
}

uint64_t RHIPersistentHeapPolicy::ChooseSegmentBytes(uint64_t requiredBytes,
    uint64_t alignment) const
{
    std::lock_guard lock(m_mutex);
    uint64_t result = (std::max)(requiredBytes, m_config.defaultSegmentBytes);
    uint64_t aligned = 0;
    return AlignUp(result, alignment, aligned) ? aligned : 0;
}

RHIPersistentHeapConfig RHIPersistentHeapPolicy::GetConfig() const
{
    std::lock_guard lock(m_mutex);
    return m_config;
}

RHIPersistentHeapStats RHIPersistentHeapPolicy::GetStats() const
{
    std::lock_guard lock(m_mutex);
    return m_stats;
}

RHIPersistentHeapBudgetDecision RHIPersistentHeapPolicy::UpdateBudget(
    const RHIPersistentHeapBudget& budget)
{
    std::lock_guard lock(m_mutex);
    RHIPersistentHeapBudgetDecision decision{};

    ++m_stats.budgetRefreshes;
    m_stats.budgetUsageBytes = budget.usageBytes;
    m_stats.budgetBytes = budget.budgetBytes;
    m_stats.budgetEstimated = budget.estimated;
    if (!budget.IsValid())
    {
        m_stats.softBudgetBytes = 0;
        m_stats.memoryPressure = false;
        return decision;
    }

    const uint64_t releaseThreshold = budget.budgetBytes - budget.budgetBytes / 5;
    const uint64_t enterThreshold = budget.budgetBytes - budget.budgetBytes / 10;
    decision.memoryPressure = !budget.estimated && (m_stats.memoryPressure
        ? budget.usageBytes > releaseThreshold
        : budget.usageBytes >= enterThreshold);
    if (!m_stats.memoryPressure && decision.memoryPressure)
        ++m_stats.budgetPressureEvents;

    // 실제 heap 전체의 남은 양을 이 allocator 하나가 독점하지 않는다. 평상시에는
    // headroom의 1/8, 최대 512 MiB까지만 새 segment 성장분으로 예약한다.
    // 최소 segment조차 들어가지 않거나 pressure 상태면 새 큰 heap을 잡지 않고
    // 현재 segment 한도를 유지해 adapter의 exact-size dedicated fallback을 탄다.
    constexpr uint64_t kMaximumGrowth = 512ull * 1024ull * 1024ull;
    const uint64_t headroom = budget.budgetBytes > budget.usageBytes
        ? budget.budgetBytes - budget.usageBytes : 0;
    uint64_t growth = 0;
    if (!decision.memoryPressure && 0 != headroom)
    {
        growth = (std::min)(kMaximumGrowth, headroom / 8);
        if (headroom >= m_config.defaultSegmentBytes)
            growth = (std::max)(growth, m_config.defaultSegmentBytes);
        growth = (std::min)(growth, headroom);
    }

    decision.softBudgetBytes = m_stats.segmentBytes;
    if (growth <= (std::numeric_limits<uint64_t>::max)() -
        decision.softBudgetBytes)
        decision.softBudgetBytes += growth;
    else
        decision.softBudgetBytes = (std::numeric_limits<uint64_t>::max)();

    m_stats.softBudgetBytes = decision.softBudgetBytes;
    m_stats.memoryPressure = decision.memoryPressure;
    return decision;
}

void RHIPersistentHeapPolicy::RecordDedicatedAllocation(uint64_t bytes,
    bool fallback)
{
    std::lock_guard lock(m_mutex);
    ++m_stats.liveDedicatedAllocations;
    ++m_stats.dedicatedAllocations;
    m_stats.dedicatedBytes += bytes;
    if (fallback) ++m_stats.dedicatedFallbacks;
}

void RHIPersistentHeapPolicy::RecordDedicatedRelease()
{
    std::lock_guard lock(m_mutex);
    if (0 != m_stats.liveDedicatedAllocations) --m_stats.liveDedicatedAllocations;
}

void RHIPersistentHeapPolicy::RecordAllocationFailure()
{
    std::lock_guard lock(m_mutex);
    ++m_stats.allocationFailures;
}

bool RunRHIPersistentHeapPolicyContractTest(std::string& outLog)
{
    RHIPersistentHeapConfig config{};
    config.defaultSegmentBytes = 1024;
    config.dedicatedThresholdBytes = 768;
    config.standbySegmentCountPerKey = 1;
    RHIPersistentHeapPolicy policy(config);

    const auto firstSegment = policy.AddSegment(7, 1024);
    const auto first = policy.Allocate(7, 120, 256);
    const auto second = policy.Allocate(7, 120, 256);
    const bool alignmentAndPadding = firstSegment.IsValid() && first.IsValid() &&
        second.IsValid() && 0 == first.offset && 256 == second.offset &&
        0 == (first.offset % 256) && 0 == (second.offset % 256);

    const bool releasedAndMerged = policy.Release(first) && policy.Release(second);
    const RHIPersistentHeapStats merged = policy.GetStats();
    const bool fullCoalesce = releasedAndMerged && 1 == merged.emptySegments &&
        1024 == merged.freeBytes && 0 == merged.allocatedBytes &&
        0 == merged.livePooledAllocations && 2 <= merged.coalesces;

    const auto secondSegment = policy.AddSegment(7, 1024);
    const auto trimmed = policy.TrimEmptySegments(1);
    const bool standbyTrim = secondSegment.IsValid() && 1 == trimmed.size() &&
        1 == policy.GetStats().activeSegments;

    policy.TrimEmptySegments(0);
    const auto reusedSegment = policy.AddSegment(7, 1024);
    const bool generationInvalidation = reusedSegment.IsValid() &&
        !policy.Release(first) && reusedSegment.generation != first.segment.generation;

    const auto boundaryA = policy.Allocate(7, 768, 1);
    const auto boundaryB = policy.Allocate(7, 256, 1);
    const auto overflow = policy.Allocate(7, 1, 1);
    const bool boundary = boundaryA.IsValid() && boundaryB.IsValid() &&
        0 == boundaryA.offset && 768 == boundaryB.offset && !overflow.IsValid();

    const bool dedicatedPolicy = policy.ShouldUseDedicated(768) &&
        !policy.ShouldUseDedicated(767) && 1024 == policy.ChooseSegmentBytes(513, 256);
    const RHIPersistentHeapBudgetDecision normalBudget = policy.UpdateBudget(
        RHIPersistentHeapBudget{ 1000, 10000, false });
    const RHIPersistentHeapBudgetDecision pressureBudget = policy.UpdateBudget(
        RHIPersistentHeapBudget{ 9500, 10000, false });
    const RHIPersistentHeapBudgetDecision heldPressure = policy.UpdateBudget(
        RHIPersistentHeapBudget{ 8100, 10000, false });
    const RHIPersistentHeapBudgetDecision releasedPressure = policy.UpdateBudget(
        RHIPersistentHeapBudget{ 7900, 10000, false });
    const RHIPersistentHeapStats budgetStats = policy.GetStats();
    const bool budgetPolicy = normalBudget.softBudgetBytes >=
        budgetStats.segmentBytes && !normalBudget.memoryPressure &&
        pressureBudget.memoryPressure && heldPressure.memoryPressure &&
        !releasedPressure.memoryPressure && 4 == budgetStats.budgetRefreshes &&
        1 == budgetStats.budgetPressureEvents;
    const bool passed = alignmentAndPadding && fullCoalesce && standbyTrim &&
        generationInvalidation && boundary && dedicatedPolicy && budgetPolicy;

    outLog += passed
        ? "[공통 persistent heap] 정렬·padding·best-fit·병합·trim·generation·budget hysteresis 검증 통과\n"
        : "[공통 persistent heap] allocator contract 검증 실패\n";
    return passed;
}
