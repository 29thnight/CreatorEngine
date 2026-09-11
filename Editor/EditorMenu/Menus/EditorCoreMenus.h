#pragma once
// 에디터 기본 메뉴 선언 (PHASE 21 M1 · 계획서 부록 A.4).
//
// ★ 이 헤더는 **선언만** 한다. 동작 본문은 `EditorCoreMenus.cpp` 가 든다. M4 가
//   창 쪽에서 쓴 구성과 같다 — 선언 헤더는 `consteval` 표기와 std 만 보고,
//   본문은 씬·자산·플랫폼을 본다. 덕분에 중앙 목록이 이 헤더를 include 해도
//   에디터 전역 헤더가 따라 들어오지 않는다.
//
// ── 왜 이 항목들인가 ──────────────────────────────────────────────────────
//
// M1 은 배선을 세우는 슬라이스이고, 기존 상단 19항목·팝업 68항목의 이관은 하지
// 않는다(A.7). 그래서 여기 실린 것은 **배선이 실제로 쓸모를 낸 자리**만이다.
//
// ① 신원 복사 넷 — 엔티티·자산·폴더·컴포넌트. CLI 명령은 전부 신원을 인자로 받는데
//    (`@scene:index:generation`, 자산 경로) 그것을 화면에서 얻을 방법이 없었다.
//    이 저장소의 정찰이 "명령 98 중 66 이 GUI 에 도달하지 않는다"고 셌던 간극의
//    가장 값싼 절반이 이것이다. 문맥 타입이 호스트마다 다르다는 계약이 여기서
//    그대로 값을 한다 — 같은 "복사" 동작이 호스트마다 다른 것을 집는다.
//
// ② Tools 진단 둘 — `editor.windows` 와 `editor.selftest`. M4 가 만든 관측을
//    메뉴에서 부를 수 있게 한다. 새 Tools 뿌리가 실제로 서는지도 이 둘이 증명한다.
//
// ③ 자산 Delete 하나 — **인라인 두 벌을 대체한다.** 계획서가 실측으로 적어 둔
//    결함이 이것이다: Content Browser 의 Delete 가 확인도 Undo 도 없이
//    `file::remove` 를 부르고, 그 코드가 두 군데 복제돼 있다. `confirm` 어휘가
//    있는 이유가 바로 이 자리여서, 이관 금지의 예외로 둔다. 선언 하나가 복제
//    둘을 지우고 확인을 더한다.

#include "../EditorMenuSchema.h"

namespace editor::menus
{
    // ── 신원 복사 ─────────────────────────────────────────────────────────
    //
    // 서명이 결속을 선언한다(A.4). 인자 타입이 그 호스트의 문맥과 어긋나면
    // 컴파일이 멈춘다.

    void copy_entity_identity(const entity_target& target);
    void copy_asset_path(const asset_target& target);
    void copy_folder_path(const folder_target& target);
    void copy_component_identity(const component_target& target);

    /// `.meta` 짝이 실제로 있을 때만 활성. 술어는 같은 문맥 타입을 받는다.
    bool has_meta_sidecar(const asset_target& target);
    void copy_meta_path(const asset_target& target);

    // ── Tools 진단 ────────────────────────────────────────────────────────
    //
    // 인자 없는 서명이라 전역 동작이고 항상 활성이다.

    void report_declared_windows();
    void run_declaration_selftest();

    // ── 파괴적 동작 ───────────────────────────────────────────────────────

    void delete_asset(const asset_target& target);
}

namespace editor
{
    struct editor_core_menus
    {
        static consteval auto for_editor()
        {
            return menu_set(
                // Tools — 인라인 메뉴가 없는 새 뿌리다. 항목이 0 이면 그려지지
                // 않으므로, 이 둘이 곧 Tools 가 화면에 서는 조건이다.
                in_tools<&menus::report_declared_windows>("Diagnostics/Declared windows")
                    .order(10),
                in_tools<&menus::run_declaration_selftest>("Diagnostics/Declaration selftest")
                    .order(20),

                // Edit — 선택 대상 서명이라 선택이 없으면 registry 가 비활성으로 낸다.
                in_edit<&menus::copy_entity_identity>("Copy selected identity"),

                // 팝업 — 호스트마다 집는 것이 다르다.
                in_hierarchy<&menus::copy_entity_identity>("Copy identity"),
                in_behavior_tree_node<&menus::copy_entity_identity>("Copy owner identity"),
                in_animator_node<&menus::copy_entity_identity>("Copy owner identity"),
                in_inspector_component<&menus::copy_component_identity>("Copy component identity"),
                in_content_folder<&menus::copy_folder_path>("Copy path"),

                in_content_asset<&menus::copy_asset_path>("Copy path").order(10),
                in_content_asset<&menus::copy_meta_path>("Copy .meta path")
                    .order(20)
                    .enabled(&menus::has_meta_sidecar),
                in_content_asset<&menus::delete_asset>("Delete")
                    .order(90)
                    .confirm("이 파일을 지운다. 되돌릴 수 없다."));
        }
    };
}
