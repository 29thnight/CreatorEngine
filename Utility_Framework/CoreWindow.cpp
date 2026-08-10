#include "CoreWindow.h"
#include "WinProcProxy.h"

CoreWindow* CoreWindow::s_instance = nullptr;
CoreWindow::MessageHandler CoreWindow::m_CreateEventHandler = nullptr;
DUMP_TYPE CoreWindow::g_dumpType = DUMP_TYPE::DUNP_TYPE_MINI;
// IMGUI_IMPL_API는 imgui.h가 준다. 전에는 CoreWindow.h의 imgui_internal.h가
// 대신 끌어와 줬는데, 그 헤더는 UF 전체를 imgui에 묶고 있었다.
#include <imgui.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CoreWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CoreWindow* self = nullptr;

    // ── 모드 분기 (B0-1: BUILD_FLAG → 런타임 EngineMode) ──
    //
    // 에디터만 메시지를 WinProcProxy 큐에 넣는다 — 소비자(ImGui WndProc 중계)가
    // 에디터의 CE 스레드에만 있어서, 플레이어가 넣으면 비우는 이 없이 쌓인다.
    if (EngineMode::IsEditor())
    {
        if (message == WM_SETCURSOR)
        {
            // 커서 설정
            if (LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE; // 커서 변경을 완료했음을 알림
            }
            return FALSE; // 기본 커서 처리를 계속함
        }
        else
        {
            WinProcProxy::GetInstance()->PushMessage(hWnd, message, wParam, lParam);
        }
    }

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

    if (message == WM_CREATE)
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
