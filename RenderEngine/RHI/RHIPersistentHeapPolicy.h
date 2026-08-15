#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/// Persistent GPU buffer heap의 백엔드 중립 free-list 정책.
///
/// 네이티브 heap/memory 생성과 resource object 파괴는 DX12/Vulkan adapter가 맡고,
/// 이 타입은 compatibility key별 segment와 offset 범위만 관리한다. 따라서 이
/// 헤더에는 D3D12/Vulkan 타입이 들어가지 않는다.
struct RHIPersistentHeapConfig
{
    uint64_t defaultSegmentBytes{ 64ull * 1024ull * 1024ull };
    uint64_t dedicatedThresholdBytes{ 32ull * 1024ull * 1024ull };
    uint32_t standbySegmentCountPerKey{ 1 };
};

/// 백엔드가 읽은 현재 device-local memory heap 상태.
/// DX12는 DXGI LOCAL segment, Vulkan은 memory type이 속한 heap을 채운다.
struct RHIPersistentHeapBudget
{
    uint64_t usageBytes{ 0 };
    uint64_t budgetBytes{ 0 };
    bool estimated{ false };

    bool IsValid() const { return 0 != budgetBytes; }
};

struct RHIPersistentHeapBudgetDecision
{
    uint64_t softBudgetBytes{ 0 };
    bool memoryPressure{ false };
};

struct RHIPersistentHeapSegmentHandle
{
    static constexpr uint32_t kInvalidSlot =
        (std::numeric_limits<uint32_t>::max)();

    uint32_t slot{ kInvalidSlot };
    uint32_t generation{ 0 };

    bool IsValid() const { return kInvalidSlot != slot && 0 != generation; }
    friend bool operator==(const RHIPersistentHeapSegmentHandle&,
        const RHIPersistentHeapSegmentHandle&) = default;
};

struct RHIPersistentHeapAllocation
{
    RHIPersistentHeapSegmentHandle segment;
    uint64_t compatibilityKey{ 0 };
    uint64_t offset{ 0 };
    uint64_t size{ 0 };
    uint64_t alignment{ 1 };

    /// DX12 adapter는 true일 때 첫 접근 전에 conservative aliasing barrier를
    /// 기록한다. 새 segment의 첫 allocation만 false다.
    bool requiresAliasingBarrier{ false };

    bool IsValid() const { return segment.IsValid() && 0 != size; }
};

struct RHIPersistentHeapStats
{
    uint32_t activeSegments{ 0 };
    uint32_t emptySegments{ 0 };
    uint64_t segmentBytes{ 0 };
    uint64_t allocatedBytes{ 0 };
    uint64_t freeBytes{ 0 };
    uint32_t livePooledAllocations{ 0 };
    uint32_t liveDedicatedAllocations{ 0 };

    uint64_t pooledAllocations{ 0 };
    uint64_t dedicatedAllocations{ 0 };
    uint64_t dedicatedBytes{ 0 };
    uint64_t dedicatedFallbacks{ 0 };
    uint64_t allocationFailures{ 0 };
    uint64_t segmentCreates{ 0 };
    uint64_t segmentSlotReuses{ 0 };
    uint64_t blockReuses{ 0 };
    uint64_t coalesces{ 0 };
    uint64_t trimmedSegments{ 0 };
    uint64_t trimmedBytes{ 0 };

    uint64_t budgetUsageBytes{ 0 };
    uint64_t budgetBytes{ 0 };
    uint64_t softBudgetBytes{ 0 };
    uint64_t budgetRefreshes{ 0 };
    uint64_t budgetPressureEvents{ 0 };
    bool budgetEstimated{ false };
    bool memoryPressure{ false };
};

class RHIPersistentHeapPolicy
{
public:
    explicit RHIPersistentHeapPolicy(
        const RHIPersistentHeapConfig& config = RHIPersistentHeapConfig{});

    RHIPersistentHeapPolicy(const RHIPersistentHeapPolicy&) = delete;
    RHIPersistentHeapPolicy& operator=(const RHIPersistentHeapPolicy&) = delete;

    void Reset(const RHIPersistentHeapConfig& config = RHIPersistentHeapConfig{});

    RHIPersistentHeapSegmentHandle AddSegment(uint64_t compatibilityKey,
        uint64_t bytes);
    RHIPersistentHeapAllocation Allocate(uint64_t compatibilityKey,
        uint64_t bytes, uint64_t alignment);
    bool Release(const RHIPersistentHeapAllocation& allocation);

    /// 완전히 빈 segment를 compatibility key마다 keepPerKey개만 남기고 제거한다.
    /// 반환 handle의 네이티브 heap/memory는 adapter가 이 호출 뒤 파괴한다.
    std::vector<RHIPersistentHeapSegmentHandle> TrimEmptySegments(uint32_t keepPerKey);

    bool ShouldUseDedicated(uint64_t requiredBytes) const;
    uint64_t ChooseSegmentBytes(uint64_t requiredBytes, uint64_t alignment) const;
    RHIPersistentHeapConfig GetConfig() const;
    RHIPersistentHeapStats GetStats() const;

    /// 실제 backend budget을 allocator가 차지해도 되는 성장 예산으로 바꾼다.
    /// pressure 진입/해제에는 90%/80% hysteresis를 둔다.
    RHIPersistentHeapBudgetDecision UpdateBudget(
        const RHIPersistentHeapBudget& budget);

    void RecordDedicatedAllocation(uint64_t bytes, bool fallback);
    void RecordDedicatedRelease();
    void RecordAllocationFailure();

private:
    struct Segment
    {
        uint64_t compatibilityKey{ 0 };
        uint64_t bytes{ 0 };
        uint32_t generation{ 1 };
        bool alive{ false };
        bool everAllocated{ false };
        std::map<uint64_t, uint64_t> freeByOffset;
        std::multimap<uint64_t, uint64_t> freeBySize;
        std::map<uint64_t, uint64_t> allocatedByOffset;
    };

    static bool AlignUp(uint64_t value, uint64_t alignment, uint64_t& outAligned);
    static void AddFreeBlock(Segment& segment, uint64_t offset, uint64_t bytes);
    static bool RemoveFreeBlock(Segment& segment, uint64_t offset, uint64_t bytes);
    bool IsLiveHandle(const RHIPersistentHeapSegmentHandle& handle) const;

    mutable std::mutex m_mutex;
    RHIPersistentHeapConfig m_config;
    std::vector<Segment> m_segments;
    std::vector<uint32_t> m_freeSegmentSlots;
    RHIPersistentHeapStats m_stats;
};

/// 네이티브 API 없이 address/size index, 정렬 padding, 병합, trim, generation을
/// 검증한다. DX12/Vulkan adapter 검증 전에 한 번 실행한다.
bool RunRHIPersistentHeapPolicyContractTest(std::string& outLog);
