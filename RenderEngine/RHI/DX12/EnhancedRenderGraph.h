#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <wrl/client.h>
#include <d3d12.h>

#include "DX12GpuProfiler.h"
#include "DX12CommandListPool.h"

// DX12 실행 경로용 렌더 그래프 (PHASE 3-5).
//
// 기존 RenderGraphBuilder를 쓰지 않는 이유:
//   - 싱글턴이다. 그래프는 카메라별·프레임별로 있어야 하는데 프로세스에 하나뿐이면
//     씬 뷰와 게임 뷰가 같은 그래프를 공유하게 된다.
//   - 배리어 유도가 없다. 위상 정렬까지만 있고, DX12에서 그래프를 두는 이유의
//     절반이 배리어 자동화인데 그 절반이 비어 있다.
//   - 리소스 타입이 Texture*와 uint32 format 자리표시자다.
//   - 참조하는 코드가 0곳이다.
// 3-6에서 패스를 새로 쓰기로 한 것과 같은 이유다 — 껍데기를 맞추는 데 드는 노력이
// 새로 쓰는 것보다 크고, 결과도 더 나쁘다.
//
// 이 그래프가 하는 일은 넷이다:
//   ① 선언이 데이터 흐름과 어긋나는지 검증한다 — 아무도 쓰지 않은 리소스를 읽는 패스
//   ② 리소스 상태를 추적해 전이 배리어를 자동으로 만든다 — 패스 앞에 모아서 한 번에
//      넣는다. 호출마다 배리어를 붙이면 파이프라인이 끊긴다.
//   ③ 결과에 기여하지 않는 패스를 걷어낸다
//   ④ transient 리소스의 수명을 알려 준다(힙 앨리어싱의 재료 — 실제 앨리어싱은
//      실전 예산이 보이는 3-6에서 붙인다)
//
// 하지 않는 일: 패스 재정렬.
//
// 처음에는 위상 정렬로 순서를 유도하게 짰다가 자가 검증에서 뒤집었다. 순수
// 데이터 흐름만으로는 순서가 정해지지 않는다 — 한 리소스에 두 패스가 쓰면 둘 중
// 무엇이 먼저인지 알 방법이 없고 결국 선언 순서로 되돌아온다. 그러면 정렬은
// 선언 순서를 다시 만들어 내는 일이 되고, 어쩌다 뒤집히면 프레임이 실행마다
// 달라져 픽셀 대조가 흔들린다. 실행 순서 = 선언 순서(컬링된 것만 빠짐)가 계약이다.
//
// 배리어를 사람이 붙이지 않는 것이 요점이다. DX11은 드라이버가 해 주던 일이라
// 손으로 옮기면 빠뜨리기 쉽고, 빠뜨린 배리어는 '가끔 이상하게 보인다'로만 드러난다.

// 그래프가 아는 리소스 상태. D3D12 상태의 부분집합이되, 그래프가 판단해야 하는
// 구분만 남긴다 — 상태를 다 노출하면 호출부가 배리어를 손으로 짜는 것과 같아진다.
enum class RGResourceState
{
    Common,
    RenderTarget,
    DepthWrite,
    DepthRead,
    ShaderResource,   // 픽셀 셰이더에서 읽기
    UnorderedAccess,
    CopySource,
    CopyDest,
};

struct RGHandle
{
    uint16_t index{ 0xFFFF };
    static constexpr uint16_t kInvalid = 0xFFFF;
    bool IsValid() const { return kInvalid != index; }
};

struct RGPassId
{
    uint16_t index{ 0xFFFF };
    static constexpr uint16_t kInvalid = 0xFFFF;
    bool IsValid() const { return kInvalid != index; }
};

// 그래프가 만들 리소스의 설명. transient(그래프 소유)만 이 설명을 쓴다.
struct RGTextureDesc
{
    uint32_t    width{ 0 };
    uint32_t    height{ 0 };

    // 배열 슬라이스. 캐스케이드 그림자맵처럼 같은 규격이 여러 장 필요할 때 쓴다.
    // 텍스처를 여러 개 만드는 것보다 낫다 — 리소스가 하나라 배리어도 하나고
    // (전이는 전체 서브리소스 단위라 슬라이스가 늘어도 배리어 수가 그대로다),
    // 셰이더에서 슬라이스 번호가 좌표의 일부라 분기 없이 고를 수 있다.
    uint32_t    arraySize{ 1 };

    DXGI_FORMAT format{ DXGI_FORMAT_R8G8B8A8_UNORM };
    bool        allowRenderTarget{ false };

    // 최적 클리어 값. 리소스를 만들 때 넘기는 힌트이고, 실제 ClearRenderTargetView가
    // 다른 값을 쓰면 검증 레이어가 "느려진다"고 경고한다. 힌트를 안 주는 것도
    // 경고 대상이라(3-3 실측) 기본값을 두고 필요하면 호출부가 맞춘다.
    float       clearColor[4]{ 0.f, 0.f, 0.f, 0.f };
    bool        allowDepthStencil{ false };
    bool        allowUnorderedAccess{ false };
    std::string name;
};

class EnhancedRenderGraph
{
public:
    // 패스가 기록할 때 받는 것. 커맨드 리스트는 호출부가 준 것을 그대로 넘긴다 —
    // 그래프는 순서와 배리어를 정할 뿐 기록 방식을 정하지 않는다(3-6에서 패스별
    // 커맨드 리스트를 워커가 병렬로 기록하게 될 때 이 경계가 필요하다).
    struct ExecuteContext
    {
        ID3D12GraphicsCommandList* commandList{ nullptr };
        const EnhancedRenderGraph* graph{ nullptr };

        // 선언한 리소스의 실제 D3D12 객체. transient는 그래프가 만든 것.
        ID3D12Resource* Resolve(RGHandle handle) const;
    };

    using ExecuteCallback = std::function<void(const ExecuteContext&)>;

    /// 쪼갤 수 있는 패스의 기록 콜백.
    ///
    /// slice/sliceCount로 자기 몫만 기록한다. sliceCount가 1이면 통째로
    /// 기록하는 것과 같아야 한다 — 순차 경로가 그 형태로 부른다.
    ///
    /// ★ 조각마다 상태를 다시 걸어야 한다. 커맨드 리스트는 서로의 상태를
    /// 물려받지 않으므로, 루트 시그니처·PSO·렌더 타깃·뷰포트·힙 바인딩을
    /// 조각마다 세워야 한다. 클리어처럼 한 번만 해야 하는 것은 slice==0에서만.
    using SplitExecuteCallback =
        std::function<void(const ExecuteContext&, uint32_t slice, uint32_t sliceCount)>;

    struct Stats
    {
        uint32_t passesDeclared{ 0 };
        uint32_t passesCulled{ 0 };
        uint32_t passesExecuted{ 0 };
        uint32_t barriersEmitted{ 0 };
        uint32_t barrierBatches{ 0 };   // 배리어를 몇 번에 나눠 넣었는가 — 적을수록 좋다
        uint32_t transientCreated{ 0 };

        // 병렬 기록에서만 채워진다. 제출 리스트 수가 워커 수보다 적으면
        // 놀고 있는 워커가 있다는 뜻이다(패스가 워커보다 적을 때 정상).
        uint32_t recordWorkers{ 0 };
        uint32_t submittedLists{ 0 };

        // 기록 단위 수. 분할하지 않으면 패스 수와 같다. 이 값이 워커 수보다
        // 작으면 노는 워커가 생긴다 — 분할이 필요한지 판단하는 근거다.
        uint32_t recordUnits{ 0 };

        // 병렬을 요청받았지만 기록량이 적어 순차로 되물렀는가, 그리고 그
        // 판단의 근거가 된 총 기록량.
        //
        // 되무른 사실을 통계로 내보내는 이유: 이것이 없으면 '병렬을 켰는데
        // 왜 워커가 1이지'를 코드를 읽어야 알 수 있다. 조용히 다르게 도는
        // 것이 가장 나쁘다.
        bool     parallelDeclined{ false };
        uint32_t totalRecordCost{ 0 };
    };

    /// 병렬로 갈 최소 기록량. 이 아래에서는 워커를 깨우는 비용이 기록보다 크다.
    ///
    /// ★ 이 값을 두 번 다시 잡았다. 두 번 다 측정이 앞의 근거를 무너뜨렸다.
    ///
    /// 1차(Debug, 임계 1024) — 기록량 1419에서 1.37배로 이기는 것처럼 보였다.
    /// 2차(Release, 임계 32768) — 같은 규모가 0.51~0.57배로 뒤집혔다. 순차
    ///   기록이 Debug 5~6 ms에서 Release 0.36 ms로 15배 빨라지니 워커 기동
    ///   비용이 상대적으로 커진 것이다. Debug에서 본 교차점은 Debug의 것이었다.
    /// 3차(Release, 배치를 갈라서) — 여기가 답이었다.
    ///
    /// 2차에서 '실측한 모든 규모에서 이득이 없다'고 적으면서 그 측정의 한계도
    /// 함께 적어 두었다: 같은 메시 11종을 복제하므로 드로우 11264가 배치 11개로
    /// 묶여, 나눌 것이 거의 없는 조건이었다. 그 한계를 없애고 다시 쟀다.
    /// 복제마다 다른 텍스처를 물려 배치를 갈랐더니(각 2회):
    ///
    ///     배치  11 · 드로우   704 → 0.63배 / 0.61배
    ///     배치 704 · 드로우   704 → 2.10배 / 1.43배   ← 같은 드로우 수다
    ///     배치  11 · 드로우 11264 → 1.52배 / 0.95배
    ///     배치 704 · 드로우 11264 → 1.57배 / 1.26배
    ///     배치  44 · 드로우    44 → 0.55배 / 0.54배
    ///
    /// 드로우 수가 같아도 배치가 많으면 병렬이 이긴다. 즉 병렬화의 이득을
    /// 정하는 것은 배치 수다. 그래서 recordCost의 정의도 배치 수로 바꿨다.
    ///
    /// 경계는 배치 44(짐)와 704(이김) 사이다. 그 사이를 잡는다.
    /// 실측이 직접 지목한 것은 '44는 아니고 704는 맞다'까지이므로, 정확한
    /// 경계가 256이라는 뜻은 아니다 — 두 점 사이의 보수적인 선택이다.
    static constexpr uint32_t kParallelRecordCostThreshold = 256;

    /// 임계값을 바꾼다. 0이면 되무르지 않는다.
    ///
    /// 자가 검증이 교차점을 재려고 쓴다 — 임계값이 켜진 채로는 '임계값 아래에서
    /// 병렬이 얼마나 지는가'를 잴 수 없다. 되무름이 병렬 시간을 순차와 같게
    /// 만들어 버리기 때문이다. 재는 도구가 재려는 것을 가리면 안 된다.
    void SetParallelRecordCostThreshold(uint32_t threshold)
    {
        m_parallelCostThreshold = threshold;
    }

    void Reset();

    // 외부 리소스를 그래프에 들인다. 현재 상태를 알려 줘야 첫 전이를 맞게 만든다.
    RGHandle ImportTexture(ID3D12Resource* resource, RGResourceState currentState,
        const std::string& name);

    // 그래프가 소유할 리소스를 선언한다. 실제 생성은 Compile에서 한다 —
    // 컬링으로 사라진 패스만 쓰던 리소스는 만들지 않기 위해서다.
    RGHandle CreateTexture(const RGTextureDesc& desc);

    // 패스 선언. usages는 (핸들, 그 패스가 요구하는 상태) 목록이다.
    //
    // hasSideEffect는 컬링에서 뿌리가 되는 표시다. 화면에 내보내거나 외부가
    // 읽는 리소스를 쓰는 패스는 결과가 그래프 밖으로 나가므로 걷어내면 안 된다.
    // 기본값을 false로 두는 이유는, 뿌리를 명시하지 않으면 전부 살아남아
    // 컬링이 무의미해지기 때문이다.
    struct RGPassUsage
    {
        RGHandle        handle;
        RGResourceState state{ RGResourceState::Common };
    };

    RGPassId AddPass(const std::string& name, const std::vector<RGPassUsage>& usages,
        ExecuteCallback execute, bool hasSideEffect = false);

    /// 여러 커맨드 리스트에 나눠 기록할 수 있는 패스.
    ///
    /// 왜 필요한가: 패스 단위 병렬화는 '가장 무거운 패스' 이상으로 빨라질 수
    /// 없다. 실측에서 GBuffer·Shadow가 기록 시간의 대부분을 차지해 워커를
    /// 늘려도 1.25배에서 평평해졌다. 그 벽을 넘으려면 한 패스 안을 쪼개야 한다.
    ///
    /// maxSlices는 상한이다. 그래프가 워커 수와 견주어 실제 조각 수를 정하고,
    /// 순차 경로에서는 1로 부른다.
    /// recordCost — 이 패스가 기록하면서 할 일의 양. 단위는 배치 수다.
    ///
    /// 한때 드로우 수로 뒀다. 컬링 판정과 인스턴스 수집이 드로우마다 도니
    /// 그쪽이 비용을 따를 거라고 봤는데, 실측이 아니라고 답했다 — 같은 드로우
    /// 704를 배치 11로 묶으면 순차 0.34 ms, 배치 704로 흩으면 1.25~2.08 ms다.
    /// 인스턴스를 배열에 미는 일은 싸고, 배치마다 디스크립터·버퍼·PSO를 다시
    /// 거는 일이 비싸다.
    ///
    /// 조각 수(maxSlices)와는 여전히 다른 값이다. 조각 수는 '얼마나 쪼갤 수
    /// 있나'라 워커 수에 눌리고, 이 값은 '병렬로 갈 만한가'를 정한다.
    RGPassId AddSplitPass(const std::string& name, const std::vector<RGPassUsage>& usages,
        SplitExecuteCallback execute, uint32_t maxSlices, bool hasSideEffect = false,
        uint32_t recordCost = 0);

    // 순서 유도 → 컬링 → 배리어 계획. 실패 사유는 문자열로.
    bool Compile(ID3D12Device* device, std::string& outError);

    // Compile이 정한 순서대로 배리어를 넣고 패스를 기록한다.
    bool Execute(ID3D12GraphicsCommandList* commandList, std::string& outError);

    // 패스별 GPU 시간을 잰다. 붙여 두면 Execute가 패스마다 자동으로 감싼다 —
    // 패스 작성자가 계측을 잊어버릴 수 있는 종류의 일을 구조가 대신한다.
    //
    // 배리어는 측정 구간 안에 넣는다. 배리어도 GPU 시간을 쓰고, 그 비용이
    // 어느 패스 때문에 생겼는지가 곧 그 패스의 비용이기 때문이다.
    void SetProfiler(DX12GpuProfiler* profiler) { m_profiler = profiler; }

    Stats GetStats() const { return m_stats; }

    // 검증·진단용. Compile 뒤에 유효하다.
    const std::vector<uint16_t>& GetExecuteOrder() const { return m_executeOrder; }
    /// 패스를 여러 커맨드 리스트에 나눠 기록하고 선언 순서로 제출한다.
    ///
    /// ── 왜 이 순서인가 ──
    ///
    /// DX12는 ExecuteCommandLists에 넘긴 순서대로 실행한다. 그러므로 기록을
    /// 병렬로 해도 제출 순서만 선언 순서를 지키면 그래프가 넣은 배리어의 앞뒤
    /// 관계가 그대로 보존된다. 3-5에서 "선언 순서 = 실행 순서"를 계약으로
    /// 정해 둔 것이 여기서 값을 한다 — 재정렬하는 그래프였다면 병렬 기록과
    /// 배리어 순서를 동시에 만족시키기 어려웠다.
    ///
    /// ── 왜 패스 단위로 나누는가 ──
    ///
    /// 한 패스를 여러 리스트로 쪼개면 그 안의 상태(루트 시그니처·PSO·힙 바인딩)를
    /// 리스트마다 다시 걸어야 하고, 배리어가 패스 중간에 끼면 순서가 깨진다.
    /// 패스 하나는 리스트 하나가 통째로 맡는다.
    ///
    /// workerCount가 1이면 순차 실행과 같은 경로를 탄다 — 비교 기준이 된다.
    bool ExecuteParallel(DX12CommandListPool& pool, ID3D12CommandQueue* queue,
        uint32_t workerCount, std::string& outError);

    bool IsPassCulled(RGPassId pass) const;
    uint32_t GetPassBarrierCount(RGPassId pass) const;

    // transient 리소스의 수명(첫 사용 패스 ~ 마지막 사용 패스, 실행 순서 기준).
    // 힙 앨리어싱의 재료다 — 수명이 겹치지 않는 둘은 같은 메모리를 쓸 수 있다.
    bool GetTransientLifetime(RGHandle handle, uint32_t& outFirst, uint32_t& outLast) const;

    static D3D12_RESOURCE_STATES ToD3D12(RGResourceState state);

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Resource
    {
        RGTextureDesc   desc;
        ID3D12Resource* external{ nullptr };
        Microsoft::WRL::ComPtr<ID3D12Resource> owned;   // transient
        RGResourceState state{ RGResourceState::Common };
        bool imported{ false };
        bool used{ false };          // 살아남은 패스가 쓰는가 — 아니면 만들지 않는다
        uint32_t firstUse{ 0xFFFFFFFF };
        uint32_t lastUse{ 0 };
        std::string name;
    };

    struct Pass
    {
        std::string              name;
        std::vector<RGPassUsage> usages;
        ExecuteCallback          execute;
        SplitExecuteCallback     splitExecute;
        uint32_t                 maxSlices{ 1 };
        uint32_t                 recordCost{ 0 };
        bool                     hasSideEffect{ false };
        bool                     culled{ false };
        std::vector<D3D12_RESOURCE_BARRIER> barriers;   // 이 패스 직전에 한 번에 넣는다
    };

    bool BuildOrder(std::string& outError);
    void CullPasses();
    bool CreateTransients(ID3D12Device* device, std::string& outError);
    void PlanBarriers();

    std::vector<Resource> m_resources;
    std::vector<Pass>     m_passes;
    std::vector<uint16_t> m_executeOrder;
    DX12GpuProfiler*      m_profiler{ nullptr };
    bool  m_compiled{ false };
    Stats m_stats;

    uint32_t m_parallelCostThreshold{ kParallelRecordCostThreshold };
};

#endif
