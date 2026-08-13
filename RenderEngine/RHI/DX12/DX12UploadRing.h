#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

// 프레임 업로드 링버퍼 (PHASE 3-3 · 세그먼트화).
//
// DX12에서는 CPU가 채운 데이터를 GPU에 올리려면 업로드 힙을 거쳐야 한다.
// 브링업 단계에서는 텍스처 하나마다 CreateCommittedResource로 업로드 버퍼를
// 만들어 썼는데, 씬을 이식하면 프레임마다 상수·정점·스테이징이 수십~수백 건이
// 되므로 그 방식은 두 가지로 무너진다: 생성 비용 자체가 비싸고, GPU가 다 읽을
// 때까지 각 버퍼를 살려 둬야 해서 수명 관리가 호출부마다 붙는다.
//
// ── 왜 하나의 고정 버퍼에서 세그먼트 리스트로 바꿨나 ──
//
// 예전에는 큰 버퍼 하나를 만들고 프레임 수만큼 **정적으로** 나눠 썼다. 그
// 정적 분할이 곧 "한 프레임이 쓸 수 있는 총 바이트"라는 상한이었는데, 그
// 상한은 정상 상태에서 측정 가능한 값이 아니다 — 사용자가 씬에 무엇을 끌어다
// 놓느냐에 달렸다. 실제로 스폰자(메시 20.6MB)를 배치하자 16MB 구간이 모자라
// 업로드가 거절됐고, 그 한 번의 거절이 렌더러를 통째로 껐다.
//
// 그래서 옳은 고정값을 찾는 대신 상한 자체를 없앤다. 세그먼트를 리스트로 두고
// 모자라면 프레임 경계에서 하나 더 붙인다. 크기는 실제 고수위로 수렴한다.
//
// 참조 카운트로는 이 문제를 풀 수 없다. 카운트가 0이 되는 시점은 CPU가 기록을
// 마친 때고, GPU가 그 바이트를 다 읽은 때가 아니다 — 두 시점 사이가 최대
// 인플라이트 프레임 수만큼 벌어진다. 안전 판정은 끝까지 펜스가 한다.
//
// ── 반납 규칙은 여전히 구조가 보장한다 ──
//
// 세그먼트는 프레임 인덱스별로 소유한다. 프레임 i가 쓰는 세그먼트는 프레임 i만
// 쓰고, DX12DeviceResources::BeginFrame이 이미 그 슬롯의 펜스를 기다린 뒤에
// 진입하므로 다시 손대는 시점에는 GPU가 다 읽은 것이 보장된다. 세그먼트마다
// 펜스를 따로 달 필요가 없다.
//
// ── 세그먼트 생성은 BeginFrame에서만 한다 ──
//
// 생성한 세그먼트는 리소스 표에 등록해야 하는데(슬라이스가 핸들을 든다) 표는
// 스레드 안전하지 않다. Allocate는 병렬 기록 스레드에서 불리므로 그 안에서
// 만들면 표가 깨진다. 그래서 부족은 프레임 안에서는 거절로 남기고 수요만
// 기록해 둔 뒤, 다음 BeginFrame(단일 스레드)에서 그만큼 늘린다. 갑자기 큰
// 것이 들어오는 프레임 하나가 건너뛰어지고 그 다음부터 통과한다.
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
    // 세그먼트는 수명 내내 Map된 상태라 조각마다 Map/Unmap하지 않는다.
    //
    // offset은 **그 세그먼트 안에서의** 오프셋이다. 세그먼트마다 리소스가
    // 다르므로 절대 오프셋이라는 개념이 없다 — resource와 짝으로 쓴다.
    struct Allocation
    {
        void*                     cpuAddress{ nullptr };
        D3D12_GPU_VIRTUAL_ADDRESS gpuAddress{ 0 };
        ID3D12Resource*           resource{ nullptr };
        uint64_t                  offset{ 0 };
        uint64_t                  size{ 0 };
        uint32_t                  segment{ 0 };   // 표 핸들을 찾는 키

        bool IsValid() const { return nullptr != cpuAddress; }
    };

    struct Stats
    {
        uint64_t allocations{ 0 };
        uint64_t bytesAllocated{ 0 };
        uint64_t overflows{ 0 };      // 세그먼트가 모자라 거절한 횟수(수요로 기록돼 다음 프레임에 해소된다)
        uint64_t peakFrameBytes{ 0 }; // 한 프레임이 쓴 최대치
        uint32_t segmentCount{ 0 };   // 지금까지 만든 세그먼트 수
        uint64_t segmentBytes{ 0 };   // 세그먼트 총 바이트(= 실제 상주 업로드 힙)
        uint32_t growths{ 0 };        // 세그먼트를 늘린 횟수. 잦으면 초기 크기가 작다는 뜻
    };

    // 통계는 원자적으로 센다. 커맨드 기록이 병렬로 돌면 여러 스레드가 같은
    // 링에서 잘라 가고, 그때 통계가 어긋나면 '얼마나 썼는가'라는 유일한
    // 근거를 잃는다.
    struct AtomicStats
    {
        std::atomic<uint64_t> allocations{ 0 };
        std::atomic<uint64_t> bytesAllocated{ 0 };
        std::atomic<uint64_t> overflows{ 0 };
        std::atomic<uint64_t> peakFrameBytes{ 0 };
    };

    /// segmentBytes는 세그먼트 하나의 기본 크기다. 이보다 큰 단일 할당이
    /// 들어오면 그 크기에 맞춘 세그먼트를 따로 만든다(.NET의 LOH와 같은 자리).
    bool Initialize(ID3D12Device* device, uint64_t segmentBytes, uint32_t frameCount,
        std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return !m_segments.empty(); }

    /// 프레임 시작에 커서를 되감고, 지난 프레임들이 요구한 만큼 세그먼트를 늘린다.
    /// DX12DeviceResources::BeginFrame이 펜스를 기다린 뒤에 부르는 것이 계약이다.
    ///
    /// ★ 세그먼트가 여기서만 늘어난다. 호출자는 이 뒤에 GetSegmentCount()를
    ///   보고 새로 생긴 것을 표에 등록해야 한다.
    void BeginFrame(uint32_t frameIndex);

    /// 이 프레임 몫에서 잘라낸다. 모자라면 무효 Allocation을 돌려주고 수요로
    /// 기록한다 — 조용히 남의 세그먼트를 침범하지 않는다. 그건 다음 프레임에
    /// 화면이 깨지는 방식으로만 드러나서 추적이 어렵다.
    Allocation Allocate(uint64_t size, uint64_t alignment);

    Stats GetStats() const;

    /// 세그먼트 개수와 각 버퍼. 호출자가 표에 등록해 슬라이스가 핸들을 든다.
    uint32_t GetSegmentCount() const { return static_cast<uint32_t>(m_segments.size()); }
    ID3D12Resource* GetSegmentBuffer(uint32_t index) const
    {
        return (index < m_segments.size()) ? m_segments[index].buffer.Get() : nullptr;
    }

    /// 세그먼트 하나의 기본 크기. "한 번에 이만큼까지는 링으로 들어간다"는 뜻이라
    /// 호출부가 전용 스테이징으로 갈지 판단하는 기준으로 쓴다.
    uint64_t GetBytesPerFrame() const { return m_segmentBytes; }
    uint32_t GetFrameCount() const { return m_frameCount; }
    /// 이번 프레임이 지금까지 쓴 총 바이트(세그먼트를 넘나든 합).
    uint64_t GetFrameUsedBytes() const { return m_frameBytesUsed.load(std::memory_order_relaxed); }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Segment
    {
        ComPtr<ID3D12Resource>    buffer;
        uint8_t*                  mapped{ nullptr };
        D3D12_GPU_VIRTUAL_ADDRESS gpuBase{ 0 };
        uint64_t                  bytes{ 0 };
    };

    // 커서 상태를 하나의 원자값에 담는다.
    //
    // 슬롯(이 프레임이 쓰는 세그먼트 목록 안의 위치)과 커서(그 세그먼트 안의
    // 사용량)가 함께 움직여야 한다. 둘을 따로 두면 세그먼트를 넘기는 순간
    // 다른 스레드가 옛 슬롯에 새 커서를 적용해 같은 구간을 두 번 내준다 —
    // 증상은 '가끔 상수가 다른 드로우 것으로 보인다'라 추적이 매우 어렵다.
    // 상위 16비트가 슬롯, 하위 48비트가 커서다(세그먼트는 256TB 미만).
    static constexpr uint64_t kCursorBits = 48;
    static constexpr uint64_t kCursorMask = (1ull << kCursorBits) - 1;
    static uint64_t PackState(uint32_t slot, uint64_t cursor)
    {
        return (static_cast<uint64_t>(slot) << kCursorBits) | (cursor & kCursorMask);
    }

    bool CreateSegment(uint64_t bytes, std::string& outError);
    void RecordPeak(uint64_t used);
    void RecordDemand(uint64_t size);

    ID3D12Device* m_device{ nullptr };

    std::vector<Segment> m_segments;                     // 인덱스가 곧 세그먼트 id
    std::vector<std::vector<uint32_t>> m_frameSegments;  // 프레임 인덱스별 소유 목록

    uint64_t m_segmentBytes{ 0 };
    uint32_t m_frameCount{ 0 };
    uint32_t m_frameIndex{ 0 };

    std::atomic<uint64_t> m_cursorState{ 0 };     // (슬롯 << 48) | 커서
    std::atomic<uint64_t> m_frameBytesUsed{ 0 };  // 이번 프레임 누적(세그먼트 합)

    // 다음 BeginFrame이 얼마나 늘려야 하는지. 거절된 요청이 여기 쌓인다.
    std::atomic<uint64_t> m_demandTotal{ 0 };   // 이번 프레임이 더 필요했던 총량
    std::atomic<uint64_t> m_demandSingle{ 0 };  // 그중 가장 큰 단일 요청

    uint64_t m_neededSingle{ 0 };  // 지금까지 본 최대 단일 수요(LOH 세그먼트 기준)
    uint32_t m_growths{ 0 };

    /// 프레임 하나가 가질 수 있는 세그먼트 수의 상한. 폭주 방지용 안전망이지
    /// 튜닝 값이 아니다 — 여기 닿으면 그 자체가 조사할 거리다.
    static constexpr size_t kMaxSegmentsPerFrame = 64;

    AtomicStats m_stats;
};

#endif
