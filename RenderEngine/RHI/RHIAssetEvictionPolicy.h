#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct RHIAssetEvictionConfig
{
    uint64_t normalRetireAfterFrames{ 120 };
    uint64_t pressureRetireAfterFrames{ 3 };
    uint64_t minimumPressureTargetBytes{ 64ull * 1024ull * 1024ull };
};

struct RHIAssetEvictionCandidate
{
    uint64_t assetId{ 0 };
    uint64_t lastUsedFrame{ 0 };
    uint64_t bytes{ 0 };
    bool evictable{ true };
};

struct RHIAssetEvictionSelectionEntry
{
    uint64_t assetId{ 0 };
    bool pressureDriven{ false };
};

struct RHIAssetEvictionSelection
{
    std::vector<RHIAssetEvictionSelectionEntry> entries;
    uint32_t pressureProtectedRecent{ 0 };
    uint32_t pressureUploadPending{ 0 };
};

/// 한 device의 cache들이 순서대로 공유하는 pressure 회수 예산.
/// retiredBytes는 completion을 기다리는 묘지로 이동한 논리 자산 바이트다.
/// 실제 native budget 감소는 completion 뒤 persistent segment trim에서 일어난다.
struct RHIAssetEvictionPass
{
    bool memoryPressure{ false };
    uint64_t targetBytes{ 0 };
    uint64_t retiredBytes{ 0 };
    uint64_t pressureRetiredBytes{ 0 };
    uint32_t retiredCount{ 0 };
    uint32_t pressureRetiredCount{ 0 };

    uint64_t RemainingBytes() const;
    void RecordRetired(uint64_t bytes, bool pressureDriven);
};

struct RHIAssetEvictionStats
{
    uint64_t pressurePasses{ 0 };
    uint64_t pressureRetired{ 0 };
    uint64_t pressureRetiredBytes{ 0 };
    uint64_t pressureProtectedRecent{ 0 };
    uint64_t pressureUploadPending{ 0 };
};

RHIAssetEvictionPass BeginRHIAssetEvictionPass(bool memoryPressure,
    uint64_t targetReleaseBytes,
    const RHIAssetEvictionConfig& config = RHIAssetEvictionConfig{});

/// normal threshold를 넘은 항목은 pressure와 무관하게 모두 고른다.
/// pressure 상태에서는 나머지 회수 목표까지 pressure threshold를 넘은 항목을
/// last-used 오름차순, 같은 시각이면 큰 자산 우선으로 고른다.
RHIAssetEvictionSelection SelectRHIAssetEvictionCandidates(
    const std::vector<RHIAssetEvictionCandidate>& candidates,
    uint64_t currentFrame, const RHIAssetEvictionPass* pass = nullptr,
    const RHIAssetEvictionConfig& config = RHIAssetEvictionConfig{});

bool RunRHIAssetEvictionPolicyContractTest(std::string& outLog);
