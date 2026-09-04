#pragma once
#include "CommandCore/CommandResult.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class ConsoleCommandSystem;

// 명령 하나가 받는 입력 묶음.
//
// 명령을 else-if 사슬이 아니라 이름→핸들러 표로 받게 되면서 생겼다. 사슬일
// 때는 본문이 Execute의 지역 변수(cmd·parts·line)를 그냥 봤지만, 핸들러가
// 독립 함수가 되면 그것들을 넘겨받아야 한다.
struct ConsoleCommandContext
{
    // 실제로 입력된 이름. 한 핸들러가 별칭 여럿을 받을 때 갈라 쓴다
    // (scene.load/scene.switch, mem.stats/mem.delta/mem.reset 등).
    const std::string&              cmd;

    // 공백으로 자른 토큰. parts[0]이 cmd다.
    const std::vector<std::string>& parts;

    // 입력 원문 한 줄. 경로처럼 공백이 들어가는 인자는 토큰이 아니라
    // 여기서 명령어 길이만큼 잘라 쓴다.
    const std::string&              line;

    ConsoleCommandSystem&           system;
};

// 이행 전 핸들러. 결과를 내지 않는다.
//
// LC1 이 이 서명을 한 번에 갈아엎지 않는 이유는 §15 의 위험 표에 있다 —
// 205 개 등록을 동시에 바꾸는 patch 는 검토할 수 없고, 그 안에 섞인 거동 변경
// 하나를 아무도 못 본다. 두 서명이 공존하고 domain 단위로 넘어간다.
using ConsoleCommandHandler = void(*)(const ConsoleCommandContext&);

// LC1 이후의 핸들러. 정확히 하나의 terminal 결과를 만든다.
using ConsoleCommandResultHandler = CommandCore::CommandResult(*)(const ConsoleCommandContext&);

// 콘솔 명령 계층.
//
// 에디터는 GUI 앱이라 자동화 수단이 없었다. 씬을 열고 전환하고 종료하는 일을
// 사람이 클릭해야 해서, 씬 전환 반복 같은 회귀 검증(0-3 벤치마크)을 돌릴 수 없었다.
//
// 이 계층은 표준 입력과 실행 인자로 그 조작을 받아 게임 스레드에서 실행한다.
// 명령 파싱은 백그라운드 스레드가 하고, 실제 수행은 프레임 경계에서만 일어나므로
// 엔진의 스레드 규약("관리 상태는 게임 스레드에서만")을 지킨다.
//
// 사용 예:
//   Academy_4Q.exe --exec "scene.load Assets/Scenes/A.scene" --exec "wait 60" --exec "quit"
//   Academy_4Q.exe --script bench.txt
//   (또는 실행 후 콘솔에 직접 입력)
//
// 명령은 Execute 안의 else-if 사슬이 아니라 이름→핸들러 표가 받는다
// (ConsoleCommandSystem.cpp의 ConsoleCmd 이름공간).
class ConsoleCommandSystem
{
public:
    static ConsoleCommandSystem& Get();

    // 실행 인자를 해석해 --exec / --script / --console 을 처리한다.
    void InitializeFromCommandLine();

    // 매 프레임 게임 스레드에서 호출한다. 큐에 쌓인 명령을 실행한다.
    void Pump();

    // 종료 요청이 들어왔는지. 메인 루프가 확인해 빠져나간다.
    bool IsQuitRequested() const noexcept { return m_quitRequested.load(std::memory_order_acquire); }

    void Shutdown();

    // 외부에서 명령을 밀어 넣는다(스레드 안전).
    void Enqueue(std::string command);

    /// 이미 갈라진 인자로 명령을 밀어 넣는다(스레드 안전).
    ///
    /// ★ **라인 문법을 거치지 않는다.** `arguments[0]` 이 명령 이름이고 나머지가
    ///   인자다. 따옴표도 escape 도 개입하지 않는다 — 이미 갈라져 있으므로.
    ///
    ///   계획 §3.2 가 지적한 왕복 손실이 여기서 원천적으로 없어진다. JSON 이
    ///   `{"args":["Big Boss","Main Characters"]}` 로 가져온 값을 라인으로 이어
    ///   붙였다 다시 자르면 그 과정에서 따옴표가 새는데, 이 경로는 이을 일이 없다.
    ///   LC4 의 수신 스레드가 쓸 진입점이고, 오늘은 `--exec-args` 가 같은 문을 쓴다.
    void EnqueueStructured(std::vector<std::string> arguments);

    // ── 핸들러가 쓰는 표면 ───────────────────────────────────────────────
    //
    // 명령 본문이 독립 함수로 나가면서, 예전에 멤버를 직접 만지던 세 자리가
    // 갈 곳을 잃었다. 통째로 friend를 주는 대신 필요한 만큼만 연다 —
    // 나중에 명령을 다른 파일로 쪼갤 때도 이 표면만 있으면 된다.

    /// quit / exit. 메인 루프가 IsQuitRequested로 받아 빠져나간다.
    void RequestQuit() noexcept;

    /// wait N. 다음 명령을 N 프레임 뒤로 미룬다.
    void SetWaitFrames(int frames) noexcept;

    /// help.
    void PrintHelp() const;

    /// PrintHelp가 찍는 바로 그 문자열.
    ///
    /// LC0(PHASE 14.5)이 "등록된 명령 중 몇 개가 help에 실려 있는가"를 재려면
    /// help가 stdout으로만 존재해서는 안 된다 — 출력을 되읽는 것은 계획이
    /// 끊겠다고 한 바로 그 습관이다. 그래서 문자열을 정본으로 두고 PrintHelp는
    /// 그것을 찍기만 한다. LC3이 descriptor를 세우면 이 상수는 사라진다.
    static const char* HelpText() noexcept;

    /// 에디터 카메라를 게임 카메라와 같은 자세로 맞춘다(camera.editor match).
    /// 두 뷰의 시점을 통일해야 성립하는 대조 실험이 있어서 명령으로 뺐다 —
    /// 마우스로 맞추면 근사치라 "차이가 시점 탓인가 렌더 탓인가"를 못 가른다.
    /// 게임 카메라가 없으면 false.
    static bool MatchEditorCameraToGameCamera();

    /// camera.editor follow on 이 켜져 있으면 매 프레임 위를 다시 부른다.
    /// 게임 스레드의 프레임 경계(App)에서만 읽는다.
    static bool IsEditorCameraFollowing() noexcept;

private:
    ConsoleCommandSystem() = default;
    ~ConsoleCommandSystem() = default;

    void StartStdinReader();
    void LoadScriptFile(const std::string& path);

    /// 한 줄을 토큰으로 자른 뒤 실행한다. **라인 문법은 여기까지만 산다.**
    ///
    /// 반환값이 생긴 것이 LC1 이다. 예전에는 void 였고, 그래서 unknown command 가
    /// printf 한 줄 뒤 그냥 return 했다 — 오타 하나가 조용히 exit 0 이었다.
    CommandCore::CommandResult Execute(const std::string& line);

    /// 이미 갈라진 토큰으로 실행한다. `parts[0]` 이 명령 이름이다.
    ///
    /// 라인 경로와 구조화 경로가 **정확히 여기서 만난다.** 두 입력이 같은
    /// invocation 을 만든다는 §14.2 의 단정이 성립하는 이유다.
    CommandCore::CommandResult ExecuteParsed(const std::vector<std::string>& parts,
                                             const std::string&              diagnosticLine);

    /// 결과를 session 에 넣고 사람이 읽는 줄을 찍는다.
    void PublishResult(const std::string& commandId, const CommandCore::CommandResult& result);

    /// 큐에 남은 명령을 버린다(`--fail-fast`).
    void DiscardPending();

    // 큐에 든 명령 하나.
    //
    // 예전에는 그냥 std::string이었다. LC0이 "명령을 넣은 순간부터 실행될
    // 때까지"를 재려면 넣은 시각이 명령과 함께 다녀야 한다 — 실행 시점에
    // 되짚을 방법이 없기 때문이다. 이 두 필드가 §7.1 지연 예산의 바닥값이고,
    // LC5가 서비스 응답의 timing.queuedMs / waitedFrames로 그대로 승계한다.
    struct PendingCommand
    {
        /// 라인 입력의 원문. structured 입력에서는 진단용으로만 채운다.
        std::string                           text;

        /// 구조화 입력의 토큰(`[0]` 이 명령 이름). 비어 있으면 라인 입력이다.
        std::vector<std::string>              arguments;

        std::chrono::steady_clock::time_point enqueuedAt{};
        uint64_t                              enqueuedFrame{};

        bool IsStructured() const noexcept { return !arguments.empty(); }
    };

    std::deque<PendingCommand> m_pending;
    std::mutex m_mutex;

    // Pump가 도는 횟수. 프레임 경계에서 정확히 한 번이라 프레임 수와 같다.
    // Enqueue가 게임 스레드 밖에서 읽으므로 원자적이어야 한다.
    std::atomic<uint64_t> m_frameIndex{ 0 };

    // 표준 입력 읽기 스레드.
    //
    // 이 스레드는 종료 시 반드시 회수해야 한다. 블로킹 읽기에 갇힌 채로 프로세스가
    // 죽으면 ExitProcess가 CRT 내부(힙 락·iostream 버퍼)의 임의 지점에서 그 스레드를
    // 강제 종료하고, 남은 종료 절차가 그 위에서 힙을 만지게 된다.
    // m_stdinDone은 스레드가 실제로 빠져나왔는지 기한을 두고 확인하기 위한 것이다.
    std::thread m_stdinThread;
    std::promise<void> m_stdinDone;
    std::future<void>  m_stdinDoneFuture;

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_quitRequested{ false };

    // wait N : N 프레임 동안 다음 명령을 보류한다.
    int m_waitFrames{ 0 };

    // --script가 파일을 못 열었는가. 명령이 하나도 없는 무인 실행은 종료시킨다.
    bool m_scriptLoadFailed{ false };
};
