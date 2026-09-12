#pragma once
#include <mathematics/vector3.hpp>
#include "InspectorDrawer.h"
#include "EditorAxisField3.h"

// 엔진이 기본으로 싣는 값 타입 드로어 (PHASE 21 W2-I).
//
// `math::vector3` 는 원래 `ReflectionTypedDraw.h` 의 `if constexpr` 사슬 안에
// 있었다. 이곳으로 옮겼다. 그리는 결과는 같고, 옮긴 이유는 둘이다.
//
//   · 확장점이 실제로 드로어를 하나 이상 나르는지 증명한다. 기제만 두고 소비가
//     0 이면 그것이 도는지 알 수 없다 — 이 저장소가 "생산만 있고 소비 0" 으로
//     여러 번 속은 양식이다.
//   · 포크한 쪽이 베낄 본보기를 엔진 안에 남긴다.
//
// 여기 있는 특수화도 포크가 덮어쓸 수 있다. `InspectorDrawerList.h` 에서 이
// 헤더를 빼고 자기 것을 넣으면 된다.

template<>
struct editor::inspector::InspectorDrawer<math::vector3>
{
    static bool Draw(const char* label, math::vector3& value)
    {
        editor::widgets::axis_field3_request axes{};
        axes.label = label;
        axes.values = &value.x;
        return editor::widgets::draw_axis_field3(axes);
    }
};
static_assert(editor::inspector::HasInspectorDrawer<math::vector3>);
