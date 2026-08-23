#pragma once
#include <windows.h>
#include <functional>
#include <unordered_map>
#include <shellapi.h> // 추가
#include "DumpHandler.h"
#include "WindowDesc.h"
// <strsafe.h> include가 여기 있었다 (2026-08-10). 유일한 소비자가
// 표시 모드 전환의 StringCchCopyW(대상 모니터 장치명 복사)였다.

#pragma warning(disable: 28251)
#define MAIN_ENTRY int WINAPI

class CoreWindow
{
public:
    using MessageHandler = std::function<LRESULT(HWND, WPARAM, LPARAM)>;

    CoreWindow(HINSTANCE hInstance, const WindowDesc& desc)
        : m_hInstance(hInstance), m_desc(desc)
        , m_width(desc.clientWidth), m_height(desc.clientHeight)
        , m_iconResourceId(desc.iconResourceId)
    {
        s_instance = this;
        RegisterWindowClass();
        CreateAppWindow();

        // 크래시 후크는 여기서 걸지 않는다 — LogSystem::InstallCrashGuards가 이미
        // 전부 걸어 두었고, 여기서 또 걸면 설치 순서 싸움이 된다.
        // 덤프 기록자 등록은 SetDumpType에서 한다.
    }

    CoreWindow(const CoreWindow&) = delete;
    CoreWindow& operator=(const CoreWindow&) = delete;
    CoreWindow(CoreWindow&&) = delete;
    CoreWindow& operator=(CoreWindow&&) = delete;

    ~CoreWindow()
    {
        // RestoreDisplayMode() 호출이 여기 있었다 (2026-08-10).
        // 플레이어가 표시 모드를 바꾸지 않게 된 뒤로 되돌릴 것이 없다.

        // DestroyWindow can dispatch messages synchronously. The host-side consumer
        // (Editor presentation thread) has already been destroyed at this point, so
        // detach the interceptor before destroying the HWND.
        m_desc.messageInterceptor = {};
        if (m_hWnd)
        {
            DestroyWindow(m_hWnd);
            m_hWnd = nullptr;
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
    CoreWindow& InitializeTask(Initializer fn_initializer)
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

    /// 숨겨 둔 창을 표시한다. showOnCreate=false인 Host가 초기화를 끝낸 뒤 부른다.
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
    WindowDesc m_desc{};
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

    void CreateAppWindow()
    {
        RECT windowRect{ 0, 0, m_width, m_height };
        AdjustWindowRectEx(&windowRect, m_desc.style, FALSE, m_desc.extendedStyle);

        // A monitor-fitted popup needs a deterministic seed monitor. The previous
        // Player path started at (0, 0), so keep that behavior before expanding it
        // to the selected monitor below.
        int x = m_desc.fitNearestMonitor ? 0 : CW_USEDEFAULT;
        int y = m_desc.fitNearestMonitor ? 0 : CW_USEDEFAULT;
        if (m_desc.centerOnDesktop)
        {
            RECT desktopRect{};
            GetWindowRect(GetDesktopWindow(), &desktopRect);
            const int windowWidth = windowRect.right - windowRect.left;
            const int windowHeight = windowRect.bottom - windowRect.top;
            x = desktopRect.left + (desktopRect.right - desktopRect.left - windowWidth) / 2;
            y = desktopRect.top + (desktopRect.bottom - desktopRect.top - windowHeight) / 2;
        }

        m_hWnd = CreateWindowEx(
            m_desc.extendedStyle,
            L"CoreWindowApp",
            m_desc.title.c_str(),
            m_desc.style,
            x, y,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr, nullptr,
            m_hInstance,
            this);

        if (!m_hWnd) return;

        if (m_desc.acceptFileDrops)
        {
            DragAcceptFiles(m_hWnd, TRUE);
        }

        if (m_desc.fitNearestMonitor)
        {
            RECT target{ 0, 0,
                GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(MONITORINFO);
            const HMONITOR monitor =
                MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(monitor, &monitorInfo))
            {
                target = monitorInfo.rcMonitor;
            }

            m_width = target.right - target.left;
            m_height = target.bottom - target.top;
            SetWindowPos(
                m_hWnd, HWND_TOP,
                target.left, target.top,
                m_width, m_height,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER |
                    (m_desc.showOnCreate ? SWP_SHOWWINDOW : 0));
        }

        if (m_desc.showOnCreate)
        {
            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
        }
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
