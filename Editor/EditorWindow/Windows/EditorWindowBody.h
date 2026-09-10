#pragma once
// 창 본문 보관소 (PHASE 21 M4 2·3단계 · 계획서 부록 B.3).
//
// 계획서가 정한 대로 **이관은 프레임만 옮긴다.** 본문 코드는 창 클래스가 있던
// 자리에 그대로 두고 `Begin`과 `End`만 셸이 가져간다. 그 사이를 잇는 것이 여기다 —
// 창 클래스의 생성자가 자기 본문을 이름으로 걸고, 선언의 진입점이 그것을 부른다.
//
// ── 왜 진입점을 창마다 한 쌍씩 적는가 ─────────────────────────────────────
//
// 셸이 부르는 것은 함수 포인터다. 안정 식별자를 비타입 템플릿 인자로 넘길 수
// 없어서(문자열은 구조적 타입이 아니다) `draw_*`/`has_*` 한 쌍을 창마다 적는다.
// 부록 A가 상단 저장소를 둘로 나눌 수밖에 없었던 것과 같은 제약이다.
//
// ── std::function 이 여기 남는 이유 ───────────────────────────────────────
//
// 옛 `ContextRegister`가 이미 쓰던 그대로다. 이관이 타입소거를 새로 들이지
// 않는다는 뜻이고, 본문을 캡처 없는 함수로 분해하는 것은 별건이다. 표(registry)
// 쪽은 함수 포인터만 들므로 CT6-a가 걷어낸 타입소거가 되살아나지 않는다.

#include <functional>
#include <string_view>

namespace editor::windows
{
    /// 창 클래스의 생성자가 자기 본문을 건다. 옛 `ContextRegister` 자리를
    /// 그대로 대신하고, 이름도 그때 쓰던 것을 그대로 쓴다.
    void bind_window_body(std::string_view stable_id, std::function<void()> body);

    /// 소멸자가 부른다. 걸지 않은 이름을 풀어도 아무 일도 일어나지 않는다.
    void unbind_window_body(std::string_view stable_id);

    namespace detail
    {
        /// 걸린 본문을 부른다. 걸리지 않았으면 아무 일도 하지 않는다.
        void run_window_body(std::string_view stable_id);

        /// 본문이 걸려 있는가. 선언의 `available` 술어가 이것을 답한다 —
        /// 창 객체가 아직 없거나 이미 사라졌으면 프레임을 열지 않는다.
        bool window_body_bound(std::string_view stable_id);
    }
}

/// 진입점 한 쌍을 정의한다. 헤더에는 두 줄을 손으로 적고(grep 가능해야 한다)
/// 정의만 이 매크로가 편다.
#define EDITOR_DEFINE_WINDOW_ENTRY(suffix, name_expr)                        \
    void draw_##suffix() { ::editor::windows::detail::run_window_body(name_expr); } \
    bool has_##suffix()  { return ::editor::windows::detail::window_body_bound(name_expr); }
