#pragma once
// 인스펙터 컴포넌트 머리줄 (PHASE 21 W2 · 계획서 §7.1 의 `EditorSectionHeader`).
//
// ── 승계다. 신규 작성이 아니다 ────────────────────────────────────────────
//
// `ImGuiHelper/CustomCollapsingHeader.h` 의 `DrawCollapsingHeaderWithButton`
// 두 오버로드가 원본이고, 승계 결정표(`docs/analysis/EditorWidgetInheritanceW2.md`
// §2.1)가 "기존 구현을 토큰화해 개명" 으로 판정한 것이다. 그리는 순서와 배치는
// 원본 그대로다.
//
// ── 승계하며 고친 것 셋 ───────────────────────────────────────────────────
//
// ① **hover 가 죽어 있었다.** 원본은 `bool hovered = false;` 를 선언한 **직후**
//    그것으로 배경색을 고르고, 진짜 hover 값은 그 **뒤**에 대입했다. 그래서
//    `ImGuiCol_HeaderHovered` 가 한 번도 그려지지 않았다. 여기서는 자리를
//    먼저 잡아(`ItemAdd`) 상태를 읽고 그 뒤에 배경을 그린다.
// ② **`held` 는 참이 될 수 없었다.** 맨 `ItemAdd` 는 ActiveId 를 세우지 않으니
//    `IsItemActive()` 는 언제나 거짓이다. 읽히지 않은 것이 아니라 값이 없었다.
//    `ButtonBehavior` 로 바꿔 hover·active·press 를 한 자리에서 받는다.
//    `ImGuiTreeNodeFlags` 도 뺐다 — 세 호출자가 모두 `DefaultOpen` 을 넘기는데
//    본문이 한 번도 보지 않았다. 넘길 수 없으면 오해가 사라진다.
// ③ **치수가 매직 넘버였다.** 버튼 너비 24.0f, 아래 여백 3.0f 가 박혀 있었다.
//    너비는 `GetFrameHeight()`(= `ControlHeight` 의 런타임 실현)로, 여백은
//    `EditorThemeTokens::CompactGap` 으로 바꿨다.
//
// ── 오버로드 둘을 하나로 ──────────────────────────────────────────────────
//
// 둘의 차이는 체크박스 하나뿐이었다. `enabled` 가 비어 있으면 체크박스를 그리지
// 않는 것으로 합쳤다. 결과도 구조체 하나로 돌려준다 — 원본은 "눌렸을 때만 true 를
// 써 넣는" 출력 인자라 호출자가 매 프레임 스스로 초기화해야 했다.
//
// ── 상태 둘만 든다 ────────────────────────────────────────────────────────
//
// hover 와 active 다. disabled·focus·nav·mixed·error 는 이 머리줄에 소비자가
// 없어서 넣지 않았다(결정표 §4 의 실측). 특히 disabled 는 넣었다 뺐다 —
// 세 호출자 누구도 쓰지 않아 한 번도 실행되지 않는 경로가 되고, 돈 적 없는
// 경로를 "된다" 고 적을 수는 없다. 필요해지면 `BeginDisabled` 로 감싸는
// 한 겹이다.

#include <cstdint>

namespace editor::widgets
{
    /// 배경이 가리는 네 상태. 넷이 서로 다른 색이어야 상태가 눈에 갈린다.
    enum class section_header_surface
    {
        Closed,
        Open,
        Hovered,
        Held,
        Count
    };

    /// 그 상태가 쓰는 배경색. `0xRRGGBB`. 구현이 고르는 값의 정본이므로
    /// 검사가 이것을 읽어 넷의 상이함을 단정한다 — 누가 토큰 둘을 같은 값으로
    /// 만들면 머리줄이 조용히 상태를 잃는데, 그것을 게이트가 잡게 하기 위해서다.
    /// 여기서 `ThemeColor` 가 아니라 생짜 hex 를 돌려주는 이유는 `EditorTheme.h`
    /// 가 `ImVec4`·`ImGuiStyle` 때문에 imgui 를 들기 때문이다. 이 헤더는 그것을
    /// 소비자에게 옮기지 않는다.
    std::uint32_t section_header_surface_hex(section_header_surface surface) noexcept;

    /// 머리줄 하나에 넘기는 것.
    struct section_header_request
    {
        /// 표시 이름이자 ImGui ID 의 씨앗. 비울 수 없다.
        const char* label{ nullptr };

        /// 오른쪽 끝 버튼의 라벨(보통 `ICON_FA_BARS`). 비면 버튼을 그리지 않는다.
        const char* menu_icon{ nullptr };

        /// 왼쪽 체크박스가 읽고 쓰는 값. 비면 체크박스를 그리지 않는다.
        bool* enabled{ nullptr };
    };

    /// 머리줄 하나가 돌려주는 것. 매 호출 새로 채워진다.
    struct section_header_result
    {
        /// 펼쳐져 있는가. 본문을 그릴지 이 값으로 정한다.
        bool open{ false };

        /// 이번 프레임에 오른쪽 버튼이 눌렸는가.
        bool menu_clicked{ false };

        /// 이번 프레임에 체크박스 값이 바뀌었는가. `enabled` 가 비면 항상 거짓.
        bool enabled_changed{ false };
    };

    /// 머리줄을 그린다. `Begin`/`End` 쌍이 없는 단발 호출이다.
    section_header_result draw_section_header(const section_header_request& request);
}
