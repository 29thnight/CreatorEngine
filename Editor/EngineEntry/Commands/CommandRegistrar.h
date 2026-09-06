#pragma once
// LC6 (PHASE 14.5) — 도메인 TU 가 자기 명령을 등록하는 창구.
//
// ── 왜 표를 넘겨주지 않고 창구를 두는가 ─────────────────────────────────
//
// 등록 한 번에 붙는 일이 네 가지다: ① 조회 표 emplace ② 중복 이름을 registry
// 에 알리기 ③ seed 로 descriptor 세우기 ④ LC0 inventory 기록. 도메인 TU 에
// 표를 직접 넘기면 그 넷을 일곱 파일이 각자 하게 되고, 한 곳만 빠뜨려도
// 그 도메인의 명령이 조용히 half-registered 가 된다 — LC3 이 없앤 drift 를
// 파일 수만큼 늘리는 셈이다.
//
// 창구는 그 넷을 한 곳에 묶어 둔다. 도메인 TU 는 "무엇을 등록할지"만 말한다.
//
// ── 핸들러는 도메인 TU 안에서 계속 `static` 이다 ────────────────────────
//
// 등록을 도메인이 직접 하므로 핸들러 주소가 TU 밖으로 나갈 일이 없다. 외부
// 링크 심볼을 181 개 늘리지 않는 것이 요점이다 — 이 프로젝트는 유니티 빌드라
// 심볼 충돌이 엉뚱한 파일에서 터진다.

#include "../ConsoleCommandSystem.h"

#include <initializer_list>

namespace ConsoleCmd
{
    /// 도메인 TU 가 받는 등록 창구.
    ///
    /// 구현은 `ConsoleCommandSystem.cpp` 의 표 생성부에 있다. 이 헤더는 방향을
    /// 지키기 위한 것이다 — 도메인은 창구를 알고, 표는 도메인을 모른다.
    class Registrar
    {
    public:
        /// 이행 전 핸들러. 결과를 내지 않아 `LegacyUnreported` 가 된다.
        virtual void Legacy(std::initializer_list<const char*> names,
                            ConsoleCommandHandler fn) = 0;

        /// LC1 이후의 핸들러. 정확히 하나의 terminal 결과를 만든다.
        ///
        /// 이름을 `Legacy` 와 다르게 둔 것은 오버로드 해석에 기대지 않기
        /// 위해서다 — 이행 중에는 "이 명령이 넘어갔는가"가 등록 줄만 보고
        /// 눈에 보여야 한다.
        virtual void Result(std::initializer_list<const char*> names,
                            ConsoleCommandResultHandler fn) = 0;

        /// 일부러 프로세스를 죽이는 명령. 예외 경계를 통과시킨다.
        ///
        /// 이것이 따로 있는 이유는 실측된 회귀 때문이다 — `Execute` 에 예외
        /// 경계를 두자 죽는 것이 일인 명령이 죽지 않게 됐고, 크래시 덤프 회귀가
        /// 프로세스 종료를 기다리다 타임아웃 났다.
        virtual void Escaping(std::initializer_list<const char*> names,
                              ConsoleCommandHandler fn) = 0;

        /// ★ **검증은 명령이 아니다**(2026-09-06).
        ///
        /// 이 저장소의 기준은 "기본 동작만 명령으로 둔다" 이다. 그런데 registry
        /// 199 개 중 114 개가 특정 자산·시나리오에 묶인 **검증 프로브**였다 —
        /// `dx12.ssao` · `vk.gbuffer` · `experiment.boneresolve` 처럼 하나의
        /// 검사에 하나의 이름이 붙어 있었다.
        ///
        /// 그것들이 명령이 되면 골든 행 · descriptor · help 줄 · 서명 이행
        /// 대상이 되어 영구 유지 비용을 문다. §18 이 "모든 command 가 terminal
        /// `CommandResult` 를 만든다" 를 요구하므로, 이행하는 순간 **나가야 할
        /// 것이 못 나가게 박힌다.**
        ///
        /// 그래서 이름을 registry 에서 뺀다. 기본 동작은 하나다 — **"이름을
        /// 받아 그 검사를 돌린다"**. `selftest <이름>` 이 그것이고, 목록은
        /// registry 가 아니라 아래 표가 갖는다.
        ///
        /// 도메인 TU 는 여기에 검사를 등록한다. `Result` 와 달리 이름이
        /// `commands.list` 에 나오지 않는다 — 명령이 아니기 때문이다.
        virtual void SelfTest(const char* name,
                              ConsoleCommandResultHandler fn) = 0;

    protected:
        // 창구는 표가 소유한다. 도메인이 지울 수 있으면 안 된다.
        ~Registrar() = default;
    };

    // ── 도메인별 등록 진입점 ────────────────────────────────────────────
    //
    // 표 생성부가 이 목록을 차례로 부른다. 도메인을 하나 옮길 때마다 여기에
    // 한 줄이 늘고 `ConsoleCommandSystem.cpp` 에서 그만큼이 빠진다.
    //
    // ★ 등록 줄을 빠뜨리면 그 명령은 **조용히 사라진다.** registry 에도 help
    //   에도 없으므로 둘을 맞대 보는 검사는 전부 초록으로 남는다. 그래서
    //   `commands.selftest` 가 반대 방향(seed 는 있는데 등록이 없다)을 보고,
    //   `verify-cli-registry-golden.ps1` 이 이동 전 표와 맞댄다.

    void RegisterRenderTestCommands(Registrar& reg);   // 검증 프로브 56
    void RegisterRenderDebugCommands(Registrar& reg);
    void RegisterDiagnosticsCommands(Registrar& reg);
    void RegisterScriptUiAnimatorCommands(Registrar& reg);
    void RegisterSceneObjectCommands(Registrar& reg);
    void RegisterAssetAuthoringCommands(Registrar& reg);
    void RegisterCoreCommands(Registrar& reg);  // 라이브 조회·조정 8
}
