// 메뉴 그리기 배선 (PHASE 21 M1 · 계획서 부록 A.6).
//
// 이 TU 는 유니티 빌드에서 일부러 뺀다(Editor.vcxproj) — `EditorMenuRegistry.cpp`
// 와 같은 이유다. 유니티에 넣으면 앞 파일이 들여온 헤더에 기대도 통과해서, 각
// TU 가 자기 include 를 소유하는지가 평상시 빌드에서 검사되지 않는다.
//
// ★ 선언 계층에서 ImGui 를 보는 파일은 이것 하나다. M4 가 창 쪽에서 같은 구성을
//   썼다(`EditorWindowHost.cpp` 하나만 ImGui 를 본다).

#include "EditorMenuDraw.h"

#include "ImGui.h"

#include <algorithm>
#include <string>
#include <vector>

namespace editor
{
    namespace
    {
        // 하위 경로를 '/' 로 가른다. 마지막 구간이 라벨이고 앞은 중첩 메뉴다.
        // 빈 경로는 이름 없는 항목이 되어 ImGui 에서 보이지 않으므로, 선언이
        // 빈 경로를 주면 action_id 를 라벨로 쓴다 — 자가 검사가 잡기 전에
        // 화면에서 사라지는 것보다 이름이 이상한 채 보이는 것이 낫다.
        std::vector<std::string> split_path(std::string_view path, std::string_view fallback)
        {
            std::vector<std::string> parts;
            std::size_t begin = 0;
            while (begin <= path.size())
            {
                const std::size_t slash = path.find('/', begin);
                const std::size_t end = (std::string_view::npos == slash) ? path.size() : slash;
                if (end > begin)
                {
                    parts.emplace_back(path.substr(begin, end - begin));
                }
                if (std::string_view::npos == slash) break;
                begin = slash + 1;
            }
            if (parts.empty())
            {
                parts.emplace_back(fallback);
            }
            return parts;
        }

        // 마지막 '/' 앞까지. 같은 하위 메뉴에 드는 항목을 인접하게 만드는 정렬 키다.
        std::string_view parent_of(std::string_view path)
        {
            const std::size_t slash = path.rfind('/');
            return (std::string_view::npos == slash) ? std::string_view{} : path.substr(0, slash);
        }

        std::string_view leaf_of(std::string_view path)
        {
            const std::size_t slash = path.rfind('/');
            return (std::string_view::npos == slash) ? path : path.substr(slash + 1);
        }

        struct open_level
        {
            std::string name;
            bool        opened{ false };
        };
    }

    namespace detail
    {
        std::size_t draw_menu_items(std::span<const menu_item_view> items)
        {
            if (items.empty()) return no_menu_item;

            // 색인을 정렬한다. 뷰 배열은 건드리지 않는다 — 호출자가 자기 배열로
            // 되짚으므로 색인이 정본이다.
            //
            // 키는 (부모 경로, order, 라벨)이다. 부모 경로를 맨 앞에 두는 것이
            // 요점이다: 같은 하위 메뉴의 항목이 **반드시 인접**해져서 중첩이
            // 한 번만 열리고 한 번만 닫힌다. order 를 맨 앞에 두면 같은 하위
            // 메뉴가 두 번 열릴 수 있다. 그래서 order 는 계약대로 "같은 하위
            // 메뉴 안 안정 정렬"이고(A.4), 하위 메뉴끼리의 순서는 경로순이다.
            std::vector<std::size_t> order(items.size());
            for (std::size_t i = 0; i < items.size(); ++i) { order[i] = i; }

            std::stable_sort(order.begin(), order.end(),
                [items](std::size_t a, std::size_t b)
                {
                    const std::string_view parentA = parent_of(items[a].sub_path);
                    const std::string_view parentB = parent_of(items[b].sub_path);
                    if (parentA != parentB) return parentA < parentB;
                    if (items[a].order != items[b].order) return items[a].order < items[b].order;
                    return leaf_of(items[a].sub_path) < leaf_of(items[b].sub_path);
                });

            std::size_t pressed = no_menu_item;
            std::vector<open_level> stack;

            const auto close_down_to = [&stack](std::size_t depth)
            {
                while (stack.size() > depth)
                {
                    if (stack.back().opened) { ImGui::EndMenu(); }
                    stack.pop_back();
                }
            };

            for (const std::size_t index : order)
            {
                const menu_item_view& item = items[index];
                const std::vector<std::string> parts = split_path(item.sub_path, "(unnamed)");
                const std::size_t parentDepth = parts.size() - 1;

                // 이미 열린 것과 겹치는 만큼만 남기고 닫는다.
                std::size_t common = 0;
                while (common < stack.size() && common < parentDepth &&
                       stack[common].name == parts[common])
                {
                    ++common;
                }
                close_down_to(common);

                // 부족한 단계를 연다. 닫힌 단계를 만나면 그 아래는 그리지 않는다
                // (ImGui 규약 — BeginMenu 가 false 면 EndMenu 를 부르지 않는다).
                bool blocked = (!stack.empty() && !stack.back().opened);
                for (std::size_t depth = common; !blocked && depth < parentDepth; ++depth)
                {
                    const bool opened = ImGui::BeginMenu(parts[depth].c_str());
                    stack.push_back(open_level{ parts[depth], opened });
                    blocked = !opened;
                }
                if (blocked) continue;

                const std::string& label = parts.back();
                const std::string shortcut{ item.shortcut_hint };
                const char* shortcutText = item.shortcut_hint.empty() ? nullptr : shortcut.c_str();

                if (item.confirm_text.empty())
                {
                    if (ImGui::MenuItem(label.c_str(), shortcutText, false, item.enabled))
                    {
                        pressed = index;
                    }
                    continue;
                }

                // ★ 확인은 **같은 프레임 안에서** 끝낸다. 하위 메뉴 한 겹을 씌워
                //   두 번 누르게 만드는 방식이다.
                //
                //   모달로 물으면 "눌렸다"와 "실행한다"가 서로 다른 프레임에
                //   놓이고, 그 사이 문맥 값(자산 경로·엔티티 신원)을 들고 있어야
                //   한다. 그 보류 저장소는 호스트마다 타입이 달라서, 한 군데 두려면
                //   타입소거가 필요해진다 — CT6-a 가 걷어낸 그것이다. 같은 프레임에
                //   끝내면 보류 상태가 아예 없다.
                if (ImGui::BeginMenu(label.c_str(), item.enabled))
                {
                    const std::string confirm{ item.confirm_text };
                    ImGui::TextUnformatted(confirm.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem(label.c_str(), shortcutText))
                    {
                        pressed = index;
                    }
                    ImGui::EndMenu();
                }
            }

            close_down_to(0);
            return pressed;
        }
    }

    bool top_menu_root_has_items(top_menu_root root)
    {
        return !top_menu_entries_of(root).empty();
    }

    bool popup_host_has_items(popup_host host)
    {
        bool found = false;
        for_each_popup_host(
            [&found, host]<popup_host Host>()
            {
                if (Host == host && !popup_menu_entries<Host>().empty()) { found = true; }
            });
        return found;
    }

    bool draw_top_menu_items(top_menu_root root, const entity_target* selection)
    {
        const top_menu_storage& storage = top_menu_entries_of(root);
        if (storage.empty()) return false;

        // 두 목록을 하나로 합쳐 그린다. 합친 색인의 앞쪽이 전역, 뒤쪽이 선택
        // 대상이다 — 저장소가 둘인 것은 타입 때문이고(Registry.h 머리),
        // 사용자에게는 한 메뉴로 보여야 하므로 여기서 접는다.
        const std::size_t globalCount = storage.global_items.size();

        std::vector<detail::menu_item_view> views;
        views.reserve(storage.size());

        const global_target always{};
        for (const menu_entry<global_target>& entry : storage.global_items)
        {
            views.push_back(detail::to_view(entry, &always));
        }
        for (const menu_entry<entity_target>& entry : storage.selection_items)
        {
            views.push_back(detail::to_view(entry, selection));
        }

        const std::size_t pressed = detail::draw_menu_items(views);
        if (detail::no_menu_item == pressed) return true;

        if (pressed < globalCount)
        {
            const auto& entry = storage.global_items[pressed];
            if (nullptr != entry.invoke) { entry.invoke(global_target{}); }
        }
        else if (nullptr != selection)
        {
            const auto& entry = storage.selection_items[pressed - globalCount];
            // 문맥을 복사해 둔 뒤 부른다. 동작이 선택을 바꿀 수 있고, 그러면
            // 호출자가 든 `selection` 이 가리키는 값이 그 사이 달라진다.
            const entity_target copy = *selection;
            if (nullptr != entry.invoke) { entry.invoke(copy); }
        }
        return true;
    }

    bool append_top_menu_items(top_menu_root root, const entity_target* selection)
    {
        if (!top_menu_root_has_items(root)) return false;
        ImGui::Separator();
        return draw_top_menu_items(root, selection);
    }

    void draw_top_menu_root(top_menu_root root, const entity_target* selection)
    {
        // 항목이 0 이면 BeginMenu 자체를 부르지 않는다 — 빈 메뉴가 화면에 생기면
        // 배선 착지만으로 셸 픽셀이 움직이고 W0 의 시각 골든이 무효가 된다(A.6).
        if (!top_menu_root_has_items(root)) return;

        if (ImGui::BeginMenu(display_label(root)))
        {
            draw_top_menu_items(root, selection);
            ImGui::EndMenu();
        }
    }
}
