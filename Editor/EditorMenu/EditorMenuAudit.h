#pragma once
// 메뉴 배선 감사 (PHASE 21 M2 · 계획서 부록 A.5).
//
// ── 이 감사가 보지 못하는 것을 먼저 적는다 ────────────────────────────────
//
// A.5 는 게이트 셋을 예정했다. 그중 **둘은 런타임이 볼 수 없다.**
//
// ① "목록에서 선언자를 빼면 그 항목만 사라진 것을 잡는다" — 목록이 등록의
//    **유일한 출처**이므로, 줄을 지우면 표도 기대치도 같이 줄어든다. 프로그램 안에
//    기준이 없다. 그래서 여기서는 `expected_declarers`(중앙 목록이 컴파일 타임에
//    내놓는 이름 배열)와 표에 실제로 실린 선언자를 맞대 보는 것까지만 한다 —
//    **항목을 0개 낸 선언자**는 잡힌다. 목록에서 줄을 빼는 것은 게이트 쪽에서
//    "선언자 0 이면 붉다"로 막는다(빈 집합을 성공으로 읽지 않는다).
//
// ② "`popup_host` 열거자마다 그리는 자리가 하나 이상 있고 그 역도 성립한다" —
//    그리는 자리는 C++ 호출 지점이라 실행 중에 세어지지 않는다. 팝업이 실제로
//    열려야 한 번 도는 코드이기 때문이다. 이것은 **소스 대조 게이트**로 옮겼다:
//    `verify-editor-menu-wiring.ps1` 이 `EditorMenuSurface.h` 의 열거자와 트리
//    전체의 `draw_popup_menu_items<...::popup_host::X>` 호출을 양쪽에서 뽑아
//    맞댄다. 양쪽이 다 소스에서 유도되므로 손으로 적은 목록이 없다.
//
// 런타임이 볼 수 있는 것만 여기 있다. 못 보는 것을 침묵으로 덮지 않는 것까지가
// 감사의 몫이다.

#include "EditorMenuRegistry.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
    struct menu_audit
    {
        std::size_t top_items{ 0 };
        std::size_t popup_items{ 0 };
        std::size_t declarers_seen{ 0 };
        std::size_t declarers_expected{ 0 };

        /// 같은 표면 안에서 같은 하위 경로가 두 번. 뒤엣것이 앞엣것과 같은 자리를
        /// 다투고, 어느 쪽이 눌렸는지 사용자가 알 수 없다. "표면:경로" 형태.
        std::vector<std::string> path_conflicts;

        /// 하위 경로가 빈 항목. 그리는 쪽이 "(unnamed)" 로 대신 그리므로 화면에서
        /// 사라지지는 않지만, 이름 없는 메뉴가 서는 것은 선언 쪽 실수다.
        std::vector<std::string> unnamed_items;

        /// 중앙 목록에 있는데 항목을 하나도 내지 않은 선언자. `for_editor()` 가 빈
        /// 묶음을 돌려주거나, 실릴 자리가 없어 조용히 사라진 경우다.
        std::vector<std::string> silent_declarers;

        bool clean() const noexcept
        {
            return path_conflicts.empty() && unnamed_items.empty() &&
                   silent_declarers.empty() && (declarers_seen == declarers_expected);
        }
    };

    /// `expected_declarers` 는 중앙 목록이 내놓는 이름 배열
    /// (`editor_menu_declarer_names`). 감사가 그 헤더를 직접 include 하지 않는
    /// 이유는, 그러면 감사가 제품 선언자 전부에 딸려 붙게 되기 때문이다.
    menu_audit audit_declared_menus(std::span<const std::string_view> expected_declarers);

    /// 조립된 메뉴 표를 TSV 로 낸다(`commands.list` 와 같은 골든 형태).
    /// 열: surface · root_or_host · sub_path · action_id · shortcut · order ·
    ///     has_enabled · confirm · declarer
    std::string dump_menu_table();

    /// 감사 결과를 사람이 읽는 줄로 낸다.
    std::string dump_menu_audit(std::span<const std::string_view> expected_declarers);
}
