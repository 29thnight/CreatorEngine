#pragma once
// X/Y/Z 축 필드 (PHASE 21 W2 · 계획서 §7.1 의 `EditorAxisField3`).
//
// ── 신설이다. 승격이 아니다 ───────────────────────────────────────────────
//
// 계획서는 `ImGuiDrawHelperRectTransformComponent.cpp` 의 축 필드를 "정본으로
// 끌어올린다" 고 적었다. 실측이 그것을 뒤집었다(결정표 §2.4) — 그 파일의 축
// 필드는 **vec2** 이고, X/Y/Z 색 badge 는 저장소에 한 자리도 없었다. vec3 를
// 그리는 네 자리는 전부 맨 `DragFloat3` 이다. 끌어올릴 구현이 없으므로 신설이며,
// 승계한 것이 있다면 축 필드가 아니라 `EditorPropertyRow` 의 행 규약이다.
//
// ── 라벨은 ImGui 규약을 그대로 쓴다 ───────────────────────────────────────
//
// `label` 은 `DragFloat3` 에 넘기던 것과 같은 뜻이다 — `##` 앞이 오른쪽에
// 표시되고 전체가 ID 다. 라벨을 왼쪽으로 옮기는 규약을 새로 세우지 않은 이유가
// 있다. 가장 넓은 소비자가 `ReflectionTypedDraw.h` 이고 그곳의 다른 열두 타입이
// 모두 ImGui 기본(라벨 오른쪽)이다. vector3 만 왼쪽이 되면 리플렉션 인스펙터
// 안에서 그 줄만 어긋난다. 라벨을 앞에 두고 싶은 호출자는 지금처럼
// `Text` + `SameLine` 뒤에 `##` 이름을 넘기면 된다.
//
// ── 축 색은 의미 색이 아니다 ──────────────────────────────────────────────
//
// 팔레트의 `Error`(빨강)·`Positive`(초록)·`Primary`(파랑)를 그대로 쓰면 세 축이
// 곧 오류·성공·강조가 된다. 그러면 나중에 이 줄에 error 상태를 넣는 순간 X 축과
// 색이 같아져 상태 표시가 사라진다 — `EditorSectionHeader` 가 이미 같은 충돌로
// hover 를 잃었던 자리다(테마가 `HeaderHovered` 와 `HeaderActive` 를 같은 값으로
// 두었다). 그래서 축 색을 따로 두고, 검사가 **의미 색 셋과 다름**까지 단정한다.

#include <cstdint>

namespace editor::widgets
{
    /// 세 축. 순서가 곧 `values` 의 순서다.
    enum class axis
    {
        X,
        Y,
        Z,
        Count
    };

    /// 축 badge 의 배경색. `0xRRGGBB`. 구현이 고르는 값의 정본이므로 검사가
    /// 이것을 읽어 셋의 상이함과 의미 색과의 구별, 글자 대비를 단정한다.
    std::uint32_t axis_badge_hex(axis which) noexcept;

    /// badge 위 글자색. `0xRRGGBB`. 대비 단정의 다른 한쪽이다.
    std::uint32_t axis_badge_text_hex() noexcept;

    /// badge 에 찍히는 한 글자. 색만으로 축을 알리면 색을 구별하지 못하는
    /// 눈에는 세 칸이 같은 칸이 된다.
    const char* axis_badge_label(axis which) noexcept;

    /// 축 필드 한 줄에 넘기는 것.
    struct axis_field3_request
    {
        /// ImGui 라벨 규약 그대로. 비울 수 없다.
        const char* label{ nullptr };

        /// 세 축이 읽고 쓰는 연속된 float 셋.
        float* values{ nullptr };

        /// 드래그 감도.
        float speed{ 1.f };

        /// 값 범위. `min == max` 면 제한을 끈다.
        float min{ 0.f };
        float max{ 0.f };

        /// 숫자 표기.
        const char* format{ "%.3f" };

        /// 축 셋을 세로로 놓는다 (W2-I2). 가용 폭을 셋으로 나눈 슬롯이
        /// badge 와 숫자의 최소 폭을 못 담을 때 호출자가 켠다. 판정은
        /// `EditorPropertyRow` 의 `measure_property_layout` 이 한다 — 이 위젯이
        /// 스스로 정하면 같은 인스펙터 안에서 줄마다 다른 답이 나온다.
        bool stacked{ false };
    };

    /// 세 축을 한 줄에 그린다. 이번 프레임에 값이 바뀌면 참.
    bool draw_axis_field3(const axis_field3_request& request);
}
