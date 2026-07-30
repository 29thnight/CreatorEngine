#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

private:
    ConsoleCommandSystem() = default;
    ~ConsoleCommandSystem() = default;

    void StartStdinReader();
    void LoadScriptFile(const std::string& path);
    void Execute(const std::string& line);
    void PrintHelp() const;

    std::deque<std::string> m_pending;
    std::mutex m_mutex;

    std::thread m_stdinThread;
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_quitRequested{ false };

    // wait N : N 프레임 동안 다음 명령을 보류한다.
    int m_waitFrames{ 0 };
};
#endif // !DYNAMICCPP_EXPORTS
