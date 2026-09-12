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

    // ── 배치 계약 (PHASE 21 W2-I2) ────────────────────────────────────────
    //
    // 계획서 계약 1 이 "배치와 값 편집을 분리한다. 공통 계층이 가용 content
    // rect·라벨 열·값 영역·보조 버튼 공간과 행 전환을 계산하고 기존 표준 ImGui
    // 위젯이 값을 편집한다" 고 한 자리다. 새 입력 체계도 매크로 선언도 만들지
    // 않는다 — 아래는 치수 계산과 커서 배치뿐이고, 값은 여전히 `DragFloat` 가
    // 편집한다.
    //
    // 계산을 **순수 함수**로 뗀 이유가 있다. 폭 전환에는 완충과 편집 중 보류가
    // 들어가는데, 그것이 ImGui 안에 숨어 있으면 경계 왕복을 검사할 길이 화면
    // 캡처뿐이다. 입력을 전부 인자로 받으면 검사가 합성 폭으로 직접 몰 수 있다.

    /// 배치 모드. 값 열이 최소 폭을 못 얻으면 라벨 아래로 내려간다.
    enum class property_layout_mode
    {
        inline_row, ///< 라벨 왼쪽, 값 오른쪽
        stacked,    ///< 라벨 한 줄, 값 다음 줄
    };

    /// 프레임을 건너 사는 것. 호출자가 소유한다 — 숨은 전역을 두면 인스펙터가
    /// 둘 이상 열렸을 때 두 창이 서로의 모드를 흔든다.
    struct property_layout_state
    {
        property_layout_mode mode{ property_layout_mode::inline_row };
        bool axis_stacked{ false };
    };

    /// 배치를 정하는 모든 치수. ImGui 도 테마도 읽지 않으므로 검사가 이 구조체를
    /// 손으로 채워 `measure_property_layout` 을 직접 부를 수 있다.
    struct property_layout_inputs
    {
        /// `ImGui::GetContentRegionAvail().x`. 창 전체 폭이 아니다(§4.1).
        float available{ 0.f };

        /// 라벨 열의 상한. 남는 폭은 값 열이 가져간다.
        float label_max{ 0.f };

        /// 상한에 닿기 전까지 라벨 열이 가져가는 가용 폭의 비율.
        float label_ratio{ 0.40f };

        /// 값 칸 하나의 최소 판독 폭. **고정 대표 문자열**로 잰 값이다 —
        /// 현재 숫자로 재면 값이 바뀔 때마다 열이 흔들린다.
        float value_min{ 0.f };

        /// 축 badge 하나의 폭. 축 줄의 최소 폭 판정에 쓴다.
        float badge_width{ 0.f };

        /// 축 칸 하나의 최소 판독 폭. `value_min` 과 **다른 값**이다.
        ///
        /// 한 줄에 값이 하나면 `-0000.000` 이 다 보여야 하지만, 축은 셋이
        /// 한 줄을 나눠 쓰므로 같은 잣대를 대면 셋이 절대 한 줄에 서지 못한다.
        /// 실제로 처음 판을 그렇게 짰다가 넉넉한 폭에서도 X·Y·Z 가 세로로
        /// 떨어졌다. 축은 `%.3f` 한 칸이 보이는 폭을 기준으로 잰다.
        float axis_value_min{ 0.f };

        /// 축 사이 간격. `gap` 과 다르다 — 축 위젯은 `ItemInnerSpacing` 으로
        /// 붙이므로 여기서 다른 값을 쓰면 판정과 실제 배치가 어긋난다.
        float axis_gap{ 0.f };

        /// 이 구간이 그릴 **고정 라벨** 중 가장 넓은 폭. 0 이면 비율·상한만 쓴다.
        ///
        /// 없으면 라벨 열이 언제나 가용 폭의 `label_ratio` 를 차지한다. 짧은
        /// 이름만 있는 구간에서는 그 폭이 통째로 버려지고, 그만큼 값 열이
        /// 줄어 축이 불필요하게 세로로 떨어진다.
        ///
        /// **컴파일 시 정해진 문자열로만 잰다.** 매 프레임 바뀌는 값으로 재면
        /// 계획서가 금지한 "라벨 최대값 변화로 열이 흔들리는" 상태가 된다.
        float label_hint{ 0.f };

        /// 열 사이 간격.
        float gap{ 0.f };

        /// 보조 버튼이 미리 잡아 두는 폭. 없으면 0.
        float aux_reserve{ 0.f };

        /// 완충 폭. 전환한 모드에서 되돌아오려면 임계값보다 이만큼 더 넓어야
        /// 한다. 0 이면 경계에서 한 픽셀 왕복이 모드를 진동시킨다.
        float hysteresis{ 0.f };

        /// 편집 중인가(`ImGui::IsAnyItemActive`). 참이면 직전 모드를 붙든다 —
        /// 드래그 중에 값 칸이 다른 줄로 옮겨 가면 드래그가 끊긴다.
        bool editing{ false };
    };

    /// 계산 결과.
    struct property_layout_metrics
    {
        property_layout_mode mode{ property_layout_mode::inline_row };

        /// 축 셋을 세로로 놓아야 하는가. 축 슬롯이 `badge_width + value_min`
        /// 미만이면 참이다.
        bool axis_stacked{ false };

        /// 라벨 열 폭. `stacked` 에서는 가용 폭 전부다.
        float label_col{ 0.f };

        /// 값 영역 폭.
        float value_col{ 0.f };

        float gap{ 0.f };
        float aux_reserve{ 0.f };
    };

    /// 라벨 열 상한의 논리 픽셀 값. 검사가 읽는 정본이다.
    float property_layout_label_max_logical() noexcept;

    /// 라벨 열 비율의 정본.
    float property_layout_label_ratio() noexcept;

    /// 값 최소 폭을 재는 대표 문자열. 현재 값이 아니라 이것으로 잰다.
    const char* property_layout_value_sample() noexcept;

    /// 축 칸 하나의 최소 폭을 재는 대표 문자열.
    const char* property_layout_axis_sample() noexcept;

    /// 고정 라벨 집합에서 가장 넓은 폭을 잰다. `label_hint` 에 넣을 값이다.
    /// 넘기는 문자열은 컴파일 시 정해진 것이어야 한다.
    float property_layout_label_hint(const char* const* labels, int count);

    /// 지금 프레임의 ImGui·테마 상태에서 입력을 채운다. 그리는 자리에서 쓴다.
    /// `label_hint` 는 0 이면 비율·상한만 쓴다.
    property_layout_inputs property_layout_inputs_now(int aux_button_count,
        float label_hint = 0.f);

    /// 배치를 정한다. `state` 는 읽고 갱신한다(완충·보류가 직전 모드를 본다).
    /// ImGui 를 부르지 않으므로 검사가 그대로 부를 수 있다.
    property_layout_metrics measure_property_layout(const property_layout_inputs& inputs,
        property_layout_state& state) noexcept;

    /// 한 줄의 라벨을 놓고 값 커서를 세운다. 값 영역 폭을 돌려주므로 호출자는
    /// `ImGui::SetNextItemWidth` 에 그대로 넘기면 된다.
    ///
    /// 라벨이 열보다 길면 잘라 그리고 그 자리에 tooltip 으로 전체 이름을 준다.
    /// 공백 문자열로 자리를 맞추지 않는다 — 그 방식은 폰트가 바뀌면 어긋난다.
    float begin_property_line(const char* label, const property_layout_metrics& metrics);
}
