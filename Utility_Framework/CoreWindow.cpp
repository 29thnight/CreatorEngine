#include "CoreWindow.h"
#include "WinProcProxy.h"

CoreWindow* CoreWindow::s_instance = nullptr;
CoreWindow::MessageHandler CoreWindow::m_CreateEventHandler = nullptr;
DUMP_TYPE CoreWindow::g_dumpType = DUMP_TYPE::DUNP_TYPE_MINI;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CoreWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CoreWindow* self = nullptr;

    //if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    //{
    //    return true;
    //}

    if (message == WM_SETCURSOR)
    {
        // Ŀ�� ����
        if (LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
            return TRUE; // Ŀ�� ������ �Ϸ������� �˸�
        }
		return FALSE; // �⺻ Ŀ�� ó���� �����
    }
    else
    {
        WinProcProxy::GetInstance()->PushMessage(hWnd, message, wParam, lParam);
    }

    if (message == WM_NCCREATE)
    {
        // ������ ���� �� �ʱ�ȭ
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
