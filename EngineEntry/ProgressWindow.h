#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include <wingdi.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")

enum class ProgressWindowStyle
{
    Basic,         // 텍스트 + 프로그레스바
    InitStyle     // 배경이미지 + 프로그레스바
};

class ProgressWindow : public Singleton<ProgressWindow>
{
private:
    friend class Singleton;
    ProgressWindow() = default;
    ~ProgressWindow() = default;

public:
    void Launch(ProgressWindowStyle style = ProgressWindowStyle::Basic, const std::wstring& imagePath = L"")
    {
        m_style = style;
        m_imagePath = imagePath;
        InitCommonControls();
        m_hThread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);

		if (m_hThread == nullptr)
		{
			MessageBoxW(nullptr, L"Failed to create thread", L"Error", MB_ICONERROR);
			return;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

	void SetTitle(const std::wstring& title)
	{
		if (m_hWnd)
			SetWindowTextW(m_hWnd, title.c_str());
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
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (m_hWnd)
            PostMessage(m_hWnd, WM_CLOSE, 0, 0);

		if (m_hWnd && IsWindow(m_hWnd))
		{
			DestroyWindow(m_hWnd);
			m_hWnd = nullptr;
		}

        if (m_hThread)
        {
            WaitForSingleObject(m_hThread, INFINITE);
            CloseHandle(m_hThread);
            m_hThread = nullptr;
        }
        if (m_hBitmap)
        {
            DeleteObject(m_hBitmap);
            m_hBitmap = nullptr;
        }
        if (m_hFont)
        {
            DeleteObject(m_hFont);
            m_hFont = nullptr;
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

        if (self->m_style == ProgressWindowStyle::Basic)
        {
            self->CreateBasicUI();
        }
        else
        {
            self->CreateInitUI();
        }

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return 0;
    }

    void CreateBasicUI()
    {
        const int width = 450;
        const int height = 150;
        int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

        m_hWnd = CreateWindowEx(WS_EX_TOPMOST, L"ProgressWindowClass", m_title.c_str(),
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            x, y, width, height, nullptr, nullptr, GetModuleHandle(nullptr), this);

        m_hFont = CreateFont(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"맑은 고딕");

        m_hText = CreateWindowEx(0, L"STATIC", L"Loading...",
            WS_CHILD | WS_VISIBLE,
            20, 20, width - 40, 20,
            m_hWnd, nullptr, GetModuleHandle(nullptr), nullptr);

        SendMessage(m_hText, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hProgress = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            20, 50, width - 60, 25,
            m_hWnd, nullptr, GetModuleHandle(nullptr), nullptr);

        SendMessage(m_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        UpdateWindow(m_hWnd);
    }

    void CreateInitUI()
    {
        const int width = 512;
        const int height = 300;
        int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

        m_hWnd = CreateWindowEx(WS_EX_TOPMOST, L"ProgressWindowClass", nullptr,
            WS_POPUP,
            x, y, width, height, nullptr, nullptr, GetModuleHandle(nullptr), this);

        if (!m_imagePath.empty())
        {
            m_hBitmap = (HBITMAP)LoadImage(nullptr, m_imagePath.c_str(), IMAGE_BITMAP,
                                           0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
        }

        m_hFont = CreateFont(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"맑은 고딕");

        m_hText = CreateWindowEx(0, L"STATIC", L"Loading...",
            WS_CHILD | WS_VISIBLE,
            20, height - 60, width - 40, 20,
            m_hWnd, nullptr, GetModuleHandle(nullptr), nullptr);

        SendMessage(m_hText, WM_SETFONT, (WPARAM)m_hFont, TRUE);

        m_hProgress = CreateWindowEx(0, PROGRESS_CLASS, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            0, height - 5, width, 5,
            m_hWnd, nullptr, GetModuleHandle(nullptr), nullptr);

        SendMessage(m_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        UpdateWindow(m_hWnd);
    }


    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ProgressWindow* self = reinterpret_cast<ProgressWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        switch (msg)
        {
        case WM_CREATE:
        {
            CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            self = reinterpret_cast<ProgressWindow*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        {
            if ((HWND)lParam == self->m_hText && self->m_style == ProgressWindowStyle::InitStyle)
            {
                SetTextColor((HDC)wParam, RGB(255, 255, 255));
                SetBkColor((HDC)wParam, RGB(0, 0, 0));
                static HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
                return (INT_PTR)hBrush;
            }
            break;
        }
        case WM_PAINT:
        {
            if (self && self->m_style == ProgressWindowStyle::InitStyle && self->m_hBitmap)
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, self->m_hBitmap);

                BITMAP bmp;
                GetObjectW(self->m_hBitmap, sizeof(BITMAP), &bmp);

                RECT clientRect;
                GetClientRect(hwnd, &clientRect);
                StretchBlt(hdc, 0, 0, clientRect.right, clientRect.bottom,
                           memDC, 0, 0, bmp.bmWidth, bmp.bmHeight, SRCCOPY);

                SelectObject(memDC, oldBmp);
                DeleteDC(memDC);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

private:
    ProgressWindowStyle m_style = ProgressWindowStyle::Basic;
    file::path m_imagePath = L"";
    HWND m_hWnd = nullptr;
    HWND m_hProgress = nullptr;
    HWND m_hText = nullptr;
    HBITMAP m_hBitmap = nullptr;
    HFONT m_hFont = nullptr;
    HANDLE m_hThread = nullptr;
	std::wstring m_title = L"Initializing...";
};

inline static auto& g_progressWindow = ProgressWindow::GetInstance();
#endif // !DYNAMICCPP_EXPORTS