// 에디터 창 배선 자가 검사 (PHASE 21 M4 1단계).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.
//
// ImGui를 부르지 않는다. 셸이 프레임을 여는 것은 프레임 루프 안에서만 검사할
// 수 있고, 여기서 보는 것은 그 앞 단계 — **선언이 표가 되는가**뿐이다.

#include "EditorWindowSelfTest.h"

#include "EditorWindowRegistry.h"
#include "EditorWindowSchema.h"
#include "EditorWindowSurface.h"

#include <string>
#include <string_view>
#include <vector>

namespace editor
{
    namespace
    {
        // 검사용 본문 셋. 부른 흔적을 남겨 draw 포인터가 진짜로 이어졌는지 본다.
        int g_scene_draws = 0;
        int g_inspector_draws = 0;
        int g_loading_draws = 0;

        void selftest_draw_scene()     { ++g_scene_draws; }
        void selftest_draw_inspector() { ++g_inspector_draws; }
        void selftest_draw_loading()   { ++g_loading_draws; }

        bool g_inspector_available = true;
        bool selftest_inspector_available() { return g_inspector_available; }

        bool g_loading_closable = false;
        bool selftest_loading_closable() { return g_loading_closable; }

        // 선언 표기 그대로다. 실제 선언자와 다른 점은 EDITOR_WINDOW_LIST를
        // 거치지 않는다는 것뿐이고, 그것이 이 파일이 유일하게 허용된 예외다.
        struct selftest_windows
        {
            static consteval auto for_editor()
            {
                return editor::window_set(
                    editor::central<&selftest_draw_scene>("selftest.scene", "SelfTest Scene")
                           .stacking(window_stacking::display_back)
                           .background(0.f, 0.f, 0.f, 1.f)
                           .padding(0.f, 0.f),

                    editor::panel<&selftest_draw_inspector>("selftest.inspector", "SelfTest Inspector")
                           .dock(dock_slot::right_lower)
                           .add_traits(window_trait::no_scrollbar)
                           .available(&selftest_inspector_available)
                           .min_size(120.f, 64.f)
                           .initial_size(800.f, 600.f)
                           .order(3),

                    editor::transient<&selftest_draw_loading>("selftest.loading", "SelfTest Loading")
                           .open_by_default(false)
                           .initial_size(440.f, 0.f, size_policy::on_appearing)
                           .closable_when(&selftest_loading_closable));
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

        // 검사 전용 이름의 접두어. 제품 표가 이것을 쓰지 않음을 아래에서 확인하고,
        // 그 위에 "인자를 빠뜨린 여닫기는 제품에 무해하다" 가 선다.
        constexpr std::string_view kSelfTestPrefix{ "selftest." };

        // 제품 표를 건드리지 않았는지 재는 자리.
        //
        // 함수로 둔 이유가 있다. 끝에만 두면 앞의 단정이 붉어 조기 반환할 때
        // 이 단정은 **아예 돌지 않는다** — 이 저장소는 "앞에 선 단정이 뒤를
        // 가린다"로 이미 한 번 데었다. 그래서 두 출구에서 모두 부른다.
        //
        // 잡는 것은 **등록과 비우기**뿐이다. 여닫기는 항목 수를 바꾸지 않으므로
        // 크기로는 영원히 안 잡히고, 그쪽은 위의 접두어 불변식이 맡는다.
        // 제품 표의 `open` 을 앞뒤로 대조하는 쪽이 더 촘촘하겠으나 그 값은
        // PresentationThread 가 `Begin` 의 p_open 으로 쓴다. 사용자가 검사 도중
        // 창 하나를 닫으면 거짓으로 붉어지는 게이트가 된다 — 택하지 않는다.
        //
        // 변이 넷으로 쟀다(2026-09-11). 인자를 빠뜨린 `clear_window_registry`·
        // `close_window`·`register_declarer_windows` 셋은 모두 붉었으나 기존
        // 단정도 함께 붉었으므로 이 단정만의 공로가 아니다. 이 단정이 **홀로**
        // 잡은 것은 넷째다 — 지역 표는 온전히 두고 표 인자 없는 등록을 한 줄
        // 더한 경우. 짝이 되는 단정이 없는 새 호출이 정확히 그 모양이고,
        // 그것이 이 검사를 세운 이유다.
        void check_product_untouched(std::string& report, std::size_t before)
        {
            if (process_windows().entries.size() != before)
            {
                fail(report, "검사가 제품 표를 건드렸다 — 표 인자를 빠뜨린 호출이 있다");
            }
        }
    }

    bool run_editor_window_selftest(std::string& report)
    {
        report.clear();

        // 빈 지역 표 위에서 돈다. 제품 표는 손대지 않는다.
        //
        // 전에는 제품 표를 `swap` 으로 옆에 치웠다 되돌렸다. "표가 비어 있지
        // 않으면 실패" 로 두면 부팅 전 한순간에만 돌 수 있고, 그 말은 도는
        // 세트에 넣을 수 없다는 뜻이어서 그렇게 했던 것인데(이 저장소는
        // "게이트가 도는 세트에 없으면 없는 것"으로 두 번 데었다), 그 치우기는
        // **CLI 명령이 도는 게임 스레드**에서 일어나고 표를 순회하는 것은
        // **PresentationThread** 다. 잠금이 없으므로 순회 도중에 벡터의 버퍼가
        // 바뀔 수 있는 자리였다. 표를 인자로 받게 되면서 그 자리가 없어졌다 —
        // 막은 것이 아니라 없앤 것이다.
        window_table table;

        // 표를 받는 정본 옆에 제품 표에 거는 짧은 표기가 나란히 있어서, 인자를
        // 빠뜨리면 **조용히 컴파일되고 제품 표로 간다.** 실제로 이 파일을 옮기며
        // `close_window` 하나를 빠뜨렸고, 그때 붉어진 이유는 제품 표에 그 이름이
        // 없어서였다. 그 "없어서" 를 우연에서 불변식으로 바꾼다.
        const std::size_t product_before = process_windows().entries.size();
        for (const window_entry& declared : process_windows().entries)
        {
            if (declared.stable_id.starts_with(kSelfTestPrefix))
            {
                fail(report,
                    "제품 창이 검사 전용 접두어를 쓰고 있다 — 인자를 빠뜨린 "
                    "여닫기가 제품 창을 건드려도 조용할 수 있다");
            }
        }

        g_scene_draws = 0;
        g_inspector_draws = 0;
        g_loading_draws = 0;
        g_inspector_available = true;
        g_loading_closable = false;

        register_declarer_windows<selftest_windows>(table, "selftest_windows");

        // ① 선언 순서가 표 순서인가. unordered_map 순회를 이 순서가 대체한다.
        const std::vector<window_entry>& entries = table.entries;
        if (entries.size() != 3)
        {
            fail(report, "창 3개가 실리지 않았다 (size=" +
                         std::to_string(entries.size()) + ")");
            check_product_untouched(report, product_before);
            return false;
        }
        if (entries[0].stable_id != std::string_view{ "selftest.scene" } ||
            entries[1].stable_id != std::string_view{ "selftest.inspector" } ||
            entries[2].stable_id != std::string_view{ "selftest.loading" })
        {
            fail(report, "표 순서가 선언 순서가 아니다");
        }

        // ② 역할이 기본값을 물고 왔는가. 선언에 적지 않은 것은 역할이 답한다.
        const window_entry& scene = entries[0];
        const window_entry& inspector = entries[1];
        const window_entry& loading = entries[2];

        if (scene.role != window_role::central || scene.dock != dock_slot::center)
        {
            fail(report, "central 기본 도킹 자리가 center가 아니다");
        }
        if (!has_trait(scene.traits, window_trait::no_move) ||
            !has_trait(scene.traits, window_trait::no_bring_to_front_on_focus))
        {
            fail(report, "central 기본 성질이 실리지 않았다");
        }
        if (scene.closable)
        {
            fail(report, "central이 닫을 수 있는 것으로 실렸다");
        }
        if (!has_trait(loading.traits, window_trait::no_saved_layout) ||
            !has_trait(loading.traits, window_trait::no_docking) ||
            !has_trait(loading.traits, window_trait::auto_resize))
        {
            fail(report, "transient 기본 성질이 실리지 않았다");
        }
        if (loading.persist_open)
        {
            fail(report, "transient의 열림 여부가 워크스페이스에 남는 것으로 실렸다");
        }
        if (!inspector.closable)
        {
            fail(report, "panel 기본값이 닫을 수 있는 것이 아니다");
        }

        // ③ 수정자가 기본값을 덮는가.
        if (inspector.dock != dock_slot::right_lower)
        {
            fail(report, "dock 수정자가 panel 기본값(floating)을 덮지 못했다");
        }
        if (!has_trait(inspector.traits, window_trait::no_scrollbar))
        {
            fail(report, "add_traits 수정자가 실리지 않았다");
        }
        if (inspector.min_width != 120.f || inspector.min_height != 64.f)
        {
            fail(report, "min_size 수정자가 실리지 않았다");
        }
        if (inspector.order != 3)
        {
            fail(report, "order 수정자가 실리지 않았다");
        }
        if (scene.stacking != window_stacking::display_back)
        {
            fail(report, "stacking 수정자가 실리지 않았다");
        }
        if (!scene.has_background || scene.background_rgba[3] != 1.f)
        {
            fail(report, "background 재정의가 실리지 않았다");
        }
        if (!scene.has_padding || scene.padding_xy[0] != 0.f)
        {
            fail(report, "padding 재정의가 실리지 않았다");
        }
        if (inspector.has_background || inspector.has_padding)
        {
            fail(report, "지정하지 않은 스타일 재정의가 켜져 있다");
        }
        if (loading.open)
        {
            fail(report, "open_by_default(false)가 표시 상태에 실리지 않았다");
        }
        if (!scene.open || !inspector.open)
        {
            fail(report, "표시 상태 기본값이 열림이 아니다");
        }
        if (scene.declarer != std::string_view{ "selftest_windows" })
        {
            fail(report, "declarer 이름이 실리지 않았다");
        }

        // ④ available 술어가 실려 왔고 값을 보고 갈리는가.
        if (nullptr == inspector.available)
        {
            fail(report, "available 술어가 실리지 않았다");
        }
        else
        {
            g_inspector_available = false;
            const bool closed = inspector.available();
            g_inspector_available = true;
            if (closed || !inspector.available())
            {
                fail(report, "available 술어가 상태로 갈리지 않는다");
            }
        }
        if (nullptr != scene.available)
        {
            fail(report, "지정하지 않은 available이 비어 있지 않다");
        }

        // ⑤ draw 포인터가 진짜로 그 함수로 이어졌는가.
        for (const window_entry& entry : entries)
        {
            if (nullptr != entry.draw)
            {
                entry.draw();
            }
        }
        if (g_scene_draws != 1 || g_inspector_draws != 1 || g_loading_draws != 1)
        {
            fail(report, "draw 포인터가 선언한 함수로 이어지지 않았다");
        }

        // ⑥ 안정 식별자로 찾는가. 없는 것은 없는 것으로 답하는가.
        if (nullptr == find_window_of(table, "selftest.inspector"))
        {
            fail(report, "안정 식별자로 항목을 찾지 못한다");
        }
        if (nullptr != find_window_of(table, "selftest.inspecto"))
        {
            fail(report, "없는 식별자가 항목을 만들어 낸다 — 유령 삽입이 되살아났다");
        }

        // ⑦ 닫기 술어가 실려 왔고 값을 보고 갈리는가.
        if (nullptr == loading.closable_when)
        {
            fail(report, "closable_when 술어가 실리지 않았다");
        }
        else
        {
            g_loading_closable = true;
            const bool opened_state = loading.closable_when();
            g_loading_closable = false;
            if (!opened_state || loading.closable_when())
            {
                fail(report, "closable_when 술어가 상태로 갈리지 않는다");
            }
        }
        if (nullptr != scene.closable_when)
        {
            fail(report, "지정하지 않은 closable_when이 비어 있지 않다");
        }

        // ⑧ 이름으로 여닫는 창구가 도는가. 없는 이름은 아무 일도 하지 않는가.
        close_window(table, "selftest.inspector");
        if (is_window_open(table, "selftest.inspector"))
        {
            fail(report, "close_window가 표시 상태를 닫지 못했다");
        }
        open_window(table, "selftest.inspector");
        if (!is_window_open(table, "selftest.inspector"))
        {
            fail(report, "open_window가 표시 상태를 열지 못했다");
        }

        // 등록된 적 없는 이름이다. 옛 GetContext는 여기서 유령을 만들어 넣었다.
        const std::size_t before_phantom = table.entries.size();
        open_window(table, "selftest.effect_edit");
        if (is_window_open(table, "selftest.effect_edit") ||
            window_declared(table, "selftest.effect_edit") ||
            table.entries.size() != before_phantom)
        {
            fail(report, "없는 이름을 열자 유령 항목이 생겼다");
        }

        // ⑨ 집계가 역할과 중복을 세는가.
        const window_registry_stats stats = collect_window_registry_stats(table);
        if (stats.total != 3 || stats.central != 1 || stats.panel != 1 || stats.transient != 1)
        {
            fail(report, "역할별 집계가 어긋난다");
        }
        if (stats.duplicate_ids != 0)
        {
            fail(report, "중복 없는 표에서 중복이 세어졌다");
        }
        // center와 right_lower 둘이 차고 right_upper·bottom 둘이 비었다.
        if (stats.empty_dock_slots != 2)
        {
            fail(report, "빈 도킹 자리 집계가 어긋난다 (=" +
                         std::to_string(stats.empty_dock_slots) + ")");
        }

        // ⑩ 첫 크기가 조건까지 실려 오는가. 최소 크기 제약과 다른 축이다 —
        // 이쪽은 `Begin` 앞에 한 번 부르는 호출이고 저쪽은 계속 강제된다.
        if (inspector.sizing != size_policy::first_use_ever ||
            inspector.initial_width != 800.f || inspector.initial_height != 600.f)
        {
            fail(report, "initial_size 기본 조건(first_use_ever)이 실리지 않았다");
        }
        if (loading.sizing != size_policy::on_appearing ||
            loading.initial_width != 440.f || loading.initial_height != 0.f)
        {
            fail(report, "initial_size의 조건 인자가 실리지 않았다");
        }
        if (scene.sizing != size_policy::none ||
            scene.initial_width != 0.f || scene.initial_height != 0.f)
        {
            fail(report, "지정하지 않은 첫 크기가 비어 있지 않다");
        }
        if (inspector.min_width == inspector.initial_width)
        {
            fail(report, "첫 크기와 최소 크기가 같은 칸을 쓰고 있다");
        }

        clear_window_registry(table);

        const window_registry_stats after = collect_window_registry_stats(table);
        if (after.total != 0)
        {
            fail(report, "clear_window_registry가 표를 비우지 않았다");
        }

        check_product_untouched(report, product_before);

        return report.empty();
    }
}
