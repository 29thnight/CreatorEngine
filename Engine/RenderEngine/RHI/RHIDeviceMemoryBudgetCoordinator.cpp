#include "RHIDeviceMemoryBudgetCoordinator.h"

#include <algorithm>
#include <limits>

RHIDeviceMemoryBudgetCoordinator::RHIDeviceMemoryBudgetCoordinator(
    const RHIDeviceMemoryBudgetCoordinatorConfig& config)
    : m_config(config)
{
}

void RHIDeviceMemoryBudgetCoordinator::Reset(
    const RHIDeviceMemoryBudgetCoordinatorConfig& config)
{
    std::lock_guard lock(m_mutex);
    m_config = config;
    m_domains.clear();
    m_owners.clear();
    m_tickets.clear();
    m_nextTicketId = 1;
    m_nextOwnerId = 1;
    m_stats = {};
}

uint64_t RHIDeviceMemoryBudgetCoordinator::SaturatingAdd(uint64_t left,
    uint64_t right)
{
    const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
    return right <= maximum - left ? left + right : maximum;
}

RHIDeviceMemoryBudgetOwner RHIDeviceMemoryBudgetCoordinator::RegisterOwner()
{
    std::lock_guard lock(m_mutex);
    RHIDeviceMemoryBudgetOwner owner = m_nextOwnerId++;
    if (0 == owner) owner = m_nextOwnerId++;
    m_owners.emplace(owner, OwnerState{});
    m_stats.registeredOwners = static_cast<uint32_t>(m_owners.size());
    return owner;
}

void RHIDeviceMemoryBudgetCoordinator::UnregisterOwner(
    RHIDeviceMemoryBudgetOwner owner)
{
    if (0 == owner) return;
    std::lock_guard lock(m_mutex);
    const auto foundOwner = m_owners.find(owner);
    if (foundOwner == m_owners.end()) return;

    for (auto ticket = m_tickets.begin(); ticket != m_tickets.end();)
    {
        if (ticket->second.owner != owner)
        {
            ++ticket;
            continue;
        }
        ++m_stats.growthCancels;
        ticket = m_tickets.erase(ticket);
    }

    for (const auto& pair : foundOwner->second.domains)
    {
        const auto domain = m_domains.find(pair.first);
        if (domain == m_domains.end()) continue;
        domain->second.reservedBytes -= (std::min)(domain->second.reservedBytes,
            pair.second.reservedBytes);
        domain->second.committedBytes -= (std::min)(domain->second.committedBytes,
            pair.second.committedBytes);
    }
    m_owners.erase(foundOwner);
    m_stats.registeredOwners = static_cast<uint32_t>(m_owners.size());
}

uint64_t RHIDeviceMemoryBudgetCoordinator::ComputeGrowthBudget(
    const RHIPersistentHeapBudget& budget) const
{
    if (!budget.IsValid()) return (std::numeric_limits<uint64_t>::max)();
    const uint64_t headroom = budget.budgetBytes > budget.usageBytes
        ? budget.budgetBytes - budget.usageBytes : 0;
    if (0 == headroom) return 0;

    const uint32_t divisor = 0 == m_config.headroomDivisor
        ? 1 : m_config.headroomDivisor;
    uint64_t growth = headroom / divisor;
    if (headroom >= m_config.minimumGrowthBytes)
        growth = (std::max)(growth, m_config.minimumGrowthBytes);
    if (0 != m_config.maximumGrowthBytes)
        growth = (std::min)(growth, m_config.maximumGrowthBytes);
    return (std::min)(growth, headroom);
}

void RHIDeviceMemoryBudgetCoordinator::UpdateBudget(
    RHIDeviceMemoryBudgetDomain domainKey,
    const RHIPersistentHeapBudget& budget)
{
    std::lock_guard lock(m_mutex);
    DomainState& domain = m_domains[domainKey];
    const bool wasPressure = domain.memoryPressure;
    domain.budget = budget;
    ++domain.snapshotGeneration;
    domain.growthBudgetBytes = ComputeGrowthBudget(budget);
    domain.committedBytes = 0;

    for (auto& owner : m_owners)
    {
        const auto found = owner.second.domains.find(domainKey);
        if (found != owner.second.domains.end()) found->second.committedBytes = 0;
    }

    // heap-size estimate는 사용량이 실제 값이 아니므로 pressure 판정에 쓰지 않는다.
    if (!budget.IsValid() || budget.estimated)
    {
        domain.memoryPressure = false;
    }
    else
    {
        const uint64_t effectiveUsage = SaturatingAdd(budget.usageBytes,
            domain.reservedBytes);
        const uint64_t releaseThreshold = budget.budgetBytes - budget.budgetBytes / 5;
        const uint64_t enterThreshold = budget.budgetBytes - budget.budgetBytes / 10;
        domain.memoryPressure = wasPressure
            ? effectiveUsage > releaseThreshold
            : effectiveUsage >= enterThreshold;
    }
    if (!wasPressure && domain.memoryPressure) ++m_stats.pressureEvents;

    ++m_stats.snapshotRefreshes;
    m_stats.activeDomains = static_cast<uint32_t>(m_domains.size());
}

RHIDeviceMemoryBudgetDecision
RHIDeviceMemoryBudgetCoordinator::GetDecisionLocked(
    RHIDeviceMemoryBudgetDomain domainKey) const
{
    RHIDeviceMemoryBudgetDecision decision{};
    const auto found = m_domains.find(domainKey);
    if (found == m_domains.end()) return decision;
    const DomainState& domain = found->second;
    decision.budget = domain.budget;
    decision.snapshotGeneration = domain.snapshotGeneration;
    decision.growthBudgetBytes = domain.growthBudgetBytes;
    decision.reservedGrowthBytes = domain.reservedBytes;
    decision.committedSinceRefreshBytes = domain.committedBytes;
    decision.effectiveUsageBytes = SaturatingAdd(domain.budget.usageBytes,
        SaturatingAdd(domain.reservedBytes, domain.committedBytes));
    decision.memoryPressure = domain.memoryPressure;
    return decision;
}

RHIDeviceMemoryBudgetDecision RHIDeviceMemoryBudgetCoordinator::GetDecision(
    RHIDeviceMemoryBudgetDomain domain) const
{
    std::lock_guard lock(m_mutex);
    return GetDecisionLocked(domain);
}

RHIDeviceMemoryPressureInfo
RHIDeviceMemoryBudgetCoordinator::GetMemoryPressureInfo() const
{
    std::lock_guard lock(m_mutex);
    RHIDeviceMemoryPressureInfo result{};
    for (const auto& pair : m_domains)
    {
        const DomainState& domain = pair.second;
        if (!domain.memoryPressure || !domain.budget.IsValid() ||
            domain.budget.estimated)
            continue;

        result.memoryPressure = true;
        ++result.pressureDomains;
        const uint64_t releaseThreshold = domain.budget.budgetBytes -
            domain.budget.budgetBytes / 5;
        const uint64_t effectiveUsage = SaturatingAdd(domain.budget.usageBytes,
            SaturatingAdd(domain.reservedBytes, domain.committedBytes));
        if (effectiveUsage > releaseThreshold)
        {
            result.targetReleaseBytes = SaturatingAdd(result.targetReleaseBytes,
                effectiveUsage - releaseThreshold);
        }
    }
    return result;
}

RHIDeviceMemoryGrowthTicket RHIDeviceMemoryBudgetCoordinator::TryReserveGrowth(
    RHIDeviceMemoryBudgetOwner owner, RHIDeviceMemoryBudgetDomain domainKey,
    uint64_t bytes)
{
    if (0 == owner || 0 == bytes) return {};
    std::lock_guard lock(m_mutex);
    ++m_stats.growthRequests;
    const auto foundOwner = m_owners.find(owner);
    if (foundOwner == m_owners.end())
    {
        ++m_stats.growthDenials;
        return {};
    }

    DomainState& domain = m_domains[domainKey];
    const uint64_t growthUsed = SaturatingAdd(domain.reservedBytes,
        domain.committedBytes);
    bool allowed = !domain.memoryPressure;
    if (allowed && domain.budget.IsValid())
    {
        allowed = bytes <= domain.growthBudgetBytes &&
            growthUsed <= domain.growthBudgetBytes - bytes;

        if (allowed && !domain.budget.estimated)
        {
            const uint64_t enterThreshold = domain.budget.budgetBytes -
                domain.budget.budgetBytes / 10;
            const uint64_t after = SaturatingAdd(domain.budget.usageBytes,
                SaturatingAdd(growthUsed, bytes));
            allowed = after < enterThreshold;
        }
    }
    if (!allowed)
    {
        ++m_stats.growthDenials;
        return {};
    }

    uint64_t id = m_nextTicketId++;
    if (0 == id) id = m_nextTicketId++;
    RHIDeviceMemoryGrowthTicket ticket{ id, owner, domainKey, bytes };
    m_tickets.emplace(id, ticket);
    domain.reservedBytes = SaturatingAdd(domain.reservedBytes, bytes);
    foundOwner->second.domains[domainKey].reservedBytes = SaturatingAdd(
        foundOwner->second.domains[domainKey].reservedBytes, bytes);
    ++m_stats.growthGrants;
    const uint64_t unrefreshed = SaturatingAdd(domain.reservedBytes,
        domain.committedBytes);
    m_stats.peakUnrefreshedGrowthBytes = (std::max)(
        m_stats.peakUnrefreshedGrowthBytes, unrefreshed);
    return ticket;
}

bool RHIDeviceMemoryBudgetCoordinator::CommitGrowth(
    RHIDeviceMemoryGrowthTicket& ticket)
{
    if (!ticket.IsValid()) return false;
    std::lock_guard lock(m_mutex);
    const auto found = m_tickets.find(ticket.id);
    if (found == m_tickets.end() || found->second.owner != ticket.owner ||
        found->second.domain != ticket.domain || found->second.bytes != ticket.bytes)
        return false;

    DomainState& domain = m_domains[ticket.domain];
    OwnerDomainState& owner = m_owners[ticket.owner].domains[ticket.domain];
    domain.reservedBytes -= (std::min)(domain.reservedBytes, ticket.bytes);
    owner.reservedBytes -= (std::min)(owner.reservedBytes, ticket.bytes);
    domain.committedBytes = SaturatingAdd(domain.committedBytes, ticket.bytes);
    owner.committedBytes = SaturatingAdd(owner.committedBytes, ticket.bytes);
    m_tickets.erase(found);
    ticket = {};
    ++m_stats.growthCommits;
    return true;
}

bool RHIDeviceMemoryBudgetCoordinator::CancelGrowth(
    RHIDeviceMemoryGrowthTicket& ticket)
{
    if (!ticket.IsValid()) return false;
    std::lock_guard lock(m_mutex);
    const auto found = m_tickets.find(ticket.id);
    if (found == m_tickets.end() || found->second.owner != ticket.owner ||
        found->second.domain != ticket.domain || found->second.bytes != ticket.bytes)
        return false;

    DomainState& domain = m_domains[ticket.domain];
    OwnerDomainState& owner = m_owners[ticket.owner].domains[ticket.domain];
    domain.reservedBytes -= (std::min)(domain.reservedBytes, ticket.bytes);
    owner.reservedBytes -= (std::min)(owner.reservedBytes, ticket.bytes);
    m_tickets.erase(found);
    ticket = {};
    ++m_stats.growthCancels;
    return true;
}

void RHIDeviceMemoryBudgetCoordinator::RecordAllocation(
    RHIDeviceMemoryBudgetOwner ownerKey, RHIDeviceMemoryBudgetDomain domainKey,
    uint64_t bytes)
{
    if (0 == ownerKey || 0 == bytes) return;
    std::lock_guard lock(m_mutex);
    const auto foundOwner = m_owners.find(ownerKey);
    if (foundOwner == m_owners.end()) return;
    DomainState& domain = m_domains[domainKey];
    OwnerDomainState& owner = foundOwner->second.domains[domainKey];
    domain.committedBytes = SaturatingAdd(domain.committedBytes, bytes);
    owner.committedBytes = SaturatingAdd(owner.committedBytes, bytes);
    const uint64_t unrefreshed = SaturatingAdd(domain.reservedBytes,
        domain.committedBytes);
    m_stats.peakUnrefreshedGrowthBytes = (std::max)(
        m_stats.peakUnrefreshedGrowthBytes, unrefreshed);
}

void RHIDeviceMemoryBudgetCoordinator::RecordRelease(
    RHIDeviceMemoryBudgetOwner ownerKey, RHIDeviceMemoryBudgetDomain domainKey,
    uint64_t bytes)
{
    if (0 == ownerKey || 0 == bytes) return;
    std::lock_guard lock(m_mutex);
    const auto foundOwner = m_owners.find(ownerKey);
    const auto foundDomain = m_domains.find(domainKey);
    if (foundOwner == m_owners.end() || foundDomain == m_domains.end()) return;
    OwnerDomainState& owner = foundOwner->second.domains[domainKey];
    const uint64_t released = (std::min)(owner.committedBytes, bytes);
    owner.committedBytes -= released;
    foundDomain->second.committedBytes -= (std::min)(
        foundDomain->second.committedBytes, released);
}

RHIDeviceMemoryBudgetCoordinatorStats
RHIDeviceMemoryBudgetCoordinator::GetStats() const
{
    std::lock_guard lock(m_mutex);
    RHIDeviceMemoryBudgetCoordinatorStats result = m_stats;
    result.registeredOwners = static_cast<uint32_t>(m_owners.size());
    result.activeDomains = static_cast<uint32_t>(m_domains.size());
    result.reservedGrowthBytes = 0;
    result.committedSinceRefreshBytes = 0;
    for (const auto& domain : m_domains)
    {
        result.reservedGrowthBytes = SaturatingAdd(result.reservedGrowthBytes,
            domain.second.reservedBytes);
        result.committedSinceRefreshBytes = SaturatingAdd(
            result.committedSinceRefreshBytes, domain.second.committedBytes);
    }
    return result;
}

bool RunRHIDeviceMemoryBudgetCoordinatorContractTest(std::string& outLog)
{
    RHIDeviceMemoryBudgetCoordinatorConfig config{};
    config.minimumGrowthBytes = 1024;
    config.maximumGrowthBytes = 8192;
    config.headroomDivisor = 2;
    RHIDeviceMemoryBudgetCoordinator coordinator(config);
    const auto mesh = coordinator.RegisterOwner();
    const auto texture = coordinator.RegisterOwner();
    coordinator.UpdateBudget(0, RHIPersistentHeapBudget{ 1000, 10000, false });

    auto meshTicket = coordinator.TryReserveGrowth(mesh, 0, 2048);
    auto textureTicket = coordinator.TryReserveGrowth(texture, 0, 2048);
    auto denied = coordinator.TryReserveGrowth(texture, 0, 1024);
    const bool aggregateGate = meshTicket.IsValid() && textureTicket.IsValid() &&
        !denied.IsValid() && coordinator.CommitGrowth(meshTicket) &&
        coordinator.CancelGrowth(textureTicket);

    auto replacement = coordinator.TryReserveGrowth(texture, 0, 2048);
    const bool returnedTicket = replacement.IsValid() &&
        coordinator.CommitGrowth(replacement);
    coordinator.RecordAllocation(texture, 0, 256);
    coordinator.RecordRelease(texture, 0, 256);

    coordinator.UpdateBudget(0, RHIPersistentHeapBudget{ 9500, 10000, false });
    const bool entered = coordinator.GetDecision(0).memoryPressure;
    const RHIDeviceMemoryPressureInfo pressureInfo =
        coordinator.GetMemoryPressureInfo();
    const bool releaseTarget = pressureInfo.memoryPressure &&
        1 == pressureInfo.pressureDomains &&
        1500 == pressureInfo.targetReleaseBytes;
    coordinator.UpdateBudget(0, RHIPersistentHeapBudget{ 8100, 10000, false });
    const bool held = coordinator.GetDecision(0).memoryPressure;
    coordinator.UpdateBudget(0, RHIPersistentHeapBudget{ 7900, 10000, false });
    const bool released = !coordinator.GetDecision(0).memoryPressure;
    coordinator.UpdateBudget(0, RHIPersistentHeapBudget{ 9500, 10000, true });
    const bool estimateIgnored = !coordinator.GetDecision(0).memoryPressure;

    const auto stats = coordinator.GetStats();
    const bool telemetry = 2 == stats.registeredOwners &&
        5 == stats.snapshotRefreshes && 4 == stats.growthRequests &&
        3 == stats.growthGrants && 1 == stats.growthDenials &&
        2 == stats.growthCommits && 1 == stats.growthCancels &&
        1 == stats.pressureEvents && 0 == stats.reservedGrowthBytes;
    coordinator.UnregisterOwner(mesh);
    coordinator.UnregisterOwner(texture);

    const bool passed = aggregateGate && returnedTicket && entered && held &&
        releaseTarget && released && estimateIgnored && telemetry &&
        0 == coordinator.GetStats().registeredOwners;
    outLog += passed
        ? "[공통 device budget] 단일 snapshot·owner 합산 ticket·commit/cancel·90/80 hysteresis·회수 target·estimate 제외 검증 통과\n"
        : "[공통 device budget] coordinator contract 검증 실패\n";
    return passed;
}
