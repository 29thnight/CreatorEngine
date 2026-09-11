#pragma once
// 메뉴 그리기 배선 (PHASE 21 M1 · 계획서 부록 A.6).
//
// ImGui 를 아는 곳은 `EditorMenuDraw.cpp` **하나**다. 선언 계층(Surface·Schema·
// Registry)은 std 만 본다 — M4 가 창 쪽에서 쓴 구성(`EditorWindowHost.cpp` 하나만
// ImGui 를 본다)과 같다. 덕분에 선언 어휘를 고치는 작업이 ImGui 판 올림에 얽히지
// 않고, 반대로 그리기를 고치는 작업이 `consteval` 표기를 건드리지 않는다.
//
// ── 문맥 타입이 .cpp 경계에서 사라지지 않는 방법 ──────────────────────────
//
// 그리기는 호스트마다 문맥 타입이 다른데(`popup_context_t<Host>`), ImGui 를 아는
// 쪽은 .cpp 하나다. 보통 여기서 `void*` 나 `std::function` 으로 지우게 되는데
// 그러면 CT6-a 가 걷어낸 이중 타입소거가 새 소비자로 되살아난다.
//
// 그래서 **지우지 않고 자른다.** .cpp 는 그리기에 필요한 납작한 값
// (`menu_item_view`)만 받고, 눌린 항목의 **색인**을 돌려준다. 그 색인으로
// `invoke` 를 부르는 것은 타입을 아는 이 헤더의 템플릿이다. 함수 포인터는
// 선언된 타입 그대로 남고, 경계를 넘는 것은 `string_view` 와 `size_t` 뿐이다.
//
// ── 빈 표면은 그리지 않는다 ───────────────────────────────────────────────
//
// 항목이 0 인 상단 뿌리는 `BeginMenu` 자체를 부르지 않고, 항목이 0 인 팝업
// 호스트는 구분선조차 더하지 않는다(A.6). 그래서 배선이 착지해도 항목을 태우기
// 전까지 **셸 픽셀이 움직이지 않는다** — W0 의 시각 골든과 순서 제약이 없다.

#include "EditorMenuRegistry.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace editor
{
    namespace detail
    {
        /// 그리기에 필요한 것만 담은 납작한 뷰. 문맥 타입이 여기 없다.
        struct menu_item_view
        {
            std::string_view sub_path{};
            std::string_view shortcut_hint{};
            std::string_view confirm_text{};
            int              order{ 0 };
            bool             enabled{ true };
        };

        inline constexpr std::size_t no_menu_item = static_cast<std::size_t>(-1);

        /// 항목을 하위 경로대로 중첩해 그리고, 눌린 항목의 색인을 돌려준다.
        /// 아무것도 눌리지 않으면 `no_menu_item`.
        ///
        /// 돌려주는 색인은 **넘긴 `items` 의 색인**이다. 정렬은 안에서 하지만
        /// 색인은 그대로 유지한다 — 호출자가 자기 배열로 되짚어야 하므로.
        std::size_t draw_menu_items(std::span<const menu_item_view> items);

        /// 납작한 뷰를 만든다. 선택 대상 항목은 선택이 없으면 비활성으로 둔다.
        template<class Context>
        menu_item_view to_view(const menu_entry<Context>& entry, const Context* context)
        {
            return menu_item_view{
                entry.sub_path, entry.shortcut_hint, entry.confirm_text, entry.order,
                (nullptr != context) &&
                    ((nullptr == entry.enabled) || entry.enabled(*context)) };
        }
    }

    /// 상단 뿌리의 선언 항목만 그린다. **이미 열린 `BeginMenu` 안에서** 부른다 —
    /// File·Edit·Settings·Window·Help 는 인라인 메뉴가 이미 있으므로 그 `EndMenu`
    /// 직전에 덧붙인다(A.6). 같은 이름으로 두 번째 메뉴를 열면 안 된다.
    ///
    /// `selection` 이 널이면 선택 대상 항목은 비활성으로 나온다 — 서명이 결속을
    /// 선언한다는 계약(A.4)의 런타임 절반이 여기다. 술어(`enabled`)는 선택이
    /// 있을 때만 불린다(없는 대상에 술어를 물으면 그 자체가 결함이다).
    ///
    /// 반환값은 "항목이 있어서 그렸는가"다. 인라인 항목과 섞이는 자리가 구분선을
    /// 넣을지 판단하는 데 쓴다.
    bool draw_top_menu_items(top_menu_root root, const entity_target* selection);

    /// 인라인 항목 **뒤에** 선언 항목을 덧붙인다. 항목이 0 이면 구분선조차 넣지
    /// 않는다(A.6) — 그래서 배선이 착지해도 항목을 태우기 전까지 셸 픽셀이 움직이지
    /// 않고, W0 의 시각 골든과 순서 제약이 생기지 않는다.
    ///
    /// ★ 이 규칙이 호출 자리마다 복제되지 않도록 여기 둔다. 그리기 규칙이므로
    ///   그리기 계층의 몫이고, 덧붙여서 게이트의 표면 대조가 **이 함수 이름**으로
    ///   그리는 자리를 찾을 수 있다(호출 자리마다 다른 지역 도우미를 쓰면 게이트가
    ///   그 이름들을 손으로 알고 있어야 한다).
    bool append_top_menu_items(top_menu_root root, const entity_target* selection);

    /// 상단 뿌리 하나를 `BeginMenu`/`EndMenu` 까지 포함해 그린다. 인라인 메뉴가
    /// 없는 뿌리(Tools)가 쓴다. 항목이 0 이면 `BeginMenu` 자체를 부르지 않는다.
    void draw_top_menu_root(top_menu_root root, const entity_target* selection);

    /// 상단 뿌리에 선언 항목이 있는가. 기존 인라인 항목과 섞어 그리는 자리가
    /// 구분선을 넣을지 판단하는 데 쓴다.
    bool top_menu_root_has_items(top_menu_root root);

    /// 팝업 호스트에 선언 항목이 있는가. 항목 0 이면 호출자가 아무것도 하지 않는다.
    bool popup_host_has_items(popup_host host);

    /// 팝업 호스트의 선언 항목을 그린다. `BeginPopup`/`EndPopup` **안에서** 부른다.
    ///
    /// 반환값은 "이 프레임에 무언가를 그렸는가"다. 기존 인라인 항목과 섞이는
    /// 자리가 구분선을 넣을지 판단하는 데 쓴다.
    template<popup_host Host>
    bool draw_popup_menu_items(const popup_context_t<Host>& context)
    {
        using context_type = popup_context_t<Host>;

        const std::vector<menu_entry<context_type>>& entries = popup_menu_entries<Host>();
        if (entries.empty()) return false;

        std::vector<detail::menu_item_view> views;
        views.reserve(entries.size());
        for (const menu_entry<context_type>& entry : entries)
        {
            views.push_back(detail::to_view(entry, &context));
        }

        const std::size_t pressed = detail::draw_menu_items(views);
        if (detail::no_menu_item != pressed && nullptr != entries[pressed].invoke)
        {
            // 문맥을 복사해 둔 뒤 부른다. 동작이 표를 건드릴 수 있고(선언자 재등록),
            // 그러면 `entries` 참조가 무효가 된다.
            const context_type copy = context;
            entries[pressed].invoke(copy);
        }
        return true;
    }
}
