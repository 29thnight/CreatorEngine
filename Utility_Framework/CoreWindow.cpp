#include "CoreWindow.h"
#include "WinProcProxy.h"

CoreWindow* CoreWindow::s_instance = nullptr;
DUMP_TYPE CoreWindow::g_dumpType = DUMP_TYPE::DUNP_TYPE_MINI;
#ifndef BUILD_FLAG
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif // !BUILD_FLAG
LRESULT CoreWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CoreWindow* self = nullptr;

#ifndef BUILD_FLAG
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
#endif // !BUILD_FLAG

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

    // WM_CREATE용 정적 콜백 슬롯은 제거했다.
    //
    // 널 검사 없이 std::function을 호출하고 있었는데(등록 전에 창을 만들면
    // WndProc 안에서 bad_function_call이 던져진다), 정작 등록된 유일한 핸들러는
    // EngineBootstrap의 `return 0;` 하나뿐이었다. 창 생성 시점에 할 일이 다시
    // 생기면 인스턴스 핸들러(RegisterHandler)로 WM_NCCREATE를 받으면 된다.

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
