// 애니메이터 인스펙터 본문 (PHASE 21 M4 3b 뒤).
//
// 편집 창 셋(Event · Animation Controllers · AvatarMask)의 본문은 여기 있지
// 않다 — 셸이 프레임을 소유하므로 AnimatorEditorWindows.cpp로 갔다. 여기
// 남은 것은 인스펙터 안에 그리는 접힘머리와, 그 창들을 여는 단추뿐이다.

#include "EditorObjectOperations.h"
#include "ReflectionImGuiHelper.h"
#include "ReflectionTypedDraw.h"
#include "ClrHost.h"
#include "Animator.h"
#include "NodeEditor.h"
#include "AnimationController.h"
#include "EditorIcons.h"
#include "ExternUI.h"
// DataSystems가 여기 있다. 유니티 빌드에서는 앞선 파일이 공급했다.
#include "DataSystem.h"
#include "AnimatorEditorWindows.h"
#include "EditorWindowNames.h"
#include "EditorWindowRegistry.h"
#include "EditorPropertyRow.h"
#include "imgui_stdlib.h"

namespace
{
    // 창 하나를 여닫는 단추 하나. 옛 코드가 `bool = !bool` 로 쓰던 자리다.
    void ToggleAnimatorWindow(const char* window)
    {
        if (::editor::is_window_open(window)) ::editor::close_window(window);
        else                                  ::editor::open_window(window);
    }

    // 전용 드로어의 줄 배치 상태 (PHASE 21 W2-I4). 인스펙터는 한 스레드에서 그린다.
    editor::widgets::property_layout_state g_animatorLayout{};
}

void ImGuiDrawHelperAnimator(Animator* animator)
{
	if (!animator) return;

	const auto& aniType = Meta::Find(animator->GetTypeID());
	Meta::TypedDraw::DrawOwnMembers(*animator);
	Meta::DrawMethods(animator, *aniType);

	const editor::widgets::property_sheet sheet(g_animatorLayout, { "Clip", "Loop", "Key Frame Event", "Controllers" });
	if (ImGui::CollapsingHeader("animations"))
	{
		// I5-D5b — 열거·이름도 창구를 지난다. D4e-2가 편집을 Animator
		// 소유로 옮겼으나 목록은 공유 자산을 직접 훑고 있었다 — 인덱스
		// 축이 두 출처로 갈리면 편집 정본과 표시 대상이 어긋난다.
		const int clipCount = static_cast<int>(animator->GetClipCount());
		for (int i = 0; i < clipCount; ++i)
		{
			std::string clipName = animator->GetClipName(i);
			// 이름이 같은 클립이 둘일 수 있다 — ID 는 인덱스로 묶는다.
			ImGui::PushID(i);
			ImGui::SetNextItemWidth(sheet.line("Clip"));
			ImGui::InputText("##ClipName", &clipName, ImGuiInputTextFlags_ReadOnly);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", clipName.c_str());

			// I5-D4e-2 — 루프·이벤트 편집의 정본은 Animator 클립
			// 오버라이드다. 구 코드는 공유 자산(m_Skeleton->m_animations)을
			// 직접 편집해 같은 스켈레톤을 공유하는 다른 Animator까지
			// 바뀌었다 — 발화·저장 정본과 편집 표면을 함께 옮긴다.
			bool looping = animator->IsClipLooping(i);
			sheet.line("Loop");
			if (ImGui::Checkbox("##Loop", &looping))
			{
				animator->SetClipLooping(i, looping);
			}
			const bool openEvents = ImGui::Button("Edit###KeyFrameEvent", ImVec2(sheet.line("Key Frame Event"), 0.f));
			ImGui::PopID();
			if (openEvents)
			{
				::editor::animator_editing::select_clip(i);
				ToggleAnimatorWindow(EditorWindowName::kAnimatorEvent);
			}
			ImGui::Separator();
		}
	}

	if (ImGui::Button("Edit###AnimationControllers", ImVec2(sheet.line("Controllers"), 0.f)))
	{
		ToggleAnimatorWindow(EditorWindowName::kAnimationControllers);
	}
}
