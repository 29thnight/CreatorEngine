#pragma once
#include <windows.h>
#include <functional>
#include <unordered_map>
#include <directxtk/Keyboard.h>
#include <imgui_internal.h>
#include <shellapi.h> // 추가
#include "DumpHandler.h"
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
        SetUnhandledExceptionFilter(ErrorDumpHandeler);
    }

    ~CoreWindow()
    {
        if (m_hWnd)
        {
            DestroyWindow(m_hWnd);
        }
        UnregisterClass(L"CoreWindowApp", m_hInstance);
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

    static LONG WINAPI ErrorDumpHandeler(EXCEPTION_POINTERS* pExceptionPointers)
    {
        int msgResult = MessageBox(NULL, L"Should Create Dump ?", L"Exception", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND);

        if (msgResult == IDYES)
        {
            CreateDump(pExceptionPointers, g_dumpType, s_instance->m_hWnd);
        }

        return msgResult;
    }

    static void SetDumpType(DUMP_TYPE dumpType)
    {
        g_dumpType = dumpType;
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
    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    int m_width = 800;
    int m_height = 600;
    std::unordered_map<UINT, MessageHandler> m_handlers;
	static MessageHandler m_CreateEventHandler;

    // 표시 모드 전환 상태 & 대상 모니터 장치 이름
    bool    m_displayModeChanged = false;
    wchar_t m_targetDeviceName[CCHDEVICENAME] = { 0 };

    void RegisterWindowClass() const
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = CoreWindow::WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = L"CoreWindowApp";
#ifndef BUILD_FLAG
        wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ACADEMY4Q));      // 큰 아이콘
#endif // BUILD_FLAG
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

#ifndef BUILD_FLAG
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
            ShowWindow(m_hWnd, SW_SHOWNORMAL);
            UpdateWindow(m_hWnd);
        }
#else
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

        //// 2) 현재 모니터 전체 크기로 확장 (Borderless Fullscreen)
        //MONITORINFO mi{ sizeof(mi) };
        //HMONITOR mon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        //GetMonitorInfo(mon, &mi);

        //SetWindowPos(
        //    m_hWnd, HWND_TOP,
        //    mi.rcMonitor.left, mi.rcMonitor.top,
        //    mi.rcMonitor.right - mi.rcMonitor.left,
        //    mi.rcMonitor.bottom - mi.rcMonitor.top,
        //    SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);

        //ShowWindow(m_hWnd, SW_SHOW); // SWP_SHOWWINDOW로 이미 표시됨
        //UpdateWindow(m_hWnd);

                // 2) 현재 창이 올라간 모니터 정보 획득 (디바이스 이름 포함)
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(MONITORINFOEXW);
        HMONITOR mon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(mon, reinterpret_cast<MONITORINFO*>(&mi)))
        {
            // 디바이스명 복사
            StringCchCopyW(m_targetDeviceName, _countof(m_targetDeviceName), mi.szDevice);

            // 3) 대상 모니터 표시모드(해상도/주사율) 강제 적용
            const int desiredHz = 0; // 60/120/144 등 지정 가능, 0이면 OS 기본
            if (ApplyDisplayModeToMonitor(m_targetDeviceName, m_width, m_height, desiredHz))
            {
                m_displayModeChanged = true;
            }

            // 4) 창을 모니터 영역으로 확장 (Borderless Fullscreen)
            SetWindowPos(
                m_hWnd, HWND_TOP,
                0,0,1920,1080,
               /* mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,*/
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
#endif // BUILD_FLAG

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

public:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};
