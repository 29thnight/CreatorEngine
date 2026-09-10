#pragma once
// 창 배선 감사 (PHASE 21 M4 4단계 · 계획서 부록 B.3의 "고아 0").
//
// 표와 본문 보관소를 맞대 본다. 둘은 서로를 모르고 이름으로만 만나므로,
// 어느 한쪽에만 있는 이름이 곧 조용히 죽은 창이다.
//
// 방향이 둘인 것이 요점이다. 옛 `ImGuiRegister::GetContext`는 `operator[]`라
// 없는 이름을 읽으면 빈 항목을 만들어 넣었고, 그래서 오타 하나가 영영 비어
// 있는 창을 매 프레임 그리게 했다. 새 창구는 없는 이름에 아무 일도 하지
// 않는데, 그러면 이번에는 **아무 일도 일어나지 않아서** 오타가 안 보인다.
// 한 방향만 보면 결함이 한쪽 벽을 넘어 그대로 산다.
//
// 이 파일은 ImGui를 부르지 않는다 — 감사는 프레임 밖에서 돈다.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace editor
{
    struct window_audit
    {
        std::size_t declared{ 0 };
        std::size_t bound{ 0 };

        /// 본문은 걸렸는데 표에 그 이름이 없다. 그 본문은 영영 불리지 않는다.
        std::vector<std::string_view> orphan_bodies;

        /// 표에는 있는데 본문이 걸릴 자리가 비었다. 창은 등록됐지만 뜨지 않는다.
        /// 자유 함수로 직접 부르는 창(애니메이터 셋)은 여기 세지 않는다 —
        /// 그런 창은 보관소를 쓰지 않으므로 비어 있는 것이 정상이다.
        std::vector<std::string_view> bodyless_windows;

        /// 같은 안정 식별자가 두 번 선언됐다. 뒤엣것이 앞엣것을 가린다.
        std::vector<std::string_view> duplicate_ids;

        /// 아무 창도 가지 않는 도킹 자리. 배치가 빈 노드를 만든다.
        std::vector<std::string_view> empty_dock_slots;

        /// 넷 다 비어 있는가.
        bool clean() const noexcept
        {
            return orphan_bodies.empty() && bodyless_windows.empty() &&
                   duplicate_ids.empty() && empty_dock_slots.empty();
        }
    };

    /// 지금 표와 보관소를 맞대 본다. 표가 비어 있으면 전부 0이다.
    window_audit audit_declared_windows();

    /// 감사 결과를 사람이 읽는 한 덩어리로 만든다. TSV 표 뒤에 요약이 붙는다.
    std::string dump_window_audit();
}
