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

#include <cstdint>

namespace editor::widgets
{
    // Display-only normalization. Units, exponents and non-numbers stay intact.
    void compact_property_number(char* text) noexcept;
    bool drag_property_float(const char* label, float* value, float speed = 1.f,
        float min = 0.f, float max = 0.f, const char* format = "%.3f",
        int flags = 0, bool joined_left = false);
    bool drag_property_floats(const char* label, float* values, int count,
        float speed = 1.f, float min = 0.f, float max = 0.f,
        const char* format = "%.3f", int flags = 0);
    bool property_group_header(const char* label);

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

        /// 라벨 열의 **최소** 폭.
        ///
        /// 처음 판은 "가용 폭의 40%, 상한 160" 이었다. 비율이 두 방향으로
        /// 어긋났다 — 짧은 이름만 있는 구간이 그 폭을 통째로 버렸고, 창을
        /// 넓히면 라벨 열이 같이 자라 남는 폭이 값에 가지 않았다. s&box 는
        /// 라벨 열에 비율을 주지 않는다. 최소 폭만 두고(`ControlSheetLabel`
        /// 의 `MinimumWidth = 140f`) 늘어나는 열은 값 쪽 하나다
        /// (`ControlSheetRow.Rebuild` 의 `SetColumnStretch(0, 0, 0, 1)`).
        float label_min{ 0.f };

        /// 값 칸 하나의 최소 판독 폭. **고정 대표 문자열**로 잰 값이다 —
        /// 현재 숫자로 재면 값이 바뀔 때마다 열이 흔들린다.
        float value_min{ 0.f };

        /// 축 badge 하나의 폭. 축 줄의 최소 폭 판정에 쓴다.
        float badge_width{ 0.f };

        /// 축 칸 하나의 최소 판독 폭. `value_min` 과 **다른 값**이다.
        ///
        /// 셋이 한 줄을 나눠 쓰므로 compact 표시의 고정 대표 문자열로 잰다.
        /// 편집 포맷·저장 정밀도와는 별개이며 현재 숫자 값으로 폭을 바꾸지 않는다.
        float axis_value_min{ 0.f };

        /// 축 사이 간격. `gap` 과 다르다 — 축 위젯은 `ItemInnerSpacing` 으로
        /// 붙이므로 여기서 다른 값을 쓰면 판정과 실제 배치가 어긋난다.
        float axis_gap{ 0.f };

        /// 이 구간이 그릴 **고정 라벨** 중 가장 넓은 폭. 라벨 열의 자연 폭이다.
        ///
        /// `label_min` 보다 넓으면 열이 그만큼 넓어지고, 좁으면 최소 폭이
        /// 이긴다 — 상한이 아니라 내용 폭이다. s&box 의 라벨 위젯이
        /// `SizeMode.Flexible` 로 자기 내용만큼 차지하되 최소 폭 아래로는
        /// 안 내려가는 것과 같다.
        ///
        /// **컴파일 시 정해진 문자열로만 잰다.** 매 프레임 바뀌는 값으로 재면
        /// 계획서가 금지한 "라벨 최대값 변화로 열이 흔들리는" 상태가 된다.
        float label_hint{ 0.f };

        /// 열 사이 간격.
        float gap{ 0.f };

        /// 보조 버튼이 미리 잡아 두는 폭. 없으면 0.
        float aux_reserve{ 0.f };

        /// 완충 폭. 전환한 모드에서 되돌아오려면 임계값보다 이만큼 더 넓어야
        /// 한다. 행 전체에 한 번 적용하며 축별로 중복하지 않는다.
        /// 0 이면 경계에서 한 픽셀 왕복이 모드를 진동시킨다.
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

        /// 축 한 칸이 필요로 하는 최소 폭(badge + 숫자). 넓은 줄로 바꿀 때
        /// 축 판정을 다시 하려고 싣는다 — 폭이 늘었는데 축이 세로로 남으면
        /// 넓힌 뜻이 없다.
        float axis_need{ 0.f };

        /// 축 사이 간격. 위와 같은 이유로 싣는다.
        float axis_gap{ 0.f };
    };

    /// 필드 식별자에서 표시 이름을 유도한다 (PHASE 21 W2-I3).
    ///
    /// `m_nearPlane` → `Near Plane`, `m_isPrimary` → `Is Primary`,
    /// `position` → `Position`. 규칙은 셋뿐이다 — `m_` 접두 제거, 소문자 뒤에
    /// 오는 대문자 앞에서 끊기, 첫 글자 대문자.
    ///
    /// **속성이 아니라 규칙으로 유도하는 이유.** 저장소의 필드는 416개이고
    /// `meta::displayName` 선언은 **0건**이었다. 손으로 붙이는 길은 분량도
    /// 분량이지만, 안 붙인 필드가 조용히 원시 이름으로 나오는 것을 막을 수단이
    /// 없다. 규칙을 기본으로 두면 안 붙인 필드가 제대로 나오고,
    /// `meta::displayName` 은 유도가 틀리는 자리의 **예외 표기**가 된다.
    ///
    /// 돌려주는 포인터는 다음 호출까지만 유효하다. 호출 즉시 그리는 자리에서
    /// 쓴다. 고정 버퍼라 프레임당 할당이 없다(§8.2).
    const char* display_label(const char* identifier) noexcept;

    /// 유도 결과를 담는 버퍼의 크기. 이보다 긴 이름은 잘린다.
    int display_label_capacity() noexcept;

    /// 라벨 열 최소 폭의 논리 픽셀 값. 검사가 읽는 정본이다.
    float property_layout_label_min_logical() noexcept;

    /// 값 최소 폭을 재는 대표 문자열. 현재 값이 아니라 이것으로 잰다.
    const char* property_layout_value_sample() noexcept;

    /// 축 칸 하나의 최소 폭을 재는 대표 문자열.
    const char* property_layout_axis_sample() noexcept;

    /// 고정 라벨 집합에서 가장 넓은 폭을 잰다. `label_hint` 에 넣을 값이다.
    /// 넘기는 문자열은 컴파일 시 정해진 것이어야 한다.
    float property_layout_label_hint(const char* const* labels, int count);

    /// 지금 프레임의 ImGui·테마 상태에서 입력을 채운다. 그리는 자리에서 쓴다.
    /// `label_hint` 가 0 이면 라벨 열은 최소 폭 그대로다.
    property_layout_inputs property_layout_inputs_now(int aux_button_count,
        float label_hint = 0.f);

    /// 배치를 정한다. `state` 는 읽고 갱신한다(완충·보류가 직전 모드를 본다).
    /// ImGui 를 부르지 않으므로 검사가 그대로 부를 수 있다.
    property_layout_metrics measure_property_layout(const property_layout_inputs& inputs,
        property_layout_state& state) noexcept;

    /// 지금 그리는 줄의 배치 (PHASE 21 W2-I3).
    ///
    /// 리플렉션 드로어는 타입마다 분기가 갈리고 커스텀 드로어는 아예 다른
    /// 번역 단위에 있다. 그 전부에 배치를 인자로 꿰면 서명 하나 바뀔 때마다
    /// 사슬 전체가 따라 바뀐다. 그리는 쪽이 한 번 세우고 아래쪽이 읽는다.
    ///
    /// ImGui 는 한 스레드에서만 도는 즉시 모드라 "지금 그리는 줄" 은 언제나
    /// 하나다. 그래도 중첩이 있으므로 `push_` 가 직전 값을 돌려주고 호출자가
    /// 되돌린다.
    const property_layout_metrics& current_property_layout() noexcept;

    /// 지금 배치를 바꾸고 직전 값을 돌려준다. 돌려받은 값을 다시 넣어 되돌린다.
    property_layout_metrics push_property_layout(
        const property_layout_metrics& metrics) noexcept;

    /// 한 줄을 **넓은 줄**로 바꾼 사본 (s&box 의 wide mode).
    ///
    /// s&box 의 `ControlSheetRow.Rebuild` 는 wide 일 때 라벨에 `xSpan: 2` 를
    /// 주어 두 열을 덮게 하고 컨트롤을 다음 그리드 행으로 내린다. 여기서는
    /// 이미 `stacked` 가 같은 모양을 그리므로, 모드를 그것으로 바꾸고 라벨·값
    /// 열을 줄 전체 폭으로 넓히면 된다.
    ///
    /// 좁아서 내려가는 `stacked` 와 **결과는 같고 이유가 다르다.** 그쪽은 폭을
    /// 재서 정하므로 창을 넓히면 되돌아오고, 이쪽은 선언이라 폭과 무관하다.
    /// 이미 `stacked` 인 줄은 그대로 돌려준다 — 두 번 넓히면 열이 가용 폭을
    /// 넘는다.
    property_layout_metrics widen_property_line(
        const property_layout_metrics& metrics) noexcept;

    /// 인스펙터 디버그 모드. `meta::debugOnly()` 로 표시한 항목은 이때만 그린다.
    ///
    /// 값을 여기 두는 이유는 읽는 쪽이 리플렉션 드로어이고 켜는 쪽이 인스펙터
    /// 창이라 둘 사이에 공통 자리가 필요해서다. 창이 여럿 열려도 모드는 하나다 —
    /// "지금 디버그를 하고 있다" 는 사람의 상태이지 창의 상태가 아니다.
    bool property_debug_mode() noexcept;
    void set_property_debug_mode(bool enabled) noexcept;

    /// 디버그 모드의 **기본값**. 검사가 읽는 정본이다.
    ///
    /// 살아 있는 값과 따로 두는 이유. 자가 검사는 CLI 명령이라 아무 때나 돌 수
    /// 있고, 사람이 모드를 켜 둔 뒤에도 돈다. 그때 살아 있는 값을 읽어
    /// "기본은 꺼짐" 이라고 재면 사람이 켠 것 때문에 검사가 붉어진다. 반대로
    /// 검사가 먼저 끄고 나서 재면 세터를 재는 것이지 기본값을 재는 것이 아니다.
    bool property_debug_mode_default() noexcept;

    /// 한 줄의 라벨을 놓고 값 커서를 세운다. 값 영역 폭을 돌려주므로 호출자는
    /// `ImGui::SetNextItemWidth` 에 그대로 넘기면 된다.
    ///
    /// 라벨이 열보다 길면 잘라 그리고 그 자리에 tooltip 으로 전체 이름을 준다.
    /// 공백 문자열로 자리를 맞추지 않는다 — 그 방식은 폰트가 바뀌면 어긋난다.
    float begin_property_line(const char* label, const property_layout_metrics& metrics);

    /// `begin_property_line` 을 부른 누계 (PHASE 21 W2-I4). 인스펙터 본문이 공통 배치를
    /// 지났는지를 소스 셈이 아니라 **돈 줄 수**로 읽으려고 둔다. 그리는 스레드만 쓴다.
    std::uint64_t property_line_count() noexcept;
}
