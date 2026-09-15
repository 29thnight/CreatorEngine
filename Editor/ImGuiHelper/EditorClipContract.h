#pragma once
// PHASE 21 W2-2 — 잘라 그리기 계약을 **잴 수 있게** 만든다.
//
// 계획서 §7.1 의 같은 문장에서 이번엔 두 절이다:
//
//   *"custom widget은 ImGui ID, nav, focus, disabled, clipping, tooltip,
//     testability를 보존해야 하며 …"*
//
// `clipping` 과 `tooltip` 은 **한 규칙의 양쪽**이다. 자기 칸보다 넓은 글자는
// 잘라 그려야 하고(안 자르면 옆 칸의 버튼·아이콘 위로 넘어가 둘 다 안 읽힌다),
// 자른 줄은 전체를 tooltip 으로 돌려줘야 한다(안 주면 이름을 영영 못 읽는다).
// 자르기만 하고 돌려주지 않으면 정보가 조용히 사라진다 — 그것이 이 장부가
// `overflow` 와 나란히 `silent` 를 세는 이유다.
//
// ── 규칙은 이미 서 있었다. 한 위젯만 알고 있었을 뿐이다 ──────────────────
//
// `EditorPropertyRow` 는 `CalcTextSize` 로 폭을 재고, 넘치면 `PushClipRect` 로
// 자르고, 잘린 줄에 `SetTooltip` 으로 전체 이름을 준다. 완성된 규칙이다.
// 그런데 형제 셋은 그 규칙의 일부만 안다(2026-09-15 실측):
//
//   · `EditorInspectorPanel` — 자르기는 한다. **tooltip 이 없다.** 주석에는
//     "오른쪽 버튼 위로 넘어가면 둘 다 안 읽힌다" 고 적어 두었으면서, 잘라 낸
//     이름을 돌려줄 길은 만들지 않았다.
//   · `EditorSectionHeader` — **둘 다 없다.** 컴포넌트 이름이 길면 오른쪽
//     더보기 버튼 위로 그대로 그려진다. 글자가 버튼보다 나중에 그려지므로
//     버튼을 덮는다.
//   · `EditorModeButton` — 아이콘을 상자 가운데에 자르지 않고 놓는다.
//
// 기준을 새로 지어내지 않는다. **이미 서 있는 구현**이 정본이다.
//
// ── 왜 장부인가 ───────────────────────────────────────────────────────────
//
// "자르는 줄이 있다" 는 소스에서도 센다. 그러나 소스는 **그 조건이 참이 되는
// 상황이 실제로 오는지** 모른다. 칸이 넓으면 자를 일이 없고, 그러면 자르는
// 분기는 한 번도 안 돌면서 초록이다 — 이 저장소가 여러 번 겪은 죽은 분기다.
// 그래서 위젯이 잴 때마다 신고하고, 판정은 **신고된 수**로 한다.
//
//   ① 위젯이 글자를 그리기 직전에 `announce_text` 로 잰 값을 신고한다:
//      글자 폭, 쓸 수 있는 폭, 잘랐는가, tooltip 을 줄 수 있는가.
//   ② 장부가 그 자리에서 판정한다.
//        · 넓은데 안 잘랐다      → `overflowFrames`  (옆 칸을 덮는다)
//        · 잘랐는데 tooltip 이 없다 → `silentFrames`  (이름이 사라진다)
//        · 넓어서 잘랐고 tooltip 도 준다 → `truncated` (정상. 수만 남긴다)
//   ③ 한 번도 좁아지지 않았으면 `truncated` 가 0 이다. 게이트는 그 수를
//      **먼저** 보고 "자극하지 못했다" 를 구별한다 — W2-1 과 같은 규율이다.
//
// ── 클립 스택은 균형이 맞아야 한다 ────────────────────────────────────────
//
// `PushClipRect` 를 하고 조기 반환하면 그 뒤의 **모든 그리기**가 좁은 사각형에
// 갇힌다. 증상은 그 위젯이 아니라 한참 뒤에서 나타나므로 눈으로는 원인을 못
// 찾는다. 위젯이 자기 호출 앞뒤의 스택 깊이를 신고하면 그 자리에서 잡힌다.
//
// ── 스레드 ────────────────────────────────────────────────────────────────
//
// 신고와 프레임 관측은 ImGui(Presentation) 스레드, 읽기와 리셋은 게임 스레드의
// CLI 다. `editor::nav` 와 같은 규약으로 게시본만 잠금으로 가른다.

// ★ 이 헤더도 ImGui 를 끌어오지 않는다 — 이유는 `EditorNavContract.h` 와 같다.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::clipping
{
    // 이름을 몇 개까지 기억할지. 판정은 수로 하고 이름은 진단용이다.
    inline constexpr std::size_t kTrackedNames = 16;

    struct contract_view
    {
        std::uint64_t frames{0};
        std::uint64_t announced{0};

        // 넘치는데 안 잘랐다 — 옆 칸을 덮는다.
        std::uint64_t overflowFrames{0};
        std::vector<std::string> overflowWidgets;

        // 잘랐는데 전체를 돌려줄 길이 없다 — 이름이 조용히 사라진다.
        std::uint64_t silentFrames{0};
        std::vector<std::string> silentWidgets;

        // 넘쳐서 잘랐고 tooltip 도 준다. 정상이지만 **수를 남긴다** —
        // 이 수가 0 이면 좁은 상황을 한 번도 안 만든 것이라 위 둘의 0 은
        // 아무 뜻이 없다.
        std::uint64_t truncated{0};
        std::vector<std::string> truncatedWidgets;

        // 잴 기회가 온 위젯들. 자극이 닿았는지를 게이트가 먼저 본다.
        std::vector<std::string> measuredWidgets;

        // 클립 스택이 균형을 잃은 횟수.
        std::uint64_t unbalanced{0};
        std::vector<std::string> unbalancedWidgets;
    };

    // 글자를 그리기 직전에 부른다.
    //   text_width   : `CalcTextSize` 로 잰 글자 폭
    //   column_width : 그 글자가 쓸 수 있는 폭
    //   clipped      : 이 호출이 클립을 걸었는가
    //   tooltip      : 잘렸을 때 전체를 돌려줄 길이 있는가
    void announce_text(const char* widget, float text_width, float column_width,
        bool clipped, bool tooltip);

    // 클립 스택 깊이를 신고한다. 위젯이 자기 본문 앞뒤에서 한 번씩 부른다.
    void enter_widget(const char* widget, int clip_stack_depth);
    void leave_widget(const char* widget, int clip_stack_depth);

    // 프레임 끝(EditorRenderer::EndRender).
    void observe_frame();

    contract_view read();
    void reset_counts();
}
