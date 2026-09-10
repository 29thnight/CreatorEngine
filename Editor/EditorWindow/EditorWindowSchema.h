#pragma once
// 에디터 창 선언 어휘 (PHASE 21 M4 · 계획서 부록 B.3) — std만 의존한다.
//
// 부록 A의 `EditorMenuSchema.h`와 같은 층 구성이다: 짧은 별칭이 정본 표기이고,
// 종류를 값으로 넘기는 원형은 `detail::`에 원시 표기로 남는다.
//
// ── 메뉴와 다른 점 하나 ───────────────────────────────────────────────────
//
// 메뉴 항목의 `action_id`는 함수 이름에서 뽑는다 — 라벨이 바뀌어도 id가
// 살아남아야 단축키 바인딩이 그 위에 서기 때문이다. 창은 그 반대다.
// 창의 안정 식별자는 **저자가 고르는 값**이어야 한다. 계획서 §1.4가 정한
// `표시 이름###고정 ID` 규칙의 오른쪽이 바로 이 값이고, 기존 `imgui.ini`의
// 도크 항목을 이 id로 이주시켜야 하므로 함수 이름에서 유도할 수 없다.
// 그래서 여기에는 `__FUNCSIG__` 장치가 없다.
//
// ── 본문의 모양 ───────────────────────────────────────────────────────────
//
// 본문은 `void draw()` 하나다. `Begin`과 `End`는 셸이 부르므로 본문이 부르면
// 안 된다 — 그 규약은 컴파일로 강제할 수 없어 이관 게이트가 맡는다(M4 4단계).

#include "EditorWindowSurface.h"

#include <string_view>
#include <tuple>
#include <type_traits>

namespace editor
{
    // ── 항목 서술자 ───────────────────────────────────────────────────────
    //
    // 정체성(역할·본문 함수)은 static, 나머지는 값이다. 수정자 연쇄는
    // `field_info::with`와 같은 모양으로 복사본을 돌려준다.
    template<window_role Role, auto Draw>
    struct window_item
    {
        static constexpr window_role role = Role;
        static constexpr auto        draw = Draw;

        std::string_view stable_id{};
        std::string_view label{};

        dock_slot        dock_value{ default_dock_slot(Role) };
        window_trait     trait_value{ default_traits(Role) };
        window_stacking  stacking_value{ window_stacking::normal };
        bool             closable_value{ default_closable(Role) };
        bool             open_by_default_value{ true };
        bool             persist_open_value{ default_persist_open(Role) };
        float            min_width_value{ 0.f };
        float            min_height_value{ 0.f };
        int              order_value{ 0 };

        // 첫 크기. `Begin` 앞에 한 번 부르는 호출이라 최소 크기 제약과 다르다 —
        // 이쪽은 사용자가 옮기면 그만이고, 저쪽은 계속 강제된다.
        size_policy      size_policy_value{ size_policy::none };
        float            initial_width_value{ 0.f };
        float            initial_height_value{ 0.f };

        // 프레임 스타일 재정의. `Begin` **앞에** 눌러야 하는 둘만 받는다 —
        // Scene이 창 배경과 창 여백을 그렇게 쓰고 있다(SceneViewWindow.cpp:206).
        // 항목 간격이나 버튼 색처럼 본문에 걸리는 것은 본문에 남긴다.
        bool  has_background{ false };
        float background_rgba[4]{};
        bool  has_padding{ false };
        float padding_xy[2]{};

        // 이 창이 존재할 조건. nullptr면 항상 존재한다. 애니메이터 창 셋이
        // 이것을 요구한다 — 선택이 풀리면 창 자체가 없어야 한다.
        bool (*available_fn)(){ nullptr };

        // 닫기 단추가 뜰 조건. nullptr면 위의 고정값을 쓴다. 자산 브라우저가
        // 이것을 요구한다 — 타일 스타일에서는 하단 서랍이라 닫히고, 트리
        // 스타일에서는 도킹된 패널이라 닫히지 않는다(ContentsBrowserWindow.cpp:110).
        bool (*closable_fn)(){ nullptr };

        consteval window_item label_text(std::string_view v) const
        {
            window_item copy = *this; copy.label = v; return copy;
        }
        consteval window_item dock(dock_slot v) const
        {
            static_assert(window_role::transient != Role,
                "transient 창은 도킹하지 않는다 — 배치를 남기지 않는 것이 그 역할이다");
            window_item copy = *this; copy.dock_value = v; return copy;
        }
        consteval window_item traits(window_trait v) const
        {
            window_item copy = *this; copy.trait_value = v; return copy;
        }
        consteval window_item add_traits(window_trait v) const
        {
            window_item copy = *this; copy.trait_value = copy.trait_value | v; return copy;
        }
        consteval window_item stacking(window_stacking v) const
        {
            window_item copy = *this; copy.stacking_value = v; return copy;
        }
        consteval window_item closable(bool v = true) const
        {
            window_item copy = *this; copy.closable_value = v; return copy;
        }
        consteval window_item open_by_default(bool v = true) const
        {
            window_item copy = *this; copy.open_by_default_value = v; return copy;
        }
        consteval window_item persist_open(bool v = true) const
        {
            window_item copy = *this; copy.persist_open_value = v; return copy;
        }
        consteval window_item min_size(float width, float height) const
        {
            window_item copy = *this;
            copy.min_width_value = width; copy.min_height_value = height;
            return copy;
        }
        consteval window_item order(int v) const
        {
            window_item copy = *this; copy.order_value = v; return copy;
        }
        consteval window_item initial_size(float width, float height,
                                           size_policy policy = size_policy::first_use_ever) const
        {
            window_item copy = *this;
            copy.size_policy_value = policy;
            copy.initial_width_value = width;
            copy.initial_height_value = height;
            return copy;
        }
        consteval window_item background(float r, float g, float b, float a) const
        {
            window_item copy = *this;
            copy.has_background = true;
            copy.background_rgba[0] = r; copy.background_rgba[1] = g;
            copy.background_rgba[2] = b; copy.background_rgba[3] = a;
            return copy;
        }
        consteval window_item padding(float x, float y) const
        {
            window_item copy = *this;
            copy.has_padding = true;
            copy.padding_xy[0] = x; copy.padding_xy[1] = y;
            return copy;
        }
        consteval window_item available(bool (*predicate)()) const
        {
            window_item copy = *this; copy.available_fn = predicate; return copy;
        }
        consteval window_item closable_when(bool (*predicate)()) const
        {
            window_item copy = *this;
            copy.closable_value = true;   // 술어가 답하므로 고정값은 상한만 뜻한다
            copy.closable_fn = predicate;
            return copy;
        }
    };

    namespace detail
    {
        template<window_role Role, auto Draw>
        consteval auto make_window_item(std::string_view stable_id, std::string_view label)
        {
            static_assert(std::is_invocable_v<decltype(Draw)>,
                "창 본문은 인자 없는 void draw() 여야 한다 — Begin/End는 셸이 부른다");
            return window_item<Role, Draw>{ stable_id, label };
        }
    }

    // ── 짧은 별칭 — 역할이 이름에 드러난다 ────────────────────────────────
    //
    // 부록 A에서 `in_tools`/`in_hierarchy`가 표면을 이름에 드러낸 것과 같다.
    // 역할을 먼저 고르면 기본값이 따라오고 어긋난 조합이 표기 단계에서 막힌다.

    template<auto Draw>
    consteval auto central(std::string_view stable_id, std::string_view label)
    { return detail::make_window_item<window_role::central, Draw>(stable_id, label); }

    template<auto Draw>
    consteval auto panel(std::string_view stable_id, std::string_view label)
    { return detail::make_window_item<window_role::panel, Draw>(stable_id, label); }

    template<auto Draw>
    consteval auto transient(std::string_view stable_id, std::string_view label)
    { return detail::make_window_item<window_role::transient, Draw>(stable_id, label); }

    // ── 선언 묶음 ─────────────────────────────────────────────────────────

    template<class... Items>
    struct window_declaration
    {
        std::tuple<Items...> items{};
    };

    template<class... Items>
    consteval auto window_set(Items... items)
    {
        return window_declaration<Items...>{ std::tuple<Items...>{ items... } };
    }

    namespace detail
    {
        template<class T>
        concept declares_editor_window = requires { T::for_editor(); };
    }
}
