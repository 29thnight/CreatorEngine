#pragma once
// 에디터 메뉴 런타임 표 (PHASE 21 M0 · 계획서 부록 A.5) — 선언을 항목으로 낮춘다.
//
// 그리는 배선(13곳)은 M1이 얹는다. 이 헤더는 **선언 → 표**까지만 책임진다.
//
// ── 왜 자기 등록자가 아니라 중앙 목록인가 (실측) ──────────────────────────
//
//   Editor.vcxproj      ConfigurationType = StaticLibrary (네 구성 전부)
//   CreatorEditor.vcxproj                 = Application
//   /WHOLEARCHIVE                         = Editor·Engine 전 vcxproj에 0건
//
// 정적 라이브러리의 오브젝트 파일은 밖에서 심볼을 참조할 때만 링커가 끌어온다.
// 독립 .cpp에 네임스페이스 스코프 자기 등록자를 두면 **조용히 사라진다** —
// 이 저장소가 흩어진 정적 등록자를 은퇴시키고 명시 등록 진입점으로 간 이유이고,
// 등록 한 줄이 빠지면 명령이 말없이 없어진다는 경고가 이미
// `Commands/CommandRegistrar.h:14-18`에 적혀 있다.
//
// 그래서 리플렉션이 쓴 형태를 그대로 쓴다. RegisterEditorMenuManual.h가 선언자
// 헤더 전부를 include하면서 EDITOR_MENU_LIST로 열거한다 — **include가 인스턴스화를
// 링크되는 TU 안으로 끌어오고, 열거가 등록을 돌린다.** 목록 하나가 두 일을 한다.
//
// ── 상단 메뉴 저장소가 둘인 이유 ──────────────────────────────────────────
//
// 상단 항목은 서명에 따라 전역과 선택 대상 둘로 갈린다(부록 A.4). 선택 대상을
// 전역 하나로 접으려면 사용자 술어를 람다가 붙잡아야 하는데, 항목은 string_view를
// 품어 **구조적 타입이 아니라** NTTP로 넘길 수 없고, 붙잡으려면 std::function이
// 필요해져 CT6-a가 걷어낸 타입소거가 되돌아온다. 그래서 접지 않고 **두 목록으로
// 나눠 든다.** 선택 해석은 그리는 자리가 프레임당 한 번 한다(M1).

#include "EditorMenuSchema.h"
#include "EditorMenuSurface.h"

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
    // 표에 실린 항목 하나. 문맥 타입이 표면으로 고정돼 있어 타입소거가 없다.
    template<class Context>
    struct menu_entry
    {
        std::string_view sub_path{};
        std::string_view action_id{};
        std::string_view shortcut_hint{};
        std::string_view confirm_text{};
        int              order{ 0 };
        void (*invoke)(const Context&){ nullptr };
        bool (*enabled)(const Context&){ nullptr };   // nullptr == 항상 활성
        std::string_view declarer{};                  // 어느 선언자에서 왔는가
    };

    // 상단 뿌리 하나가 드는 것. 두 목록인 까닭은 파일 머리 참조.
    struct top_menu_storage
    {
        std::vector<menu_entry<global_target>> global_items;
        std::vector<menu_entry<entity_target>> selection_items;

        bool empty() const noexcept
        {
            return global_items.empty() && selection_items.empty();
        }

        std::size_t size() const noexcept
        {
            return global_items.size() + selection_items.size();
        }
    };

    top_menu_storage&       top_menu_entries(top_menu_root root);
    const top_menu_storage& top_menu_entries_of(top_menu_root root);

    // 팝업은 호스트마다 문맥 타입이 다르므로 저장소도 호스트마다다. 함수 지역
    // static이라 프로그램 전체에 하나이고, 등록 사슬이 중앙 목록에서 시작하므로
    // 링커가 반드시 끌어온다.
    template<popup_host Host>
    std::vector<menu_entry<popup_context_t<Host>>>& popup_menu_entries()
    {
        static std::vector<menu_entry<popup_context_t<Host>>> storage;
        return storage;
    }

    // 전 호스트 순회 — M2의 덤프와 "배선 안 된 표면 0" 게이트가 열거 원본으로 쓴다.
    // 호출자는 템플릿 operator()를 가진 함수 객체를 넘긴다:
    //   editor::for_each_popup_host([]<editor::popup_host H>{ ... });
    namespace detail
    {
        template<class F, std::size_t... I>
        void visit_popup_hosts(F&& f, std::index_sequence<I...>)
        {
            (f.template operator()<all_popup_hosts[I]>(), ...);
        }
    }

    template<class F>
    void for_each_popup_host(F&& f)
    {
        detail::visit_popup_hosts(std::forward<F>(f),
            std::make_index_sequence<all_popup_hosts.size()>{});
    }

    // ── 선언 → 표 ─────────────────────────────────────────────────────────

    namespace detail
    {
        template<top_menu_root Root, auto Fn, class Context>
        void add_declared_item(const top_menu_item<Root, Fn, Context>& item,
                               std::string_view declarer)
        {
            top_menu_storage& storage = top_menu_entries(Root);

            if constexpr (std::is_same_v<Context, global_target>)
            {
                storage.global_items.push_back(menu_entry<global_target>{
                    item.sub_path, item.action_id, item.shortcut_hint, item.confirm_text,
                    item.order_value,
                    +[](const global_target&) { Fn(); },
                    item.enabled_fn,
                    declarer });
            }
            else
            {
                storage.selection_items.push_back(menu_entry<entity_target>{
                    item.sub_path, item.action_id, item.shortcut_hint, item.confirm_text,
                    item.order_value,
                    +[](const entity_target& target) { Fn(target); },
                    item.enabled_fn,
                    declarer });
            }
        }

        template<popup_host Host, auto Fn>
        void add_declared_item(const popup_menu_item<Host, Fn>& item,
                               std::string_view declarer)
        {
            using context_type = popup_context_t<Host>;

            popup_menu_entries<Host>().push_back(menu_entry<context_type>{
                item.sub_path, item.action_id, item.shortcut_hint, item.confirm_text,
                item.order_value,
                +[](const context_type& context) { Fn(context); },
                item.enabled_fn,
                declarer });
        }
    }

    /// 선언자 하나를 표에 붓는다. **직접 부르지 않는다** — EDITOR_MENU_LIST를
    /// 거치는 것만이 등록 경로이고, 그래야 누락을 기동 게이트가 잡는다.
    template<class Declarer>
    void register_declarer(std::string_view declarer_name)
    {
        static_assert(detail::declares_editor_menu<Declarer>,
            "선언자에 static consteval auto for_editor()가 없다");

        constexpr auto declaration = Declarer::for_editor();
        std::apply(
            [declarer_name](const auto&... items)
            {
                (detail::add_declared_item(items, declarer_name), ...);
            },
            declaration.items);
    }

    // ── 게이트·시험용 ─────────────────────────────────────────────────────

    struct menu_registry_stats
    {
        std::size_t top_menu_items{ 0 };
        std::size_t popup_items{ 0 };
        std::size_t empty_top_roots{ 0 };
        std::size_t empty_popup_hosts{ 0 };
    };

    menu_registry_stats collect_menu_registry_stats();

    /// 표를 비운다. 시험이 등록을 되풀이할 때만 쓴다.
    void clear_menu_registry();
}
