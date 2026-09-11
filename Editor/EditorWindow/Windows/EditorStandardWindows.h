#pragma once
// 표준 에디터 창 선언 (PHASE 21 M4 2단계 · 계획서 부록 B.3).
//
// 옛 `ImGui::ContextRegister` 열 곳이 여기로 온다. 계획서가 정한 대로
// **이관은 프레임만 옮긴다** — 본문 코드는 창 클래스가 있던 자리에 그대로 두고
// `Begin`과 `End`만 셸이 가져간다. 그래서 아래 진입점은 본문을 담지 않고,
// 창 클래스의 생성자가 걸어 둔 본문을 부르기만 한다.
//
// ── 안정 식별자가 아직 표시 이름인 이유 ───────────────────────────────────
//
// `ImHashStr`은 `###` 앞을 버린다. 실측으로 확인했다 —
// `ImHashStr("이름###이름") == ImHashStr("이름")` 이다. 그래서 지금처럼
// 안정 식별자에 표시 이름을 그대로 두면 셸이 `이름###이름`으로 `Begin`을
// 불러도 ImGui가 매기는 창 id가 이관 전과 **한 비트도 다르지 않다.**
// 기존 `imgui.ini`의 도크 항목이 그대로 살아 있다는 뜻이다.
//
// 식별자를 `Editor.Hierarchy` 같은 값으로 바꾸는 것은 W3의 몫이다. 그때
// 배치가 끊기므로 legacy ini 이주를 함께 얹어야 한다(§5.3).
//
// ── 왜 본문을 옮기지 않는가 ───────────────────────────────────────────────
//
// 본문 하나가 수백 줄이고, 그것을 잘라 옮기는 편집은 이 저장소에서 이미
// 함수를 통째로 먹은 적이 있다. 프레임만 옮기면 이관의 계약(선언이 표가 되고
// 셸이 프레임을 소유한다)은 전부 서고, 본문 분해는 별건으로 남는다.
// 본문 보관에 `std::function`을 쓰는 것은 옛 `ContextRegister`가 이미 쓰던
// 그대로다 — 이관이 타입소거를 새로 들이지는 않는다.

#include "EditorWindowSchema.h"
#include "EditorWindowNames.h"
#include "EditorWindowBody.h"

namespace editor::windows
{
    // ── 진입점 ────────────────────────────────────────────────────────────

    void draw_hierarchy();                bool has_hierarchy();
    void draw_inspector();                bool has_inspector();
    void draw_asset_bundle();             bool has_asset_bundle();
    void draw_content_browser();          bool has_content_browser();
    void draw_resource_counter();         bool has_resource_counter();

    void draw_render_pass();              bool has_render_pass();
    void draw_light_map();                bool has_light_map();
    void draw_collision_matrix();         bool has_collision_matrix();
    void draw_texture_import_selector();  bool has_texture_import_selector();
    void draw_material_picker();          bool has_material_picker();

    /// 자산 브라우저가 하단 서랍인가. 타일 스타일에서만 참이고, 그때만
    /// 닫기 단추가 뜬다 — 옛 코드가 본문 첫 줄에서 매 프레임 `SetPopup`으로
    /// 하던 판단이다(ContentsBrowserWindow.cpp:110). 판단은 그 파일에 남는다.
    bool content_browser_is_drawer();
}

// ── 도킹되는 패널 다섯 ────────────────────────────────────────────────────
struct editor_panel_windows
{
    static consteval auto for_editor()
    {
        using namespace editor;
        return window_set(
            // 도크 자리의 정본은 **여기**다. `BuildInitialDockLayout`이 이 표를
            // 훑어 `slotNode[entry.dock]`에 도크한다 — 이름 목록을 두 벌로 들지
            // 않으므로 갈릴 자리가 없다(M4 4단계). 이 주석은 "빌더가 손으로
            // 적고 W3·W6이 표를 읽게 만든다"고 적혀 있었는데 그 일은 끝났다.
            // 남은 것은 자리 **이름**을 `dock_slot` 열거자가 아니라 workspace
            // 선언에서 받는 일이고 그것이 W3·W6이다.
            panel<&windows::draw_hierarchy>(
                EditorWindowName::kHierarchy, EditorWindowName::kHierarchy)
                .dock(dock_slot::right_upper)
                .traits(window_trait::no_move)
                .stacking(window_stacking::display_back)
                .closable(false)
                .available(&windows::has_hierarchy),

            panel<&windows::draw_inspector>(
                EditorWindowName::kInspector, EditorWindowName::kInspector)
                .dock(dock_slot::right_lower)
                .traits(window_trait::no_move |
                        window_trait::no_bring_to_front_on_focus |
                        window_trait::no_focus_on_appearing)
                .stacking(window_stacking::display_back)
                .closable(false)
                .available(&windows::has_inspector),

            panel<&windows::draw_asset_bundle>(
                EditorWindowName::kAssetBundle, EditorWindowName::kAssetBundle)
                .dock(dock_slot::bottom)
                .traits(window_trait::auto_resize)
                .closable(false)
                .available(&windows::has_asset_bundle),

            // 등록이 `ImGuiWindowFlags_None`을 명시한 유일한 창이다. isPopup이
            // 참이라 NoCollapse만 얹혔다.
            panel<&windows::draw_content_browser>(
                EditorWindowName::kContentBrowser, EditorWindowName::kContentBrowser)
                .dock(dock_slot::bottom)
                .traits(window_trait::no_collapse)
                .closable_when(&windows::content_browser_is_drawer)
                .available(&windows::has_content_browser),

            panel<&windows::draw_resource_counter>(
                EditorWindowName::kResourceCounter, EditorWindowName::kResourceCounter)
                .dock(dock_slot::bottom)
                .traits(window_trait::always_vertical_scrollbar |
                        window_trait::no_collapse)
                .open_by_default(false)
                .available(&windows::has_resource_counter));
    }
};

// ── 떠 있는 도구 창 다섯 ──────────────────────────────────────────────────
//
// 다섯 다 도크 빌더에 자리가 없다. `floating`은 panel 역할의 기본값이라
// 적지 않는다 — 적지 않은 것을 역할이 답하는 것이 이 어휘의 요점이다.
struct editor_tool_windows
{
    static consteval auto for_editor()
    {
        using namespace editor;
        return window_set(
            panel<&windows::draw_render_pass>(
                EditorWindowName::kRenderPass, EditorWindowName::kRenderPass)
                .traits(window_trait::always_vertical_scrollbar |
                        window_trait::no_collapse)
                .open_by_default(false)
                .available(&windows::has_render_pass),

            // 아래 넷도 생성자가 곧바로 닫던 것들이다. 열 곳 중 여섯이
            // 그랬고, 그 판단이 전부 선언으로 왔다.
            panel<&windows::draw_light_map>(
                EditorWindowName::kLightMap, EditorWindowName::kLightMap)
                .traits(window_trait::auto_resize | window_trait::no_collapse)
                .open_by_default(false)
                .available(&windows::has_light_map),

            panel<&windows::draw_collision_matrix>(
                EditorWindowName::kCollisionMatrix, EditorWindowName::kCollisionMatrix)
                .traits(window_trait::always_vertical_scrollbar |
                        window_trait::always_horizontal_scrollbar |
                        window_trait::no_collapse)
                .open_by_default(false)
                .available(&windows::has_collision_matrix),

            panel<&windows::draw_texture_import_selector>(
                EditorWindowName::kTextureImportSelector,
                EditorWindowName::kTextureImportSelector)
                .traits(window_trait::auto_resize | window_trait::no_collapse)
                .open_by_default(false)
                .available(&windows::has_texture_import_selector),

            panel<&windows::draw_material_picker>(
                EditorWindowName::kMaterialPicker, EditorWindowName::kMaterialPicker)
                .traits(window_trait::no_scrollbar | window_trait::no_collapse)
                .open_by_default(false)
                .available(&windows::has_material_picker));
    }
};
