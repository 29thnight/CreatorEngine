#ifndef DYNAMICCPP_EXPORTS
#include "GameViewWindow.h"
#include "RHI/ScreenSizedResource.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "CameraComponent.h"
#include "IconsFontAwesome6.h"
#include "SceneManager.h"
#include "fa.h"

void GameViewWindow::RenderGameViewWindow()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin(ICON_FA_GAMEPAD "  Game        ", 0, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
	ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
	{
		ImVec2 windowPos = ImGui::GetWindowPos();
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

		auto scene = SceneManagers->GetRenderScene();
		auto camera = CameraManagement->GetLastCamera();
		if(nullptr == camera || 0 == camera->m_cameraIndex)
		{
			ImVec2 rectMin = ImVec2(windowPos.x + currentPos.x + offset.x, windowPos.y + currentPos.y + offset.y);
			ImVec2 rectMax = ImVec2(rectMin.x + imageSize.x, rectMin.y + imageSize.y);
			ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.2f, 1.0f)));

			const char* noCameraText = "No Camera rendering";
			ImVec2 textSize = ImGui::CalcTextSize(noCameraText);

			ImVec2 textPos = ImVec2(rectMin.x + (imageSize.x - textSize.x) * 0.5f, rectMin.y + (imageSize.y - textSize.y) * 0.5f);

			ImGui::SetCursorPos(ImVec2(currentPos.x + offset.x + (imageSize.x - textSize.x) * 0.5f, 
									   currentPos.y + offset.y + (imageSize.y - textSize.y) * 0.5f));
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), noCameraText);

			ImGui::SetCursorPos(ImVec2(currentPos.x, currentPos.y + imageSize.y)); // Reset cursor position after drawing text
		}
		else
		{
			ImTextureID displayed = 0;
			if (const uint64_t liveTextureId =
				EnhancedSceneRenderer::GetLiveDisplayImTextureId(camera.get()))
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
#endif // !DYNAMICCPP_EXPORTS
