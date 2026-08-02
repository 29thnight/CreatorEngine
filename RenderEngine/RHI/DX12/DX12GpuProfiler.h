#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

// 패스별 GPU 시간 측정 (PHASE 3-6).
//
// 3-6의 계약이 "패스마다 DX11 대비 개선을 확인하고 넘어간다"이다. 그러려면
// 재는 수단이 먼저 있어야 하고, 없으면 "일단 돌게 만들고 나중에 최적화"로
// 흘러간다 — 그 나중은 오지 않는다.
//
// 그래프에 붙이는 이유: 그래프가 패스 경계를 알고 있으므로, 패스마다 계측
// 코드를 넣을 필요 없이 한 곳에서 전부 감싼다. 패스 작성자가 잊어버릴 수 있는
// 종류의 일을 구조가 대신하는 편이 낫다.
//
// 주의: 타임스탬프는 큐 시간이지 순수 GPU 작업 시간이 아니다. 앞 패스가
// 아직 돌고 있으면 그 대기가 이 패스 시간에 섞인다. 절대값보다 같은 조건에서의
// 상대 변화(DX11 대비, 최적화 전후)를 보는 도구다.
class DX12GpuProfiler
{
public:
    struct PassTiming
    {
        std::string name;
        double      milliseconds{ 0.0 };
    };

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        uint32_t maxPassesPerFrame, uint32_t frameCount, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_queryHeap.Get(); }

    /// 프레임 시작에 부른다. 그 프레임 구간의 슬롯을 되감는다 —
    /// 업로드 링·디스크립터 링과 같은 패턴이고, 이유도 같다(GPU가 아직
    /// 이전 프레임 결과를 쓰고 있을 수 있다).
    void BeginFrame(uint32_t frameIndex);

    /// 패스 시작. 돌려준 슬롯을 EndPass에 그대로 넘긴다.
    /// 슬롯이 모자라면 kInvalidSlot을 돌려주고, 그 패스는 측정에서 빠진다.
    static constexpr uint32_t kInvalidSlot = 0xFFFFFFFF;
    uint32_t BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name);
    void     EndPass(ID3D12GraphicsCommandList* commandList, uint32_t slot);

    /// 프레임 끝에, 커맨드 리스트를 닫기 전에 부른다. 질의 결과를 리드백으로 옮긴다.
    void ResolveFrame(ID3D12GraphicsCommandList* commandList);

    /// GPU가 그 프레임을 끝낸 뒤에 부른다(펜스 대기 후).
    bool Collect(std::vector<PassTiming>& outTimings, std::string& outError);

    /// 마지막으로 수집한 것의 합계.
    double GetLastTotalMilliseconds() const { return m_lastTotalMs; }

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct PassRecord
    {
        std::string name;
        uint32_t    beginQuery{ 0 };
        uint32_t    endQuery{ 0 };
        bool        used{ false };
    };

    ComPtr<ID3D12QueryHeap> m_queryHeap;
    ComPtr<ID3D12Resource>  m_readback;

    uint32_t m_maxPassesPerFrame{ 0 };
    uint32_t m_frameCount{ 0 };
    uint32_t m_frameIndex{ 0 };
    // 슬롯 카운터는 원자적이다.
    //
    // 병렬 기록에서는 여러 워커가 동시에 BeginPass를 부른다. 단순 증가면
    // 두 워커가 같은 슬롯을 받고, 그러면 한쪽의 타임스탬프가 다른 쪽 것으로
    // 덮여 '어느 패스가 얼마나 걸렸는가'가 조용히 틀린 값이 된다.
    //
    // 질의 힙 자체는 하나로 충분하다 — 인덱스가 겹치지 않으면 여러 커맨드
    // 리스트가 같은 힙에 써도 된다. 워커마다 힙을 나눌 필요는 없었다.
    std::atomic<uint32_t> m_usedPasses{ 0 };

    // 틱을 밀리초로 바꾸는 값. 큐마다 다를 수 있어 초기화 때 받아 둔다.
    uint64_t m_ticksPerSecond{ 0 };

    // 레코드는 미리 크기를 잡고 인덱스로 접근한다. push_back은 재할당을
    // 일으켜 병렬에서 성립하지 않는다.
    std::vector<PassRecord> m_records;
    double m_lastTotalMs{ 0.0 };
};

#endif
