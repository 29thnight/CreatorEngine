#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>
#include <wrl/client.h>
#include <d3d12.h>

// 디스크립터 힙 관리 (PHASE 3-4).
//
// DX11에서는 SRV·샘플러를 슬롯에 그냥 꽂았다. DX12는 디스크립터를 힙에 만들어 두고
// 테이블로 묶어 넘기는데, 제약이 둘 있다:
//   - 셰이더 가시 힙은 종류별로 한 번에 하나만 바인딩된다(CBV/SRV/UAV 하나, 샘플러 하나).
//     그래서 패스마다 힙을 만들면 패스 경계마다 힙을 갈아 끼워야 하고, 그건 비싸다.
//   - 디스크립터를 GPU가 다 읽기 전에 덮어쓰면 안 된다. 수명 관리가 붙는다.
//
// 두 문제의 성격이 달라서 자료구조도 둘로 나눈다.
//
//   DX12DescriptorRing  — 프레임마다 바뀌는 것(CBV/SRV/UAV). 큰 힙 하나를 프레임
//                         구간으로 잘라 쓴다. 업로드 링(3-3)과 같은 패턴이고,
//                         반납도 같은 방식으로 구조가 보장한다.
//   DX12SamplerHeap     — 샘플러. 종류가 적고(필터·주소 모드 조합) 프레임마다
//                         바뀌지 않는다. 링으로 돌리면 같은 샘플러를 매 프레임 다시
//                         만드는 꼴이라, 해시 캐시로 한 번만 만들고 계속 쓴다.
//                         하드웨어 상한도 2048개로 작아서 링과 맞지 않는다.

class DX12DescriptorRing
{
public:
    // 힙에서 잘라낸 연속 구간.
    //
    // 디스크립터 테이블은 연속이어야 하므로 낱개가 아니라 구간 단위로 준다.
    // cpu 핸들로 뷰를 만들고(또는 CPU 전용 힙에서 복사해 오고), gpu 핸들을
    // SetGraphicsRootDescriptorTable에 넘긴다.
    struct Allocation
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        uint32_t count{ 0 };
        uint32_t incrementSize{ 0 };

        bool IsValid() const { return 0 != count; }

        // 구간 안의 i번째 디스크립터. 뷰를 여러 개 만들 때 쓴다.
        D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(uint32_t index) const
        {
            D3D12_CPU_DESCRIPTOR_HANDLE handle = cpu;
            handle.ptr += static_cast<SIZE_T>(index) * incrementSize;
            return handle;
        }
    };

    struct Stats
    {
        uint64_t allocations{ 0 };
        uint64_t descriptors{ 0 };
        uint64_t overflows{ 0 };        // 구간이 모자라 거절한 횟수 — 0이 아니면 크기를 늘려야 한다
        uint32_t peakFrameDescriptors{ 0 };
    };

    bool Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
        uint32_t descriptorsPerFrame, uint32_t frameCount, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_heap.Get(); }

    // 프레임 시작에 그 프레임 구간의 커서를 되감는다.
    // 업로드 링과 같은 계약 — DX12DeviceResources::BeginFrame이 펜스를 기다린
    // 뒤에 부르므로, 되감는 시점에는 GPU가 그 구간을 다 읽은 것이 보장된다.
    void BeginFrame(uint32_t frameIndex);

    // 연속 count개를 잘라낸다. 모자라면 무효를 돌려준다 — 조용히 다음 구간을
    // 침범하면 GPU가 읽는 중인 디스크립터를 덮게 되고, 증상은 다음 프레임에
    // 엉뚱한 텍스처가 보이는 식으로만 드러난다.
    Allocation Allocate(uint32_t count);

    ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }
    uint32_t GetDescriptorsPerFrame() const { return m_descriptorsPerFrame; }
    uint32_t GetFrameCount() const { return m_frameCount; }
    uint32_t GetFrameUsed() const { return m_cursor; }
    Stats    GetStats() const { return m_stats; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D12DescriptorHeap> m_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuBase{};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuBase{};
    uint32_t m_incrementSize{ 0 };

    uint32_t m_descriptorsPerFrame{ 0 };
    uint32_t m_frameCount{ 0 };
    uint32_t m_frameIndex{ 0 };
    uint32_t m_cursor{ 0 };

    Stats m_stats;
};

// 샘플러 힙 + 중복 제거 캐시.
//
// 같은 샘플러 설정을 여러 머티리얼이 공유하는 것이 보통이라(선형+클램프,
// 포인트+랩 ...) 설정을 해시해 한 번만 만든다. 루트 시그니처 캐시와 같은 발상이고
// 이유도 같다 — 같은 것을 여러 벌 만들면 상한(2048)에 먼저 부딪힌다.
class DX12SamplerHeap
{
public:
    struct Stats
    {
        uint32_t hits{ 0 };
        uint32_t creates{ 0 };
        uint32_t overflows{ 0 };   // 힙이 꽉 참 — 상한이 2048이라 실제로 닿을 수 있다
    };

    bool Initialize(ID3D12Device* device, uint32_t capacity, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_heap.Get(); }

    // 같은 설정이면 같은 핸들을 돌려준다. 실패하면 ptr이 0인 핸들.
    D3D12_GPU_DESCRIPTOR_HANDLE GetOrCreate(const D3D12_SAMPLER_DESC& desc);

    /// 연속한 N개를 만들어 그 시작 핸들을 돌려준다.
    ///
    /// 디스크립터 테이블은 연속이어야 하는데, GetOrCreate를 두 번 불러 인접을
    /// 기대하는 것은 위험하다 — 사이에 다른 호출이 끼면 조용히 어긋나고,
    /// 증상은 '샘플러가 뒤바뀐다'라 원인이 멀다. 범위가 필요하면 이쪽을 쓴다.
    ///
    /// 중복 제거는 하지 않는다. 범위는 종류가 적고(패스마다 한둘) 캐시하려면
    /// 조합 전체를 키로 잡아야 하는데 이득이 없다.
    D3D12_GPU_DESCRIPTOR_HANDLE CreateRange(const D3D12_SAMPLER_DESC* descs, uint32_t count);

    static uint64_t ComputeHash(const D3D12_SAMPLER_DESC& desc);

    ID3D12DescriptorHeap* GetHeap() const { return m_heap.Get(); }
    Stats  GetStats() const;
    size_t GetCachedCount() const;

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D12Device>         m_device;
    ComPtr<ID3D12DescriptorHeap> m_heap;
    D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuBase{};
    D3D12_GPU_DESCRIPTOR_HANDLE  m_gpuBase{};
    uint32_t m_incrementSize{ 0 };
    uint32_t m_capacity{ 0 };
    uint32_t m_used{ 0 };

    mutable std::mutex m_mutex;
    std::unordered_map<uint64_t, D3D12_GPU_DESCRIPTOR_HANDLE> m_cache;
    Stats m_stats;
};

#endif
