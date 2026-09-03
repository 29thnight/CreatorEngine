#pragma once
// LC0 — 라이브 커맨드 서비스(PHASE 14.5)의 기준선 계측.
//
// 이 파일은 명령을 하나도 바꾸지 않는다. 재는 것만 한다.
//
// ── 왜 재는 것이 먼저인가 ────────────────────────────────────────────────
//
// 계획(EditorAutomationCLIPlan.md §7.1)이 "짧은 명령 왕복 p50 50ms / p95 150ms"를
// 목표로 잡았는데, **그 예산의 분모가 저장소 어디에도 없다.** 에디터 한 프레임이
// 몇 ms인지 아무도 재지 않았다. 실측 없이 "60fps니까 16ms"라고 적으면 그 순간부터
// 계획이 거짓말을 시작하고, LC5의 SLO 게이트는 지어낸 숫자를 지키게 된다.
//
// 마찬가지로 §2.1의 명령 수(219개 이름 · 184개 handler)는 C++ 소스를 grep한 값이다.
// 소스 스크래핑은 `Invoke-Dx12Suite`가 이미 두 번 틀린 방식이고(35개 중 26개만
// 실행, 그리고 등록 형식이 바뀌자 35개를 0개로 읽음), 계획이 끊겠다고 한 바로 그
// 습관이다. 여기서 런타임 표를 직접 덤프해 그 습관을 LC0에서 끝낸다.
//
// ── 의존을 지지 않는다 ───────────────────────────────────────────────────
//
// 이 모듈은 Scene·SceneManager·CoreWindow를 모른다. 프레임 상태는 호출자
// (ConsoleCommandSystem::Pump)가 판정해 값으로 넘긴다. LC4에서 이 계측이
// Engine/CommandService로 따라갈 수 있어야 하고, 그때 Editor 헤더를 물고 가면
// Player가 같은 코드를 못 쓴다(계획 §12의 의존 방향 규칙).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CommandBaseline
{
    // ── 등록 표 ──────────────────────────────────────────────────────────
    //
    // 오늘의 명령 표는 `unordered_map<std::string, ConsoleCommandHandler>` 하나다.
    // 그 표는 두 가지를 잃는다.
    //
    //   ① **별칭이 별도 entry다.** reg({"quit","exit"}, &Cmd_quit)는 항목을 둘
    //      만들고, 어느 쪽이 canonical인지는 표에 남지 않는다.
    //   ② **순회 순서가 비결정적이다.** unordered_map이라 실행마다 순서가 다르다.
    //      정렬 없이 덤프한 artifact는 매 실행 diff가 난다.
    //
    // 그래서 조회용 표는 그대로 두고, 등록 시점에 canonical/alias 관계를 여기에
    // 한 벌 더 남긴다. 이 벡터가 LC3 descriptor registry의 씨앗이다 — LC3이 서면
    // 같은 artifact를 descriptor에서 다시 생성해 두 값이 일치하는지 대조한다.
    struct Registration
    {
        std::string              canonical;   // reg()에 처음 적힌 이름 중 실제로 표에 들어간 것
        std::vector<std::string> aliases;     // 나머지 중 표에 들어간 것
        std::vector<std::string> rejected;    // 이름 충돌로 표에 못 들어간 것
        std::size_t              handlerIndex{}; // handler 포인터의 안정 서수(주소 아님)
    };

    /// 명령 등록 하나를 기록한다. ConsoleCmd::GetTable()의 reg 람다가 부른다.
    ///
    /// handler 주소는 ASLR로 실행마다 달라지므로 저장하지 않고, 최초 등장 순서로
    /// 매긴 서수만 남긴다 — artifact가 실행 간 diff 0이어야 하기 때문이다.
    ///
    /// ★ `accepted`와 `rejected`를 나눠 받는 이유. 조회 표는 `emplace`라서 같은
    ///   이름이 두 번 등록되면 **나중 것이 조용히 버려진다**. 그때 inventory가
    ///   등록 시도를 그대로 적으면, 실제로는 살아 있지 않은 이름이 "이 handler로
    ///   간다"고 주장하게 된다. LC0의 존재 이유가 소스 스크래핑과 런타임의 drift를
    ///   없애는 것인데, 그 자리에서 새 drift를 만드는 셈이다. 그래서 호출자가
    ///   emplace 결과를 갈라 넘기고, 버려진 이름은 별도 열로 남긴다.
    void RecordRegistration(const std::vector<const char*>& accepted,
                            const std::vector<const char*>& rejected,
                            const void*                     handler);

    /// 등록 순서대로. 정렬은 덤프 시점에 한다.
    const std::vector<Registration>& Registrations() noexcept;

    // ── 프레임·명령 계측 ─────────────────────────────────────────────────

    /// 한 프레임의 성격. 프레임 시간 분포를 조건별로 가르는 축이다.
    /// 계획 §13 LC0이 요구하는 네 조건(포커스/비포커스/씬 로딩/재생 중)에
    /// `waiting`(wait N 보류)을 더했다 — 큐가 멈추는 시간 분포도 같은 슬라이스의
    /// 산출물이라 같은 표에서 나오는 편이 대조하기 쉽다.
    struct FrameState
    {
        bool focused{};
        bool sceneLoading{};
        bool playing{};
        bool waiting{};

        // 창 핸들을 실제로 물어볼 수 있었는가.
        //
        // 이것이 없으면 focused=false가 두 가지를 동시에 뜻한다 — "창이 앞에
        // 없다"와 "창을 못 찾았다". 앞은 관측이고 뒤는 계측 고장이다. 둘을
        // 섞으면 "비포커스 100%"라는 결론이 고장을 근거로 나올 수 있다.
        bool windowKnown{};
    };

    /// 계측이 켜져 있는가. **기본은 꺼져 있다.**
    ///
    /// ★ 계측 비용을 제품 상태에 항상 얹지 않는다. 켜 두면 매 프레임
    ///   `GetForegroundWindow()` 시스템 호출과 표본 적재가 붙는데, 그것은
    ///   ① 아무도 재지 않는 실행에도 붙는 상시 비용이고 ② 하필 **재려는 대상인
    ///   프레임 시간**을 흔든다. 관측이 관측 대상을 바꾸면 그 값은 분모로 못 쓴다.
    ///   꺼져 있을 때 드는 비용은 원자 변수 읽기 하나다.
    bool IsCollecting() noexcept;

    /// 계측을 켜고 끈다. 켤 때 이전 표본과 프레임 기준점을 함께 버린다 —
    /// 껐다 켠 구간을 가로지르는 delta는 프레임 시간이 아니라 정지 시간이다.
    void SetCollecting(bool enabled) noexcept;

    /// 프레임 경계에서 정확히 한 번. Pump()가 그 성질을 이미 가지고 있어서
    /// 새 호출 지점을 만들지 않는다. 꺼져 있으면 즉시 반환한다.
    void NoteFrame(FrameState state);

    /// 명령 하나가 큐에서 나와 실행을 마쳤다.
    ///   queuedMs     : Enqueue → 큐에서 꺼낸 시각 (지연의 바닥값)
    ///   waitedFrames : 그 사이에 지나간 프레임 수
    ///   executedMs   : handler 실행 시간
    void NoteCommand(std::string_view name, double queuedMs, uint64_t waitedFrames, double executedMs);

    /// 지금까지 모은 표본을 버린다. 측정 구간을 명령으로 여닫기 위한 것.
    void ResetSamples() noexcept;

    // ── 산출 ─────────────────────────────────────────────────────────────
    //
    // 형식은 TSV다. JSON이 아닌 이유는 계획 §4가 JSON codec을 LC4의 산출물로
    // 두었기 때문이다 — 여기서 손으로 JSON을 흉내 내면 LC4가 걷어내야 할 두 번째
    // codec이 생긴다. TSV는 저장소의 기존 기준선 자산(lifecycle_baseline.tsv,
    // mbc_cutover_freeze.baseline.tsv)과 같은 형식이고 그대로 diff된다.

    /// 이름↔handler 그룹 inventory와 help 대조. helpText는 PrintHelp가 찍는 그 문자열.
    bool WriteInventory(const std::string& path, const char* helpText);

    /// 프레임 시간 분포와 명령 왕복 지연.
    bool WriteTiming(const std::string& path);
}
