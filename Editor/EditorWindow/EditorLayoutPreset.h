#pragma once
// 배치 preset 어휘 (PHASE 21 W6 · 계획서 §W6) — 자리의 정본을 선언에서 preset 으로 옮긴다.
//
// W3 이 남겨 둔 한 줄이 이것이다: *"남은 것은 자리 **이름**을 `dock_slot` 열거자가
// 아니라 workspace 선언에서 받는 일이고 그것이 W3·W6이다"*
// (`Windows/EditorStandardWindows.h`). 창 선언의 `.dock(...)` 은 이제 **기본 preset
// 에서의 자리**이고, 다른 preset 은 그 위에 재정의를 얹는다.
//
// ── 기본 preset 이 재정의 0 인 이유 ───────────────────────────────────────
//
// 계획서가 "**현재 외관을 유지하는** 5종 배치 preset" 이라고 적었다. 기본 preset
// 을 값으로 베껴 적으면 선언과 두 벌이 되고, 언젠가 한쪽만 고쳐져 "현재 외관" 이
// 조용히 갈린다. 그래서 `sbox_compact` 는 **재정의를 하나도 갖지 않는다** — 오늘의
// 모습과 같다는 것이 값의 일치가 아니라 **출처의 동일성**으로 선다.
//
// ── 자리를 안 쓰는 preset 은 그 노드를 만들지 않는다 ──────────────────────
//
// `legacy_unity` 는 오른쪽 열을 위아래로 가르지 않는다(Inspector 가 전체 높이를
// 쓴다). 그런 preset 에서 `right_lower` 를 그래도 갈라 두면 **아무도 안 들어오는
// 빈 노드**가 남는다 — 계획서 W6 의 판정이 바로 "orphan dock node 가 없다" 이므로,
// 빌더는 그 preset 에서 실제로 쓰이는 자리만 가른다.
//
// ── 최소 중앙 영역 ────────────────────────────────────────────────────────
//
// 비율만 쓰면 창이 작아질수록 가운데가 먼저 사라진다(계획서: *"small window와 DPI
// 변화에서 minimum central area를 보존한다"*). preset 은 비율을 말하고, 빌더가
// 그 비율을 픽셀로 푼 뒤 가운데가 최소치보다 작아지면 **옆과 아래를 줄인다.**

#include "EditorWindowRegistry.h"
#include "EditorWindowSurface.h"

#include <span>
#include <string_view>

namespace editor
{
    /// 배치가 지켜야 할 하한. **논리 픽셀**이라 창 배율이 곱해진다.
    ///
    /// 여기 있는 이유는 게이트 때문이다. 이 값이 빌더의 `.cpp` 안에만 있으면
    /// 게이트가 같은 수를 제 파일에 베껴 적게 되고, 그것이 바로 이 조각이
    /// 없애려던 **두 벌**이다. 제품은 배율을 곱한 실효값을 스냅샷으로 내보내고
    /// 게이트는 그것을 읽는다 — 어느 쪽도 상수를 다시 적지 않는다.
    struct layout_minimums
    {
        /// 뷰포트로 보이는 최소. 이보다 작으면 씬 툴바가 접히고 기즈모가
        /// 화면 밖으로 밀린다(W2-V 의 툴바 폭 모드 하한과 같은 자리다).
        static constexpr float central_width = 480.f;
        static constexpr float central_height = 300.f;

        /// 패널 쪽 바닥. 가운데만 지키면 창이 작아질 때 옆 열이 **폭 0** 으로
        /// 접히는데, 트리는 멀쩡하고 사람에게만 패널이 사라진다.
        static constexpr float side_width = 200.f;
        static constexpr float bottom_height = 120.f;
    };

    /// 재정의가 열림 상태를 말하지 않는 경우가 대부분이라 3상이다.
    enum class preset_visibility : std::uint8_t { inherit = 0, opened, closed };

    /// 자리 나누기. 전부 **가르기 직전 노드**가 아니라 전체 크기에 대한 비율이다 —
    /// 빌더가 픽셀로 풀어 순서에 맞는 비율로 되돌린다. 그래야 최소 중앙 보존이
    /// 한자리에서 계산된다.
    struct layout_split
    {
        float left{ 0.f };          ///< 0 이면 왼쪽 열을 만들지 않는다
        float right{ 0.22f };
        float right_lower{ 0.45f }; ///< 오른쪽 열 **안에서** 아래가 갖는 비율
        float bottom{ 0.28f };
    };

    struct layout_override
    {
        std::string_view  stable_id{};
        dock_slot         dock{ dock_slot::floating };
        preset_visibility open{ preset_visibility::inherit };
    };

    struct layout_preset
    {
        std::string_view id{};      ///< CLI·파일에 적히는 값. 사람이 읽는 이름은 label
        std::string_view label{};
        layout_split     split{};
        std::span<const layout_override> overrides{};
    };

    /// 내장 preset 다섯. 순서가 메뉴 순서다.
    std::span<const layout_preset> layout_presets();
    const layout_preset&  default_layout_preset();
    const layout_preset*  find_layout_preset(std::string_view id);

    /// 이 창이 이 preset 에서 앉을 자리. 재정의가 없으면 **선언값**이다.
    dock_slot slot_of(const window_entry& entry, const layout_preset& preset);

    /// 이 preset 이 이 창의 열림을 지정했는가. `inherit` 면 건드리지 않는다.
    preset_visibility visibility_of(const window_entry& entry, const layout_preset& preset);

    /// 이 표에서 그 자리에 앉는 창이 하나라도 있는가(그 preset 기준).
    bool slot_occupied(const window_table& table, const layout_preset& preset, dock_slot slot);

    /// 어느 preset 도 쓰지 않는 자리는 **죽은 어휘**다. 창 감사가 "빈 도킹 자리" 를
    /// 셀 때 이것을 함께 본다 — 선언이 비어 있어도 preset 이 쓰면 죽은 것이 아니다.
    bool slot_used_by_any_preset(const window_table& table, dock_slot slot);
}
