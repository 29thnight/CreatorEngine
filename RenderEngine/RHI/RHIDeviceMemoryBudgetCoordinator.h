#pragma once

#include "RHIPersistentHeapPolicy.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

using RHIDeviceMemoryBudgetDomain = uint32_t;
using RHIDeviceMemoryBudgetOwner = uint32_t;
inline constexpr RHIDeviceMemoryBudgetDomain kRHIDeviceLocalMemoryBudgetDomain = 0;

struct RHIDeviceMemoryBudgetCoordinatorConfig
{
    uint64_t minimumGrowthBytes{ 64ull * 1024ull * 1024ull };
    uint64_t maximumGrowthBytes{ 512ull * 1024ull * 1024ull };
    uint32_t headroomDivisor{ 8 };
};

struct RHIDeviceMemoryGrowthTicket
{
    uint64_t id{ 0 };
    RHIDeviceMemoryBudgetOwner owner{ 0 };
    RHIDeviceMemoryBudgetDomain domain{ 0 };
    uint64_t bytes{ 0 };

    bool IsValid() const { return 0 != id && 0 != owner && 0 != bytes; }
};

struct RHIDeviceMemoryBudgetDecision
{
    RHIPersistentHeapBudget budget;
    uint64_t snapshotGeneration{ 0 };
    uint64_t effectiveUsageBytes{ 0 };
    uint64_t growthBudgetBytes{ 0 };
    uint64_t reservedGrowthBytes{ 0 };
    uint64_t committedSinceRefreshBytes{ 0 };
    bool memoryPressure{ false };
};

struct RHIDeviceMemoryPressureInfo
{
    bool memoryPressure{ false };
    uint32_t pressureDomains{ 0 };
    uint64_t targetReleaseBytes{ 0 };
};

struct RHIDeviceMemoryBudgetCoordinatorStats
{
    uint32_t registeredOwners{ 0 };
    uint32_t activeDomains{ 0 };
    uint64_t snapshotRefreshes{ 0 };
    uint64_t pressureEvents{ 0 };
    uint64_t growthRequests{ 0 };
    uint64_t growthGrants{ 0 };
    uint64_t growthDenials{ 0 };
    uint64_t growthCommits{ 0 };
    uint64_t growthCancels{ 0 };
    uint64_t reservedGrowthBytes{ 0 };
    uint64_t committedSinceRefreshBytes{ 0 };
    uint64_t peakUnrefreshedGrowthBytes{ 0 };
};

/// 한 물리 device의 여러 persistent allocator가 같은 memory-budget snapshot과
/// 성장 한도를 공유하게 하는 백엔드 중립 coordinator.
///
/// native budget 조회는 DeviceResources가 프레임당 한 번 수행해 UpdateBudget으로
/// 게시한다. allocator는 새 segment를 만들기 전에 ticket을 받아야 하며, 생성에
/// 성공하면 CommitGrowth, 실패하면 CancelGrowth를 호출한다. snapshot 이후 생성된
/// dedicated allocation도 RecordAllocation으로 합산되어 다른 allocator가 보지
/// 못하는 과예약을 막는다.
class RHIDeviceMemoryBudgetCoordinator
{
public:
    explicit RHIDeviceMemoryBudgetCoordinator(
        const RHIDeviceMemoryBudgetCoordinatorConfig& config = {});

    RHIDeviceMemoryBudgetCoordinator(const RHIDeviceMemoryBudgetCoordinator&) = delete;
    RHIDeviceMemoryBudgetCoordinator& operator=(
        const RHIDeviceMemoryBudgetCoordinator&) = delete;

    void Reset(const RHIDeviceMemoryBudgetCoordinatorConfig& config = {});

    RHIDeviceMemoryBudgetOwner RegisterOwner();
    void UnregisterOwner(RHIDeviceMemoryBudgetOwner owner);

    void UpdateBudget(RHIDeviceMemoryBudgetDomain domain,
        const RHIPersistentHeapBudget& budget);
    RHIDeviceMemoryBudgetDecision GetDecision(
        RHIDeviceMemoryBudgetDomain domain) const;
    RHIDeviceMemoryPressureInfo GetMemoryPressureInfo() const;

    RHIDeviceMemoryGrowthTicket TryReserveGrowth(
        RHIDeviceMemoryBudgetOwner owner,
        RHIDeviceMemoryBudgetDomain domain, uint64_t bytes);
    bool CommitGrowth(RHIDeviceMemoryGrowthTicket& ticket);
    bool CancelGrowth(RHIDeviceMemoryGrowthTicket& ticket);

    /// ticket을 사용하지 않는 dedicated/committed allocation의 snapshot 이후 증감.
    void RecordAllocation(RHIDeviceMemoryBudgetOwner owner,
        RHIDeviceMemoryBudgetDomain domain, uint64_t bytes);
    void RecordRelease(RHIDeviceMemoryBudgetOwner owner,
        RHIDeviceMemoryBudgetDomain domain, uint64_t bytes);

    RHIDeviceMemoryBudgetCoordinatorStats GetStats() const;

private:
    struct DomainState
    {
        RHIPersistentHeapBudget budget;
        uint64_t snapshotGeneration{ 0 };
        uint64_t growthBudgetBytes{ 0 };
        uint64_t reservedBytes{ 0 };
        uint64_t committedBytes{ 0 };
        bool memoryPressure{ false };
    };

    struct OwnerDomainState
    {
        uint64_t reservedBytes{ 0 };
        uint64_t committedBytes{ 0 };
    };

    struct OwnerState
    {
        std::unordered_map<RHIDeviceMemoryBudgetDomain, OwnerDomainState> domains;
    };

    static uint64_t SaturatingAdd(uint64_t left, uint64_t right);
    uint64_t ComputeGrowthBudget(const RHIPersistentHeapBudget& budget) const;
    RHIDeviceMemoryBudgetDecision GetDecisionLocked(
        RHIDeviceMemoryBudgetDomain domain) const;

    mutable std::mutex m_mutex;
    RHIDeviceMemoryBudgetCoordinatorConfig m_config;
    std::unordered_map<RHIDeviceMemoryBudgetDomain, DomainState> m_domains;
    std::unordered_map<RHIDeviceMemoryBudgetOwner, OwnerState> m_owners;
    std::unordered_map<uint64_t, RHIDeviceMemoryGrowthTicket> m_tickets;
    uint64_t m_nextTicketId{ 1 };
    RHIDeviceMemoryBudgetOwner m_nextOwnerId{ 1 };
    RHIDeviceMemoryBudgetCoordinatorStats m_stats;
};

bool RunRHIDeviceMemoryBudgetCoordinatorContractTest(std::string& outLog);
