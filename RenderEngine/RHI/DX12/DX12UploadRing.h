#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <atomic>
#include <cstdint>
#include <string>
#include <wrl/client.h>
#include <d3d12.h>

// 프레임 업로드 링버퍼 (PHASE 3-3).
//
// DX12에서는 CPU가 채운 데이터를 GPU에 올리려면 업로드 힙을 거쳐야 한다.
// 브링업 단계에서는 텍스처 하나마다 CreateCommittedResource로 업로드 버퍼를
// 만들어 썼는데, 씬을 이식하면 프레임마다 상수·정점·스테이징이 수십~수백 건이
// 되므로 그 방식은 두 가지로 무너진다: 생성 비용 자체가 비싸고, GPU가 다 읽을
// 때까지 각 버퍼를 살려 둬야 해서 수명 관리가 호출부마다 붙는다.
//
// 큰 버퍼 하나를 만들어 두고 프레임 구간으로 잘라 쓴다.
//
// 반납 규칙은 펜스가 아니라 구조가 보장한다. 링을 인플라이트 프레임 수만큼의
// 고정 구간으로 나누고, 프레임 i는 구간 i만 쓴다. DX12DeviceResources::BeginFrame이
// 이미 그 슬롯의 펜스를 기다린 뒤에 진입하므로, 구간 i에 다시 손대는 시점에는
// GPU가 그 구간을 다 읽은 것이 보장된다. 별도 추적이 필요 없다.
//
// 정렬 규약: 상수 버퍼는 256바이트, 텍스처 배치는 512바이트가 필요하다.
// 호출부가 용도에 맞는 값을 넘긴다 — 기본값을 하나로 두면 둘 중 하나가 조용히
// 틀린다(텍스처 쪽은 CopyTextureRegion이 실패하거나 검증 레이어가 잡는다).
class DX12UploadRing
{
public:
    static constexpr uint64_t kConstantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; // 256
    static constexpr uint64_t kTexturePlacementAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;       // 512

    // 링에서 잘라낸 한 조각.
    //
    // cpuAddress에 쓰고, 복사 원본으로는 resource + offset을 쓴다.
    // 링 전체가 계속 Map된 상태라 조각마다 Map/Unmap하지 않는다.
    struct Allocation
    {
        void*                     cpuAddress{ nullptr };
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress{ 0 };
        ID3D12Resource*           resource{ nullptr };
        uint64_t                  offset{ 0 };
        uint64_t                  size{ 0 };

        bool IsValid() const { return nullptr != cpuAddress; }
    };

    struct Stats
    {
        uint64_t allocations{ 0 };
        uint64_t bytesAllocated{ 0 };
        uint64_t overflows{ 0 };     // 구간이 모자라 거절한 횟수 — 0이 아니면 크기를 늘려야 한다
        uint64_t peakFrameBytes{ 0 };// 한 프레임이 쓴 최대치. 구간 크기를 정하는 근거가 된다
    };

    // 통계는 원자적으로 센다. 커맨드 기록이 병렬로 돌면 여러 스레드가 같은
    // 링에서 잘라 가고, 그때 통계가 어긋나면 '구간을 얼마나 썼는가'라는
    // 유일한 근거를 잃는다.
    struct AtomicStats
    {
        std::atomic<uint64_t> allocations{ 0 };
        std::atomic<uint64_t> bytesAllocated{ 0 };
        std::atomic<uint64_t> overflows{ 0 };
        std::atomic<uint64_t> peakFrameBytes{ 0 };
    };

    bool Initialize(ID3D12Device* device, uint64_t bytesPerFrame, uint32_t frameCount,
        std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_buffer.Get(); }

    // 프레임 시작에 그 프레임 구간의 커서를 되감는다.
    // DX12DeviceResources::BeginFrame이 펜스를 기다린 뒤에 부르는 것이 계약이다.
    void BeginFrame(uint32_t frameIndex);

    // 현재 프레임 구간에서 잘라낸다. 모자라면 무효 Allocation을 돌려준다 —
    // 조용히 다른 프레임 구간을 침범하지 않는다. 그건 다음 프레임에 화면이
    // 깨지는 방식으로만 드러나서 추적이 어렵다.
    Allocation Allocate(uint64_t size, uint64_t alignment);

    Stats GetStats() const
    {
        Stats stats{};
        stats.allocations = m_stats.allocations.load(std::memory_order_relaxed);
        stats.bytesAllocated = m_stats.bytesAllocated.load(std::memory_order_relaxed);
        stats.overflows = m_stats.overflows.load(std::memory_order_relaxed);
        stats.peakFrameBytes = m_stats.peakFrameBytes.load(std::memory_order_relaxed);
        return stats;
    }
    uint64_t GetBytesPerFrame() const { return m_bytesPerFrame; }
    uint32_t GetFrameCount() const { return m_frameCount; }
    uint64_t GetFrameUsedBytes() const { return m_cursor.load(std::memory_order_relaxed); }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D12Resource> m_buffer;
    uint8_t*  m_mapped{ nullptr };
    D3D12_GPU_VIRTUAL_ADDRESS m_gpuBase{ 0 };

    uint64_t m_bytesPerFrame{ 0 };
    uint32_t m_frameCount{ 0 };

    uint32_t m_frameIndex{ 0 };
    // 커서는 원자적이다. 커맨드 기록을 여러 스레드가 나눠 하면 같은 링에서
    // 동시에 잘라 가고, 그때 단순 증가면 두 스레드가 같은 구간을 받는다 —
    // 증상은 '가끔 상수가 다른 드로우 것으로 보인다'라 추적이 매우 어렵다.
    //
    // fetch_add로는 부족하다. 정렬을 맞춘 뒤에 크기를 더해야 하는데 그 둘이
    // 한 연산이 아니기 때문이다. compare_exchange로 묶는다.
    std::atomic<uint64_t> m_cursor{ 0 };   // 현재 프레임 구간 안에서의 사용량

    AtomicStats m_stats;

    void RecordPeak(uint64_t used);
};

#endif
