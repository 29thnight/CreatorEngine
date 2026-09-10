#include "EditorWindowChrome.h"

#include "EditorSettingsStore.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <windowsx.h>

#include <algorithm>

namespace
{
    // 제목표시줄 오른쪽 버튼 셋. 폭은 프레임 높이에 비례해 스케일을 따라간다.
    constexpr float kWindowButtonWidthRatio = 1.6f;

    std::wstring WidenUtf8(const std::string& value)
    {
        if (value.empty()) return {};
        const int required = MultiByteToWideChar(
            CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
            nullptr, 0);
        if (required <= 0) return {};
        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
            static_cast<int>(value.size()), wide.data(), required);
        return wide;
    }

    // 창 조작은 표시 스레드에서 부르므로 동기 ShowWindow가 아니라 게시로 넘긴다.
    // WndProc가 도는 메인 스레드가 실행한다.
    void PostSystemCommand(HWND windowHandle, WPARAM command) noexcept
    {
        if (nullptr != windowHandle)
            PostMessageW(windowHandle, WM_SYSCOMMAND, command, 0);
    }
}

std::wstring ComposeEditorWindowTitle()
{
    const std::wstring projectName =
        WidenUtf8(EditorSettingsStore::Get().Build().GetProjectName());
    if (projectName.empty()) return L"Creator Engine";
    return projectName + L" - Creator Engine";
}

EditorWindowChrome& EditorWindowChrome::Get() noexcept
{
    static EditorWindowChrome instance;
    return instance;
}

void EditorWindowChrome::Attach(HWND windowHandle) noexcept
{
    m_windowHandle = windowHandle;
    if (nullptr == windowHandle) return;

    // 이미 만들어진 창이라 프레임을 다시 계산시켜야 WM_NCCALCSIZE가 돈다.
    SetWindowPos(windowHandle, nullptr, 0, 0, 0, 0,
        SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

std::optional<LRESULT> EditorWindowChrome::HandleWindowMessage(
    HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_NCCALCSIZE:
        return HandleNonClientCalcSize(windowHandle, wParam, lParam);
    case WM_NCHITTEST:
        return HandleNonClientHitTest(windowHandle, lParam);
    default:
        return std::nullopt;
    }
}

std::optional<LRESULT> EditorWindowChrome::HandleNonClientCalcSize(
    HWND windowHandle, WPARAM wParam, LPARAM lParam) noexcept
{
    if (TRUE != wParam) return std::nullopt;

    NCCALCSIZE_PARAMS* const parameters =
        reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
    if (nullptr == parameters) return std::nullopt;

    // 상단 캡션만 지우고 좌·우·하단 프레임은 Windows가 계산한 값을 지킨다.
    // 프레임을 전부 지우면 크기 조절 테두리와 Aero 스냅이 같이 사라진다.
    const RECT requested = parameters->rgrc[0];
    DefWindowProcW(windowHandle, WM_NCCALCSIZE, wParam, lParam);
    const RECT adjusted = parameters->rgrc[0];

    RECT client = requested;
    client.left = adjusted.left;
    client.right = adjusted.right;
    client.bottom = adjusted.bottom;

    // 최대화된 창의 사각은 작업 영역보다 테두리 두께만큼 크다. 상단을 창
    // 경계에 그대로 붙이면 제목표시줄이 화면 위로 밀려 잘린다. 그렇다고
    // Windows가 준 값을 쓰면 캡션 높이까지 함께 들어와 상단에 31px 죽은
    // 띠가 남는다(실측). 넘침을 막을 테두리 두께만 남기고 캡션은 지운다.
    if (IsZoomed(windowHandle))
    {
        client.top = requested.top +
            GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
    }

    parameters->rgrc[0] = client;
    return 0;
}

std::optional<LRESULT> EditorWindowChrome::HandleNonClientHitTest(
    HWND windowHandle, LPARAM lParam) noexcept
{
    // 좌·우·하단 테두리와 그 코너 판정은 Windows 것을 그대로 쓴다.
    const LRESULT frameHit =
        DefWindowProcW(windowHandle, WM_NCHITTEST, 0, lParam);
    if (HTCLIENT != frameHit) return std::nullopt;

    POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    if (!ScreenToClient(windowHandle, &point)) return std::nullopt;

    RECT client{};
    if (!GetClientRect(windowHandle, &client)) return std::nullopt;

    // ── 상단 테두리는 우리가 되살린다 ──
    //
    // WM_NCCALCSIZE가 상단 캡션을 지우면서 그 자리가 클라이언트가 됐고,
    // 그래서 Windows는 창 위쪽 몇 픽셀을 HTTOP이 아니라 HTCLIENT로 답한다.
    // 되살리지 않으면 위쪽으로 크기를 늘릴 수 없다 — 실측으로 잡았다.
    // 최대화 상태에는 늘릴 방향이 없으므로 건너뛴다.
    if (!IsZoomed(windowHandle))
    {
        const int padded = GetSystemMetrics(SM_CXPADDEDBORDER);
        const int verticalBorder = GetSystemMetrics(SM_CYSIZEFRAME) + padded;
        const int horizontalBorder = GetSystemMetrics(SM_CXSIZEFRAME) + padded;
        if (point.y >= 0 && point.y < verticalBorder)
        {
            // 코너도 함께 돌려준다. 상단만 HTTOP으로 답하면 위쪽 두 코너가
            // 대각선 조절을 잃는다.
            if (point.x < horizontalBorder) return HTTOPLEFT;
            if (point.x >= client.right - horizontalBorder) return HTTOPRIGHT;
            return HTTOP;
        }
    }

    // ── 제목표시줄의 빈 자리는 캡션으로 본다 ──
    const float titleBarHeight = m_titleBarHeight.load(std::memory_order_relaxed);
    if (titleBarHeight <= 0.f) return std::nullopt;
    if (point.y < 0 || static_cast<float>(point.y) >= titleBarHeight)
        return std::nullopt;

    const float left = m_dragRegionLeft.load(std::memory_order_relaxed);
    const float right = m_dragRegionRight.load(std::memory_order_relaxed);
    const float x = static_cast<float>(point.x);
    if (x < left || x >= right) return std::nullopt;

    return HTCAPTION;
}

void EditorWindowChrome::DrawTitleBarTail()
{
    ImGuiWindow* const window = ImGui::GetCurrentWindow();
    if (nullptr == window) return;

    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const ImVec2 rowMin = window->Pos;
    const float rowHeight = ImGui::GetFrameHeight();
    const float rowRight = rowMin.x + window->Size.x;

    // 메뉴가 끝난 자리가 끌기 영역의 시작이다.
    const float dragLeft = ImGui::GetCursorScreenPos().x;

    const float buttonWidth = rowHeight * kWindowButtonWidthRatio;
    const float buttonsLeft = rowRight - buttonWidth * 3.f;

    // ── 가운데 제목 ──
    const std::wstring wideTitle = ComposeEditorWindowTitle();
    char title[256]{};
    WideCharToMultiByte(CP_UTF8, 0, wideTitle.c_str(),
        static_cast<int>(wideTitle.size()), title,
        static_cast<int>(sizeof(title) - 1), nullptr, nullptr);

    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    const ImVec2 titleSize = ImGui::CalcTextSize(title);
    const float titleLeft = rowMin.x + (window->Size.x - titleSize.x) * 0.5f;
    // 메뉴나 버튼과 겹치면 제목을 그리지 않는다. 겹쳐 그리면 어느 쪽도 못 읽는다.
    if (titleLeft > dragLeft && titleLeft + titleSize.x < buttonsLeft)
    {
        drawList->AddText(
            ImVec2(titleLeft, rowMin.y + (rowHeight - titleSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
    }

    // ── 오른쪽 창 버튼 셋 ──
    const ImU32 glyphColor = ImGui::GetColorU32(ImGuiCol_Text);
    const float glyph = ImGui::GetFontSize() * 0.32f;

    for (int index = 0; index < 3; ++index)
    {
        const float left = buttonsLeft + buttonWidth * static_cast<float>(index);
        ImGui::SetCursorScreenPos(ImVec2(left, rowMin.y));

        const char* const ids[] = { "##chromeMinimize", "##chromeMaximize", "##chromeClose" };
        const bool pressed = ImGui::InvisibleButton(
            ids[index], ImVec2(buttonWidth, rowHeight));
        const bool hovered = ImGui::IsItemHovered();

        if (hovered)
        {
            // 닫기만 붉게. 되돌릴 수 없는 버튼과 나머지를 색으로 가른다.
            const ImU32 hoverColor = 2 == index
                ? IM_COL32(196, 43, 28, 255)
                : ImGui::GetColorU32(ImGuiCol_ButtonHovered);
            drawList->AddRectFilled(ImVec2(left, rowMin.y),
                ImVec2(left + buttonWidth, rowMin.y + rowHeight), hoverColor);
        }

        const ImVec2 center{ left + buttonWidth * 0.5f, rowMin.y + rowHeight * 0.5f };
        switch (index)
        {
        case 0:
            drawList->AddLine(ImVec2(center.x - glyph, center.y),
                ImVec2(center.x + glyph, center.y), glyphColor, 1.f);
            break;
        case 1:
            if (nullptr != m_windowHandle && IsZoomed(m_windowHandle))
            {
                // 복원 — 겹친 사각 둘로 "원래 크기로"를 표시한다.
                drawList->AddRect(ImVec2(center.x - glyph, center.y - glyph * 0.6f),
                    ImVec2(center.x + glyph * 0.6f, center.y + glyph),
                    glyphColor, 0.f, 0, 1.f);
                drawList->AddRect(ImVec2(center.x - glyph * 0.6f, center.y - glyph),
                    ImVec2(center.x + glyph, center.y + glyph * 0.6f),
                    glyphColor, 0.f, 0, 1.f);
            }
            else
            {
                drawList->AddRect(ImVec2(center.x - glyph, center.y - glyph),
                    ImVec2(center.x + glyph, center.y + glyph),
                    glyphColor, 0.f, 0, 1.f);
            }
            break;
        default:
            drawList->AddLine(ImVec2(center.x - glyph, center.y - glyph),
                ImVec2(center.x + glyph, center.y + glyph), glyphColor, 1.f);
            drawList->AddLine(ImVec2(center.x - glyph, center.y + glyph),
                ImVec2(center.x + glyph, center.y - glyph), glyphColor, 1.f);
            break;
        }

        if (!pressed) continue;
        switch (index)
        {
        case 0:  PostSystemCommand(m_windowHandle, SC_MINIMIZE); break;
        case 1:
            PostSystemCommand(m_windowHandle,
                (nullptr != m_windowHandle && IsZoomed(m_windowHandle))
                    ? SC_RESTORE : SC_MAXIMIZE);
            break;
        default:
            if (nullptr != m_windowHandle)
                PostMessageW(m_windowHandle, WM_CLOSE, 0, 0);
            break;
        }
    }

    // ── 끌기 영역 게시 ──
    // ImGui 좌표는 다중 뷰포트가 꺼져 있어 메인 뷰포트 원점이 클라이언트
    // 원점이다. WndProc가 ScreenToClient로 옮겨 온 좌표와 같은 자로 잰다.
    m_titleBarHeight.store(rowHeight, std::memory_order_relaxed);
    m_dragRegionLeft.store(
        std::max(0.f, dragLeft - viewport->Pos.x), std::memory_order_relaxed);
    m_dragRegionRight.store(
        std::max(0.f, buttonsLeft - viewport->Pos.x), std::memory_order_relaxed);
}
