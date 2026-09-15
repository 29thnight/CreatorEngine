#pragma once
// PHASE 21 W2-3 — 상태 행렬을 **데이터로** 고정한다.
//
// 계획서 W2 의 요구는 한 줄이다:
//
//   *"hover/active/focus/nav/disabled/mixed/error 상태 matrix를 고정한다."*
//
// ── "고정한다" 를 문장으로 적으면 고정되지 않는다 ─────────────────────────
//
// 지금까지 이 행렬은 어디에도 없었다. 있는 것은 위젯 둘의 색 열거
// (`mode_button_surface`, `section_header_surface`)뿐이고, 그나마 둘의 항목이
// 서로 다르며(Idle/Hovered/Held 대 Closed/Open/Hovered/Held) 나머지 넷은 그런
// 열거조차 없다. 일곱 상태를 한자리에서 볼 수단이 0 이다.
//
// 그래서 행렬을 **선언**으로 만든다. 위젯마다 "나는 이 상태들을 구분한다" 를
// 코드로 적고(`declare`), 매 프레임 "지금 이 상태다" 를 신고한다(`announce`).
// 판정은 선언과 관측을 맞대는 것이다 — 선언했는데 한 번도 관측되지 않은 상태는
// **죽은 분기이거나 자극하지 못한 것**이고, 게이트가 둘을 갈라 이름으로 찍는다.
//
// ── 일곱 중 둘은 자가 없는 게 아니라 대상이 없다 ─────────────────────────
//
// 2026-09-15 실측:
//
//   · `mixed`  — 씬에는 다중 선택이 있는데(`m_selectedEntities`) **Inspector 는
//     `m_selectedEntity` 하나만 그린다.** 값이 갈리는 상황이 위젯에 오지 않는다.
//     구현이 없는 것이 아니라 원천이 안 온다. W2-I3(중첩/배열·공유 소비자)의 몫.
//   · `error`  — 원천 자체가 없다. 인스펙터에 값 검증이라는 것이 없다.
//
// 이 둘은 `not_applicable` 로 **선언하고 이유를 남긴다.** 목록에서 지우면 다음
// 사람이 "일곱 중 다섯만 있네" 를 다시 발견해야 한다.
//
// ── 자극 ──────────────────────────────────────────────────────────────────
//
// `hover`·`active` 는 포인터가 있어야 관측된다. 그 표면이 0 이었으므로
// `editor::nav` 에 포인터 주입을 더했다(`request_pointer`) — 키 주입과 같은
// 자리, 같은 규약이다. 어디로 옮길지는 이 장부가 낸 **위젯의 사각형**이 말해
// 준다: 게이트가 장부에서 자리를 읽고 그 자리로 포인터를 옮긴다.
//
// ── 스레드 ────────────────────────────────────────────────────────────────
//
// 신고·관측은 ImGui(Presentation) 스레드, 읽기·리셋은 게임 스레드의 CLI 다.
// `editor::nav`·`editor::clipping` 과 같은 규약으로 게시본만 잠금으로 가른다.

#include <cstdint>
#include <string>
#include <vector>

struct ImRect;

namespace editor::state
{
    // 계획서가 적은 일곱. 순서가 곧 출력 순서다.
    enum : std::uint32_t
    {
        hover    = 1u << 0,
        active   = 1u << 1,
        focus    = 1u << 2,
        nav      = 1u << 3,
        disabled = 1u << 4,
        mixed    = 1u << 5,
        error    = 1u << 6,
        all      = 0x7fu,
    };

    /// 상태 하나의 이름. 모르는 비트면 nullptr.
    const char* name_of(std::uint32_t bit) noexcept;
    /// 이름 → 비트. 모르면 0.
    std::uint32_t bit_of(const char* name) noexcept;

    struct widget_view
    {
        std::string widget;
        std::uint32_t declared{0};       // 이 위젯이 구분한다고 선언한 상태
        std::uint32_t notApplicable{0};  // 이 위젯에 올 수 없다고 선언한 상태
        std::string reason;              // notApplicable 의 이유
        std::uint32_t observed{0};       // 실제로 한 번이라도 관측된 상태
        std::uint64_t frames{0};         // 이 위젯이 그려진 프레임 수
        float x0{0.f}, y0{0.f}, x1{0.f}, y1{0.f}; // 마지막으로 그려진 자리
    };

    struct contract_view
    {
        std::uint64_t frames{0};
        std::uint64_t announced{0};
        std::vector<widget_view> widgets;
    };

    /// 위젯이 자기 행렬을 선언한다. 같은 이름으로 여러 번 불러도 한 번만 산다.
    ///   declared      — 구분한다(관측되어야 한다)
    ///   notApplicable — 이 위젯에는 올 수 없다. 이유를 반드시 남긴다.
    void declare(const char* widget, std::uint32_t declared,
        std::uint32_t notApplicable, const char* reason);

    /// 매 프레임, 그리기 직전에 지금 상태를 신고한다.
    void announce(const char* widget, std::uint32_t states, const ImRect& bounds);

    void observe_frame();
    contract_view read();
    void reset_counts();
}
