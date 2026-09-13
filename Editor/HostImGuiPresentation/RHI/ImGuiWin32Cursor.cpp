#include "ImGuiWin32Cursor.h"

#include <imgui.h>
#include <atomic>

namespace
{
    constexpr UINT kRefreshCursorMessage = WM_APP + 0x101;
    std::atomic<ImGuiMouseCursor> s_requestedCursor{ ImGuiMouseCursor_Arrow };
    // Windows의 이동/크기 변경 루프도 메인 HWND를 캡처한다.
    // 아래 상태는 창 소유 스레드에서만 접근한다.
    bool s_inSystemSizeMove = false;

    void ApplyRequestedCursor() noexcept
    {
        LPCWSTR resource = IDC_ARROW;
        switch (s_requestedCursor.load(std::memory_order_acquire))
        {
        case ImGuiMouseCursor_None:        ::SetCursor(nullptr); return;
        case ImGuiMouseCursor_TextInput:   resource = IDC_IBEAM; break;
        case ImGuiMouseCursor_ResizeAll:   resource = IDC_SIZEALL; break;
        case ImGuiMouseCursor_ResizeEW:    resource = IDC_SIZEWE; break;
        case ImGuiMouseCursor_ResizeNS:    resource = IDC_SIZENS; break;
        case ImGuiMouseCursor_ResizeNESW:  resource = IDC_SIZENESW; break;
        case ImGuiMouseCursor_ResizeNWSE:  resource = IDC_SIZENWSE; break;
        case ImGuiMouseCursor_Hand:        resource = IDC_HAND; break;
        case ImGuiMouseCursor_Wait:        resource = IDC_WAIT; break;
        case ImGuiMouseCursor_Progress:    resource = IDC_APPSTARTING; break;
        case ImGuiMouseCursor_NotAllowed:  resource = IDC_NO; break;
        default: break;
        }
        ::SetCursor(::LoadCursorW(nullptr, resource));
    }

    void RequestRefresh(HWND windowHandle) noexcept
    {
        // 메시지에 오래된 커서나 좌표를 싣지 않고 수신 시점의 상태를 쓴다.
        ::PostMessageW(windowHandle, kRefreshCursorMessage, 0, 0);
    }

    void RefreshCursor(HWND windowHandle) noexcept
    {
        if (s_inSystemSizeMove) return;

        const HWND capture = ::GetCapture();
        if (capture != nullptr && capture != windowHandle) return;

        POINT point{};
        if (!::GetCursorPos(&point)) return;
        if (capture != windowHandle && ::WindowFromPoint(point) != windowHandle) return;

        // 게시 뒤 마우스가 테두리/제목표시줄로 이동했을 수 있다. 판정도
        // HWND 스레드에서 다시 하여 Windows의 비클라이언트 커서를 보존한다.
        const LRESULT hit = ::SendMessageW(windowHandle, WM_NCHITTEST, 0,
            MAKELPARAM(point.x, point.y));
        if (hit == HTCLIENT)
            ApplyRequestedCursor();
    }
}

std::optional<LRESULT> ImGuiWin32Cursor::HandleWindowMessage(
    HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && !s_inSystemSizeMove)
        {
            ApplyRequestedCursor();
            return TRUE;
        }
        // 비클라이언트 처리도 지금 끝낸다. WM_SETCURSOR를 UI 큐에서
        // 뒤늦게 재생하면 원래 메시지의 반환값과 커서 소유 스레드를 잃는다.
        return ::DefWindowProcW(windowHandle, message, wParam, lParam);
    case kRefreshCursorMessage:
        RefreshCursor(windowHandle);
        return 0;
    case WM_ENTERSIZEMOVE:
        s_inSystemSizeMove = true;
        break;
    case WM_EXITSIZEMOVE:
        s_inSystemSizeMove = false;
        RequestRefresh(windowHandle);
        break;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        RequestRefresh(windowHandle);
        break;
    }
    return std::nullopt;
}

void ImGuiWin32Cursor::PublishFrameCursor(HWND windowHandle) noexcept
{
    const ImGuiMouseCursor cursor = ImGui::GetIO().MouseDrawCursor
        ? ImGuiMouseCursor_None : ImGui::GetMouseCursor();
    if (s_requestedCursor.exchange(cursor, std::memory_order_acq_rel) != cursor)
        RequestRefresh(windowHandle);
}

void ImGuiWin32Cursor::Reset(HWND windowHandle) noexcept
{
    s_requestedCursor.store(ImGuiMouseCursor_Arrow, std::memory_order_release);
    s_inSystemSizeMove = false;
    RequestRefresh(windowHandle);
}
