#pragma once

#include <Windows.h>
#include <optional>

namespace ImGuiWin32Cursor
{
    // 창 소유 스레드 전용. ImGui 컨텍스트를 읽지 않고 게시된 커서만 적용한다.
    std::optional<LRESULT> HandleWindowMessage(
        HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

    // PresentationThread 전용. UI가 결정한 모양을 GPU Present 전에 게시한다.
    void PublishFrameCursor(HWND windowHandle) noexcept;

    // 창 소유 스레드에서 UI 스레드 시작 전 / join 후 호출한다.
    void Reset(HWND windowHandle) noexcept;
}
