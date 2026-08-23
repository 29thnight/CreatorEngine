#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

#include "../RHIResourceTypes.h"

class DX12ResourceTable;

/// 완료점 기반 transient upload segment allocator.
///
/// 파일명은 기존 프로젝트 항목과의 이행 호환 때문에 유지하지만, 수명 모델은
/// 더 이상 frame-index ring이 아니다. 세그먼트는 recording에 귀속되고 제출
/// 완료값이 지난 뒤에만 Available로 돌아온다.
/// ReserveBatch는 같은 recording의 worker들이 병렬 호출할 수 있다. 반면
/// BeginRecording/OnSubmitted/AbortRecording은 worker join 뒤 owner thread가
/// 호출하는 recording 경계다.
class DX12UploadSegmentAllocator
{
public:
    static constexpr uint64_t kConstantBufferAlignment =
        D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    static constexpr uint64_t kTexturePlacementAlignment =
        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;

    /// backend 성능/회귀 검사만 쓰는 네이티브 관측값. 패스와 캐시는
    /// RHIBufferSlice 계약을 사용한다.
    struct Allocation
    {
        void* cpuAddress{ nullptr };
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress{ 0 };
        ID3D12Resource* resource{ nullptr };
        uint64_t offset{ 0 };
        uint64_t size{ 0 };
        uint32_t segment{ 0 };
        bool IsValid() const { return nullptr != cpuAddress; }
    };

    bool Initialize(ID3D12Device* device, DX12ResourceTable& table,
        const RHIUploadSegmentPolicy& policy, std::string& outError);
    void Shutdown();

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
    uint64_t GetRegularSegmentBytes() const { return m_regularSegmentBytes; }
    void UpdateBudget(uint64_t softBudgetBytes, bool memoryPressure);
    void SetBudgetForTesting(uint64_t softBudgetBytes,
        uint64_t largeCacheBudgetBytes, bool memoryPressure);
    void ClearBudgetOverrideForTesting();
    Allocation Allocate(uint64_t bytes, uint64_t alignment);
    uint64_t GetBytesPerFrame() const { return m_regularSegmentBytes; }
    uint32_t GetFrameCount() const { return 3; }
    uint64_t GetFrameUsedBytes() const { return GetRecordingUsedBytes(); }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Segment
    {
        ComPtr<ID3D12Resource> buffer;
        uint8_t* mapped{ nullptr };
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

    ID3D12Device* m_device{ nullptr };
    DX12ResourceTable* m_table{ nullptr };
    std::vector<std::unique_ptr<Segment>> m_segments;
    uint64_t m_regularSegmentBytes{ 0 };
    uint64_t m_largeThreshold{ 0 };
    uint32_t m_standbyRegularSegments{ 0 };
    uint32_t m_trimDelayCollects{ 0 };
    uint64_t m_defaultLargeCacheBudgetBytes{ 0 };
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
    mutable std::mutex m_mutex;
};

