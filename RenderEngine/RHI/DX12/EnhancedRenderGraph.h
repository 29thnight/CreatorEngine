#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../RHIFormat.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <wrl/client.h>
#include <d3d12.h>

#include "DX12GpuProfiler.h"
#include "DX12CommandListPool.h"
#include "../RHIEncoder.h"   // ExecuteContext::encoder 를 받는 쪽은 예외 없이 역참조한다
#include "../RHIHandle.h"
#include "../RHIResourceState.h"

/// G-2a. 그래프가 드는 것이 이제 이 인터페이스다. 포인터·참조만 쓰므로
/// 이름만 안다 — 정의는 `RenderFrameServices.h` 에 있고 그쪽이 d3d12 를
/// 문다(그 헤더가 `RHI/` 로 올라가면 이 전방 선언도 사라진다).
class IRenderDeviceServices;

// DX12 실행 경로용 렌더 그래프 (PHASE 3-5).
//
// 기존 RenderGraphBuilder(RenderEngine/RenderGraphBuilder.*)를 잇지 않고 새로 썼고,
// 그쪽은 그 뒤로 참조가 0곳인 채 남아 있다가 제거했다. 잇지 않은 이유:
//   - 싱글턴이다. 그래프는 카메라별·프레임별로 있어야 하는데 프로세스에 하나뿐이면
//     씬 뷰와 게임 뷰가 같은 그래프를 공유하게 된다.
//   - 배리어 유도가 없다. 위상 정렬까지만 있고, DX12에서 그래프를 두는 이유의
//     절반이 배리어 자동화인데 그 절반이 비어 있다.
//   - 리소스 타입이 Texture*와 uint32 format 자리표시자다.
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

    RHIFormat   format{ RHIFormat::RGBA8Unorm };
    bool        allowRenderTarget{ false };

    // 최적 클리어 값. 리소스를 만들 때 넘기는 힌트이고, 실제 ClearRenderTargetView가
    // 다른 값을 쓰면 검증 레이어가 "느려진다"고 경고한다. 힌트를 안 주는 것도
    // 경고 대상이라(3-3 실측) 기본값을 두고 필요하면 호출부가 맞춘다.
    float       clearColor[4]{ 0.f, 0.f, 0.f, 0.f };
    bool        allowDepthStencil{ false };
    bool        allowUnorderedAccess{ false };
    std::string name;
};

// transient 리소스 풀 (PHASE 3-9, 상시 러너의 프레임당 생성 비용 제거).
//
// 그래프는 프레임마다 새로 만들어지는데 transient도 매번 CreateCommittedResource로
// 만들면 상시 실행에서 그 비용이 프레임당 수십 ms로 쌓인다(실측 — 테스트는
// 몇 프레임뿐이라 안 보였다). 풀은 desc 해시 키로 (리소스, 마지막 상태)를
// 보관하고, 그래프가 소멸할 때(호출부가 GPU 완료를 보장한 뒤) 반납받는다.
// 상태를 함께 보관하는 이유: 재사용 리소스는 COMMON이 아니라 지난 프레임의
// 마지막 상태로 시작하고, 배리어 계획이 그 상태에서 출발해야 맞는다.
class RGTransientPool
{
public:
    // ★ V2-c2에서 ComPtr이 핸들로 바뀌었다. 리소스는 표가 계속 들고 있고,
    //   풀은 "어느 핸들이 지금 놀고 있는가"만 안다 — 반납·재대여 때 표에서
    //   빼고 넣지 않으므로 핸들이 프레임을 넘어 그대로 유효하다.
    struct Entry
    {
        RHITextureHandle handle;
        RHIResourceState state{ RHIResourceState::Common };
    };
    std::unordered_map<uint64_t, std::vector<Entry>> freeList;
};

class EnhancedRenderGraph
{
public:
    // 패스가 기록할 때 받는 것. 커맨드 리스트는 호출부가 준 것을 그대로 넘긴다 —
    // 그래프는 순서와 배리어를 정할 뿐 기록 방식을 정하지 않는다(3-6에서 패스별
    // 커맨드 리스트를 워커가 병렬로 기록하게 될 때 이 경계가 필요하다).
    struct ExecuteContext
    {
        const EnhancedRenderGraph* graph{ nullptr };

        // 패스가 커맨드를 적는 통로 (R3).
        //
        // ★ V2-d에서 commandList가 사라지고 이 자리만 남았다 — R3가
        //   "다 옮기고 나면 그렇게 된다"고 적어 둔 그대로다(계획서 §3.3).
        //   마지막까지 원시 커맨드 리스트를 붙들던 것은 자가 검증의 리드백
        //   복사 35곳이었고, 그것이 인코더로 오면서 통로가 하나가 됐다.
        //
        //   '검증용이니까 원시로 둔다'가 안 되는 이유: 검증이 도는 경로와
        //   실제로 그리는 경로가 같아야 검증이 뜻을 갖는다.
        //
        //   조각마다 다른 인코더다. AddSplitPass의 워커들이 각자 다른 커맨드
        //   리스트에 적으므로, 프레임 전역 인코더를 쓰면 조각이 서로의 기록을
        //   덮는다.
        RHIEncoder* encoder{ nullptr };

        // 선언한 리소스의 핸들. transient든 임포트든 같은 표에 있다 (V2-c2).
        //
        // ★ 이것이 V2-c의 목적이다. 그래프 리소스와 패스 소유 리소스가 같은
        //   어휘(RHITextureHandle)로 나오면 경계 desc가 둘을 구분 없이 받을 수
        //   있다 — V2-b가 그 위에 선다. 그 전에는 desc가 "그래프 것이면
        //   포인터, 패스 것이면 핸들"을 둘 다 받아야 했다.
        RHITextureHandle ResolveHandle(RGHandle handle) const;

        // ★ `ID3D12Resource* Resolve(RGHandle)` 가 여기 있었다. G-1 에서
        //   지웠다 — 그 선언이 스스로 적어 둔 소멸 조건("V2-b·V3·V4가
        //   소비처를 다 걷어내 호출자가 0이 되면 지운다")이 충족됐다.
        //   113곳이 전부 `ResolveHandle` 로 갔고 마지막 넷은 A-6 이 걷었다.
        //
        //   미루면서 조건을 적어 둔 값이 이렇게 회수된다 — 다시 판단하지
        //   않고 충족만 확인하면 됐다(A-2 가 `RHIResourceState` 에서 한 것과
        //   같은 방식).
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
    //
    // stateWriteback — 프레임이 끝났을 때 이 리소스가 어느 상태로 남는지를
    // Compile이 여기에 적어 준다(널이면 적지 않는다).
    //
    // ★ 프레임을 넘겨 사는 리소스에 필요하다. 그래프는 프레임마다 새로
    // 세워지므로 transient가 될 수 없는 것들(볼류메트릭 포그의 프록셀 격자
    // 같은 시간축 히스토리)은 패스가 들고 매 프레임 다시 Import하는데,
    // 그때 알려 줄 '현재 상태'를 패스가 스스로 알 방법이 없다. 자기 패스가
    // 마지막으로 쓴 상태를 적어 두는 것으로는 모자란다 — 뒤에 붙은 소비자가
    // 한 번 더 전이시키면 그 값이 어긋나고, 다음 프레임의 첫 배리어가
    // 틀린 before 상태로 나간다(실제로 리드백 패스가 그렇게 잡혔다).
    // 상태를 아는 것은 그래프뿐이므로 그래프가 알려 준다.
    /// transient 풀 연결(선택). 있으면 Compile이 생성 대신 재사용을 시도하고,
    /// 소멸자가 반납한다 — 호출부는 소멸 시점에 GPU 완료를 보장해야 한다
    /// (그래프 수명 규칙과 같은 계약).
    void SetTransientPool(RGTransientPool* pool) { m_transientPool = pool; }

    /// 디바이스 서비스는 필수다 — 인코더가 이것 없이는 아무것도 못 건다.
    ///
    /// ★ R3-1이 이것을 setter(SetDeviceServices)로 두면서 "선택이다, 안 주면
    ///   ClearUnorderedAccess 하나만 아무 일도 하지 않는다"고 적었다. 그런데
    ///   그 setter는 만들어진 뒤로 호출자가 하나도 없었고, 유일한 소비자도
    ///   호출자가 0이라 아무도 눈치채지 못했다. R4-1에서 렌더 타깃 바인딩이
    ///   인코더로 오자 그 즉시 전 패스가 타깃 없이 그렸다 — 자가 검증 34종 중
    ///   32종이 한꺼번에 실패해서 잡혔다.
    ///
    ///   그래서 생성자로 옮긴다. 부르는 것을 잊으면 조용히 잘못 그리는 setter
    ///   대신, 안 주면 컴파일이 안 되는 인자로 둔다.
    /// ★ 중립 생성자 (G-2a). 그래프가 `IRenderDeviceServices` 만 알면 된다는
    ///   것이 실측으로 확인됐다 — 부르는 것 여섯 중 다섯이 이미 인터페이스에
    ///   있었고, 배리어는 `TransitionResources`(V3)·`UavBarrier`(A-6)로,
    ///   인코더는 `GetImmediateEncoder()`(A-3)로 난다.
    ///
    ///   이 생성자로 만든 그래프는 **`ExecuteParallel` 이 거부한다** — 워커
    ///   리스트마다 인코더가 따로여야 하는데 그것을 중립으로 표현할 길이 아직
    ///   없다(G-3). 아래 DX12 생성자가 그 길을 연다.
    explicit EnhancedRenderGraph(IRenderDeviceServices& services);

    /// DX12 전용 생성자. 위와 같고 **병렬 실행이 추가로 가능하다.**
    explicit EnhancedRenderGraph(class DX12DeviceResources& resources);

    ~EnhancedRenderGraph();

    /// 이미 표에 있는 리소스를 들인다 — 패스가 소유한 것(V2-a로 핸들이 된 것들).
    RGHandle ImportTexture(RHITextureHandle resource, RHIResourceState currentState,
        const std::string& name, RHIResourceState* stateWriteback = nullptr);

    /// 표에 없는 리소스를 들인다 — 스왑체인 백버퍼·공유 텍스처·자가 검증이
    /// 손으로 만든 ComPtr처럼 아직 핸들이 아닌 것들.
    ///
    /// ★ 위 오버로드와 갈라 두는 것이 의도다. 둘은 "표에 넣어야 하는가"가
    ///   다르고, 그래서 놓는 책임도 다르다 — 이쪽은 그래프가 표에 등록했으니
    ///   그래프가 소멸할 때 놓는다. 한 함수로 합치면 그 차이가 인자 타입이
    ///   아니라 주석에만 남는다.
    ///
    ///   과도기가 아니다. 백버퍼처럼 스왑체인이 소유하는 것은 끝까지 표 밖에
    ///   있을 수 있다. 다만 손으로 만든 ComPtr을 넘기는 자가 검증 쪽은
    ///   V4까지 가면서 위 오버로드로 옮겨 갈 것이다.
    RGHandle ImportTexture(ID3D12Resource* resource, RHIResourceState currentState,
        const std::string& name, RHIResourceState* stateWriteback = nullptr);

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
        RHIResourceState state{ RHIResourceState::Common };
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
    /// ★ 디바이스를 받지 않는다 (G-1). 그래프는 `DX12DeviceResources&` 를
    ///   생성자로 이미 들고, 호출부 37곳이 전부 거기서 꺼낸 값을 도로
    ///   넘기고 있었다 — A-3 이 VolFog 의 `ClearUnorderedAccess(commandList,
    ///   ...)` 에서 없앤 것과 같은 동어반복이다.
    bool Compile(std::string& outError);

    // Compile이 정한 순서대로 배리어를 넣고 패스를 기록한다.
    /// 〃 (G-1). 호출부 30곳이 `resources.GetCommandList()` 를 넘겼다.
    bool Execute(std::string& outError);

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
    /// 〃 큐도 받지 않는다 (G-1). 풀은 남는다 — 호출부가 소유하고 수명도
    /// 그쪽 것이라, 그래프가 꺼내 올 수 있는 값이 아니다.
    bool ExecuteParallel(DX12CommandListPool& pool,
        uint32_t workerCount, std::string& outError);


    bool IsPassCulled(RGPassId pass) const;
    uint32_t GetPassBarrierCount(RGPassId pass) const;

    // transient 리소스의 수명(첫 사용 패스 ~ 마지막 사용 패스, 실행 순서 기준).
    // 힙 앨리어싱의 재료다 — 수명이 겹치지 않는 둘은 같은 메모리를 쓸 수 있다.
    bool GetTransientLifetime(RGHandle handle, uint32_t& outFirst, uint32_t& outLast) const;

    static D3D12_RESOURCE_STATES ToD3D12(RHIResourceState state);

private:
    IRenderDeviceServices* m_deviceServices{ nullptr };   // 생성자가 반드시 채운다

    /// 병렬 실행에만 쓴다 (G-2a). 중립 생성자로 만들면 null 이고
    /// `ExecuteParallel` 이 거부한다 — G-3 이 이 필드를 없앤다.
    class DX12DeviceResources* m_dx12Parallel{ nullptr };

public:

private:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    struct Resource
    {
        RGTextureDesc    desc;

        // 표 안의 자리. 임포트든 transient든 같은 표를 쓴다 (V2-c2) —
        // 예전의 external 포인터 / owned ComPtr 두 갈래가 이 한 칸이 됐다.
        RHITextureHandle handle;

        // 임포트를 그래프가 표에 등록했는가(= 그래프가 놓아야 하는가).
        //
        // ★ 임포트에만 뜻이 있다. transient는 이 플래그를 보지 않는다 —
        //   '그래프가 끝나면 내놓는다'가 예외 없는 규칙이라, 그것을 기억하는
        //   플래그를 두면 세우는 자리가 갈리고 언젠가 한쪽을 빠뜨린다.
        //   실제로 V2-c2가 풀에서 빌려 오는 경로에서 빠뜨렸고, 한 프레임
        //   걸러 전 transient를 다시 만들었다(ReleaseResources 주석 참고).
        bool     ownsRegistration{ false };
        uint64_t poolKey{ 0 };                          // 풀 반납용 desc 해시
        RHIResourceState state{ RHIResourceState::Common };
        RHIResourceState* writeback{ nullptr };   // 프레임 끝 상태를 적어 줄 곳
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
        // ★ 이 패스 직전에 한 번에 낸다. G-2a 에서 `D3D12_RESOURCE_BARRIER`
        //   벡터 하나가 중립 둘로 갈렸다 — 계획 단계가 백엔드 타입을 조립하지
        //   않으므로, 실물을 만드는 것은 기록 시점의 백엔드다.
        std::vector<RHITransition>    transitions;
        std::vector<RHITextureHandle> uavBarriers;
    };

    void ReleaseResources();
    bool BuildOrder(std::string& outError);
    void CullPasses();
    /// 〃 (G-1). 이제 `IRenderDeviceServices::CreateTexture` 로 만든다 —
    /// 막고 있던 것은 desc 어휘였다(깊이 타깃 · 클리어 힌트).
    bool CreateTransients(std::string& outError);
    void PlanBarriers();

    /// 계획한 배리어를 워커 리스트에 직접 넣는다 (G-2a · DX12 전용).
    ///
    /// ★ 순차 경로는 이것을 안 쓴다 — 거기서는 서비스의 `TransitionResources`
    ///   와 인코더의 `UavBarrier` 가 같은 일을 중립으로 한다. 워커 리스트가
    ///   '지금 열린 리스트'가 아니라서 그 길이 막힌 자리만 여기로 온다.
    void RecordPassBarriers(ID3D12GraphicsCommandList* commandList, const Pass& pass) const;

    std::vector<Resource> m_resources;
    RGTransientPool* m_transientPool{ nullptr };
    std::vector<Pass>     m_passes;
    std::vector<uint16_t> m_executeOrder;
    DX12GpuProfiler*      m_profiler{ nullptr };
    bool  m_compiled{ false };
    Stats m_stats;

    uint32_t m_parallelCostThreshold{ kParallelRecordCostThreshold };
};

#endif
