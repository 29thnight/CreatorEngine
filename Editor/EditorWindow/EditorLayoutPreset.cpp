#include "EditorLayoutPreset.h"
#include "EditorWindowNames.h"

#include <algorithm>
#include <array>

namespace editor
{
    namespace
    {
        // ── ① S&Box Compact — 오늘의 배치 ──────────────────────────────────
        //
        // 재정의가 **비어 있다.** 선언의 `.dock(...)` 이 그대로 답이고, 그래서
        // "현재 외관을 유지한다" 가 값의 일치가 아니라 출처의 동일성으로 선다.
        constexpr std::array<layout_override, 0> kCompact{};

        // ── ② Level Editing — 뷰포트를 넓힌다 ──────────────────────────────
        //
        // 레벨을 놓는 동안 오래 보는 것은 씬이다. 오른쪽 열을 좁히고 아래를
        // 줄이되 Hierarchy 에는 더 준다(오른쪽 열 안에서 Inspector 를 아래로
        // 밀어 `right_lower` 비율을 낮춘다). 아래 탭 셋 중 Content Browser 만
        // 남긴다 — 탭이 셋이면 어차피 하나만 돌고, 나머지는 자리만 차지한다.
        constexpr std::array kLevelEditing{
            layout_override{ EditorWindowName::kAssetBundle, dock_slot::bottom,
                             preset_visibility::closed },
            layout_override{ EditorWindowName::kResourceCounter, dock_slot::bottom,
                             preset_visibility::closed },
            layout_override{ EditorWindowName::kContentBrowser, dock_slot::bottom,
                             preset_visibility::opened },
        };

        // ── ③ UI Editing — Inspector 를 키운다 ─────────────────────────────
        //
        // UI 는 값을 만지는 시간이 길다. 오른쪽 열을 넓히고 그 안에서 Inspector
        // 가 대부분을 갖게 한다. 입력 맵을 떠 있는 창에서 오른쪽 아래로 끌어와
        // 함께 연다 — UI 작업에서 늘 같이 보는 짝이다.
        constexpr std::array kUiEditing{
            layout_override{ EditorWindowName::kInputActionMaps, dock_slot::right_lower,
                             preset_visibility::opened },
            layout_override{ EditorWindowName::kAssetBundle, dock_slot::bottom,
                             preset_visibility::closed },
        };

        // ── ④ Rendering & Debug — 아래를 관측으로 채운다 ───────────────────
        //
        // 넷 다 선언에서는 `floating` 이다. 렌더링을 들여다보는 동안에는 떠
        // 있는 창을 옮겨 놓는 일부터 하게 되므로, 이 preset 이 아래 탭으로
        // 모아 열어 준다.
        constexpr std::array kRenderingDebug{
            layout_override{ EditorWindowName::kRenderPass, dock_slot::bottom,
                             preset_visibility::opened },
            layout_override{ EditorWindowName::kRenderPassDebug, dock_slot::bottom,
                             preset_visibility::opened },
            layout_override{ EditorWindowName::kFrameProfiler, dock_slot::bottom,
                             preset_visibility::opened },
            layout_override{ EditorWindowName::kOutputLog, dock_slot::bottom,
                             preset_visibility::opened },
        };

        // ── ⑤ Legacy Unity — 왼쪽 Hierarchy · 오른쪽 Inspector ─────────────
        //
        // Unity 를 쓰던 손이 찾는 자리다. Hierarchy 가 **왼쪽 전체 높이**이고
        // Inspector 가 오른쪽 전체 높이이며 아래가 프로젝트 창이다. 그래서 이
        // preset 은 `right_lower` 를 쓰지 않는다 — 빌더가 그 노드를 만들지도
        // 않는다(만들면 아무도 안 들어오는 빈 노드가 남는다).
        constexpr std::array kLegacyUnity{
            layout_override{ EditorWindowName::kHierarchy, dock_slot::left,
                             preset_visibility::opened },
            layout_override{ EditorWindowName::kInspector, dock_slot::right_upper,
                             preset_visibility::opened },
            layout_override{ EditorWindowName::kContentBrowser, dock_slot::bottom,
                             preset_visibility::opened },
            layout_override{ EditorWindowName::kResourceCounter, dock_slot::bottom,
                             preset_visibility::closed },
        };

        constexpr std::array kPresets{
            layout_preset{ "sbox_compact", "S&Box Compact",
                           layout_split{ 0.f, 0.22f, 0.45f, 0.28f }, kCompact },
            layout_preset{ "level_editing", "Level Editing",
                           layout_split{ 0.f, 0.20f, 0.55f, 0.20f }, kLevelEditing },
            layout_preset{ "ui_editing", "UI Editing",
                           layout_split{ 0.f, 0.26f, 0.32f, 0.26f }, kUiEditing },
            layout_preset{ "rendering_debug", "Rendering & Debug",
                           layout_split{ 0.f, 0.22f, 0.45f, 0.38f }, kRenderingDebug },
            layout_preset{ "legacy_unity", "Legacy Unity",
                           layout_split{ 0.18f, 0.22f, 0.f, 0.30f }, kLegacyUnity },
        };

        const layout_override* find_override(std::string_view id, const layout_preset& preset)
        {
            const auto found = std::find_if(preset.overrides.begin(), preset.overrides.end(),
                [id](const layout_override& value) { return value.stable_id == id; });
            return found == preset.overrides.end() ? nullptr : &*found;
        }
    }

    std::span<const layout_preset> layout_presets() { return kPresets; }

    const layout_preset& default_layout_preset() { return kPresets[0]; }

    const layout_preset* find_layout_preset(std::string_view id)
    {
        const auto found = std::find_if(kPresets.begin(), kPresets.end(),
            [id](const layout_preset& value) { return value.id == id; });
        return found == kPresets.end() ? nullptr : &*found;
    }

    dock_slot slot_of(const window_entry& entry, const layout_preset& preset)
    {
        // 재정의가 없으면 선언값이다. `transient` 는 도킹하지 않는 것이 역할이라
        // 재정의가 있어도 자리를 주지 않는다(스키마의 `dock()` 도 같은 이유로
        // transient 를 막는다).
        if (window_role::transient == entry.role) return dock_slot::floating;
        if (const layout_override* value = find_override(entry.stable_id, preset))
            return value->dock;
        return entry.dock;
    }

    preset_visibility visibility_of(const window_entry& entry, const layout_preset& preset)
    {
        // 가운데를 차지하는 창은 닫히지 않는다 — preset 이 그것을 뒤집을 수 없다.
        if (window_role::central == entry.role) return preset_visibility::opened;
        if (const layout_override* value = find_override(entry.stable_id, preset))
            return value->open;
        return preset_visibility::inherit;
    }

    bool slot_occupied(const window_table& table, const layout_preset& preset, dock_slot slot)
    {
        if (dock_slot::floating == slot) return false;
        return std::any_of(table.entries.begin(), table.entries.end(),
            [&preset, slot](const window_entry& entry)
            {
                if (slot_of(entry, preset) != slot) return false;
                // 닫으라고 적힌 창은 그 자리를 채우지 못한다. 그 창 하나뿐이면
                // 빌더가 만든 노드는 비어 있게 된다.
                return preset_visibility::closed != visibility_of(entry, preset);
            });
    }

    bool slot_used_by_any_preset(const window_table& table, dock_slot slot)
    {
        return std::any_of(kPresets.begin(), kPresets.end(),
            [&table, slot](const layout_preset& preset)
            { return slot_occupied(table, preset, slot); });
    }
}
