#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "VulkanLoader.h"
#include "VulkanResourceTable.h"
#include "../RHIDescriptorVersionPolicy.h"
#include "../RHIResourceTypes.h"

#include <atomic>
#include <string>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

// transient 업로드 세그먼트 allocator와 descriptor pool recycler.
//
// ── 왜 한 파일인가 ──
//
// 둘 다 GPU가 읽는 메모리를 재활용하므로 완료점 확인이 필요하지만 수명 단위는
// 다르다. 둘 다 recording/completion 수명을 쓰되 upload는 byte segment,
// descriptor는 pool 전체를 한 version으로 재활용한다.
//
// DX12 쪽은 업로드 세그먼트와 `DX12DescriptorRecycler` 로 갈려 있는데, 그것은
// 저쪽에서 둘이 **다른 종류의 메모리**(업로드 힙 / 디스크립터 힙)라서다.
// Vulkan 에서는 하나가 `VkBuffer` 이고 하나가 `VkDescriptorPool` 이라 더
// 다르다.

/// 완료점 기반 transient 업로드 세그먼트 allocator — `RHIBufferSlice` 생산자.
///
/// ★ 호출부는 `RHIUploadUsage`로 의미를 넘기고 backend가 실제 정렬을 정한다.
///   `minimumAlignment`는 소비자가 추가로 요구하는 하한일 뿐이다.
///
///   Vulkan 은 다르다: `minUniformBufferOffsetAlignment` 는 **디바이스 속성**
///   이고 기계마다 갈린다(16 ~ 256). 그래서 백엔드가 호출부의 값을 **넓힌다**
///   — 좁히지 않는다. 계약은 안 바뀌고("이만큼은 맞춰 달라"), 실제 값은
///   디바이스가 요구하는 것과의 최댓값이 된다.
///
///   ★ 넓히지 않고 그대로 쓰면 조용히 틀린다. 검증 레이어가 잡아 주기는
///     하지만 그것은 이 기계 이야기이고, 정렬이 더 큰 기계에서 처음 터진다.
class VulkanUploadSegmentAllocator
{
public:
    /// ReserveBatch는 같은 recording의 worker들이 병렬 호출할 수 있다.
    /// BeginRecording/OnSubmitted/AbortRecording은 worker join 뒤 owner thread가
    /// 호출하는 recording 경계다.
    bool Initialize(VkDevice device, VkPhysicalDevice physicalDevice,
        VulkanResourceTable& table, const RHIUploadSegmentPolicy& policy,
        std::string& outError);

    void Shutdown(VkDevice device);

    void Collect(uint64_t completedValue);
    void BeginRecording(uint64_t recordingId);
    bool ReserveBatch(uint64_t recordingId,
        std::span<const RHIUploadRequest> requests,
        std::span<RHIBufferSlice> outSlices,
        std::string& outError);
    void OnSubmitted(uint64_t recordingId, RHICompletionPoint completion);
    void AbortRecording(uint64_t recordingId);

    RHIUploadStats GetStats() const;
    uint64_t GetRecordingUsedBytes() const;
    uint32_t GetMemoryHeapIndex() const { return m_memoryHeapIndex; }
    void UpdateBudget(uint64_t softBudgetBytes, bool memoryPressure);
    void SetBudgetForTesting(uint64_t softBudgetBytes,
        uint64_t largeCacheBudgetBytes, bool memoryPressure);
    void ClearBudgetOverrideForTesting();
    uint64_t GetRequiredAlignmentForTesting(RHIUploadUsage usage) const
    {
        return RequiredAlignment(RHIUploadRequest{ 1, usage, 1 });
    }

private:
    struct Segment
    {
        VkBuffer        buffer{ VK_NULL_HANDLE };
        VkDeviceMemory  memory{ VK_NULL_HANDLE };
        void*           mapped{ nullptr };
        RHIBufferHandle handle;
        uint64_t capacity{ 0 };
        std::atomic<uint64_t> cursor{ 0 };
        uint64_t recordingId{ 0 };
        uint64_t completionValue{ 0 };
        uint64_t lastCollectedEpoch{ 0 };
        bool large{ false };
        RHIUploadSegmentState state{ RHIUploadSegmentState::Available };
    };

    uint64_t RequiredAlignment(const RHIUploadRequest& request) const;
    bool TryPack(uint64_t start, uint64_t capacity,
        std::span<const RHIUploadRequest> requests, uint64_t& outEnd) const;
    bool TryReserveAtomic(Segment& segment,
        std::span<const RHIUploadRequest> requests,
        std::span<RHIBufferSlice> outSlices, bool fastPath);
    Segment* CreateSegment(uint64_t bytes, bool large, std::string& outError);
    Segment* FindAvailableSegmentLocked(bool large, uint64_t minimumBytes);
    uint64_t SegmentBytesLocked() const;
    void ReleaseSegmentLocked(Segment& segment);
    void TrimAvailableLocked(bool memoryPressure, uint64_t bytesNeeded);
    void EnsureBudgetForCreateLocked(uint64_t bytesNeeded);
    void UpdatePeakRecordingBytes(uint64_t value);

    VkDevice m_device{ VK_NULL_HANDLE };
    VulkanResourceTable* m_table{ nullptr };
    VkPhysicalDeviceMemoryProperties m_memoryProperties{};
    std::vector<std::unique_ptr<Segment>> m_segments;
    uint64_t m_regularSegmentBytes{ 0 };
    uint64_t m_largeThreshold{ 0 };
    uint32_t m_standbyRegularSegments{ 0 };
    uint32_t m_trimDelayCollects{ 0 };
    uint64_t m_defaultLargeCacheBudgetBytes{ 0 };
    uint32_t m_memoryHeapIndex{ UINT32_MAX };
    uint64_t m_collectEpoch{ 0 };
    std::atomic<Segment*> m_fastRegular{ nullptr };
    std::atomic<uint64_t> m_currentRecordingId{ 0 };
    std::atomic<uint64_t> m_recordingBytes{ 0 };
    std::atomic<uint64_t> m_peakRecordingBytes{ 0 };
    std::atomic<uint64_t> m_slowPathCreates{ 0 };
    std::atomic<uint64_t> m_reuses{ 0 };
    std::atomic<uint64_t> m_fastPathReservations{ 0 };
    std::atomic<uint64_t> m_slowPathReservations{ 0 };
    std::atomic<uint64_t> m_casRetries{ 0 };
    std::atomic<uint64_t> m_workerSegmentCreates{ 0 };
    std::atomic<uint64_t> m_tailWasteBytes{ 0 };
    std::atomic<uint64_t> m_reclaimLag{ 0 };
    std::atomic<uint64_t> m_batchRollbacks{ 0 };
    std::atomic<uint64_t> m_oomFailures{ 0 };
    std::atomic<uint64_t> m_allocations{ 0 };
    std::atomic<uint64_t> m_bytesAllocated{ 0 };
    std::atomic<uint64_t> m_softBudgetBytes{ 0 };
    std::atomic<uint64_t> m_largeCacheBudgetBytes{ 0 };
    std::atomic<bool> m_memoryPressure{ false };
    std::atomic<bool> m_budgetOverrideForTesting{ false };
    std::atomic<uint64_t> m_trimmedSegments{ 0 };
    std::atomic<uint64_t> m_trimmedBytes{ 0 };
    std::atomic<uint64_t> m_budgetPressureEvents{ 0 };
    std::atomic<uint64_t> m_budgetRetries{ 0 };
    std::atomic<uint64_t> m_budgetOvercommits{ 0 };
    std::thread::id m_ownerThread;

    uint64_t m_uniformAlignment{ 1 };
    uint64_t m_storageAlignment{ 1 };
    uint64_t m_textureCopyAlignment{ 1 };
    mutable std::mutex m_mutex;
};

struct VulkanDescriptorRecyclerStats
{
    RHIDescriptorVersionStats versions{};
    uint64_t allocations{ 0 };
    uint64_t allocationFailures{ 0 };
    uint32_t peakRecordingSets{ 0 };
};

/// recording 버전별 디스크립터 풀 recycler.
///
/// ★ **DX12의 page recycler와 네이티브 모델이 다르다.** 저쪽은 recording별
///   힙 하나에서 연속 구간을 잘라
///   `D3D12_GPU_DESCRIPTOR_HANDLE` 하나로 가리킨다 — 자를 때 "몇 개"만 알면
///   되고 종류를 몰라도 된다.
///
///   Vulkan 은 풀을 **만들 때** 종류별 예산을 요구하고, 잘라 오는 단위가
///   `VkDescriptorSet` 이며 그 셋은 **어떤 셋 레이아웃의 것인지**를 알아야
///   할당된다. V8-b 골격이 그 비대칭을 실측으로 적어 두었다(그 골격은 5
///   마무리로 갔지만 실측은 남는다): "몇 개를 어느 종류로 쓸 것인가를
///   Vulkan 은 풀을 만들 때, DX12 는 자를 때 안다."
///
///   그래서 예산이 여기 상수로 박힌다. 넘치면 조용히 넘어가지 않고 무효
///   핸들을 주며, 인코더가 그것을 계수로 남긴다.
class VulkanDescriptorPoolRecycler
{
public:
    bool Initialize(VkDevice device, uint32_t initialVersions, std::string& outError);
    void Shutdown(VkDevice device);

    void Collect(RHICompletionPoint completed);
    bool BeginRecording(VkDevice device, uint64_t recordingId,
        std::string& outError);
    void OnSubmitted(uint64_t recordingId, RHICompletionPoint completion);
    void AbortRecording(uint64_t recordingId);

    /// 셋 하나를 잘라 온다. 예산이 다하면 `VK_NULL_HANDLE`.
    VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout setLayout);
    VulkanDescriptorRecyclerStats GetStats() const;

private:
    bool EnsurePool(VkDevice device, uint32_t slot, std::string& outError);
    void RecordPeak(uint32_t used);

    std::vector<VkDescriptorPool> m_pools;
    RHIDescriptorVersionPolicy m_versions;
    RHIDescriptorVersionHandle m_activeVersion{};
    VkDescriptorPool m_activePool{ VK_NULL_HANDLE };
    uint32_t m_recordingSets{ 0 };
    uint64_t m_allocations{ 0 };
    uint64_t m_allocationFailures{ 0 };
    uint32_t m_peakRecordingSets{ 0 };
    std::mutex m_allocationMutex;
};

#endif
