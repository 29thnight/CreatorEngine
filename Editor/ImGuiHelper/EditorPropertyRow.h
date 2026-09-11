#pragma once
// 표 안의 속성 한 줄 (PHASE 21 W2 · 계획서 §7.1 의 `EditorPropertyRow`).
//
// ── 승계다. 신규 작성이 아니다 ────────────────────────────────────────────
//
// `ImGuiHelper/TableAPIHelper.h` 의 `DrawVec2Row`·`DrawVec2RowAbs` 가 원본이고,
// 승계 결정표(`docs/analysis/EditorWidgetInheritanceW2.md` §2.2)가 "label column
// 규약의 정본이 이미 여기 있다" 로 판정한 것이다. 계획서의 초기 판정은 "부분
// 승계" 였으나 실측이 그보다 강했다 — 0번 열이 라벨
// (`AlignTextToFramePadding` + `PushTextWrapPos`), 그 뒤가 축마다 한 열,
// `SetNextItemWidth(-FLT_MIN)` 로 셀 가용폭 전부. 그 규약을 그대로 든다.
//
// ── 승계하며 바꾼 것 ──────────────────────────────────────────────────────
//
// ① **vec2 전용을 열 개수 인자로.** 원본은 X·Y 두 열이 몸통에 박혀 있었다.
//    `math::vector2` 가 `float x, y;` 연속 멤버라 원본도 사실상 float 배열을
//    쓰고 있었고, 열 수만 인자로 올리면 vec3·vec4 가 같은 규약에 든다.
// ② **오버로드 둘을 하나로.** `DrawVec2Row` 와 `DrawVec2RowAbs` 의 차이는
//    `min`/`max` 를 넘기느냐뿐이었다(Abs 는 `0,0` 을 넘겨 제한을 끈다).
//    기본값을 `0,0` 으로 두면 Abs 가 되므로 함수 하나면 된다.
// ③ **필드 ID 가 안정적이다.** 원본은 `"##x"`·`"##y"` 라 세 번째 열을 더하는
//    순간 이름을 새로 지어야 했다. 열 인덱스로 고정한 표를 쓴다 — 매 프레임
//    문자열을 만들지 않고, 열 수가 늘어도 앞선 열의 ID 가 바뀌지 않는다.
//
// ── 상태는 표준 위젯의 것을 쓴다 ──────────────────────────────────────────
//
// 이 줄은 배경을 그리지 않는다. hover·active 는 `DragFloat` 자신이 들고,
// 계획서가 요구한 mixed·error·focus 는 결정표 §4 의 실측에서 저장소 소비자가
// **0** 이었다. disabled 도 지금 다섯 소비자 중 쓰는 자리가 없어 넣지 않았다 —
// 돈 적 없는 경로를 "된다" 고 적을 수는 없다. 필요해지면 호출자가
// `BeginDisabled` 로 감싸는 한 겹이고, 그때 이 규약 안으로 들인다.

namespace editor::widgets
{
    /// 한 줄이 들 수 있는 값 열의 최대 수. 필드 ID 표의 크기이자 단정 대상이다.
    /// 이보다 많은 열을 요구하면 그리지 않고 거짓을 돌려준다 — 표 밖 ID 를
    /// 지어내면 같은 프레임의 다른 위젯과 ID 가 겹친다.
    int property_row_field_limit() noexcept;

    /// `index` 번째 값 열이 쓰는 ImGui ID 문자열. 구현이 고르는 값의 정본이라
    /// 검사가 이것을 읽어 열 사이의 상이함을 단정한다. 범위 밖이면 `nullptr`.
    const char* property_row_field_id(int index) noexcept;

    /// 속성 한 줄에 넘기는 것.
    struct property_row_request
    {
        /// 0번 열에 놓일 이름이자 ImGui ID 의 씨앗. 비울 수 없다.
        const char* label{ nullptr };

        /// 값 열이 읽고 쓰는 연속된 float. `count` 개를 본다.
        float* values{ nullptr };

        /// 값 열의 수. 1 이상 `property_row_field_limit()` 이하.
        int count{ 0 };

        /// 드래그 감도.
        float speed{ 1.f };

        /// 값 범위. `min == max` 면 제한을 끈다(원본 `DrawVec2RowAbs` 의 규약).
        float min{ 0.f };
        float max{ 0.f };

        /// 숫자 표기.
        const char* format{ "%.3f" };
    };

    /// 표 안에 한 줄을 그린다. `TableNextRow` 부터 시작하므로 호출자는
    /// `BeginTable` 안에 있어야 한다. 이번 프레임에 값이 바뀌면 참.
    bool draw_property_row(const property_row_request& request);
}
