#include "GameViewWindow.h"
#include "RHI/ScreenSizedResource.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "IconsFontAwesome6.h"
#include "fa.h"

void GameViewWindow::RenderGameViewWindow()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin(ICON_FA_GAMEPAD "  Game        ", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
	{
		ImVec2 availRegion = ImGui::GetContentRegionAvail();

		float imageHeight = availRegion.y;
		float imageWidth = imageHeight * ScreenResizeBus::Get().GetAspectRatio();

		if (imageWidth > availRegion.x) {
			imageWidth = availRegion.x;
			imageHeight = imageWidth / ScreenResizeBus::Get().GetAspectRatio();
		}

		ImVec2 imageSize = ImVec2(imageWidth, imageHeight);
		ImVec2 offset = ImVec2((availRegion.x - imageSize.x) * 0.5f, (availRegion.y - imageSize.y) * 0.5f);
		ImVec2 currentPos = ImGui::GetCursorPos();
		ImGui::SetCursorPos(ImVec2(currentPos.x + offset.x, currentPos.y + offset.y));

		const EnhancedLiveDisplaySnapshot displaySnapshot =
			EnhancedSceneRenderer::GetLiveDisplaySnapshot();
		const EnhancedLiveDisplayEntrySnapshot& gameDisplay =
			displaySnapshot.Get(EnhancedLiveDisplayTarget::Game);
		if (!gameDisplay.active)
		{
			const ImVec2 rectMin = ImGui::GetCursorScreenPos();
			const ImVec2 rectMax{ rectMin.x + imageSize.x, rectMin.y + imageSize.y };
			ImGui::InvisibleButton("##GameViewNoCamera", imageSize);
			ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax,
				ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.0f)));

			const char* noCameraText = "No Camera rendering";
			const ImVec2 textSize = ImGui::CalcTextSize(noCameraText);
			const ImVec2 textPos{
				rectMin.x + (imageSize.x - textSize.x) * 0.5f,
				rectMin.y + (imageSize.y - textSize.y) * 0.5f };
			ImGui::GetWindowDrawList()->AddText(textPos,
				ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f)), noCameraText);
		}
		else
		{
			ImTextureID displayed = 0;
			if (const uint64_t liveTextureId =
				EnhancedSceneRenderer::GetLiveDisplayImTextureId(
					EnhancedLiveDisplayTarget::Game))
			{
				displayed = (ImTextureID)liveTextureId;
			}
			if (displayed != 0)
			{
				ImGui::Image(displayed, imageSize);
			}
			else
			{
				const ImVec2 rectMin = ImGui::GetCursorScreenPos();
				const ImVec2 rectMax{ rectMin.x + imageSize.x, rectMin.y + imageSize.y };
				ImGui::InvisibleButton("##EnhancedGameViewPending", imageSize);
				ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax,
					ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.09f, 1.f)));
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}
