#pragma once
#include <atomic>
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

using ConsoleCommandHandler = void(*)(const ConsoleCommandContext&);

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
    void Execute(const std::string& line);

    std::deque<std::string> m_pending;
    std::mutex m_mutex;

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
