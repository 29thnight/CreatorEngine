#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

// 워커별 커맨드 리스트 풀 (PHASE 3-6, 커맨드 기록 병렬화).
//
// ── 왜 필요한가 ──
//
// 커맨드 리스트와 얼로케이터는 스레드 안전하지 않다. 두 스레드가 같은 리스트에
// 기록하면 그 자리에서 깨지고, 같은 얼로케이터를 쓰면 내부 메모리가 겹친다.
// 그래서 병렬 기록의 첫 조건은 '워커마다 자기 리스트'다.
//
// ── 왜 프레임 구간까지 나누는가 ──
//
// 얼로케이터는 그 리스트로 기록한 커맨드를 GPU가 다 실행한 뒤에만 Reset할 수
// 있다. 프레임당 하나면 매 프레임 GPU를 기다려야 한다. 업로드 링·디스크립터
// 링과 같은 패턴으로 프레임 구간을 두면, DX12DeviceResources::BeginFrame이
// 이미 그 슬롯의 펜스를 기다린 뒤 진입하므로 Reset 시점의 안전이 구조로
// 보장된다 — 호출부 규율에 기대지 않는다.
//
// ── 제출 순서 ──
//
// 기록은 병렬로 하되 제출은 선언 순서를 지킨다. DX12는 ExecuteCommandLists에
// 넘긴 순서대로 실행하므로, 그것만 지키면 그래프가 넣은 배리어의 앞뒤 관계가
// 그대로 보존된다. 그래프의 계약("선언 순서 = 실행 순서")이 여기서 값을 한다.
class DX12CommandListPool
{
public:
    static constexpr uint32_t kMaxWorkers = 8;

    bool Initialize(ID3D12Device* device, uint32_t workerCount, uint32_t frameCount,
        std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return !m_slots.empty(); }

    uint32_t GetWorkerCount() const { return m_workerCount; }

    /// 프레임 구간을 바꾼다. 이 시점에는 그 구간의 GPU 작업이 끝나 있어야 한다
    /// (DX12DeviceResources::BeginFrame의 펜스 대기가 그것을 보장한다).
    void BeginFrame(uint32_t frameIndex);

    /// 워커의 리스트를 열어 돌려준다. 이미 열려 있으면 그대로 준다.
    /// 실패하면 nullptr — 호출부는 그 패스를 건너뛰지 말고 알려야 한다.
    ID3D12GraphicsCommandList* Open(uint32_t worker, std::string& outError);

    /// 이번 프레임에 연 리스트를 전부 닫는다. 닫힌 순서가 아니라 워커 번호
    /// 순서로 돌려주지 않는다 — 제출 순서는 호출부(그래프)가 정한다.
    bool CloseAll(std::string& outError);

    /// 워커가 이번 프레임에 실제로 기록했는가.
    bool HasRecorded(uint32_t worker) const;

    ID3D12GraphicsCommandList* Get(uint32_t worker) const;

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Slot
    {
        ComPtr<ID3D12CommandAllocator>    allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        bool opened{ false };
    };

    // [프레임][워커]. 프레임 구간이 바깥인 이유는 BeginFrame이 구간 단위로
    // 통째로 Reset하기 때문이다.
    std::vector<std::vector<Slot>> m_slots;

    ComPtr<ID3D12Device> m_device;
    uint32_t m_workerCount{ 0 };
    uint32_t m_frameCount{ 0 };
    uint32_t m_frameIndex{ 0 };
};

#endif
