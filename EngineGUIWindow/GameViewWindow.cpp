#ifndef DYNAMICCPP_EXPORTS
#include "GameViewWindow.h"
#include "SceneRenderer.h"
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
		float imageWidth = imageHeight * DirectX11::DeviceStates->g_aspectRatio;

		if (imageWidth > availRegion.x) {
			imageWidth = availRegion.x;
			imageHeight = imageWidth / DirectX11::DeviceStates->g_aspectRatio;
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
			auto renderData = RenderPassData::GetData(camera);
			ImGui::Image((ImTextureID)renderData->m_renderTarget->m_pSRV, imageSize);
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}
#endif // !DYNAMICCPP_EXPORTS