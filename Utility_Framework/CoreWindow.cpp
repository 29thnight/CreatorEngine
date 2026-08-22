#include "CoreWindow.h"

CoreWindow* CoreWindow::s_instance = nullptr;
CoreWindow::MessageHandler CoreWindow::m_CreateEventHandler = nullptr;
DUMP_TYPE CoreWindow::g_dumpType = DUMP_TYPE::DUNP_TYPE_MINI;

LRESULT CoreWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CoreWindow* self = nullptr;

    if (message == WM_NCCREATE)
    {
        // 윈도우 생성 시 초기화
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = static_cast<CoreWindow*>(cs->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<CoreWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (self && self->m_desc.messageInterceptor)
    {
        if (const std::optional<LRESULT> result =
            self->m_desc.messageInterceptor(hWnd, message, wParam, lParam))
        {
            return *result;
        }
    }

    if (message == WM_CREATE && m_CreateEventHandler)
    {
        m_CreateEventHandler(hWnd, wParam, lParam);
    }

    if (self)
    {
        return self->HandleMessage(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CoreWindow::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    auto it = m_handlers.find(message);
    if (it != m_handlers.end())
    {
        return it->second(hWnd, wParam, lParam);
    }
    
    return DefWindowProc(hWnd, message, wParam, lParam);
}

void CoreWindow::EnsureSwapChainCompatibleStyle()
{
    if (!m_hWnd)
    {
        return;
    }

    constexpr LONG requiredStyleMask = WS_POPUP | WS_BORDER | WS_CAPTION | WS_SYSMENU |
        WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME;

    LONG currentStyle = GetWindowLong(m_hWnd, GWL_STYLE);
    LONG currentExStyle = GetWindowLong(m_hWnd, GWL_EXSTYLE);

    bool styleUpdated = false;

    if ((currentStyle & requiredStyleMask) == 0)
    {
        currentStyle |= WS_OVERLAPPEDWINDOW;
        styleUpdated = true;
    }

    if ((currentExStyle & WS_EX_TOPMOST) != 0)
    {
        currentExStyle &= ~WS_EX_TOPMOST;
        styleUpdated = true;
    }

    if (styleUpdated)
    {
        SetWindowLong(m_hWnd, GWL_STYLE, currentStyle);
        SetWindowLong(m_hWnd, GWL_EXSTYLE, currentExStyle);
        SetWindowPos(m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}
