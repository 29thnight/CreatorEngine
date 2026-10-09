#pragma once

#include "EditorTheme.h"
#include "EditorWindowChrome.h"

#include <Windows.h>
#include <windowsx.h>
#include <imgui.h>

#include <algorithm>
#include <optional>

namespace ce::profiler_viewer
{
    // The viewer pumps Win32 and renders ImGui on the same thread. Only the
    // rendered empty caption rectangle is draggable; menu and action items
    // remain client input. No Editor singleton or engine window is linked here.
    class window_chrome
    {
    public:
        void invalidate() noexcept
        {
            caption_height_ = 0.f;
        }

        void publish_caption(float height, float left, float right) noexcept
        {
            caption_height_ = height;
            caption_left_ = left;
            caption_right_ = std::max(left, right);
        }

        std::optional<LRESULT> handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
        {
            switch (message)
            {
            case WM_NCCALCSIZE:
                if (wparam == TRUE)
                {
                    auto* parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                    const RECT requested = parameters->rgrc[0];
                    DefWindowProcW(window, message, wparam, lparam);
                    // Keep the standard side/bottom resize frame and replace
                    // only the native caption with client-rendered ImGui chrome.
                    parameters->rgrc[0].top = requested.top;
                    if (IsZoomed(window))
                    {
                        const UINT dpi = GetDpiForWindow(window);
                        parameters->rgrc[0].top += GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                            GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    }
                    return 0;
                }
                break;
            case WM_NCHITTEST:
                return hit_test(window, lparam);
            case WM_NCRBUTTONUP:
                if (wparam == HTCAPTION)
                {
                    show_system_menu(window, { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) });
                    return 0;
                }
                break;
            case WM_SYSCOMMAND:
                if ((wparam & 0xfff0) == SC_KEYMENU && lparam == VK_SPACE)
                {
                    POINT point{ 0, static_cast<LONG>(caption_height_) };
                    ClientToScreen(window, &point);
                    show_system_menu(window, point);
                    return 0;
                }
                break;
            case WM_SIZE:
            case WM_DPICHANGED:
                // Do not use stale button/caption geometry before the next draw.
                invalidate();
                break;
            }
            return std::nullopt;
        }

        void draw_system_buttons(HWND window, const EditorTitleBarLayout& layout, const ImVec2& origin)
        {
            auto* draw = ImGui::GetWindowDrawList();
            const char* ids[]{ "##Minimize", "##MaximizeRestore", "##CloseViewer" };
            const char* tips[]{ "Minimize", IsZoomed(window) ? "Restore" : "Maximize", "Close" };
            const float glyph = std::min(layout.height, layout.systemButtonWidth) * .17f;
            const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
            for (int index = 0; index < 3; ++index)
            {
                const ImVec2 begin{ origin.x + layout.systemButtonsLeft + index * layout.systemButtonWidth, origin.y };
                const ImVec2 end{ begin.x + layout.systemButtonWidth, begin.y + layout.height };
                ImGui::SetCursorScreenPos(begin);
                const bool pressed = ImGui::InvisibleButton(ids[index], { layout.systemButtonWidth, layout.height },
                    ImGuiButtonFlags_EnableNav);
                if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                {
                    const auto token = index == 2 ? editor::ThemeColor::Error : editor::ThemeColor::PanelRaised;
                    draw->AddRectFilled(begin, end, ImGui::GetColorU32(editor::ThemeColorValue(token)));
                }
                if (ImGui::IsItemFocused())
                {
                    draw->AddRect(begin, end, ImGui::GetColorU32(ImGuiCol_NavCursor));
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", tips[index]);
                }
                const ImVec2 center{ (begin.x + end.x) * .5f, (begin.y + end.y) * .5f };
                if (index == 0)
                {
                    draw->AddLine({ center.x - glyph, center.y }, { center.x + glyph, center.y }, color);
                }
                else if (index == 1)
                {
                    if (IsZoomed(window))
                    {
                        draw->AddRect({ center.x - glyph, center.y - glyph * .6f },
                            { center.x + glyph * .6f, center.y + glyph }, color);
                        draw->AddRect({ center.x - glyph * .6f, center.y - glyph },
                            { center.x + glyph, center.y + glyph * .6f }, color);
                    }
                    else
                    {
                        draw->AddRect({ center.x - glyph, center.y - glyph },
                            { center.x + glyph, center.y + glyph }, color);
                    }
                }
                else
                {
                    draw->AddLine({ center.x - glyph, center.y - glyph }, { center.x + glyph, center.y + glyph }, color);
                    draw->AddLine({ center.x - glyph, center.y + glyph }, { center.x + glyph, center.y - glyph }, color);
                }
                if (pressed)
                {
                    const WPARAM command = index == 0 ? SC_MINIMIZE : index == 2 ? SC_CLOSE :
                        IsZoomed(window) ? SC_RESTORE : SC_MAXIMIZE;
                    PostMessageW(window, WM_SYSCOMMAND, command, 0);
                }
            }
        }

    private:
        std::optional<LRESULT> hit_test(HWND window, LPARAM lparam) const
        {
            const LRESULT native = DefWindowProcW(window, WM_NCHITTEST, 0, lparam);
            if (native != HTCLIENT)
            {
                return native;
            }
            POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            RECT client{};
            if (!ScreenToClient(window, &point) || !GetClientRect(window, &client))
            {
                return HTCLIENT;
            }
            if (!IsZoomed(window))
            {
                const UINT dpi = GetDpiForWindow(window);
                const int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                const int vertical = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + padding;
                const int horizontal = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + padding;
                if (point.y >= 0 && point.y < vertical)
                {
                    if (point.x < horizontal)
                    {
                        return HTTOPLEFT;
                    }
                    if (point.x >= client.right - horizontal)
                    {
                        return HTTOPRIGHT;
                    }
                    return HTTOP;
                }
            }
            if (point.y < 0 || point.y >= caption_height_ || point.x < caption_left_ || point.x >= caption_right_)
            {
                return HTCLIENT;
            }
            if (ImGui::GetCurrentContext() && (ImGui::IsAnyItemActive() ||
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)))
            {
                // Clicking outside an open menu must dismiss it, not start an
                // OS move loop or steal a held ImGui drag.
                return HTCLIENT;
            }
            return HTCAPTION;
        }

        static void show_system_menu(HWND window, POINT point)
        {
            HMENU menu = GetSystemMenu(window, FALSE);
            if (!menu)
            {
                return;
            }
            const bool maximized = IsZoomed(window) != FALSE;
            EnableMenuItem(menu, SC_RESTORE, MF_BYCOMMAND | (maximized ? MF_ENABLED : MF_GRAYED));
            EnableMenuItem(menu, SC_MAXIMIZE, MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
            EnableMenuItem(menu, SC_SIZE, MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
            EnableMenuItem(menu, SC_MOVE, MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
            SetForegroundWindow(window);
            const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                point.x, point.y, 0, window, nullptr);
            if (command != 0)
            {
                PostMessageW(window, WM_SYSCOMMAND, command, 0);
            }
            PostMessageW(window, WM_NULL, 0, 0);
        }

        float caption_height_ = 0.f;
        float caption_left_ = 0.f;
        float caption_right_ = 0.f;
    };
}
