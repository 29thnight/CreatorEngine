#pragma once

namespace ce::platform::windows::detail
{
    using window_message_interceptor =
        std::function<std::optional<LRESULT>(HWND, UINT, WPARAM, LPARAM)>;

    // Only the Win32 adapter sees SDK style bits, resource IDs and raw messages.
    struct window_desc : ::Core::WindowDesc
    {
        int iconResourceId{ 0 };
        DWORD style{ WS_OVERLAPPEDWINDOW };
        DWORD extendedStyle{ 0 };
        window_message_interceptor messageInterceptor{};
    };

    inline HWND window_handle(::Core::NativeWindowHandle handle) noexcept
    {
        if (handle.kind != ::Core::NativeWindowKind::Windows)
        {
            return nullptr;
        }
        return static_cast<HWND>(handle.window);
    }
}
