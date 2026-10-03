#pragma once
#include "Core.Minimal.h"
#include "EngineVersion.h"
#include "Resource.h"
#include <wingdi.h>
#include <commctrl.h>
#include <algorithm>
#include <iterator>
#include <atomic>
#include <memory>

#pragma comment(lib, "comctl32.lib")

enum class ProgressWindowStyle
{
    Basic,         // 텍스트 + 프로그레스바
    InitStyle     // 배경이미지 + 버전/로딩 단계 + 하단 프로그레스바
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
        m_cancelRequested = false;
        m_progress = 0;
        m_step = 0;
        m_totalSteps = 0;
        m_stageText = L"Starting editor";
        m_detailText = L"Preparing engine services...";
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
        if (m_style == ProgressWindowStyle::InitStyle && m_hWnd)
        {
            SendMessageW(m_hWnd, kProgressMessage, static_cast<WPARAM>(std::clamp(value, 0, 100)), 0);
            return;
        }
        if (m_hProgress)
            SendMessage(m_hProgress, PBM_SETPOS, value, 0);
    }

    void SetStatusText(const std::wstring& text)
    {
        if (m_style == ProgressWindowStyle::InitStyle && m_hWnd)
        {
            SendMessageW(m_hWnd, kStatusMessage, 0, reinterpret_cast<LPARAM>(&text));
            return;
        }
        if (m_hText)
            SetWindowTextW(m_hText, text.c_str());
    }

    void SetBootStatus(const std::wstring& stage, const std::wstring& detail, int step, int total)
    {
        if (m_style != ProgressWindowStyle::InitStyle || !m_hWnd) return;
        const BootStatus status{ stage, detail, step, total };
        SendMessageW(m_hWnd, kBootStatusMessage, 0, reinterpret_cast<LPARAM>(&status));
    }

    void SetBootDetail(const std::wstring& detail)
    {
        if (m_style == ProgressWindowStyle::InitStyle && m_hWnd)
            SendMessageW(m_hWnd, kDetailMessage, 0, reinterpret_cast<LPARAM>(&detail));
    }

    // Own the posted text until the UI thread consumes it. No callback to GT/RT.
    void SetWarmupStatus(std::wstring detail, bool stalled = false, bool failed = false)
    {
        if (!m_hWnd || m_style != ProgressWindowStyle::InitStyle) return;
        auto status = std::make_unique<WarmupUpdate>();
        status->detail = std::move(detail);
        status->stalled = stalled;
        status->failed = failed;
        if (PostMessageW(m_hWnd, kWarmupMessage, 0, reinterpret_cast<LPARAM>(status.get())))
            status.release();
    }
    bool WarmupCancelRequested() const { return m_cancelRequested.load(); }

    void Close()
    {
		// 100%가 표시된 것을 사용자가 볼 시간을 준다.
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		// 창 파괴는 만든 스레드(ThreadProc)만 할 수 있다. 여기서 DestroyWindow를
		// 직접 부르면 조용히 실패한다 — WM_CLOSE를 보내 그쪽에서 파괴하게 한다.
        m_closing = true;
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
        m_cancelButton = nullptr;
        m_closing = false;

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
        if (m_hBrandIcon) { DestroyIcon(m_hBrandIcon); m_hBrandIcon = nullptr; }
        if (m_hBrandFont) { DeleteObject(m_hBrandFont); m_hBrandFont = nullptr; }
        if (m_hFont)
        {
            DeleteObject(m_hFont);
            m_hFont = nullptr;
        }
        if (m_hStageFont) { DeleteObject(m_hStageFont); m_hStageFont = nullptr; }
        if (m_hInfoFont) { DeleteObject(m_hInfoFont); m_hInfoFont = nullptr; }
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
        // The loading bitmap is authored at the window's native 666x390 size.
        const int width = 666;
        const int height = 390;
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
        HMODULE editorModule = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&ProgressWindow::WndProc), &editorModule))
        {
            m_hBrandIcon = static_cast<HICON>(LoadImageW(editorModule,
                MAKEINTRESOURCEW(IDI_ACADEMY4Q), IMAGE_ICON, 72, 72, LR_DEFAULTCOLOR));
        }
        m_hBrandFont = CreateFontW(40, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

        m_hStageFont = CreateFontW(19, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"맑은 고딕");
        m_hInfoFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"맑은 고딕");
        ShowWindow(m_hWnd, SW_SHOWNORMAL);
        UpdateWindow(m_hWnd);
    }

    void PaintInitUI(HDC hdc, const RECT& client)
    {
        const int width = client.right;
        const int height = client.bottom;
        if (m_hBitmap)
        {
            HDC imageDC = CreateCompatibleDC(hdc);
            HBITMAP oldBitmap = static_cast<HBITMAP>(SelectObject(imageDC, m_hBitmap));
            BITMAP bitmap{};
            GetObjectW(m_hBitmap, sizeof(bitmap), &bitmap);
            if (bitmap.bmWidth == width && bitmap.bmHeight == height)
            {
                BitBlt(hdc, 0, 0, width, height, imageDC, 0, 0, SRCCOPY);
            }
            else
            {
                SetStretchBltMode(hdc, HALFTONE);
                SetBrushOrgEx(hdc, 0, 0, nullptr);
                StretchBlt(hdc, 0, 0, width, height, imageDC,
                    0, 0, bitmap.bmWidth, bitmap.bmHeight, SRCCOPY);
            }
            SelectObject(imageDC, oldBitmap);
            DeleteDC(imageDC);
        }
        else
        {
            FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }

        // Center the icon and wordmark as one group; the BMP is background artwork only.
        SetBkMode(hdc, TRANSPARENT);
        if (m_hBrandFont)
        {
            auto oldBrandFont = SelectObject(hdc, m_hBrandFont);
            constexpr wchar_t brandText[] = L"Creator Engine";
            SIZE textSize{};
            GetTextExtentPoint32W(hdc, brandText,
                static_cast<int>(std::size(brandText) - 1), &textSize);
            constexpr int iconSize = 72;
            const int iconWidth = m_hBrandIcon ? iconSize : 0;
            const int gap = m_hBrandIcon ? 16 : 0;
            const int brandLeft = (width - iconWidth - gap - textSize.cx) / 2;
            if (m_hBrandIcon)
                DrawIconEx(hdc, brandLeft, 133, m_hBrandIcon,
                    iconSize, iconSize, 0, nullptr, DI_NORMAL);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT brandRect{ brandLeft + iconWidth + gap, 128, width, 208 };
            DrawTextW(hdc, brandText, -1, &brandRect,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdc, oldBrandFont);
        }
        else if (m_hBrandIcon)
        {
            DrawIconEx(hdc, (width - 72) / 2, 133, m_hBrandIcon,
                72, 72, 0, nullptr, DI_NORMAL);
        }

        const int left = MulDiv(width, 32, 512);
        const int top = height - 104;
        SetTextColor(hdc, RGB(185, 199, 211));
        const auto oldFont = SelectObject(hdc, m_hInfoFont);
        const std::wstring version = std::wstring(L"Version ") +
            std::wstring(std::begin(CreatorEngineVersion::Build),
                std::end(CreatorEngineVersion::Build) - 1) +
            (CreatorEngineVersion::LocalDevelopment ? L"  |  Local development" : L"");
        RECT versionRect{ left, top, width - left, top + 23 };
        DrawTextW(hdc, version.c_str(), -1, &versionRect,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SelectObject(hdc, m_hStageFont);
        SetTextColor(hdc, RGB(255, 255, 255));
        std::wstring stage = m_stageText;
        if (m_totalSteps > 0)
            stage += L" (" + std::to_wstring(m_step) + L"/" + std::to_wstring(m_totalSteps) + L")";
        RECT stageRect{ left, top + 27, width - left, top + 52 };
        DrawTextW(hdc, stage.c_str(), -1, &stageRect,
            DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        SelectObject(hdc, m_hInfoFont);
        SetTextColor(hdc, RGB(205, 216, 225));
        RECT detailRect{ left, top + 52, width - left, height - kProgressBarHeight - 3 };
        DrawTextW(hdc, m_detailText.c_str(), -1, &detailRect,
            DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(hdc, oldFont);

        // Leave the unfilled portion as the bitmap's black background.
        // The fill matches the blue background of the loading image's icon.
        if (m_progress > 0)
        {
            RECT fill{ 0, height - kProgressBarHeight,
                MulDiv(width, m_progress, 100), height };
            HBRUSH fillBrush = CreateSolidBrush(RGB(15, 166, 253));
            FillRect(hdc, &fill, fillBrush);
            DeleteObject(fillBrush);
        }
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
        case kProgressMessage:
            if (self)
            {
                self->m_progress = static_cast<int>(wParam);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case kStatusMessage:
            if (self && lParam)
            {
                self->m_stageText = *reinterpret_cast<const std::wstring*>(lParam);
                self->m_detailText.clear();
                self->m_totalSteps = 0;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case kBootStatusMessage:
            if (self && lParam)
            {
                const auto& status = *reinterpret_cast<const BootStatus*>(lParam);
                self->m_stageText = status.stage;
                self->m_detailText = status.detail;
                self->m_step = status.step;
                self->m_totalSteps = status.total;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case kDetailMessage:
            if (self && lParam)
            {
                self->m_detailText = *reinterpret_cast<const std::wstring*>(lParam);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case kWarmupMessage:
            if (lParam)
            {
                std::unique_ptr<WarmupUpdate> status(reinterpret_cast<WarmupUpdate*>(lParam));
                if (self)
                {
                    self->m_detailText = std::move(status->detail);
                    if (!self->m_cancelButton)
                    {
                        self->m_cancelButton = CreateWindowW(L"BUTTON", L"Cancel startup",
                            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 622, 12, 28, 28,
                            hwnd, reinterpret_cast<HMENU>(kCancelButton), GetModuleHandleW(nullptr), nullptr);
                    }
                    SetWindowTextW(self->m_cancelButton, status->failed ? L"Close editor" : L"Cancel startup");

                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_DRAWITEM:
            if (self && wParam == kCancelButton && lParam)
            {
                const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
                HBRUSH background = CreateSolidBrush(RGB(12, 20, 28));
                FillRect(item.hDC, &item.rcItem, background);
                DeleteObject(background);
                const COLORREF color = self->m_cancelRequested.load() ? RGB(100, 110, 120) : RGB(225, 232, 238);
                HPEN pen = CreatePen(PS_SOLID, 2, color);
                auto oldPen = SelectObject(item.hDC, pen);
                const int cx = (item.rcItem.left + item.rcItem.right) / 2;
                const int cy = (item.rcItem.top + item.rcItem.bottom) / 2;
                MoveToEx(item.hDC, cx - 5, cy - 5, nullptr); LineTo(item.hDC, cx + 6, cy + 6);
                MoveToEx(item.hDC, cx - 5, cy + 5, nullptr); LineTo(item.hDC, cx + 6, cy - 6);
                SelectObject(item.hDC, oldPen); DeleteObject(pen);
                return TRUE;
            }
            break;
        case WM_COMMAND:
            if (self && LOWORD(wParam) == kCancelButton)
            {
                self->m_cancelRequested = true;
                SetWindowTextW(self->m_cancelButton, L"Stopping safely...");
                EnableWindow(self->m_cancelButton, FALSE);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (self && self->m_cancelButton && !self->m_closing.load())
            {
                self->m_cancelRequested = true;
                return 0;
            }
            break;
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
            if (self && self->m_style == ProgressWindowStyle::InitStyle)
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT client{};
                GetClientRect(hwnd, &client);
                HDC bufferDC = CreateCompatibleDC(hdc);
                HBITMAP buffer = bufferDC
                    ? CreateCompatibleBitmap(hdc, client.right, client.bottom) : nullptr;
                if (buffer)
                {
                    HBITMAP oldBuffer = static_cast<HBITMAP>(SelectObject(bufferDC, buffer));
                    self->PaintInitUI(bufferDC, client);
                    BitBlt(hdc, 0, 0, client.right, client.bottom,
                        bufferDC, 0, 0, SRCCOPY);
                    SelectObject(bufferDC, oldBuffer);
                    DeleteObject(buffer);
                }
                else
                {
                    self->PaintInitUI(hdc, client);
                }
                if (bufferDC) DeleteDC(bufferDC);
                EndPaint(hwnd, &ps);
                return 0;
            }
            break;
        }
        case WM_ERASEBKGND:
            if (self && self->m_style == ProgressWindowStyle::InitStyle) return 1;
            break;
        case WM_DESTROY:
            // Release any owned status payloads left in this window's queue.
            { MSG pending{}; while (PeekMessageW(&pending, hwnd, kWarmupMessage, kWarmupMessage, PM_REMOVE))
                delete reinterpret_cast<WarmupUpdate*>(pending.lParam); }
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

private:
    struct WarmupUpdate { std::wstring detail; bool stalled{}, failed{}; };
    HWND m_cancelButton{};
    std::atomic<bool> m_cancelRequested{}, m_closing{};
    static constexpr UINT kWarmupMessage = WM_APP + 0x214;
    static constexpr int kCancelButton = 0x510;
    struct BootStatus
    {
        const std::wstring& stage;
        const std::wstring& detail;
        int step;
        int total;
    };

    static constexpr UINT kProgressMessage = WM_APP + 0x210;
    static constexpr UINT kStatusMessage = WM_APP + 0x211;
    static constexpr UINT kBootStatusMessage = WM_APP + 0x212;
    static constexpr UINT kDetailMessage = WM_APP + 0x213;
    static constexpr int kProgressBarHeight = 12;
    ProgressWindowStyle m_style = ProgressWindowStyle::Basic;
    file::path m_imagePath = L"";
    HWND m_hWnd = nullptr;
    HWND m_hProgress = nullptr;
    HWND m_hText = nullptr;
    HBITMAP m_hBitmap = nullptr;
    HICON m_hBrandIcon = nullptr;
    HFONT m_hFont = nullptr;
    HFONT m_hBrandFont = nullptr;
    HFONT m_hStageFont = nullptr;
    HFONT m_hInfoFont = nullptr;
    HANDLE m_hThread = nullptr;
    HANDLE m_hReadyEvent = nullptr;
	std::wstring m_title = L"Initializing...";
    std::wstring m_stageText = L"Starting editor";
    std::wstring m_detailText = L"Preparing engine services...";
    int m_step = 0;
    int m_totalSteps = 0;
    int m_progress = 0;
};

inline static auto g_progressWindow = ProgressWindow::GetInstance();
