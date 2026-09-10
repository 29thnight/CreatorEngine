#pragma once
// 애니메이터 편집 창 셋의 본문과 그 셋이 나눠 쓰는 편집 문맥 (PHASE 21 M4 3b).
//
// 옛 코드에서 셋은 `ImGuiDrawHelperAnimator(Animator*)` 안에 중첩된
// `ImGui::Begin` 이었고, 그리는 대상은 호출자의 지역 변수 `animator` 였다.
// 셸이 프레임을 소유하려면 본문이 셸에서 부를 수 있는 것이어야 하는데,
// 그러면 그 지역 변수가 사라진다.
//
// 그것을 **게시하지 않고 다시 유도한다.** 인스펙터가 그리던 Animator는
// 선택 개체의 Animator이므로 창 쪽에서 같은 것을 씬에 물어 얻을 수 있다.
// 포인터를 어딘가에 얹어 두면 게시 시점과 그리기 시점 사이에 개체가 사라질
// 수 있고, 그 창은 프레임마다 다시 물으므로 그런 틈이 없다.

class Animator;

namespace editor::animator_editing
{
    /// 지금 인스펙터가 그리고 있는 Animator. 없으면 널이고, 그것이 세 창의
    /// 존재 조건이다. 매 프레임 선택 상태에서 다시 유도한다.
    Animator* current();

    /// 이벤트 창이 편집하는 클립 번호. 인스펙터의 클립 목록 단추가 정한다.
    int selected_clip();
    void select_clip(int index);

    /// 아바타 마스크 창이 여는 컨트롤러 번호. 컨트롤러 창의 팝업이 정한다.
    /// 창 둘을 건너는 값은 이것 하나뿐이라 나머지 세션 상태는 본문에 남았다.
    int selected_avatar_controller();
    void select_avatar_controller(int index);
}

/// 셸이 창 본문으로 부른다. 셋 다 자기 대상이 없으면 곧바로 돌아간다 —
/// 존재 술어가 이미 걸러 주지만 본문이 스스로도 성립해야 한다.
void DrawAnimatorEventWindow();
void DrawAnimationControllersWindow();
void DrawAvatarMaskWindow();
