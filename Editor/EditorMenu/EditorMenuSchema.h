#pragma once
// 에디터 메뉴 선언 어휘 (PHASE 21 M0 · 계획서 부록 A.4) — std만 의존한다.
//
// 표기:
//
//     struct asset_menus
//     {
//         static consteval auto for_editor()
//         {
//             return editor::menu_set(
//                 editor::in_tools<&reimport_all>("Assets/Reimport all")
//                        .shortcut("Ctrl+R"),
//
//                 editor::in_content_asset<&reimport_one>("Reimport")
//                        .enabled(&is_model_asset),
//
//                 editor::in_hierarchy<&make_prefab>("Create/Prefab"));
//         }
//     };
//
// 선언 함수 이름 `for_editor()`는 리플렉션의 `reflect()`(SoundComponent.h:15)에
// 대응한다. 같은 자리, 같은 `static consteval auto`, 다른 계통이다.
//
// ── 서명이 결속을 선언한다 ────────────────────────────────────────────────
//
// 별도 `target(...)` 어휘를 두지 않는다. 대상은 **함수 서명이 말한다**:
//
//     void f()                                  전역. 항상 활성
//     void f(const editor::entity_target&)      선택 대상. 선택이 없으면 자동 비활성
//     void f(const popup_context_t<H>&)         그 팝업 호스트의 문맥
//
// 팝업 항목의 문맥 타입이 호스트와 어긋나면 **컴파일 오류**다. 엉뚱한 대상에
// 동작을 붙이는 실수가 실행 전에 죽는다 — 부록 A.5가 이것을 런타임 게이트가
// 아니라 빌드 게이트로 세는 이유다.
//
// ── 이름 추출의 전제 ──────────────────────────────────────────────────────
//
// action_id는 `__FUNCSIG__`의 NTTP 표기에서 뽑는다(MetaSchema.h와 같은 기법,
// 구현은 계층 경계 때문에 복제한다). 표기가 바뀌면 EditorMenuRegistry.cpp의
// 카나리아 static_assert가 먼저 멈춘다. 경로가 아니라 함수 이름을 id로 쓰는
// 이유는 **메뉴 라벨을 바꿔도 id가 살아남아야** 뒷날 단축키 바인딩이 그 위에
// 설 수 있기 때문이다(부록 A.7 — 단축키 자체는 이번 범위 밖).

#include "EditorMenuSurface.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace editor
{
    namespace detail
    {
        // 자유·정적 함수 포인터 NTTP에서 이름을 뽑는다(VS 18 실측 표기):
        //   "... action_name_raw<void __cdecl ns::reimport_all(void)>(void)"
        // 이름은 인자 여는 괄호 직전의 마지막 "::" 뒤 구간이고, 한정이 없으면
        // 그 앞 공백(호출 규약과의 경계) 뒤 구간이다.
        template<auto Fn>
        consteval std::string_view action_name_raw()
        {
            std::string_view sig = __FUNCSIG__;
            constexpr std::string_view marker = "action_name_raw<";
            const std::size_t begin = sig.find(marker) + marker.size();
            std::string_view inner = sig.substr(begin, sig.rfind(">(") - begin);

            const std::size_t argOpen = inner.find('(');
            const std::size_t colons = inner.rfind("::", argOpen);
            if (colons != std::string_view::npos)
            {
                return inner.substr(colons + 2, argOpen - (colons + 2));
            }
            const std::size_t space = inner.rfind(' ', argOpen);
            return inner.substr(space + 1, argOpen - (space + 1));
        }

        // string_view를 NUL 종단 정적 배열로 물질화한다 — ImGui가 C 문자열을
        // 요구하고, TSV 덤프도 그대로 흘려 쓴다.
        template<auto Fn>
        struct action_name_holder
        {
            static constexpr std::string_view raw = action_name_raw<Fn>();
            static constexpr auto storage = []
            {
                std::array<char, raw.size() + 1> a{};
                for (std::size_t i = 0; i < raw.size(); ++i) { a[i] = raw[i]; }
                return a;
            }();
            static constexpr std::string_view view{ storage.data(), raw.size() };
        };

        // 상단 메뉴 동작의 문맥은 서명이 정한다.
        template<auto Fn>
        using top_context_t = std::conditional_t<
            std::is_invocable_v<decltype(Fn)>, global_target, entity_target>;
    }

    // ── 항목 서술자 ───────────────────────────────────────────────────────
    //
    // 정체성(뿌리·함수·id)은 static, 수정자는 값이다. 수정자 연쇄는
    // `field_info::with`와 같은 모양으로 복사본을 돌려준다.

    template<top_menu_root Root, auto Fn, class Context>
    struct top_menu_item
    {
        static constexpr top_menu_root root = Root;
        static constexpr auto          action = Fn;
        static constexpr std::string_view action_id = detail::action_name_holder<Fn>::view;
        using context_type = Context;

        std::string_view sub_path{};
        std::string_view shortcut_hint{};
        std::string_view confirm_text{};
        int              order_value{ 0 };
        bool (*enabled_fn)(const Context&){ nullptr };

        consteval top_menu_item shortcut(std::string_view v) const
        {
            top_menu_item copy = *this; copy.shortcut_hint = v; return copy;
        }
        consteval top_menu_item confirm(std::string_view v) const
        {
            top_menu_item copy = *this; copy.confirm_text = v; return copy;
        }
        consteval top_menu_item order(int v) const
        {
            top_menu_item copy = *this; copy.order_value = v; return copy;
        }
        consteval top_menu_item enabled(bool (*predicate)(const Context&)) const
        {
            top_menu_item copy = *this; copy.enabled_fn = predicate; return copy;
        }
    };

    template<popup_host Host, auto Fn>
    struct popup_menu_item
    {
        static constexpr popup_host host = Host;
        static constexpr auto       action = Fn;
        static constexpr std::string_view action_id = detail::action_name_holder<Fn>::view;
        using context_type = popup_context_t<Host>;

        std::string_view sub_path{};
        std::string_view shortcut_hint{};
        std::string_view confirm_text{};
        int              order_value{ 0 };
        bool (*enabled_fn)(const context_type&){ nullptr };

        consteval popup_menu_item shortcut(std::string_view v) const
        {
            popup_menu_item copy = *this; copy.shortcut_hint = v; return copy;
        }
        consteval popup_menu_item confirm(std::string_view v) const
        {
            popup_menu_item copy = *this; copy.confirm_text = v; return copy;
        }
        consteval popup_menu_item order(int v) const
        {
            popup_menu_item copy = *this; copy.order_value = v; return copy;
        }
        consteval popup_menu_item enabled(bool (*predicate)(const context_type&)) const
        {
            popup_menu_item copy = *this; copy.enabled_fn = predicate; return copy;
        }
    };

    namespace detail
    {
        template<top_menu_root Root, auto Fn>
        consteval auto make_top_menu_item(std::string_view sub_path)
        {
            static_assert(
                std::is_invocable_v<decltype(Fn)> ||
                std::is_invocable_v<decltype(Fn), const entity_target&>,
                "상단 메뉴 동작은 void f() 또는 void f(const editor::entity_target&) 여야 한다");
            return top_menu_item<Root, Fn, top_context_t<Fn>>{ sub_path };
        }

        template<popup_host Host, auto Fn>
        consteval auto make_popup_menu_item(std::string_view sub_path)
        {
            static_assert(
                std::is_invocable_v<decltype(Fn), const popup_context_t<Host>&>,
                "팝업 동작의 인자 타입이 그 호스트의 문맥과 다르다 "
                "(editor::popup_context_t<Host> 참조)");
            return popup_menu_item<Host, Fn>{ sub_path };
        }
    }

    // ── 짧은 별칭 — 표면이 이름에 드러난다 ────────────────────────────────
    //
    // 이것이 정본 표기다. 뿌리를 값으로 넘기는 원형(detail::make_*)은 그 아래
    // 원시 표기로 남긴다. `meta::field<&Self::x>`가 `field_info` 위의 짧은
    // 표기인 것과 같은 층 구성이다.

    template<auto Fn> consteval auto in_file(std::string_view p)
    { return detail::make_top_menu_item<top_menu_root::file, Fn>(p); }

    template<auto Fn> consteval auto in_edit(std::string_view p)
    { return detail::make_top_menu_item<top_menu_root::edit, Fn>(p); }

    template<auto Fn> consteval auto in_settings(std::string_view p)
    { return detail::make_top_menu_item<top_menu_root::settings, Fn>(p); }

    template<auto Fn> consteval auto in_tools(std::string_view p)
    { return detail::make_top_menu_item<top_menu_root::tools, Fn>(p); }

    template<auto Fn> consteval auto in_window(std::string_view p)
    { return detail::make_top_menu_item<top_menu_root::window, Fn>(p); }

    template<auto Fn> consteval auto in_help(std::string_view p)
    { return detail::make_top_menu_item<top_menu_root::help, Fn>(p); }

    template<auto Fn> consteval auto in_hierarchy(std::string_view p)
    { return detail::make_popup_menu_item<popup_host::hierarchy, Fn>(p); }

    template<auto Fn> consteval auto in_content_folder(std::string_view p)
    { return detail::make_popup_menu_item<popup_host::content_browser_folder, Fn>(p); }

    template<auto Fn> consteval auto in_content_asset(std::string_view p)
    { return detail::make_popup_menu_item<popup_host::content_browser_asset, Fn>(p); }

    template<auto Fn> consteval auto in_inspector_component(std::string_view p)
    { return detail::make_popup_menu_item<popup_host::inspector_component, Fn>(p); }

    template<auto Fn> consteval auto in_scene_view(std::string_view p)
    { return detail::make_popup_menu_item<popup_host::scene_view, Fn>(p); }

    template<auto Fn> consteval auto in_behavior_tree_node(std::string_view p)
    { return detail::make_popup_menu_item<popup_host::behavior_tree_node, Fn>(p); }

    template<auto Fn> consteval auto in_animator_node(std::string_view p)
    { return detail::make_popup_menu_item<popup_host::animator_node, Fn>(p); }

    // ── 선언 묶음 ─────────────────────────────────────────────────────────

    template<class... Items>
    struct menu_declaration
    {
        std::tuple<Items...> items{};
    };

    template<class... Items>
    consteval auto menu_set(Items... items)
    {
        return menu_declaration<Items...>{ std::tuple<Items...>{ items... } };
    }

    namespace detail
    {
        template<class T>
        concept declares_editor_menu = requires { T::for_editor(); };
    }
}
