#pragma once
// 에디터 창 표면 정의 (PHASE 21 M4 · 계획서 부록 B.3) — std만 의존한다.
//
// 부록 A의 `EditorMenuSurface.h`와 같은 계통이고 같은 관례를 쓴다 —
// 파일명 PascalCase, 네임스페이스 안 식별자 snake_case, 선언 함수 `for_editor()`,
// 중앙 목록 하나가 인스턴스화와 등록을 겸한다.
//
// ── 왜 ImGui 플래그를 그대로 담지 않는가 ──────────────────────────────────
//
// `ImGuiWindowFlags`는 스물 몇 개가 열린 비트필드다. 그대로 담으면 선언이
// "이 창은 무엇인가"가 아니라 "ImGui를 어떻게 부르는가"를 적게 되고, 헤더가
// imgui.h에 묶여 M0처럼 std만으로 컴파일해 검증할 수 없다.
//
// 그래서 **실제로 쓰이는 것만** 닫힌 집합으로 담는다. 아래 목록은 창 25개의
// `ImGui::Begin`과 `ContextRegister` 인자를 전수로 세어 나온 것이지 미리
// 상상한 것이 아니다. 번역은 셸(`EditorWindowHost`) 한 곳이 한다.
//
// 새 창이 여기 없는 성질을 원하면 **열거자를 늘리는 것이 정상 경로다.** 늘리면
// 아래 완전성 단정과 셸의 번역 switch가 함께 막아 선다 — 조용히 무시되지 않는다.

#include <array>
#include <cstddef>
#include <cstdint>

namespace editor
{
    // ── 역할 ──────────────────────────────────────────────────────────────
    //
    // 역할이 기본값을 정하고 무엇이 허용되는지도 정한다. 팝업 호스트가 동작의
    // 인자 타입을 정하던 것(부록 A.3)과 같은 수법이다 — 종류를 먼저 고르면
    // 어긋난 조합이 표기 단계에서 막힌다.
    enum class window_role
    {
        central,     // 중앙 노드를 쓰는 뷰포트 계열. Scene · Game
        panel,       // 도킹되는 도구 패널. 대다수
        transient,   // 도킹도 영속도 원하지 않는 일시 표시. Model loading

        count,
    };

    // ── 기본 도킹 자리 ────────────────────────────────────────────────────
    //
    // M3이 세운 배치의 노드 이름이다. 실제 노드 id는 셸이 도크 빌더를 돌 때
    // 만들고, 선언은 "어느 자리에 속하는가"만 말한다.
    enum class dock_slot
    {
        center,        // 뷰포트 탭
        right_upper,   // Hierarchy 자리
        right_lower,   // Inspector 자리
        bottom,        // 자산 브라우저 계열
        floating,      // 어디에도 붙이지 않는다

        count,
    };

    // ── 창 성질 (비트) ────────────────────────────────────────────────────
    //
    // 전수 실측으로 나온 열둘이다. 셸이 ImGuiWindowFlags로 번역한다.
    enum class window_trait : std::uint32_t
    {
        none                       = 0u,
        auto_resize                = 1u << 0,   // AlwaysAutoResize
        no_saved_layout            = 1u << 1,   // NoSavedSettings
        no_move                    = 1u << 2,   // NoMove
        no_bring_to_front_on_focus = 1u << 3,   // NoBringToFrontOnFocus
        no_scrollbar               = 1u << 4,   // NoScrollbar
        no_scroll_with_mouse       = 1u << 5,   // NoScrollWithMouse
        always_vertical_scrollbar  = 1u << 6,   // AlwaysVerticalScrollbar
        always_horizontal_scrollbar= 1u << 7,   // AlwaysHorizontalScrollbar
        no_collapse                = 1u << 8,   // NoCollapse
        no_docking                 = 1u << 9,   // NoDocking
        menu_bar                   = 1u << 10,  // MenuBar
        no_title_bar               = 1u << 11,  // NoTitleBar
    };

    constexpr window_trait operator|(window_trait left, window_trait right) noexcept
    {
        return static_cast<window_trait>(
            static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
    }

    constexpr bool has_trait(window_trait set, window_trait one) noexcept
    {
        return 0u != (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(one));
    }

    // ── 표시 순서 ─────────────────────────────────────────────────────────
    //
    // 플래그가 아니라 `Begin` 직후에 부르는 함수다. 그래서 성질과 나눠 든다 —
    // 셸이 프레임을 소유하는 이상 이 호출도 셸의 몫이다.
    enum class window_stacking
    {
        normal,
        display_back,   // BringWindowToDisplayBack — Scene · Game
        focus_front,    // BringWindowToFocusFront + BringWindowToDisplayFront

        count,
    };

    inline constexpr std::array all_window_roles{
        window_role::central,
        window_role::panel,
        window_role::transient,
    };

    inline constexpr std::array all_dock_slots{
        dock_slot::center,
        dock_slot::right_upper,
        dock_slot::right_lower,
        dock_slot::bottom,
        dock_slot::floating,
    };

    inline constexpr std::array all_window_stackings{
        window_stacking::normal,
        window_stacking::display_back,
        window_stacking::focus_front,
    };

    // 열거자를 늘리고 배열을 안 늘리면 여기서 멈춘다. `editor.windows` 덤프와
    // "배선 안 된 자리 0" 게이트가 이 배열을 열거 원본으로 쓰므로, 누락은
    // 게이트의 눈을 멀게 한다.
    static_assert(all_window_roles.size() == static_cast<std::size_t>(window_role::count),
        "all_window_roles가 window_role 전부를 담지 않는다");
    static_assert(all_dock_slots.size() == static_cast<std::size_t>(dock_slot::count),
        "all_dock_slots가 dock_slot 전부를 담지 않는다");
    static_assert(all_window_stackings.size() == static_cast<std::size_t>(window_stacking::count),
        "all_window_stackings가 window_stacking 전부를 담지 않는다");

    constexpr const char* to_string(window_role role) noexcept
    {
        switch (role)
        {
        case window_role::central:   return "central";
        case window_role::panel:     return "panel";
        case window_role::transient: return "transient";
        default:                     return "?";
        }
    }

    constexpr const char* to_string(dock_slot slot) noexcept
    {
        switch (slot)
        {
        case dock_slot::center:      return "center";
        case dock_slot::right_upper: return "right_upper";
        case dock_slot::right_lower: return "right_lower";
        case dock_slot::bottom:      return "bottom";
        case dock_slot::floating:    return "floating";
        default:                     return "?";
        }
    }

    constexpr const char* to_string(window_stacking stacking) noexcept
    {
        switch (stacking)
        {
        case window_stacking::normal:       return "normal";
        case window_stacking::display_back: return "display_back";
        case window_stacking::focus_front:  return "focus_front";
        default:                            return "?";
        }
    }

    // ── 역할이 정하는 기본값 ──────────────────────────────────────────────
    //
    // 선언에 적지 않은 것은 역할이 답한다. 스물다섯 개를 이관하며 같은 값을
    // 스물다섯 번 적게 되면 그 값은 선언이 아니라 잡음이다.

    constexpr dock_slot default_dock_slot(window_role role) noexcept
    {
        switch (role)
        {
        case window_role::central:   return dock_slot::center;
        case window_role::panel:     return dock_slot::floating;
        case window_role::transient: return dock_slot::floating;
        default:                     return dock_slot::floating;
        }
    }

    constexpr window_trait default_traits(window_role role) noexcept
    {
        switch (role)
        {
        case window_role::central:
            // 중앙 뷰포트는 탭으로 자리를 지킨다. 끌어 옮기면 중앙 노드가 빈다.
            return window_trait::no_move | window_trait::no_bring_to_front_on_focus;
        case window_role::transient:
            // 일시 표시는 배치를 남기지 않는다 — Model loading이 이미 그렇게 쓴다.
            return window_trait::auto_resize | window_trait::no_saved_layout |
                   window_trait::no_docking;
        default:
            return window_trait::none;
        }
    }

    constexpr bool default_closable(window_role role) noexcept
    {
        // 중앙 뷰포트를 닫으면 다시 열 자리가 없다. 일시 표시는 스스로 사라진다.
        return window_role::panel == role;
    }

    constexpr bool default_persist_open(window_role role) noexcept
    {
        // 일시 표시의 열림 여부를 워크스페이스에 남기면 다음 실행에 유령이 뜬다.
        return window_role::transient != role;
    }
}
