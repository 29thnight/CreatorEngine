// 선언된 창의 프레임 소유자 (PHASE 21 M4 · 계획서 부록 B.3).
//
// 이 TU도 유니티에서 뺀다 — EditorWindowRegistry.cpp와 같은 이유.

#include "EditorWindowHost.h"

#include "EditorWindowRegistry.h"
#include "EditorWindowSurface.h"

#include "ImGui.h"

#include <cfloat>
#include <cstdint>
#include <string>

namespace editor
{
    namespace
    {
        // ── 성질 → ImGuiWindowFlags ───────────────────────────────────────
        //
        // 한 자리에 모은 유일한 번역이다. 열거자를 늘리고 여기를 안 늘리면
        // 새 성질이 조용히 무시된다 — 그래서 아래 완전성 단정을 함께 둔다.
        ImGuiWindowFlags to_imgui_flags(window_trait traits)
        {
            ImGuiWindowFlags flags = ImGuiWindowFlags_None;

            if (has_trait(traits, window_trait::auto_resize))
                flags |= ImGuiWindowFlags_AlwaysAutoResize;
            if (has_trait(traits, window_trait::no_saved_layout))
                flags |= ImGuiWindowFlags_NoSavedSettings;
            if (has_trait(traits, window_trait::no_move))
                flags |= ImGuiWindowFlags_NoMove;
            if (has_trait(traits, window_trait::no_bring_to_front_on_focus))
                flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
            if (has_trait(traits, window_trait::no_scrollbar))
                flags |= ImGuiWindowFlags_NoScrollbar;
            if (has_trait(traits, window_trait::no_scroll_with_mouse))
                flags |= ImGuiWindowFlags_NoScrollWithMouse;
            if (has_trait(traits, window_trait::always_vertical_scrollbar))
                flags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;
            if (has_trait(traits, window_trait::always_horizontal_scrollbar))
                flags |= ImGuiWindowFlags_AlwaysHorizontalScrollbar;
            if (has_trait(traits, window_trait::no_collapse))
                flags |= ImGuiWindowFlags_NoCollapse;
            if (has_trait(traits, window_trait::no_docking))
                flags |= ImGuiWindowFlags_NoDocking;
            if (has_trait(traits, window_trait::menu_bar))
                flags |= ImGuiWindowFlags_MenuBar;
            if (has_trait(traits, window_trait::no_title_bar))
                flags |= ImGuiWindowFlags_NoTitleBar;
            if (has_trait(traits, window_trait::no_focus_on_appearing))
                flags |= ImGuiWindowFlags_NoFocusOnAppearing;

            return flags;
        }

        // 번역이 다루는 비트를 모두 켠 값. 열거자를 늘리면서 위 switch를
        // 안 늘리면 이 단정이 어긋나 빌드가 멈춘다.
        constexpr window_trait all_translated_traits =
            window_trait::auto_resize |
            window_trait::no_saved_layout |
            window_trait::no_move |
            window_trait::no_bring_to_front_on_focus |
            window_trait::no_scrollbar |
            window_trait::no_scroll_with_mouse |
            window_trait::always_vertical_scrollbar |
            window_trait::always_horizontal_scrollbar |
            window_trait::no_collapse |
            window_trait::no_docking |
            window_trait::menu_bar |
            window_trait::no_title_bar |
            window_trait::no_focus_on_appearing;

        static_assert(static_cast<std::uint32_t>(all_translated_traits) ==
                          (1u << 13) - 1u,
            "window_trait 열거자가 늘었는데 to_imgui_flags 번역이 따라오지 않았다");

        // ── 제목 ──────────────────────────────────────────────────────────
        //
        // 계획서 §1.4가 정한 규칙이 여기서 강제된다. 왼쪽은 사람이 읽는 표시
        // 이름이고 오른쪽이 ImGui가 배치를 매다는 고정 id다. 라벨이 바뀌어도
        // `imgui.ini`의 도크 항목이 살아남는 것은 오른쪽이 안 바뀌기 때문이다.
        std::string compose_title(const window_entry& entry)
        {
            std::string title;
            title.reserve(entry.label.size() + entry.stable_id.size() + 3);
            title.append(entry.label);
            title.append("###");
            title.append(entry.stable_id);
            return title;
        }

        void apply_stacking(window_stacking stacking)
        {
            switch (stacking)
            {
            case window_stacking::display_back:
                ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
                break;
            case window_stacking::focus_front:
                ImGui::BringWindowToFocusFront(ImGui::GetCurrentWindow());
                ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
                break;
            default:
                break;
            }
        }
    }

    void draw_declared_windows()
    {
        for (window_entry& entry : window_entries())
        {
            // 존재 조건이 거짓이면 프레임 자체를 열지 않는다. 애니메이터 창 셋이
            // 선택이 풀렸을 때 빈 창을 남기던 것과 다르다.
            if (nullptr != entry.available && !entry.available())
            {
                continue;
            }
            if (!entry.open)
            {
                continue;
            }
            if (nullptr == entry.draw)
            {
                continue;
            }

            int pushed_colors = 0;
            int pushed_vars = 0;

            if (entry.has_background)
            {
                ImGui::PushStyleColor(ImGuiCol_WindowBg,
                    ImVec4(entry.background_rgba[0], entry.background_rgba[1],
                           entry.background_rgba[2], entry.background_rgba[3]));
                ++pushed_colors;
            }
            if (entry.has_padding)
            {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                    ImVec2(entry.padding_xy[0], entry.padding_xy[1]));
                ++pushed_vars;
            }

            // 첫 크기는 `Begin` 앞에서만 뜻이 있다. 조건을 선언이 들고
            // 오므로 창 본문에 `SetNextWindowSize`가 남지 않는다.
            switch (entry.sizing)
            {
            case size_policy::first_use_ever:
                ImGui::SetNextWindowSize(
                    ImVec2(entry.initial_width, entry.initial_height),
                    ImGuiCond_FirstUseEver);
                break;
            case size_policy::on_appearing:
                ImGui::SetNextWindowSize(
                    ImVec2(entry.initial_width, entry.initial_height),
                    ImGuiCond_Appearing);
                break;
            default:
                break;
            }

            if (entry.min_width > 0.f || entry.min_height > 0.f)
            {
                ImGui::SetNextWindowSizeConstraints(
                    ImVec2(entry.min_width, entry.min_height),
                    ImVec2(FLT_MAX, FLT_MAX));
            }

            const std::string title = compose_title(entry);

            // 닫기 술어가 있으면 그것이 답한다. 자산 브라우저는 스타일에 따라
            // 서랍이 되었다 패널이 되었다 하므로 매 프레임 갈린다.
            const bool closable = (nullptr != entry.closable_when)
                                      ? entry.closable_when()
                                      : entry.closable;
            bool* open_flag = closable ? &entry.open : nullptr;

            // `Begin`이 거짓이어도 `End`는 반드시 부른다. 조건부 `End`가
            // 스택을 흘리던 두 곳(§1.3)을 셸이 소유하며 없앤다.
            const bool visible = ImGui::Begin(title.c_str(), open_flag,
                                              to_imgui_flags(entry.traits));

            // 스타일은 `Begin`이 소비하고 나면 바로 되돌린다. 본문이 누른 것은
            // 본문이 되돌린다 — 셸은 자기가 누른 것만 센다.
            if (pushed_vars > 0)   ImGui::PopStyleVar(pushed_vars);
            if (pushed_colors > 0) ImGui::PopStyleColor(pushed_colors);

            if (visible)
            {
                apply_stacking(entry.stacking);
                entry.draw();
            }

            ImGui::End();
        }
    }

    std::string dump_declared_windows()
    {
        std::string out;
        out += "id\trole\tdock\tstacking\tclosable\tpersist\topen\torder\tdeclarer\n";

        for (const window_entry& entry : window_entries_of())
        {
            out.append(entry.stable_id);
            out += '\t';
            out += to_string(entry.role);
            out += '\t';
            out += to_string(entry.dock);
            out += '\t';
            out += to_string(entry.stacking);
            out += '\t';
            out += entry.closable ? "1" : "0";
            out += '\t';
            out += entry.persist_open ? "1" : "0";
            out += '\t';
            out += entry.open ? "1" : "0";
            out += '\t';
            out += std::to_string(entry.order);
            out += '\t';
            out.append(entry.declarer);
            out += '\n';
        }

        return out;
    }
}
