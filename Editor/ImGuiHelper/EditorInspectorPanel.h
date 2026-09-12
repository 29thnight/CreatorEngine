#pragma once
// 인스펙터의 **최상위 타입 한 개 = 패널 한 개** (PHASE 21 W2-I4).
//
// ── 왜 `EditorSectionHeader` 를 쓰지 않는가 ───────────────────────────────
//
// 그쪽은 **채운 막대**다. 배경을 칠하고 그 위에 이름을 올린다. 구간을 나누는
// 데는 맞지만 컴포넌트 목록에는 맞지 않는다 — 컴포넌트마다 막대가 하나씩
// 서면 목록이 줄무늬가 되고, 정작 "어디부터 어디까지가 한 컴포넌트인가" 는
// 여전히 안 보인다. 막대는 자기 줄만 칠하지 본문을 감싸지 않기 때문이다.
//
// s&box 는 반대로 한다. 머리줄에 배경을 칠하지 않고 **위쪽에 1px 선 두 줄**을
// 긋는다(`InspectorHeader.OnPaint` 의 Top border — `Theme.ControlBackground`
// 한 줄, 바로 아래 `Theme.BorderLight` 한 줄). 어두운 선과 밝은 선이 붙어
// 패인 홈처럼 보이고, 그 홈이 패널의 경계가 된다. 칠하는 면적이 0 이라
// 컴포넌트가 열 개여도 목록이 시끄러워지지 않는다.
//
// 그래서 이것은 `EditorSectionHeader` 의 개정이 아니라 **다른 위젯**이다.
// 둘 다 남는다 — 막대는 한 컴포넌트 **안**의 하위 구간(`Advanced Rendering`
// 같은)에 그대로 쓰고, 이쪽은 최상위 타입 단위에만 쓴다.
//
// ── 치수의 출처 ───────────────────────────────────────────────────────────
//
// `Facepunch/sbox-public` 의 `game/addons/tools/Code/Scene/GameObjectInspector/
// InspectorHeader.cs`(MIT). `BuildUI` 의 칸 순서와 폭을 그대로 옮겼다.
//
//     Layout.AddSpacingCell( 4 );
//     expanderRect = Layout.AddRow(); expanderRect.AddSpacingCell( 16 );
//     iconRect     = Layout.AddRow(); iconRect.AddSpacingCell( 22 );
//     Layout.AddSpacingCell( 4 );
//     if ( checkbox ) { checkbox.FixedSize = 24; Layout.Add( checkbox );
//                       Layout.AddSpacingCell( 8 ); }
//     else            { Layout.AddSpacingCell( 24 + 8 ); }
//     textRect = Layout.AddColumn( 1 );
//     rightRect.Spacing = 2;  // IconButton FixedSize = 20
//     Layout.AddSpacingCell( 16 );
//
// 마지막 `else` 가 요점이다. 체크박스가 없는 타입(Transform)도 **같은 폭을
// 비워 둔다**. 그래서 활성 토글이 있는 컴포넌트와 없는 컴포넌트의 이름이 한
// 선에 선다. 자리를 안 비우면 Transform 만 이름이 왼쪽으로 밀린다.
//
// 높이는 `FixedHeight = Theme.RowHeight + 8` 이다. 우리 쪽 `Theme.RowHeight`
// 에 해당하는 것은 `ImGui::GetFrameHeight()` 다 — 토큰을 `ThemePixels` 로 직접
// 재면 style 의 여백이 배율을 안 먹어 DPI 2 에서 어긋난다(`EditorSectionHeader`
// 가 같은 이유로 같은 선택을 했다).
//
// 색은 옮기지 않았다. s&box 의 hex(`#181818`·`Theme.Blue`)를 그대로 들이면
// 우리 팔레트와 두 벌이 된다. 뜻만 옮긴다 — 어두운 선은 `Canvas`, 밝은 선은
// `Border`, 아이콘·쉐브론의 물감은 `Primary`, 이름은 `Text`.
//
// ── 아이콘은 글리프 문자열로 받는다 ───────────────────────────────────────
//
// 이 위젯은 어떤 타입이 어떤 그림을 쓰는지 모른다. 호출자가 이미 고른 글리프를
// 넘긴다. 폰트에 글리프가 없거나 아이콘 자체가 없으면 **칸만 비우고 넘어간다**
// — 이름 x 가 아이콘 유무로 흔들리지 않는다. 이 저장소는 아이콘 폰트가 조용히
// 네모를 그린 적이 있어서, 없을 때 아무것도 안 그리는 쪽이 덜 나쁘다.

#include <cstdint>

namespace editor::widgets
{
    /// 패널 머리줄 한 줄의 칸 치수. **ImGui 를 부르지 않는다** — 검사가 합성
    /// 폭으로 직접 몰 수 있게 하려고 뗐다(`measure_property_layout` 과 같은 이유).
    struct inspector_panel_metrics
    {
        /// 머리줄 전체 높이.
        float height{ 0.f };

        /// 왼쪽 여백.
        float lead{ 0.f };

        /// 쉐브론 칸.
        float expander{ 0.f };

        /// 아이콘 칸.
        float icon{ 0.f };

        /// 아이콘과 체크박스 사이.
        float icon_gap{ 0.f };

        /// 활성 체크박스 칸. **체크박스가 없어도 비워 둔다**(위 주석 참고).
        float toggle{ 0.f };

        /// 체크박스와 이름 사이.
        float toggle_gap{ 0.f };

        /// 이름이 시작하는 x 오프셋. 위 칸들의 합이다.
        float title_x{ 0.f };

        /// 이름이 쓸 수 있는 폭. 오른쪽 버튼과 꼬리 여백을 뺀 나머지다.
        float title_w{ 0.f };

        /// 오른쪽 버튼 하나의 폭.
        float button{ 0.f };

        /// 오른쪽 버튼 사이 간격.
        float button_gap{ 0.f };

        /// 오른쪽 끝 여백.
        float trail{ 0.f };
    };

    /// 치수를 정하는 모든 것. 테마도 ImGui 도 읽지 않는다.
    struct inspector_panel_inputs
    {
        /// `ImGui::GetContentRegionAvail().x`.
        float available{ 0.f };

        /// 한 줄 높이(`ImGui::GetFrameHeight()`). 여기에 여백이 더해진다.
        float row_height{ 0.f };

        /// 논리 픽셀 1 이 실제 몇 픽셀인가(`ThemePixels(1.f)`).
        float scale{ 1.f };

        /// 오른쪽에 놓을 버튼 수.
        int button_count{ 0 };
    };

    /// 머리줄 칸을 정한다. 순수 함수다.
    inspector_panel_metrics measure_inspector_panel(
        const inspector_panel_inputs& inputs) noexcept;

    /// 접힘·비활성이 머리줄에 먹이는 불투명도.
    ///
    /// s&box `InspectorHeader.OnPaint` 의 규칙 그대로다 — 접혔으면 0.8, 대상이
    /// 꺼져 있으면 0.7, 둘 다면 낮은 쪽. 펼쳐진 활성 패널만 1.0 이다.
    /// 목록을 훑을 때 "지금 켜져 있고 열려 있는 것" 이 먼저 눈에 든다.
    float inspector_panel_opacity(bool expanded, bool disabled) noexcept;

    /// 패널 하나에 넘기는 것.
    struct inspector_panel_request
    {
        /// 표시 이름이자 ImGui ID 의 씨앗. 비울 수 없다.
        const char* label{ nullptr };

        /// 아이콘 글리프. 비면 칸만 비워 둔다.
        const char* icon{ nullptr };

        /// 오른쪽 더보기 버튼의 글자. 비면 버튼을 그리지 않는다.
        const char* menu_icon{ nullptr };

        /// 활성 체크박스가 읽고 쓰는 값. 비면 체크박스를 그리지 않는다 —
        /// 그래도 칸은 비워 두므로 이름은 다른 패널과 같은 x 에 선다.
        bool* enabled{ nullptr };

        /// 접을 수 있는가. 거짓이면 쉐브론을 그리지 않고 언제나 펼침이다.
        bool collapsible{ true };
    };

    /// 패널 하나가 돌려주는 것.
    struct inspector_panel_result
    {
        /// 펼쳐져 있는가. 본문을 그릴지 이 값으로 정한다.
        bool open{ false };

        /// 이번 프레임에 더보기 버튼이 눌렸는가.
        bool menu_clicked{ false };

        /// 이번 프레임에 체크박스 값이 바뀌었는가. `enabled` 가 비면 항상 거짓.
        bool enabled_changed{ false };
    };

    /// 패널을 연다. 돌려받은 `open` 이 참일 때만 본문을 그린다.
    ///
    /// **`end_inspector_panel` 은 `open` 과 무관하게 언제나 부른다.** 여는 쪽이
    /// ID 스택과 들여쓰기를 밀어 두기 때문이다 — 접힌 패널에서 건너뛰면 그
    /// 뒤의 모든 줄이 한 칸씩 밀린 채 프레임이 끝난다.
    inspector_panel_result begin_inspector_panel(const inspector_panel_request& request);

    /// 패널을 닫는다.
    void end_inspector_panel();
}
