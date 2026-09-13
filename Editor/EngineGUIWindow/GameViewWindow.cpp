#include "GameViewWindow.h"
#include "RHI/ScreenSizedResource.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "EditorIcons.h"
#include "EditorViewportCanvas.h"
#include "EditorWindowNames.h"
#include "Windows/EditorViewportWindows.h"

// PHASE 21 M4 3단계: 프레임은 셸이 연다. 창 여백·창 성질 둘·표시 순서가
// 선언으로 갔다(EditorViewportWindows.h). 여기에는 그림 하나만 남는다.
//
// W4: 자리 계산을 `LayoutViewportCanvas` 에 넘겼다. 이 본문은 두 곳에서 돈다 —
// 가운데 Host 의 Game 모드와 선택적 Game Preview 패널이다. 둘이 다른 크기의
// content 를 주므로 자리 계산이 여기 있으면 규약이 다시 갈린다(§1.5).
//
// ★ 종횡비의 출처도 함께 바뀌었다. 옛 코드는 `ScreenResizeBus` 의 전역 종횡비를
//   읽었는데, 그것은 **창**의 비율이지 게임 타깃 프레임버퍼의 비율이 아니다. 둘이
//   갈리면 letterbox 가 맞는 비율로 맞춰도 그림이 늘어난다. 캔버스 규약 ②대로
//   소스 픽셀에서 낸다.
void editor::windows::draw_game_view()
{
	const ImVec2 contentMin = ImGui::GetCursorScreenPos();
	const ImVec2 contentSize = ImGui::GetContentRegionAvail();
	if (contentSize.x <= 0.f || contentSize.y <= 0.f) return;

	const auto displayed =
		EnhancedSceneRenderer::GetLiveDisplayTexture(EnhancedLiveDisplayTarget::Game);
	const editor::ViewportCanvas canvas = editor::LayoutViewportCanvas(
		editor::viewport_fit::letterbox, contentMin, contentSize,
		{ static_cast<float>(displayed.width), static_cast<float>(displayed.height) },
		ImGui::GetIO().DisplayFramebufferScale);

	// 캔버스를 자리만 잡고 ImGui 의 활성/호버 항목은 가져가지 않는다 — Scene 이
	// 기즈모를 위해 그렇게 하고, Game 도 W5 가 입력 소유권을 붙일 자리다.
	ImGui::Dummy(contentSize);
	auto* draw = ImGui::GetWindowDrawList();

	// 2단 신호를 그대로 쓴다(계획서 W4). `active` 는 "그릴 카메라가 있는가",
	// 텍스처 ID 는 "그림이 준비됐는가" 다 — 둘은 다른 프레임에 참이 된다.
	const EnhancedLiveDisplaySnapshot displaySnapshot =
		EnhancedSceneRenderer::GetLiveDisplaySnapshot();
	const EnhancedLiveDisplayEntrySnapshot& gameDisplay =
		displaySnapshot.Get(EnhancedLiveDisplayTarget::Game);

	// 그림이 없을 때의 자리는 content 전체다. 캔버스가 무효인 경우(타깃이 아직
	// 0×0)도 여기로 온다 — 사각형을 억지로 만들지 않는다.
	const ImVec2 fallbackMax{ contentMin.x + contentSize.x, contentMin.y + contentSize.y };
	const ImVec2 rectMin = canvas.valid ? canvas.clipMin : contentMin;
	const ImVec2 rectMax = canvas.valid ? canvas.clipMax : fallbackMax;

	if (!gameDisplay.active)
	{
		draw->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.f)));
		const char* noCameraText = "No Camera rendering";
		const ImVec2 textSize = ImGui::CalcTextSize(noCameraText);
		const ImVec2 textPos{
			rectMin.x + (rectMax.x - rectMin.x - textSize.x) * .5f,
			rectMin.y + (rectMax.y - rectMin.y - textSize.y) * .5f };
		draw->AddText(textPos, ImGui::GetColorU32(ImVec4(1.f, 0.f, 0.f, 1.f)), noCameraText);
		return;
	}

	if (canvas.valid && displayed.textureId)
	{
		draw->AddImage(displayed.textureId, canvas.clipMin, canvas.clipMax,
			canvas.uvMin, canvas.uvMax);
		return;
	}
	draw->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.09f, 1.f)));
}
