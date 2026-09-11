#pragma once
// 툴바의 모드 버튼 (PHASE 21 W2 · 계획서 §7.1 의 `EditorModeButton`).
//
// ── 신설이다. 그리고 은퇴 하나를 데려온다 ─────────────────────────────────
//
// 계획서는 `ImGuiHelper/ToggleUI.h` 와 `widgets.{h,cpp}` 를 후보로 두고
// "겹치면 승계, 아니면 신설 후 은퇴" 로 판정을 미뤘다. 실측이 답했다
// (결정표 §2.5·§2.6) — `ToggleUI::ToggleSwitch` 는 **소비자가 0** 이고 그
// 시각(둥근 트랙 + 원형 손잡이)은 여기서 필요한 "flat icon + active marker" 와
// 다른 물건이다. `ax::Widgets::Icon` 은 노드 에디터의 핀 아이콘이라 범위 밖이다.
// 그래서 신설이고, 착지와 함께 `ToggleUI.h` 를 지운다.
//
// ── active 를 배경색으로 알리지 않는다 ────────────────────────────────────
//
// 원본 자리(`MenuBarWindow::RenderToolBar`)는 일시정지 중일 때
// `ImGuiCol_Button`·`ButtonHovered`·`ButtonActive` 세 칸을 모두 `ButtonActive`
// 색으로 덮었다. 그러면 눌린 상태와 켜진 상태가 같은 색이 되어, 켜진 버튼에
// 마우스를 올리거나 눌러도 아무 반응이 없다. 여기서는 켜짐을 **아래 marker** 가
// 들고 배경은 hover/press 를 그대로 든다. 둘이 서로 다른 축이라 겹치지 않는다.
//
// ── 색이 묻으면 표시가 사라진다 ───────────────────────────────────────────
//
// marker 가 배경 셋 중 하나와 같은 값이 되면 켜짐 표시가 조용히 사라진다.
// `EditorSectionHeader` 가 겪은 것과 같은 실패 양식이라(테마가
// `HeaderHovered` 와 `HeaderActive` 를 같은 값으로 두어 hover 를 잃었다),
// 구현이 고르는 값을 내보내고 검사가 상이함을 단정한다.

#include <cstdint>

namespace editor::widgets
{
    /// 배경이 가리는 세 상태.
    enum class mode_button_surface
    {
        Idle,
        Hovered,
        Held,
        Count
    };

    /// 그 상태가 쓰는 배경색. `0xRRGGBB`.
    std::uint32_t mode_button_surface_hex(mode_button_surface surface) noexcept;

    /// 켜짐을 알리는 아래 marker 의 색. `0xRRGGBB`. 배경 셋과 달라야 한다.
    std::uint32_t mode_button_marker_hex() noexcept;

    /// 아이콘 글자색. `0xRRGGBB`. 꺼진 버튼은 다른 값이어야 손댈 수 없음이 보인다.
    std::uint32_t mode_button_text_hex(bool enabled) noexcept;

    /// 버튼 하나가 차지할 폭. 툴바가 묶음을 가운데 놓으려면 그리기 전에
    /// 폭을 알아야 한다.
    float mode_button_width(const char* icon);

    /// 모드 버튼 하나에 넘기는 것.
    struct mode_button_request
    {
        /// 아이콘 문자열이자 ImGui ID 의 씨앗. 비울 수 없다.
        const char* icon{ nullptr };

        /// 켜져 있는가. 아래 marker 로 표시한다.
        bool active{ false };

        /// 손댈 수 있는가. 거짓이면 눌리지 않고 글자가 흐려진다.
        bool enabled{ true };

        /// 올려 두었을 때 뜨는 설명. 비면 띄우지 않는다.
        const char* tooltip{ nullptr };
    };

    /// 버튼을 그린다. 이번 프레임에 눌렸으면 참.
    bool draw_mode_button(const mode_button_request& request);
}
