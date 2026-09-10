#pragma once
// 에디터 창 런타임 표 (PHASE 21 M4 · 계획서 부록 B.3) — 선언을 항목으로 낮춘다.
//
// 프레임을 여는 배선은 `EditorWindowHost`가 얹는다. 이 헤더는 **선언 → 표**와
// **표시 상태의 단일 저장소**까지만 책임진다.
//
// ── 왜 자기 등록자가 아니라 중앙 목록인가 ─────────────────────────────────
//
// 부록 A와 같은 이유다. Editor는 네 구성 전부 StaticLibrary이고 `/WHOLEARCHIVE`가
// 0건이라, 아무도 참조하지 않는 TU는 링커가 끌어오지 않는다. 독립 .cpp의
// 네임스페이스 스코프 자기 등록자는 조용히 사라진다. 그래서
// `RegisterEditorWindowManual.h`가 선언자 헤더를 include하면서 열거한다 —
// include가 인스턴스화를 링크되는 TU 안으로 끌어오고, 열거가 등록을 돌린다.
//
// ── 표시 상태 저장소가 여기 하나인 이유 ───────────────────────────────────
//
// 지금 표시 상태는 셋으로 갈려 있다(계획서 §1.3 정정):
//   `MenuBarWindow` 멤버 bool 10 · `ImGuiRenderContext::m_opened` 10 ·
//   `ImGui::Begin`에 직접 넘기는 p_open bool 11.
// `MenuBarWindow`의 일곱 곳은 둘째와 셋째를 겸용해 같은 창의 열림 여부가 두
// 자리에 있다. 창 항목이 자기 상태를 들면 그 셋이 하나로 접힌다 — 이관이
// 끝나면 나머지 둘은 소비자가 0이 되어 사라진다.

#include "EditorWindowSchema.h"
#include "EditorWindowSurface.h"

#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace editor
{
    // 표에 실린 창 하나. 선언이 값으로 낮아진 것이고 타입소거가 없다.
    struct window_entry
    {
        std::string_view stable_id{};
        std::string_view label{};
        std::string_view declarer{};

        window_role      role{ window_role::panel };
        dock_slot        dock{ dock_slot::floating };
        window_trait     traits{ window_trait::none };
        window_stacking  stacking{ window_stacking::normal };

        bool             closable{ false };
        bool             persist_open{ true };
        float            min_width{ 0.f };
        float            min_height{ 0.f };
        int              order{ 0 };

        bool             has_background{ false };
        float            background_rgba[4]{};
        bool             has_padding{ false };
        float            padding_xy[2]{};

        void (*draw)(){ nullptr };
        bool (*available)(){ nullptr };   // nullptr == 항상 존재
        bool (*closable_when)(){ nullptr };   // nullptr == 위의 closable 고정값

        // ★ 표시 상태의 유일한 자리. 셸이 `Begin`에 넘기는 p_open이 이것을 가리킨다.
        bool             open{ true };
    };

    /// 선언 순서 그대로다. `unordered_map` 순회가 비결정적이라 골든이 흔들리던
    /// 문제(계획서 §1.3-4)를 이 순서가 대체한다.
    std::vector<window_entry>&       window_entries();
    const std::vector<window_entry>& window_entries_of();

    /// 안정 식별자로 찾는다. 없으면 nullptr — `GetContext`가 `operator[]`라
    /// 오타가 유령 창을 영구 삽입하던 것(§1.3)과 다르게, 여기서는 **없는 것이
    /// 없는 것으로 답한다.**
    window_entry*       find_window(std::string_view stable_id);
    const window_entry* find_window_of(std::string_view stable_id);

    /// 이름으로 표시 상태를 여닫는다. **없는 이름은 아무 일도 하지 않는다.**
    /// 옛 `ImGui::GetContext(name)`는 `operator[]`라 오타가 유령 창을 영구
    /// 삽입했고, 실제로 하나 살아 있다 — `MenuBarWindow.cpp:336`의 "EffectEdit"은
    /// 등록된 적이 없는데 메뉴가 그것을 읽어 매 프레임 순회에 얹는다.
    void open_window(std::string_view stable_id);
    void close_window(std::string_view stable_id);
    bool is_window_open(std::string_view stable_id);
    bool window_declared(std::string_view stable_id);

    // ── 선언 → 표 ─────────────────────────────────────────────────────────

    namespace detail
    {
        template<window_role Role, auto Draw>
        void add_declared_window(const window_item<Role, Draw>& item,
                                 std::string_view declarer)
        {
            window_entry entry{};
            entry.stable_id = item.stable_id;
            entry.label     = item.label;
            entry.declarer  = declarer;
            entry.role      = Role;
            entry.dock      = item.dock_value;
            entry.traits    = item.trait_value;
            entry.stacking  = item.stacking_value;
            entry.closable  = item.closable_value;
            entry.persist_open = item.persist_open_value;
            entry.min_width    = item.min_width_value;
            entry.min_height   = item.min_height_value;
            entry.order        = item.order_value;
            entry.has_background = item.has_background;
            entry.has_padding    = item.has_padding;
            for (int i = 0; i < 4; ++i) entry.background_rgba[i] = item.background_rgba[i];
            for (int i = 0; i < 2; ++i) entry.padding_xy[i] = item.padding_xy[i];
            // 캡처 없는 람다라 함수 포인터로 낮아진다 — std::function이 아니므로
            // CT6-a가 걷어낸 타입소거가 되살아나지 않는다.
            entry.draw      = +[]() { Draw(); };
            entry.available = item.available_fn;
            entry.closable_when = item.closable_fn;
            entry.open      = item.open_by_default_value;

            window_entries().push_back(entry);
        }
    }

    /// 선언자 하나를 표에 붓는다. **직접 부르지 않는다** — EDITOR_WINDOW_LIST를
    /// 거치는 것만이 등록 경로이고, 그래야 누락을 기동 게이트가 잡는다.
    template<class Declarer>
    void register_declarer_windows(std::string_view declarer_name)
    {
        static_assert(detail::declares_editor_window<Declarer>,
            "선언자에 static consteval auto for_editor()가 없다");

        constexpr auto declaration = Declarer::for_editor();
        std::apply(
            [declarer_name](const auto&... items)
            {
                (detail::add_declared_window(items, declarer_name), ...);
            },
            declaration.items);
    }

    // ── 게이트·시험용 ─────────────────────────────────────────────────────

    struct window_registry_stats
    {
        std::size_t total{ 0 };
        std::size_t central{ 0 };
        std::size_t panel{ 0 };
        std::size_t transient{ 0 };
        std::size_t duplicate_ids{ 0 };
        std::size_t empty_dock_slots{ 0 };
    };

    window_registry_stats collect_window_registry_stats();

    /// 표를 비운다. 시험이 등록을 되풀이할 때만 쓴다.
    void clear_window_registry();
}
