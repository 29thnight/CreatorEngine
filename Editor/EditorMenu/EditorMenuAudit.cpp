// 메뉴 배선 감사 (PHASE 21 M2 · 계획서 부록 A.5).
//
// 이 TU 도 유니티에서 뺀다(Editor.vcxproj) — 선언 계층의 다른 구현 TU 와 같은 이유.

#include "EditorMenuAudit.h"

#include <algorithm>

namespace editor
{
    namespace
    {
        bool contains(const std::vector<std::string>& seen, const std::string& one)
        {
            return seen.end() != std::find(seen.begin(), seen.end(), one);
        }

        void push_once(std::vector<std::string>& out, std::string one)
        {
            if (!contains(out, one)) { out.push_back(std::move(one)); }
        }

        /// 한 표면의 항목 하나를 감사에 먹인다. 문맥 타입이 달라도 보는 것은 같다.
        template<class Context>
        void audit_one(const menu_entry<Context>& entry, std::string_view surface,
                       std::vector<std::string>& pathsSeen,
                       menu_audit& audit, std::vector<std::string>& declarersSeen)
        {
            const std::string key = std::string(surface) + ":" + std::string(entry.sub_path);

            if (entry.sub_path.empty())
            {
                push_once(audit.unnamed_items,
                          std::string(surface) + ":" + std::string(entry.action_id));
            }
            else if (contains(pathsSeen, key))
            {
                push_once(audit.path_conflicts, key);
            }
            else
            {
                pathsSeen.push_back(key);
            }

            if (!entry.declarer.empty())
            {
                push_once(declarersSeen, std::string(entry.declarer));
            }
        }

        const char* yes_no(bool value) noexcept
        {
            return value ? "yes" : "no";
        }

        template<class Context>
        void dump_one(std::string& out, std::string_view surface, std::string_view where,
                      const menu_entry<Context>& entry)
        {
            out += surface;              out += '\t';
            out += where;                out += '\t';
            out += entry.sub_path;       out += '\t';
            out += entry.action_id;      out += '\t';
            out += entry.shortcut_hint;  out += '\t';
            out += std::to_string(entry.order);
            out += '\t';
            out += yes_no(nullptr != entry.enabled);
            out += '\t';
            out += entry.confirm_text;   out += '\t';
            out += entry.declarer;       out += '\n';
        }
    }

    menu_audit audit_declared_menus(std::span<const std::string_view> expected_declarers)
    {
        menu_audit audit;
        audit.declarers_expected = expected_declarers.size();

        std::vector<std::string> declarersSeen;

        for (const top_menu_root root : all_top_menu_roots)
        {
            const top_menu_storage& storage = top_menu_entries_of(root);
            audit.top_items += storage.size();

            // 경로 충돌은 **표면 안에서만** 본다. File 과 Edit 에 같은 경로가 있는
            // 것은 충돌이 아니다 — 서로 다른 메뉴여서 다툴 자리가 없다.
            //
            // 한 뿌리의 전역·선택 두 목록은 한 메뉴로 합쳐 그려지므로(draw 쪽)
            // 같은 바구니에서 본다. 여기서 나누면 화면에서 겹치는 경로를 놓친다.
            std::vector<std::string> pathsSeen;
            for (const menu_entry<global_target>& entry : storage.global_items)
            {
                audit_one(entry, to_string(root), pathsSeen, audit, declarersSeen);
            }
            for (const menu_entry<entity_target>& entry : storage.selection_items)
            {
                audit_one(entry, to_string(root), pathsSeen, audit, declarersSeen);
            }
        }

        for_each_popup_host(
            [&audit, &declarersSeen]<popup_host Host>()
            {
                const auto& entries = popup_menu_entries<Host>();
                audit.popup_items += entries.size();

                std::vector<std::string> pathsSeen;
                for (const auto& entry : entries)
                {
                    audit_one(entry, to_string(Host), pathsSeen, audit, declarersSeen);
                }
            });

        audit.declarers_seen = declarersSeen.size();

        // 목록에 있는데 항목을 하나도 내지 않은 선언자.
        for (const std::string_view expected : expected_declarers)
        {
            if (!contains(declarersSeen, std::string(expected)))
            {
                audit.silent_declarers.emplace_back(expected);
            }
        }

        return audit;
    }

    std::string dump_menu_table()
    {
        std::string out =
            "surface\troot_or_host\tsub_path\taction_id\tshortcut\torder\thas_enabled"
            "\tconfirm\tdeclarer\n";

        for (const top_menu_root root : all_top_menu_roots)
        {
            const top_menu_storage& storage = top_menu_entries_of(root);
            for (const menu_entry<global_target>& entry : storage.global_items)
            {
                dump_one(out, "top", to_string(root), entry);
            }
            for (const menu_entry<entity_target>& entry : storage.selection_items)
            {
                dump_one(out, "top_selection", to_string(root), entry);
            }
        }

        for_each_popup_host(
            [&out]<popup_host Host>()
            {
                for (const auto& entry : popup_menu_entries<Host>())
                {
                    dump_one(out, "popup", to_string(Host), entry);
                }
            });

        return out;
    }

    std::string dump_menu_audit(std::span<const std::string_view> expected_declarers)
    {
        const menu_audit audit = audit_declared_menus(expected_declarers);

        const auto append_list = [](std::string& out, const char* label,
                                    const std::vector<std::string>& names)
        {
            out += label;
            out += '=';
            out += std::to_string(names.size());
            if (!names.empty())
            {
                out += " [";
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    if (0 != i) { out += ", "; }
                    out += names[i];
                }
                out += ']';
            }
            out += '\n';
        };

        std::string out;
        out += "top_items=" + std::to_string(audit.top_items) +
               "  popup_items=" + std::to_string(audit.popup_items) +
               "  declarers=" + std::to_string(audit.declarers_seen) +
               "/" + std::to_string(audit.declarers_expected) + '\n';
        append_list(out, "path_conflicts", audit.path_conflicts);
        append_list(out, "unnamed_items", audit.unnamed_items);
        append_list(out, "silent_declarers", audit.silent_declarers);
        out += audit.clean() ? "audit=clean\n" : "audit=dirty\n";
        return out;
    }
}
