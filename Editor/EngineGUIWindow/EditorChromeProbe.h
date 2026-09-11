#pragma once
// 크롬 스냅샷을 뜨는 쪽 (PHASE 21 W0 전반).
//
// 이 선언 하나만 밖으로 낸다. ImGui 를 아는 곳은 짝이 되는 `.cpp` 하나다 —
// M1 의 `EditorMenuDraw` 와 같은 가름이다.
//
// **PresentationThread 에서만 부른다.** 프레임의 모든 `Begin`/`End` 가 끝난 뒤,
// `ImGui::Render` 앞이어야 한다. 그때 도크 노드 rect 와 창의 도크 소속이 이번
// 프레임 값으로 확정돼 있다.

namespace editor
{
    /// 이번 프레임에 실제로 떴으면 참. 주기(`kCaptureIntervalFrames`)와 요청이
    /// 정하므로 대부분의 프레임에서 거짓이다.
    bool capture_chrome_snapshot();

    /// 방금 뜬 스냅샷에 draw data 계측을 얹는다. **`ImGui::Render()` 뒤에서만**
    /// 불러야 한다 — 그 앞에서는 `ImDrawData` 가 유효하지 않다. 바로 앞의
    /// `capture_chrome_snapshot()` 이 참을 돌려준 프레임에만 부른다.
    void capture_chrome_draw_totals(double ui_cpu_ms);
}
