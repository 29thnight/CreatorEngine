// 에디터 메뉴 배선 자가 검사 (PHASE 21 M0 · M1에서 살아 있는 에디터용으로 고침).
//
// 이 TU도 유니티에서 뺀다 — EditorMenuRegistry.cpp와 같은 이유.
//
// ── 제품 표를 옆으로 치우고 돈다 ──────────────────────────────────────────
//
// M0은 "표가 비어 있을 때만" 돌았다. 합성 선언이 "tools 뿌리에 항목 1개"를
// 단정하므로 제품 등록이 끝난 뒤에는 성립하지 않기 때문이다. 그런데 그 말은
// **부팅 전 한순간에만** 돌 수 있다는 뜻이고, 곧 도는 회귀 세트에 넣을 수 없다는
// 뜻이었다 — 이 저장소가 "게이트가 도는 세트에 없으면 없는 것"으로 두 번 데었다.
//
// M0의 착지 기록은 이 조건을 M1에 넘겼고, M1이 EDITOR_MENU_LIST를 채우는 순간
// 전제가 깨질 것이라고 미리 적어 두었다. 그대로 됐다. 그래서 여기서 제품 표를
// 치우고(stash) 합성 선언 위에서 돌고 되돌린다 — 창 쪽 자가 검사가 M4 4단계에서
// 쓴 것과 같은 장치다. **조기 반환 경로에도 되돌리기가 달려 있어야 한다.**

#include "EditorMenuSelfTest.h"

#include "EditorMenuRegistry.h"
#include "EditorMenuSchema.h"
#include "EditorMenuSurface.h"

#include <string>

namespace editor
{
    namespace
    {
        // 검사용 동작 셋. 부른 흔적을 남겨 invoke 포인터가 진짜로 이어졌는지 본다.
        int g_global_calls = 0;
        int g_selection_calls = 0;
        int g_asset_calls = 0;

        std::string g_last_selection_identity;
        std::string g_last_asset_path;

        void selftest_global_action()
        {
            ++g_global_calls;
        }

        void selftest_selection_action(const entity_target& target)
        {
            ++g_selection_calls;
            g_last_selection_identity = target.identity;
        }

        void selftest_asset_action(const asset_target& target)
        {
            ++g_asset_calls;
            g_last_asset_path = target.path.string();
        }

        bool selftest_asset_enabled(const asset_target& target)
        {
            return target.path.extension() == ".fbx";
        }

        // 선언 표기 그대로다. 실제 선언자와 다른 점은 EDITOR_MENU_LIST를 거치지
        // 않는다는 것뿐이고, 그것이 이 파일이 유일하게 허용된 예외다.
        struct selftest_menus
        {
            static consteval auto for_editor()
            {
                return editor::menu_set(
                    editor::in_tools<&selftest_global_action>("SelfTest/Global")
                           .shortcut("Ctrl+Alt+T"),

                    editor::in_edit<&selftest_selection_action>("SelfTest/Selection")
                           .order(7),

                    editor::in_content_asset<&selftest_asset_action>("SelfTest/Asset")
                           .enabled(&selftest_asset_enabled)
                           .confirm("검사용"));
            }
        };

        void fail(std::string& report, const std::string& line)
        {
            if (!report.empty())
            {
                report += " | ";
            }
            report += line;
        }
    }

    bool run_editor_menu_selftest(std::string& report)
    {
        report.clear();

        // 제품 표를 옆으로 치운다. 겹쳐 치우는 것은 결함이므로 그대로 실패로 낸다.
        if (!stash_menu_registry())
        {
            report = "표를 치울 수 없다 — 이미 치워져 있다(자가 검사가 겹쳐 돈다)";
            return false;
        }

        const menu_registry_stats before = collect_menu_registry_stats();
        if (before.top_menu_items != 0 || before.popup_items != 0)
        {
            report = "치운 뒤에도 표가 비어 있지 않다 "
                     "(top=" + std::to_string(before.top_menu_items) +
                     " popup=" + std::to_string(before.popup_items) + ")";
            unstash_menu_registry();
            return false;
        }

        g_global_calls = 0;
        g_selection_calls = 0;
        g_asset_calls = 0;
        g_last_selection_identity.clear();
        g_last_asset_path.clear();

        register_declarer<selftest_menus>("selftest_menus");

        // ① 항목이 표면대로 갈라졌는가.
        const top_menu_storage& tools = top_menu_entries_of(top_menu_root::tools);
        const top_menu_storage& edit = top_menu_entries_of(top_menu_root::edit);
        const auto& assets = popup_menu_entries<popup_host::content_browser_asset>();

        if (tools.global_items.size() != 1 || !tools.selection_items.empty())
        {
            fail(report, "tools 뿌리에 전역 항목 1개가 아니다");
        }
        if (edit.selection_items.size() != 1 || !edit.global_items.empty())
        {
            fail(report, "edit 뿌리에 선택 대상 항목 1개가 아니다 — 서명 기반 분류가 깨졌다");
        }
        if (assets.size() != 1)
        {
            fail(report, "content_browser_asset 호스트에 항목 1개가 아니다");
        }

        if (!report.empty())
        {
            unstash_menu_registry();
            return false;
        }

        // ② action_id가 함수 이름을 물고 왔는가.
        if (tools.global_items[0].action_id != std::string_view{ "selftest_global_action" })
        {
            fail(report, "전역 항목의 action_id가 함수 이름이 아니다");
        }
        if (edit.selection_items[0].action_id != std::string_view{ "selftest_selection_action" })
        {
            fail(report, "선택 항목의 action_id가 함수 이름이 아니다");
        }
        if (assets[0].action_id != std::string_view{ "selftest_asset_action" })
        {
            fail(report, "팝업 항목의 action_id가 함수 이름이 아니다");
        }

        // ③ 수정자가 실려 왔는가.
        if (tools.global_items[0].sub_path != std::string_view{ "SelfTest/Global" } ||
            tools.global_items[0].shortcut_hint != std::string_view{ "Ctrl+Alt+T" })
        {
            fail(report, "전역 항목의 경로·단축키 표시가 실리지 않았다");
        }
        if (edit.selection_items[0].order != 7)
        {
            fail(report, "order 수정자가 실리지 않았다");
        }
        if (assets[0].confirm_text != std::string_view{ "검사용" })
        {
            fail(report, "confirm 수정자가 실리지 않았다");
        }
        if (assets[0].enabled == nullptr)
        {
            fail(report, "enabled 술어가 실리지 않았다");
        }
        if (tools.global_items[0].enabled != nullptr)
        {
            fail(report, "지정하지 않은 enabled가 비어 있지 않다");
        }
        if (tools.global_items[0].declarer != std::string_view{ "selftest_menus" })
        {
            fail(report, "declarer 이름이 실리지 않았다");
        }

        // ④ invoke 포인터가 진짜로 그 함수로 이어졌는가.
        if (tools.global_items[0].invoke != nullptr)
        {
            tools.global_items[0].invoke(global_target{});
        }
        if (edit.selection_items[0].invoke != nullptr)
        {
            edit.selection_items[0].invoke(entity_target{ "@0:12:3" });
        }
        if (assets[0].invoke != nullptr)
        {
            assets[0].invoke(asset_target{ "Model/Hero.fbx" });
        }

        if (g_global_calls != 1 || g_selection_calls != 1 || g_asset_calls != 1)
        {
            fail(report, "invoke 포인터가 선언한 함수로 이어지지 않았다");
        }
        if (g_last_selection_identity != "@0:12:3")
        {
            fail(report, "entity_target 문맥이 전달되지 않았다");
        }
        if (g_last_asset_path.find("Hero.fbx") == std::string::npos)
        {
            fail(report, "asset_target 문맥이 전달되지 않았다");
        }

        // ⑤ enabled 술어가 문맥을 보고 갈리는가.
        if (assets[0].enabled != nullptr)
        {
            if (!assets[0].enabled(asset_target{ "Model/Hero.fbx" }) ||
                assets[0].enabled(asset_target{ "Model/Hero.png" }))
            {
                fail(report, "enabled 술어가 문맥으로 갈리지 않는다");
            }
        }

        clear_menu_registry();

        const menu_registry_stats after = collect_menu_registry_stats();
        if (after.top_menu_items != 0 || after.popup_items != 0)
        {
            fail(report, "clear_menu_registry가 표를 비우지 않았다");
        }

        // ⑥ 되돌리기. 이것이 실패하면 제품 메뉴가 사라진 채 에디터가 돈다 —
        //    자가 검사가 제품을 망가뜨리는 것이라 반드시 판정에 넣는다.
        if (!unstash_menu_registry())
        {
            fail(report, "치워 둔 제품 표를 되돌리지 못했다");
        }

        return report.empty();
    }
}
