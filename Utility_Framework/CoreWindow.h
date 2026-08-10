#pragma once
#include <windows.h>
#include <functional>
#include <unordered_map>
#include <directxtk/Keyboard.h>
#include <shellapi.h> // 추가
#include "DumpHandler.h"
#include "EngineMode.h"
// <strsafe.h> include가 여기 있었다 (2026-08-10). 유일한 소비자가
// 표시 모드 전환의 StringCchCopyW(대상 모니터 장치명 복사)였다.

#pragma warning(disable: 28251)
#define MAIN_ENTRY int WINAPI

class CoreWindow
{
public:
    using MessageHandler = std::function<LRESULT(HWND, WPARAM, LPARAM)>;

    // iconResourceId: 창 클래스 아이콘의 리소스 ID. 0이면 기본 아이콘.
    //
    // ★ 예전에는 여기(코어)가 EngineEntry/Resource.h를 include해 에디터
    //   아이콘 ID를 직접 알았다 — 아래층이 실행 파일 층의 리소스를 아는
    //   상향 간선이고, TrainAsis의 동명 헤더가 사라지자 경계 게이트가
    //   잡아냈다(B0-3). 아이콘은 exe의 소유물이므로 exe가 넘긴다.
    CoreWindow(HINSTANCE hInstance, const wchar_t* title, int width, int height,
        int iconResourceId = 0)
        : m_hInstance(hInstance), m_width(width), m_height(height)
        , m_iconResourceId(iconResourceId)
    {
        s_instance = this;
        RegisterWindowClass();
        CreateAppWindow(title);

        // 크래시 후크는 여기서 걸지 않는다 — LogSystem::InstallCrashGuards가 이미
        // 전부 걸어 두었고, 여기서 또 걸면 설치 순서 싸움이 된다.
        // 덤프 기록자 등록은 SetDumpType에서 한다.
    }

    ~CoreWindow()
    {
        // RestoreDisplayMode() 호출이 여기 있었다 (2026-08-10).
        // 플레이어가 표시 모드를 바꾸지 않게 된 뒤로 되돌릴 것이 없다.

        if (m_hWnd)
        {
            DestroyWindow(m_hWnd);
        }
        UnregisterClass(L"CoreWindowApp", m_hInstance);

        // 창은 App::Initialize의 지역 객체라 종료 단계에서 먼저 사라진다.
        // s_instance를 그대로 두면 그 뒤에 난 크래시가 WriteCrashDump에서
        // 파괴된 객체의 m_hWnd를 읽어, 덤프를 쓰기도 전에 2차 크래시로 죽는다.
        if (s_instance == this) s_instance = nullptr;
    }

    template <typename Instance>
    void RegisterHandler(UINT message, Instance* instance, LRESULT(Instance::* handler)(HWND, WPARAM, LPARAM))
    {
        m_handlers[message] = [=](HWND hWnd, WPARAM wParam, LPARAM lParam)
        {
            return (instance->*handler)(hWnd, wParam, lParam);
        };
    }

    template <typename Initializer>
    CoreWindow InitializeTask(Initializer fn_initializer)
    {
        fn_initializer();

        return *this;
    }

    template <typename MessageLoop>
    void Then(MessageLoop fn_messageLoop)
    {
        MSG msg = {};
        while (true)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    break;
                }
                else
                {
                    DispatchMessage(&msg);
                }
            }
            else
            {
                fn_messageLoop();
            }
        }
    }

    /// 크래시 덤프를 남긴다. Log가 크래시 경로에서 1회만 불러 준다.
    ///
    /// 예전에는 여기가 SEH 필터를 직접 걸었는데, LogSystem::InstallCrashGuards가
    /// 같은 후크를 이미 걸고 있어 설치 순서에 따라 서로를 덮어썼다. 그래서
    /// 어떤 크래시는 덤프가 남고 어떤 크래시는 아무것도 남지 않았다.
    /// 이제 후크 설치는 LogSystem 한 곳이 맡고, 여기는 기록만 한다.
    static void WriteCrashDump(void* exceptionPointers, const char* reason)
    {
        // 무인 모드(--script/--exec로 돌린 CLI·CI)에서는 물어보지 않고 바로 남긴다.
        // 대화상자를 띄우면 아무도 답하지 않아 그대로 멈춰 있다가 덤프 없이 죽는다.
        if (!g_unattended)
        {
            const int answer = MessageBox(NULL, L"Should Create Dump ?", L"Exception",
                MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND);
            if (IDYES != answer) return;
        }

        auto* pointers = static_cast<EXCEPTION_POINTERS*>(exceptionPointers);
        HWND window = s_instance ? s_instance->m_hWnd : nullptr;

        // 덤프 기록 중 다시 죽더라도 로그는 남도록 먼저 밀어낸다.
        Log::FlushNow();

        // 순서가 핵심이다: .dmp를 먼저 쓰고 요약은 그 뒤에 만든다.
        //
        // 요약을 만드는 BuildCrashReport는 dbghelp로 스택을 걷고 std::string을
        // 늘려 가는데, 힙이 손상됐거나 스택이 고갈된 프로세스에서는 바로 거기서
        // 또 죽는다. 예전에는 요약이 먼저였던 탓에 로그에 CRASH 줄만 남고
        // .dmp도 스택도 통째로 없어진 크래시가 실제로 있었다.
        const file::path dumpPath = WriteMinidumpFile(pointers, g_dumpType);

        // SEH 경로는 예외 컨텍스트가 있어 정확한 크래시 지점을 뜰 수 있다.
        // 나머지(abort·terminate 등)는 지금 이 자리의 스택으로 대신한다.
        const std::string report = (nullptr != pointers)
            ? BuildCrashReport(pointers)
            : BuildAbnormalExitReport(reason, nullptr);

        WriteCrashReportArtifacts(dumpPath, report, window);
    }

    /// 무인 실행 여부. CLI가 --script/--exec를 받았을 때 켠다.
    static void SetUnattended(bool unattended) { g_unattended = unattended; }

    static bool IsUnattended() { return g_unattended; }

    static void SetDumpType(DUMP_TYPE dumpType)
    {
        g_dumpType = dumpType;

        // 덤프 종류가 정해지는 유일한 지점이라 여기서 기록자를 등록한다.
        // 후크 자체는 LogSystem이 걸어 두었고, 크래시 경로 전부(SEH·terminate·abort·
        // purecall·CRT 잘못된 인자)가 이 콜백으로 모인다.
        //
        // 부팅 초반(EngineBootstrap::InitializeRuntime)에서 한 번 부르는 것이 계약이다.
        // 예전에는 App::Initialize 중반에서만 불려서, 그 전에 죽으면 로그만 남고
        // 덤프는 없었다.
        Log::SetCrashDumpWriter(&CoreWindow::WriteCrashDump);

        // 종료 단계 크래시에서도 GitHash를 읽을 수 있도록 지금 복사해 둔다.
        CacheCrashGitHash();

        // 쌓인 덤프를 지금 정리한다. 디스크가 차면 다음 크래시의 덤프가 실패한다.
        PruneOldDumpFiles();
    }

    /// 숨겨 둔 창을 표시한다. 에디터 부팅이 끝난 뒤 한 번 부르는 계약이다.
    /// (게임 빌드는 CreateAppWindow가 즉시 표시하므로 다시 불러도 무해하다.)
    void Show()
    {
        if (m_hWnd && !IsWindowVisible(m_hWnd))
        {
            ShowWindow(m_hWnd, SW_SHOWNORMAL);
            UpdateWindow(m_hWnd);
            SetForegroundWindow(m_hWnd);
        }
    }

    HWND GetHandle() const { return m_hWnd; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    void EnsureSwapChainCompatibleStyle();

    static CoreWindow* GetForCurrentInstance()
    {
        return s_instance;
    }
    
	static void RegisterCreateEventHandler(MessageHandler handler)
	{
		m_CreateEventHandler = handler;
	}

private:
    static CoreWindow* s_instance;
    static DUMP_TYPE g_dumpType;
    static inline bool g_unattended{ false };
    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    int m_width = 800;
    int m_iconResourceId = 0;
    int m_height = 600;
    std::unordered_map<UINT, MessageHandler> m_handlers;
	static MessageHandler m_CreateEventHandler;

    // 표시 모드 전환 상태 넷(m_displayModeChanged · m_targetDeviceName ·
    // m_originalMode · m_hasOriginalMode)이 여기 있었다 (2026-08-10).
    // 플레이어가 테두리 없는 '창모드'가 되면서 바꿀 모드가 없어졌다.

    void RegisterWindowClass() const
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = CoreWindow::WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = L"CoreWindowApp";
        if (0 != m_iconResourceId)
        {
            wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(m_iconResourceId));
        }
        RegisterClass(&wc);
    }

    void CreateAppWindow(const wchar_t* title)
    {
        RECT rect{};
        GetWindowRect(GetDesktopWindow(), &rect);
        int x = (rect.right - rect.left - m_width) / 2;
        int y = (rect.bottom - rect.top - m_height) / 2;

        // 제목 표시줄 높이 가져오기
        int titleBarHeight = GetSystemMetrics(SM_CYCAPTION); // 제목 표시줄 높이
        int borderHeight = GetSystemMetrics(SM_CYFRAME);     // 상단 프레임 높이
        int borderWidth = GetSystemMetrics(SM_CXFRAME);      // 좌우 프레임 너비

        // 클라이언트 영역 조정
        rect = { 0, 0, m_width, m_height };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        // ── 모드 분기 (B0-1: BUILD_FLAG → 런타임 EngineMode) ──
        // 에디터는 일반 창, 플레이어는 보더리스 전체화면 + 해상도 강제 전환.
        if (EngineMode::IsEditor())
        {
            m_hWnd = CreateWindowEx(
                0,
                L"CoreWindowApp",
                title,
                WS_OVERLAPPEDWINDOW,
                x, y,
                rect.right - rect.left,
                rect.bottom - rect.top /*+ titleBarHeight + borderHeight*/,
                nullptr,
                nullptr,
                m_hInstance,
                this);

            if (m_hWnd)
            {
                DragAcceptFiles(m_hWnd, TRUE);
                // 에디터는 여기서 창을 보여주지 않는다. 초기화가 메인 스레드를
                // 점유하는 동안 응답 없는 빈 창이 로딩창과 같이 떠 있었다.
                // 초기화 완료 지점에서 Show()를 부른다.
            }
            return;
        }

        // ── 플레이어: 테두리 없는 전체화면 창 (2026-08-10 재작성) ──
        //
        // ★ 예전에는 여기서 모니터의 표시 모드를 강제로 바꿨다
        //   (ChangeDisplaySettingsExW + CDS_FULLSCREEN으로 요청 해상도 적용).
        //   그것은 창모드가 아니라 모드 전환이고, 대가가 컸다:
        //
        //     · 데스크톱 해상도가 바뀌며 다른 프로그램의 창이 재배치된다
        //     · 전환·복귀마다 화면이 검게 깜빡이고 Alt+Tab이 느려진다
        //     · 크래시로 죽으면 소멸자의 복원 코드가 돌지 않아 사용자의
        //       해상도가 바뀐 채로 남는다
        //     · 요청 크기(하드코딩 1920x1080)가 모니터와 다르고 모드 전환마저
        //       실패하면, 창이 화면을 덮지 못한 채 좌상단에 붙어 있었다
        //
        //   지금은 모니터의 네이티브 해상도를 그대로 쓰고 창을 그 사각형에
        //   맞춘다. 바꾸는 것이 없으니 복원할 것도 없다.
        //
        // 요청 크기(m_width/m_height)는 여기서 쓰이지 않는다 — 화면 크기는
        // 모니터가 정한다. 실제 값으로 덮어써서 이후 판독이 창과 어긋나지
        // 않게 한다.

        // 1) 보더리스 창 생성(임시 크기 — 바로 아래에서 모니터에 맞춘다)
        m_hWnd = CreateWindowEx(
            0,
            L"CoreWindowApp",
            title,
            WS_POPUP,                    // ← 테두리·캡션 없음
            0, 0,
            m_width, m_height,
            nullptr, nullptr,
            m_hInstance,
            this);

        if (!m_hWnd) return;

        DragAcceptFiles(m_hWnd, TRUE);

        // 2) 창이 올라간 모니터의 전체 사각형.
        //
        //    rcWork가 아니라 rcMonitor다 — 작업 영역은 작업 표시줄을 뺀 것이라
        //    그것에 맞추면 화면 아래가 남는다.
        RECT target{ 0, 0,
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

        MONITORINFO mi{};
        mi.cbSize = sizeof(MONITORINFO);
        HMONITOR monitor = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(monitor, &mi))
        {
            target = mi.rcMonitor;
        }
        // 실패하면 주 모니터 크기로 간다(위 초기값). 다중 모니터에서 엉뚱한
        // 화면을 덮을 수 있지만, 창이 화면을 못 덮는 것보다는 낫다.

        m_width = target.right - target.left;
        m_height = target.bottom - target.top;

        // 3) 모니터 영역으로 확장.
        //
        //    HWND_TOP이지 HWND_TOPMOST가 아니다 — 최상위로 못박으면 Alt+Tab으로
        //    다른 창을 띄워도 이 창이 위에 남고, 디버거·오류 대화상자가 뒤에
        //    가려 보이지 않는다.
        SetWindowPos(
            m_hWnd, HWND_TOP,
            target.left, target.top,
            m_width, m_height,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);
    }

    // ApplyDisplayModeToMonitor · CaptureOriginalDisplayMode · RestoreDisplayMode가
    // 여기 있었다 (2026-08-10). 셋 다 ChangeDisplaySettingsExW로 모니터 해상도를
    // 강제 전환하고 되돌리기 위한 것이었고, 유일한 호출자가 플레이어 창 생성
    // 경로였다 — 그 경로가 모드를 바꾸지 않게 되면서 셋 다 도달 불가가 됐다.

public:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};
