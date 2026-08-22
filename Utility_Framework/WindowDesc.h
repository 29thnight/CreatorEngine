#pragma once

#include <Windows.h>

#include <functional>
#include <optional>
#include <string>

using WindowMessageInterceptor =
    std::function<std::optional<LRESULT>(HWND, UINT, WPARAM, LPARAM)>;

// Window creation policy belongs to the process host (Editor or Player).
// CoreWindow only applies the supplied description and never asks which host is running.
struct WindowDesc
{
    std::wstring title;
    int clientWidth{ 800 };
    int clientHeight{ 600 };
    int iconResourceId{ 0 };
    DWORD style{ WS_OVERLAPPEDWINDOW };
    DWORD extendedStyle{ 0 };
    bool centerOnDesktop{ true };
    bool fitNearestMonitor{ false };
    bool showOnCreate{ false };
    bool acceptFileDrops{ false };
    WindowMessageInterceptor messageInterceptor{};
};
