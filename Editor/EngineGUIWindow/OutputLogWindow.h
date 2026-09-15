#pragma once

// Output Log 창. 상태는 `OutputLogView` 가 들고, 여기는 **그리는 일만** 한다.
//
// 옛 본문은 `MenuBarWindow::ShowLogWindow()` 안에 있었고, 그 파일은 2,826 줄로
// 규약의 800 줄 상한을 세 배 넘겼다. 창 하나를 파일 하나로 뺀다.
//
// 그리기는 표준 위젯만 쓴다 — `AddText`/`RenderTextClipped`/`ItemAdd` 를 직접
// 부르지 않는다. 그래야 ImGui 가 클리핑과 nav 커서를 맡고, W2 의 custom draw
// 계약(`verify-editor-widget-clipping`·`verify-editor-keyboard-nav`)이 요구하는
// 신고를 따로 들 필요가 없다. 자르는 일은 `EllipsizeToWidth` 가 **실측 폭**으로
// 한다.

#include "OutputLogView.h"

#include <cstdint>
#include <string>

struct ImFont;

namespace editor
{
    class OutputLogWindow
    {
    public:
        // 폰트는 싣지 않는다. 한글 폰트는 메뉴바가 이미 한 벌 올려 두므로
        // 그것을 받아 쓴다 — 여기서 또 올리면 아틀라스에 같은 얼굴이 두 벌
        // 생긴다.
        void SetFont(ImFont* font) { m_font = font; }

        // 셸이 창 본문으로 부른다.
        void Draw();

    private:
        // 수준별 누적을 내보이는 알약. 누르면 그 수준을 최소 수준으로 세운다.
        static bool CountChip(const char* icon, std::uint64_t count,
            spdlog::level::level_enum level, bool active, const char* tooltip);

        void PumpStore();
        void DrawToolbar();
        void DrawList(float height);
        void DrawDetail(float height);

        OutputLogView m_view;
        OutputLogFilter m_filter;
        ImFont* m_font{ nullptr };
        char m_search[192]{};
        int m_levelChoice{ 0 };
        bool m_autoScroll{ true };
        // 목록이 늘었을 때만 바닥으로 민다. 매 프레임 밀면 사람이 위로 올려
        // 읽는 중에도 끌려 내려간다.
        std::size_t m_lastRowCount{};
    };
}
