#pragma once
#include "Core.Minimal.h"
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

class ProgressWindow : public Singleton<ProgressWindow>
{
private:
    friend class Singleton;
    ProgressWindow() = default;
    ~ProgressWindow() = default;

public:
    void Launch()
    {
        InitCommonControls();
        m_hThread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    }

    void SetProgress(int value)
    {
        if (m_hProgress)
            SendMessage(m_hProgress, PBM_SETPOS, value, 0);
    }

    void SetStatusText(const std::wstring& text)
    {
        if (m_hText)
            SetWindowTextW(m_hText, text.c_str());
    }

    void Close()
    {
        if (m_hWnd)
            PostMessage(m_hWnd, WM_CLOSE, 0, 0);

        if (m_hThread)
        {
            WaitForSingleObject(m_hThread, INFINITE);
            CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
    }

private:
    static DWORD WINAPI ThreadProc(LPVOID param)
    {
        ProgressWindow* self = static_cast<ProgressWindow*>(param);

        WNDCLASS wc = {};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = L"ProgressWindowClass";
        RegisterClass(&wc);

        const int width = 550;
        const int height = 150;

        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        int x = (screenWidth - width) / 2;
        int y = (screenHeight - height) / 2;

        self->m_hWnd = CreateWindowEx(
            WS_EX_TOPMOST,
            wc.lpszClassName, L"Initializing...",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            x, y, width, height,
            nullptr, nullptr, wc.hInstance, self
        );

        self->m_hText = CreateWindowEx(0, L"STATIC", L"Loading...",
            WS_CHILD | WS_VISIBLE,
            20, 20, width - 40, 20,
            self->m_hWnd, nullptr, wc.hInstance, nullptr);

        self->m_hProgress = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            20, 50, width - 60, 25,
            self->m_hWnd, nullptr, wc.hInstance, nullptr);

        SendMessage(self->m_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        // 맨 앞으로 보여줌
        ShowWindow(self->m_hWnd, SW_SHOWNORMAL);
        SetWindowPos(self->m_hWnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);

        // 메시지 루프
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        return 0;
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_CREATE)
        {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }

        if (ProgressWindow* self = reinterpret_cast<ProgressWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)))
        {
            switch (msg)
            {
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            }
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

private:
    HWND m_hWnd = nullptr;
    HWND m_hProgress = nullptr;
    HWND m_hText = nullptr;
    HANDLE m_hThread = nullptr;
};

inline static auto& g_progressWindow = ProgressWindow::GetInstance();
