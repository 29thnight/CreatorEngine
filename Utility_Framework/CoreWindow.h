#pragma once
#include <windows.h>
#include <functional>
#include <unordered_map>
#include <directxtk/Keyboard.h>
#include <imgui_internal.h>
#include <shellapi.h> // 추가
#include "DumpHandler.h"
#include "EngineMode.h"
#include "Resource.h"
#include <strsafe.h>       // StringCchCopyW

#pragma warning(disable: 28251)
#define MAIN_ENTRY int WINAPI

class CoreWindow
{
public:
    using MessageHandler = std::function<LRESULT(HWND, WPARAM, LPARAM)>;

    CoreWindow(HINSTANCE hInstance, const wchar_t* title, int width, int height)
        : m_hInstance(hInstance), m_width(width), m_height(height)
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
        // ▼ 변경: 정확 원복 함수 사용
        RestoreDisplayMode();

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
    int m_height = 600;
    std::unordered_map<UINT, MessageHandler> m_handlers;
	static MessageHandler m_CreateEventHandler;

    // 표시 모드 전환 상태 & 대상 모니터 장치 이름
    bool    m_displayModeChanged = false;
    wchar_t m_targetDeviceName[CCHDEVICENAME] = { 0 };

    // ▼ 추가: 원래 표시 모드 저장용
    DEVMODEW m_originalMode{};
    bool     m_hasOriginalMode = false;

    void RegisterWindowClass() const
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = CoreWindow::WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = L"CoreWindowApp";
        if (EngineMode::IsEditor())
        {
            // 에디터 exe만 이 리소스를 가진다 — 플레이어에서 부르면
            // LoadIcon이 NULL을 돌려줄 뿐이지만, 의도를 코드에 남긴다.
            wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ACADEMY4Q));
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

        // 1) 보더리스 창 생성 (임시 크기)
        m_hWnd = CreateWindowEx(
            0,
            L"CoreWindowApp",
            title,
            WS_POPUP,                    // ← 보더리스
            0, 0,
            m_width, m_height,           // 어차피 바로 전체화면으로 키움
            nullptr, nullptr,
            m_hInstance,
            this);

        if (!m_hWnd) return;

        DragAcceptFiles(m_hWnd, TRUE);

        // 2) 현재 창이 올라간 모니터 정보 획득 (디바이스 이름 포함)
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(MONITORINFOEXW);
        HMONITOR mon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(mon, reinterpret_cast<MONITORINFO*>(&mi)))
        {
            // 디바이스명 복사
            StringCchCopyW(m_targetDeviceName, _countof(m_targetDeviceName), mi.szDevice);

            // ▼ 추가: 해상도 바꾸기 전에 원래 모드 저장
            CaptureOriginalDisplayMode(m_targetDeviceName);

            // 3) 대상 모니터 표시모드(해상도/주사율) 강제 적용
            const int desiredHz = 0; // 60/120/144 등 지정 가능, 0이면 OS 기본
            if (ApplyDisplayModeToMonitor(m_targetDeviceName, m_width, m_height, desiredHz))
            {
                m_displayModeChanged = true;
            }

            // 4) 창을 모니터 영역으로 확장 (Borderless Fullscreen)
            SetWindowPos(
                m_hWnd, HWND_TOP,
                0, 0,
                m_width, m_height,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
        }
        else
        {
            // 모니터 정보 실패 시: 최소한 전체 화면 크기로만 확장
            SetWindowPos(
                m_hWnd, HWND_TOP,
                0, 0,
                m_width, m_height,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

            ShowWindow(m_hWnd, SW_SHOW);
            UpdateWindow(m_hWnd);
        }
    }

    bool ApplyDisplayModeToMonitor(const wchar_t* deviceName, int width, int height, int refreshHz)
    {
        if (!deviceName || !deviceName[0]) return false;

        DEVMODEW dm = {};
        dm.dmSize = sizeof(DEVMODEW);
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        dm.dmPelsWidth = static_cast<DWORD>(width);
        dm.dmPelsHeight = static_cast<DWORD>(height);

        if (refreshHz > 0)
        {
            dm.dmDisplayFrequency = static_cast<DWORD>(refreshHz);
            dm.dmFields |= DM_DISPLAYFREQUENCY;
        }

        LONG r = ChangeDisplaySettingsExW(deviceName, &dm, nullptr, CDS_FULLSCREEN, nullptr);
        return (r == DISP_CHANGE_SUCCESSFUL);
    }

    bool CaptureOriginalDisplayMode(const wchar_t* deviceName)
    {
        if (!deviceName || !deviceName[0]) return false;

        ZeroMemory(&m_originalMode, sizeof(m_originalMode));
        m_originalMode.dmSize = sizeof(DEVMODEW);

        // 현재 설정(enum current) 그대로 가져오기
        if (EnumDisplaySettingsExW(deviceName, ENUM_CURRENT_SETTINGS, &m_originalMode, 0))
        {
            m_hasOriginalMode = true;
            return true;
        }
        return false;
    }

    void RestoreDisplayMode()
    {
        if (m_displayModeChanged && m_targetDeviceName[0] != L'\0')
        {
            if (m_hasOriginalMode)
            {
                // 정확히 기존 모드로
                ChangeDisplaySettingsExW(m_targetDeviceName, &m_originalMode, nullptr, 0, nullptr);
            }
            else
            {
                // 정보가 없으면 기본으로 롤백
                ChangeDisplaySettingsExW(m_targetDeviceName, nullptr, nullptr, 0, nullptr);
            }
            m_displayModeChanged = false;
        }
    }

public:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};
