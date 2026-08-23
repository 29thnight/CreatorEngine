#pragma once
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

        // 창 생성 완료를 이벤트로 기다린다. 예전의 sleep(300)은 느린 디스크에서
        // 창이 채 만들어지기 전에 SetProgress가 null 핸들로 들어가는 경합이 있었다.
        // 이벤트는 스레드가 살아 있는 동안 닫지 않는다 — Close가 join 후에 닫는다.
        if (m_hReadyEvent)
            ResetEvent(m_hReadyEvent);
        else
            m_hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        m_hThread = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);

		if (m_hThread == nullptr)
		{
			MessageBoxW(nullptr, L"Failed to create thread", L"Error", MB_ICONERROR);
			return;
		}

		if (m_hReadyEvent)
		{
			constexpr DWORD kReadyTimeoutMs = 3000;
			WaitForSingleObject(m_hReadyEvent, kReadyTimeoutMs);
		}
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
		// 100%가 표시된 것을 사용자가 볼 시간을 준다.
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		// 창 파괴는 만든 스레드(ThreadProc)만 할 수 있다. 여기서 DestroyWindow를
		// 직접 부르면 조용히 실패한다 — WM_CLOSE를 보내 그쪽에서 파괴하게 한다.
        if (m_hWnd)
            PostMessage(m_hWnd, WM_CLOSE, 0, 0);

        if (m_hThread)
        {
            // WaitForSingleObject로 그냥 자면 교착 위험이 있다: 로딩창이
            // 포그라운드인 채 파괴되면 활성화가 호출 스레드의 창으로 넘어오며
            // 동기 SendMessage가 날아오는데, 이 스레드가 펌프를 멈춘 채
            // 대기하면 양쪽 다 영원히 기다린다. 대기 중에도 메시지를 펌프한다.
            while (true)
            {
                DWORD wait = MsgWaitForMultipleObjects(1, &m_hThread, FALSE, INFINITE, QS_ALLINPUT);
                if (wait != WAIT_OBJECT_0 + 1)
                    break;

                MSG msg;
                while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessage(&msg);
                }
            }
            CloseHandle(m_hThread);
            m_hThread = nullptr;
        }

        // 스레드가 끝났으니 창 핸들은 전부 죽었다. 다음 Launch(스크립트 핫리로드,
        // 라이트맵 베이킹)가 죽은 핸들에 SendMessage 하지 않도록 비워 둔다.
        m_hWnd = nullptr;
        m_hProgress = nullptr;
        m_hText = nullptr;

        if (m_hReadyEvent)
        {
            CloseHandle(m_hReadyEvent);
            m_hReadyEvent = nullptr;
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

        // 창과 컨트롤이 전부 준비됐다 — Launch를 깨운다.
        if (self->m_hReadyEvent)
            SetEvent(self->m_hReadyEvent);

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
        case WM_NCHITTEST:
        {
            // InitStyle은 WS_POPUP이라 캡션이 없다. 클라이언트 영역 히트를
            // HTCAPTION으로 돌려주면 배경 아무 곳이나 잡고 드래그할 수 있다.
            LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam);
            if (hit == HTCLIENT && self && self->m_style == ProgressWindowStyle::InitStyle)
                return HTCAPTION;
            return hit;
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
    HANDLE m_hReadyEvent = nullptr;
	std::wstring m_title = L"Initializing...";
};

inline static auto g_progressWindow = ProgressWindow::GetInstance();
