// 에디터 메뉴 런타임 표 (PHASE 21 M0 · 계획서 부록 A.5).
//
// 이 TU는 유니티 빌드에서 일부러 뺀다(Editor.vcxproj). `Commands/*.cpp`가
// 세운 규약과 같은 이유다 — 유니티에 넣으면 앞 파일이 들여온 헤더에 기대도
// 통과해서, 각 TU가 자기 include를 소유하는지가 평상시 빌드에서 검사되지 않는다.

#include "EditorMenuRegistry.h"

#include <array>
#include <cstddef>
#include <string_view>

namespace editor
{
    namespace
    {
        // ── 이름 추출 카나리아 ────────────────────────────────────────────
        //
        // action_id는 `__FUNCSIG__`의 NTTP 표기에 기댄다(EditorMenuSchema.h).
        // 컴파일러가 표기를 바꾸면 메뉴 id가 조용히 쓰레기가 되는 대신 **여기서
        // 빌드가 멈춘다.** MetaSchema.h가 같은 이유로 같은 장치를 둔다.
        void menu_name_canary_free_function() {}

        struct menu_name_canary_owner
        {
            static void member_action() {}
        };

        static_assert(
            detail::action_name_holder<&menu_name_canary_free_function>::view
                == std::string_view{ "menu_name_canary_free_function" },
            "__FUNCSIG__ 표기가 바뀌었다 — action_name_raw의 자유 함수 경로를 다시 맞춰라");

        static_assert(
            detail::action_name_holder<&menu_name_canary_owner::member_action>::view
                == std::string_view{ "member_action" },
            "__FUNCSIG__ 표기가 바뀌었다 — action_name_raw의 정적 멤버 경로를 다시 맞춰라");

        // ── 상단 저장소 ───────────────────────────────────────────────────

        std::array<top_menu_storage, static_cast<std::size_t>(top_menu_root::count)>& top_storage()
        {
            static std::array<top_menu_storage, static_cast<std::size_t>(top_menu_root::count)> storage;
            return storage;
        }

        // ── 치워 두는 자리 ────────────────────────────────────────────────
        //
        // 자가 검사가 살아 있는 에디터에서 돌기 위한 장치다(헤더의 stash 주석).
        // 제품 표와 **같은 모양**으로 하나 더 두고 통째로 맞바꾼다 — 복사가 아니라
        // swap 이라 항목 수에 무관하게 싸고, 되돌릴 때 원본이 그대로 돌아온다.

        std::array<top_menu_storage, static_cast<std::size_t>(top_menu_root::count)>& top_stash()
        {
            static std::array<top_menu_storage, static_cast<std::size_t>(top_menu_root::count)> storage;
            return storage;
        }

        template<popup_host Host>
        std::vector<menu_entry<popup_context_t<Host>>>& popup_stash()
        {
            static std::vector<menu_entry<popup_context_t<Host>>> storage;
            return storage;
        }

        bool g_stashed = false;

        void swap_registry_with_stash()
        {
            top_storage().swap(top_stash());
            for_each_popup_host(
                []<popup_host Host>()
                {
                    popup_menu_entries<Host>().swap(popup_stash<Host>());
                });
        }
    }

    top_menu_storage& top_menu_entries(top_menu_root root)
    {
        return top_storage()[static_cast<std::size_t>(root)];
    }

    const top_menu_storage& top_menu_entries_of(top_menu_root root)
    {
        return top_storage()[static_cast<std::size_t>(root)];
    }

    menu_registry_stats collect_menu_registry_stats()
    {
        menu_registry_stats stats{};

        for (const top_menu_root root : all_top_menu_roots)
        {
            const top_menu_storage& storage = top_menu_entries_of(root);
            stats.top_menu_items += storage.size();
            if (storage.empty())
            {
                ++stats.empty_top_roots;
            }
        }

        for_each_popup_host(
            [&stats]<popup_host Host>()
            {
                const auto& entries = popup_menu_entries<Host>();
                stats.popup_items += entries.size();
                if (entries.empty())
                {
                    ++stats.empty_popup_hosts;
                }
            });

        return stats;
    }

    bool stash_menu_registry()
    {
        if (g_stashed) return false;

        // 치우는 자리를 먼저 비운다 — 맞바꾼 뒤 제품 표가 **빈 표**가 되어야 한다.
        for (top_menu_storage& one : top_stash())
        {
            one.global_items.clear();
            one.selection_items.clear();
        }
        for_each_popup_host([]<popup_host Host>() { popup_stash<Host>().clear(); });

        swap_registry_with_stash();
        g_stashed = true;
        return true;
    }

    bool unstash_menu_registry()
    {
        if (!g_stashed) return false;

        // 합성 선언이 남아 있을 수 있으니 먼저 비우고 맞바꾼다.
        clear_menu_registry();
        swap_registry_with_stash();
        g_stashed = false;
        return true;
    }

    void clear_menu_registry()
    {
        for (const top_menu_root root : all_top_menu_roots)
        {
            top_menu_storage& storage = top_menu_entries(root);
            storage.global_items.clear();
            storage.selection_items.clear();
        }

        for_each_popup_host(
            []<popup_host Host>()
            {
                popup_menu_entries<Host>().clear();
            });
    }
}
