#pragma once
#include "CommandCore/CommandResult.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
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

    /// 명령 하나의 실행이 끝났을 때 불린다. **게임 스레드에서 불린다.**
    struct CommandTiming
    {
        double   queuedMs{ 0.0 };
        uint32_t waitedFrames{ 0 };
        double   executedMs{ 0.0 };
    };
    using CommandCompletion =
        std::function<void(const CommandCore::CommandResult&, const CommandTiming&)>;

    /// 결과를 받을 사람이 있는 구조화 주입(LC4 의 HTTP 수신 스레드).
    ///
    /// completion 은 GT 에서 불리므로 **짧아야 한다**. 수신 스레드는 그 안에서
    /// 값을 넘겨받고 곧바로 깨어난다.
    /// ★ **서비스 큐로 들어간다**(LC5). 배치 큐와 분리한 이유는 §7.2 다 —
    ///   `scripts/scene_churn_benchmark.txt` 같은 기존 시나리오는 프레임 수로
    ///   시간을 재고 `wait N` 이 정확히 N 프레임을 뜻한다는 전제 위에 있다.
    ///   서비스 지연을 위해 드레인을 바꾸면서 그 전제를 같이 바꾸면 81 개
    ///   소비자의 측정값이 조용히 이동한다.
    /// 결과를 돌려받을 사람이 있는 적재(서비스 경로).
    ///
    /// `serviceQueueCap` 이 0 이 아니면 그 상한을 **적재와 같은 락 안에서**
    /// 확인하고, 넘으면 넣지 않고 false 를 돌려준다. 검사와 적재를 떼어 놓으면
    /// 동시 요청이 전부 검사를 통과한 뒤 차례로 들어와 상한을 넘긴다.
    bool EnqueueStructured(std::vector<std::string> arguments, CommandCompletion completion,
                           std::size_t serviceQueueCap = 0);

    /// 서비스 큐의 깊이. 상한을 넘으면 수신 스레드가 429 를 낸다(§7.3).
    std::size_t ServiceQueueDepth() const;
    std::size_t BatchQueueDepth() const;

    /// 명령 등록을 **지금** 끝낸다.
    ///
    /// ★ registry 는 `GetTable()` 의 function-local static 이 채운다. 그 함수는
    ///   오늘 `ExecuteParsed` 에서만 불리므로, 명령이 하나도 안 돈 상태에서는
    ///   표가 비어 있다. `--command-service` 만 준 실행이 정확히 그 상태로
    ///   수신 스레드를 띄운다 — 그러면 ① 첫 요청의 `cost` 조회가 "없는 명령"이
    ///   되어 `game.pak` 같은 Long 명령이 동기로 돌고(§6.2 가 무력화된다)
    ///   ② 게임 스레드가 표를 채우는 도중 수신 스레드가 그것을 훑어 vector 에
    ///   대한 read/write 경합이 난다. 서비스를 열기 전에 여기서 끝내 둔다.
    static void EnsureRegistryPopulated();

    /// 드레인 예산(§7.2). 0 으로 두면 서비스 큐가 돌지 않는다 —
    /// SLO 게이트가 그 상태로 **자기 이빨을 확인한다**(§14.7).
    struct DrainBudget
    {
        double      timeMs{ 2.0 };
        std::size_t count{ 8 };
    };
    void        SetDrainBudget(DrainBudget budget) noexcept;
    DrainBudget GetDrainBudget() const noexcept;

    // ── 서비스가 GT 밖에서 읽는 상태 (LC4) ──────────────────────────────
    //
    // ★ **게임 스레드가 멈춰 있어도 답해야 한다.** LC0 실측이 그 근거다 —
    //   `scene.load` 가 큐를 2.4초 막는 동안 `Pump()` 는 아예 돌지 않았고,
    //   그 구간에 상태를 못 내면 §7.3 의 "멈춤은 지연이 아니라 상태다"가
    //   성립하지 않는다. 그래서 이 값들은 `Pump()` 가 아니라 **명령 실행 전후**에
    //   찍히고, 큐 뮤텍스는 실행 중에는 잡고 있지 않다.
    struct ServiceStatus
    {
        uint64_t    frame{ 0 };

        // ★ **두 큐를 따로 낸다(LC5).**
        //
        //   LC5 가 큐를 둘로 쪼갠 뒤에도 여기는 배치 큐만 보고 있었다. 그래서
        //   `--command-service` 만으로 띄운 자동화 실행 — 배치 입력이 아예 없는
        //   실행 — 에서는 `/health` 가 서비스 큐가 상한까지 차 있어도 **항상**
        //   `queueDepth 0` 을 냈다. 클라이언트는 429 를 받기 직전까지 한가한
        //   서버를 본다. "멈춤은 지연이 아니라 상태다"를 지키겠다고 만든 창구가
        //   정작 LC5 가 새로 만든 줄에 대해서만 눈을 감고 있었다.
        std::size_t serviceQueueDepth{ 0 };
        double      oldestQueuedMs{ 0.0 };   ///< 서비스 큐 선두의 대기 시간
        std::size_t batchQueueDepth{ 0 };

        bool        executing{ false };
        std::string currentCommand;

        // 실행 중이 아니어도 큐가 안 도는 구간이 있다. `wait N` 과 씬 로딩은
        // `Pump()` 를 조기 반환시키므로 `executing` 이 거짓인 채로 서비스 큐가
        // 통째로 멈춘다. 그 구간을 "한가함"으로 내면 관측이 거짓말을 한다.
        bool        sceneLoading{ false };
        uint32_t    waitFramesRemaining{ 0 };
    };
    ServiceStatus SnapshotStatus() const;

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

        /// 결과를 기다리는 사람이 있으면 채워진다(LC4).
        CommandCompletion                     completion;

        /// 서비스(HTTP)에서 왔는가.
        ///
        /// ★ 큐를 가르는 축이자 **session 을 가르는 축**이다. LC4 직후에는
        ///   서비스 명령의 실패가 배치 session 에 누적돼 프로세스 종료 코드를
        ///   바꿨다 — HTTP 로 부른 `scene.load` 하나가 에디터를 exit 3 으로
        ///   끝내게 만들었다. 배치 session 은 배치 시나리오의 판정이어야 한다.
        bool                                  fromService{ false };

        bool IsStructured() const noexcept { return !arguments.empty(); }
    };

    // 배치 큐와 서비스 큐를 나눈다(§7.2).
    //
    // 나누는 것은 구현 편의가 아니라 **기존 측정 의미의 보존**이다. 배치 큐는
    // 프레임당 정확히 하나를 유지하고, 서비스 큐만 예산만큼 드레인한다.
    std::deque<PendingCommand> m_pending;         ///< 배치(--exec·--script·stdin)
    std::deque<PendingCommand> m_servicePending;  ///< 서비스(HTTP)
    mutable std::mutex m_mutex;

    /// 서비스 큐 상한. 0 이면 무제한. 서비스가 `Start` 에서 밀어 넣는다.
    std::atomic<size_t>   m_serviceQueueCap{ 0 };

    /// `Pump()` 가 매 프레임 갱신하는 조기 반환 사유. GT 밖에서 읽는다.
    std::atomic<bool>     m_sceneLoading{ false };
    std::atomic<uint32_t> m_waitFramesRemaining{ 0 };

    // Pump가 도는 횟수. 프레임 경계에서 정확히 한 번이라 프레임 수와 같다.
    // Enqueue가 게임 스레드 밖에서 읽으므로 원자적이어야 한다.
    std::atomic<uint64_t> m_frameIndex{ 0 };

    // 서비스가 GT 밖에서 읽는 실행 상태. 짧게만 잡는다.
    mutable std::mutex    m_statusMutex;
    std::string           m_currentCommand;
    std::atomic<bool>     m_executing{ false };

    // 드레인 예산. 게이트가 0 으로 바꿔 SLO 회귀를 재현한다(§14.7).
    std::atomic<double>      m_drainTimeMs{ 2.0 };
    std::atomic<size_t>      m_drainCount{ 8 };

    /// 지금 도는 명령이 서비스에서 왔나.
    ///
    /// 원자적이지 않아도 되는 이유: 쓰는 곳(`RunOne`)과 읽는 곳(`ExecuteParsed`)이
    /// 같은 동기 호출 사슬(`Pump → RunOne → ExecuteParsed`) 안이라 **항상 게임
    /// 스레드 하나**다. 그 전제가 깨지면(다른 스레드가 `RunOne` 을 부르면) 이
    /// 변수는 조용히 경합한다 — `RunOne` 은 게임 스레드 전용이다.
    bool                     m_executingFromService{ false };

    /// 큐에서 하나를 꺼내 실행하고 계측·판정을 남긴다. 실행했으면 true.
    bool RunOne(PendingCommand pending, uint64_t frameIndex);

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
