#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

#include "../IRHIGpuProfiler.h"

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
class DX12GpuProfiler : public IRHIGpuProfiler
{
public:
    /// 표시용으로 이름끼리 묶은 것. 원본은 PassSlice 에 남아 있다.
    struct PassTiming
    {
        std::string name;
        double      milliseconds{ 0.0 };
    };

    /// 조각 하나의 raw 구간. 분할 패스는 조각마다 구간을 찍으므로 한 패스가
    /// 여러 조각이 된다.
    ///
    /// ★ 이름으로 묶기 **전의** 것을 남긴다(§3.3: "같은 이름의 slice 는 표시
    ///   단계에서 묶되 원본 interval 은 버리지 않는다"). 묶은 것만 남기면 GPU
    ///   타임라인을 그릴 수가 없고, 조각이 서로 겹치는지도 알 수 없다.
    struct PassSlice
    {
        std::string name;
        uint64_t    beginTicks{ 0 };
        uint64_t    endTicks{ 0 };
    };

    /// 제출 하나의 수집 결과.
    ///
    /// ★ 합계를 하나로 정의하지 않는다(§3.4). queue span 은 첫 timestamp 부터
    ///   마지막까지이고, busy 는 겹치지 않는 실행 구간의 합이며, 패스별 시간은
    ///   조각마다의 것이다. 세을 더하면 서로 다른 수가 나오고, 그것이 정상이다.
    struct FrameTimings
    {
        GpuFrameToken          token{};
        std::vector<PassSlice> slices;             // 기록 순서 그대로
        uint64_t queueBeginTicks{ 0 };
        uint64_t queueEndTicks{ 0 };
        uint64_t busyTicks{ 0 };
        uint64_t ticksPerSecond{ 0 };

        // 끝이 시작보다 **앞선** 조각. 큰 음수가 되므로 뺀다 — 다만 **숨기지
        // 않고 센다.** 예전에는 조용히 0 으로 바꿔 합계에 섞여 들어갔다.
        //
        // ★ 길이가 0 인 것은 여기 들어오지 않는다. 둘을 `end <= begin` 으로 한데
        //   묶었더니, 그릴 것이 없어 일찍 빠져나간 패스(선이 없는 GizmoLine)가
        //   결함으로 세졌다. 두 timestamp 가 같은 틱에 찍히는 것은 정상이다.
        uint32_t droppedSlices{ 0 };

        // 버린 조각의 end - begin. 부호가 있어야 "얼마나 뒤집혔는가" 를 물을 수 있다.
        int64_t droppedSliceDeltaTicks{ 0 };

        // 길이가 정확히 0 인 조각. **버리지 않는다** — 그 패스는 실제로 돌았고
        // 비용이 timestamp 분해능 아래일 뿐이다. 세는 것은 정보지 판정이 아니다.
        uint32_t zeroLengthSlices{ 0 };

        // 버린 조각 중 **첫 번째의 이름.** 수만 세면 "한 개 버렸다" 까지만 알고
        // 어느 패스가 짝을 잃었는지는 모른다 — 고칠 수 없는 수는 계수기가 아니라
        // 경보음일 뿐이다.
        std::string droppedSliceName;
    };

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue,
        uint32_t maxPassesPerFrame, uint32_t frameCount, std::string& outError);
    void Shutdown();

    bool IsInitialized() const { return nullptr != m_queryHeap.Get(); }

    /// 제출 하나를 열고 그 제출의 표를 돌려준다. 링 슬롯은 여기서 고른다 —
    /// 링 산술이 한 자리에만 있어야 부르는 쪽과 읽는 쪽이 갈라지지 않는다.
    ///
    /// 돌려받은 표를 그 제출의 슬롯에 보관했다가 ResolveFrame · Collect 에 그대로
    /// 넘긴다. 보관하지 않으면 나중에 "어느 제출의 것을 읽을까" 를 물을 수 없다.
    ///
    /// ★ 슬롯의 기록(이름·질의 인덱스·사용 표시)만 되감는다. 다른 슬롯은
    ///   건드리지 않는다 — 그것이 인플라이트 제출의 기록을 지키는 유일한 방법이다.
    GpuFrameToken BeginFrame(uint64_t engineFrameId, uint64_t submissionId,
        uint64_t renderViewId);

    /// 패스 시작. 돌려준 슬롯을 EndPass에 그대로 넘긴다.
    /// 슬롯이 모자라면 kInvalidSlot을 돌려주고, 그 패스는 측정에서 빠진다.
    uint32_t BeginPass(RHIEncoder& encoder, const std::string& name) override;
    void     EndPass(RHIEncoder& encoder, uint32_t slot) override;

    // DX12 전용 benchmark와 Resolve 경로가 native list를 직접 계측할 때 쓴다.
    uint32_t BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name);
    void     EndPass(ID3D12GraphicsCommandList* commandList, uint32_t slot);

    /// 제출을 닫기 전에 부른다. 그 표가 가리키는 슬롯의 질의만 리드백으로 옮긴다.
    void ResolveFrame(ID3D12GraphicsCommandList* commandList, const GpuFrameToken& token);

    /// GPU 가 **그 제출을** 끝낸 뒤에 부른다(펜스 완료 후).
    ///
    /// ★ 표가 가리키는 슬롯에 **그 표가 그대로 있는지** 먼저 확인한다. 사이에
    ///   다른 제출이 그 슬롯을 다시 열었으면 기록은 이미 남의 것이므로, 그때는
    ///   수치를 내지 않고 **실패한다.** 예전에는 그 자리에서 그럴듯한 숫자가 나왔고
    ///   어느 프레임 것인지는 어디에도 적혀 있지 않았다.
    bool Collect(const GpuFrameToken& token, FrameTimings& outTimings,
        std::string& outError);

    /// raw 조각을 이름으로 묶어 표시용으로 만든다. 원본은 그대로 둔다.
    ///
    /// 분할 패스를 그대로 나열하면 "GBuffer 가 여섯 번 있다" 가 되어 읽을 수 없다.
    void MergeSlices(const FrameTimings& timings, std::vector<PassTiming>& outTimings);

    /// 그 슬롯이 지금 들고 있는 표. 프로브와 진단용이다.
    GpuFrameToken SlotToken(uint32_t ringSlot) const;

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

    // Collect()가 프레임마다 쓰는 병합 스크래치. 멤버로 둬서 용량을 유지한다 —
    // 지역 변수였을 때 프레임마다 벡터 하나 + 엔트리마다 std::string 하나를
    // 새로 할당했고, 그것이 할당 귀속 상위에 잡혔다(ContainerLibraryDesign C0).
    //
    // 키가 string_view인 이유: 가리키는 대상이 m_records[i].name이고 그 벡터는
    // Collect() 호출을 넘어 산다. 이 함수 안에서만 쓰므로 안전하다.
    struct MergedSpan
    {
        uint64_t begin{ 0 };
        uint64_t end{ 0 };
        uint32_t slices{ 0 };
    };
    std::vector<std::pair<std::string_view, MergedSpan>> m_mergeScratch;

    // busy 계산용 스크래치. 조각을 정렬해 훑어야 하는데 원본 순서를 깨면
    // 안 되므로 사본을 둔다.
    std::vector<std::pair<uint64_t, uint64_t>> m_busyScratch;

    ComPtr<ID3D12QueryHeap> m_queryHeap;
    ComPtr<ID3D12Resource>  m_readback;

    uint32_t m_maxPassesPerFrame{ 0 };
    uint32_t m_frameCount{ 0 };

    // 지금 **기록 중인** 슬롯. 기록은 한 번에 한 제출뿐이므로 하나면 충분하다 —
    // 읽는 쪽은 이것을 보지 않고 표의 ringSlot 을 본다.
    uint32_t m_recordingSlot{ 0 };
    // 슬롯 카운터는 원자적이다.
    //
    // 병렬 기록에서는 여러 워커가 동시에 BeginPass를 부른다. 단순 증가면
    // 두 워커가 같은 슬롯을 받고, 그러면 한쪽의 타임스탬프가 다른 쪽 것으로
    // 덮여 '어느 패스가 얼마나 걸렸는가'가 조용히 틀린 값이 된다.
    //
    // 질의 힙 자체는 하나로 충분하다 — 인덱스가 겹치지 않으면 여러 커맨드
    // 리스트가 같은 힙에 써도 된다. 워커마다 힙을 나눌 필요는 없었다.

    // 틱을 밀리초로 바꾸는 값. 큐마다 다를 수 있어 초기화 때 받아 둔다.
    uint64_t m_ticksPerSecond{ 0 };

    // 레코드는 미리 크기를 잡고 인덱스로 접근한다. push_back은 재할당을
    // 일으켜 병렬에서 성립하지 않는다.
    //
    // ★ 크기가 frameCount * maxPassesPerFrame 이다. 예전에는 maxPassesPerFrame
    //   한 벌이었고, 그래서 질의 힙·리드백은 슬롯별로 갈라져 있는데 **CPU 쪽
    //   기록만 한 벌**이었다. 인플라이트 제출이 둘이면 뒤에 온 것이 앞의 이름·
    //   질의 인덱스를 덮어 썼다.
    std::vector<PassRecord> m_records;

    // 슬롯·패스 쌍을 평탄 인덱스로 바꾼다. 두 자리에서 같은 산술을 적으면
    // 한쪽만 고칠 때 조용히 어긋난다.
    size_t RecordIndex(uint32_t ringSlot, uint32_t pass) const
    {
        return static_cast<size_t>(ringSlot) * m_maxPassesPerFrame + pass;
    }

    // 슬롯마다의 사용 패스 수와 표. atomic 은 복사도 이동도 안 되므로
    // vector 가 아니라 고정 배열로 둔다.
    std::unique_ptr<std::atomic<uint32_t>[]> m_slotUsedPasses;
    std::vector<GpuFrameToken>               m_slotTokens;
    double m_lastTotalMs{ 0.0 };
};

