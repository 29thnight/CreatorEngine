#include "RHIAssetEvictionPolicy.h"

#include <algorithm>
#include <limits>

namespace
{
    uint64_t SaturatingAdd(uint64_t left, uint64_t right)
    {
        const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
        return right <= maximum - left ? left + right : maximum;
    }

    uint64_t Age(uint64_t currentFrame, uint64_t lastUsedFrame)
    {
        return currentFrame >= lastUsedFrame ? currentFrame - lastUsedFrame : 0;
    }
}

uint64_t RHIAssetEvictionPass::RemainingBytes() const
{
    return retiredBytes < targetBytes ? targetBytes - retiredBytes : 0;
}

void RHIAssetEvictionPass::RecordRetired(uint64_t bytes, bool pressureDriven)
{
    retiredBytes = SaturatingAdd(retiredBytes, bytes);
    ++retiredCount;
    if (!pressureDriven) return;
    pressureRetiredBytes = SaturatingAdd(pressureRetiredBytes, bytes);
    ++pressureRetiredCount;
}

RHIAssetEvictionPass BeginRHIAssetEvictionPass(bool memoryPressure,
    uint64_t targetReleaseBytes, const RHIAssetEvictionConfig& config)
{
    RHIAssetEvictionPass pass{};
    pass.memoryPressure = memoryPressure;
    if (memoryPressure)
    {
        pass.targetBytes = (std::max)(targetReleaseBytes,
            config.minimumPressureTargetBytes);
    }
    return pass;
}

RHIAssetEvictionSelection SelectRHIAssetEvictionCandidates(
    const std::vector<RHIAssetEvictionCandidate>& candidates,
    uint64_t currentFrame, const RHIAssetEvictionPass* pass,
    const RHIAssetEvictionConfig& config)
{
    struct Ranked
    {
        const RHIAssetEvictionCandidate* candidate{ nullptr };
        bool pressureDriven{ false };
    };

    const bool pressure = nullptr != pass && pass->memoryPressure;
    std::vector<Ranked> normal;
    std::vector<Ranked> pressured;
    RHIAssetEvictionSelection result{};
    normal.reserve(candidates.size());
    pressured.reserve(candidates.size());

    for (const RHIAssetEvictionCandidate& candidate : candidates)
    {
        if (!candidate.evictable)
        {
            if (pressure) ++result.pressureUploadPending;
            continue;
        }

        const uint64_t age = Age(currentFrame, candidate.lastUsedFrame);
        if (age >= config.normalRetireAfterFrames)
        {
            normal.push_back(Ranked{ &candidate, false });
        }
        else if (pressure && age >= config.pressureRetireAfterFrames)
        {
            pressured.push_back(Ranked{ &candidate, true });
        }
        else if (pressure)
        {
            ++result.pressureProtectedRecent;
        }
    }

    const auto olderFirst = [](const Ranked& left, const Ranked& right)
    {
        if (left.candidate->lastUsedFrame != right.candidate->lastUsedFrame)
            return left.candidate->lastUsedFrame < right.candidate->lastUsedFrame;
        if (left.candidate->bytes != right.candidate->bytes)
            return left.candidate->bytes > right.candidate->bytes;
        return left.candidate->assetId < right.candidate->assetId;
    };
    std::sort(normal.begin(), normal.end(), olderFirst);
    std::sort(pressured.begin(), pressured.end(), olderFirst);

    uint64_t plannedBytes = 0;
    for (const Ranked& ranked : normal)
    {
        result.entries.push_back(RHIAssetEvictionSelectionEntry{
            ranked.candidate->assetId, false });
        plannedBytes = SaturatingAdd(plannedBytes, ranked.candidate->bytes);
    }

    if (pressure)
    {
        uint64_t remaining = pass->RemainingBytes();
        remaining = plannedBytes < remaining ? remaining - plannedBytes : 0;
        for (const Ranked& ranked : pressured)
        {
            if (0 == remaining) break;
            result.entries.push_back(RHIAssetEvictionSelectionEntry{
                ranked.candidate->assetId, true });
            remaining = ranked.candidate->bytes < remaining
                ? remaining - ranked.candidate->bytes : 0;
        }
    }
    return result;
}

bool RunRHIAssetEvictionPolicyContractTest(std::string& outLog)
{
    RHIAssetEvictionConfig config{};
    config.minimumPressureTargetBytes = 0;
    const std::vector<RHIAssetEvictionCandidate> candidates = {
        { 1, 0,   32, true  },
        { 2, 117, 40, true  },
        { 3, 118, 50, true  },
        { 4, 0,   64, false }
    };

    const RHIAssetEvictionSelection normal = SelectRHIAssetEvictionCandidates(
        candidates, 120, nullptr, config);
    if (1 != normal.entries.size() || 1 != normal.entries[0].assetId ||
        normal.entries[0].pressureDriven)
    {
        outLog += "[공통 asset eviction] normal 120-frame 선택 실패\n";
        return false;
    }

    RHIAssetEvictionPass pressure = BeginRHIAssetEvictionPass(true, 70, config);
    const RHIAssetEvictionSelection selected = SelectRHIAssetEvictionCandidates(
        candidates, 120, &pressure, config);
    if (2 != selected.entries.size() || 1 != selected.entries[0].assetId ||
        2 != selected.entries[1].assetId || selected.entries[0].pressureDriven ||
        !selected.entries[1].pressureDriven ||
        1 != selected.pressureProtectedRecent || 1 != selected.pressureUploadPending)
    {
        outLog += "[공통 asset eviction] pressure LRU/grace/pending 선택 실패\n";
        return false;
    }

    pressure.RecordRetired(32, false);
    pressure.RecordRetired(40, true);
    if (0 != pressure.RemainingBytes() || 72 != pressure.retiredBytes ||
        40 != pressure.pressureRetiredBytes || 2 != pressure.retiredCount ||
        1 != pressure.pressureRetiredCount)
    {
        outLog += "[공통 asset eviction] 공유 회수 목표 accounting 실패\n";
        return false;
    }

    const std::vector<RHIAssetEvictionCandidate> secondCache = {
        { 5, 100, 80, true }
    };
    if (!SelectRHIAssetEvictionCandidates(secondCache, 120, &pressure, config)
            .entries.empty())
    {
        outLog += "[공통 asset eviction] cache 간 target 공유 실패\n";
        return false;
    }

    outLog += "[공통 asset eviction] normal 120f·pressure 3f LRU·recent/pending 보호·공유 target 검증 통과\n";
    return true;
}
