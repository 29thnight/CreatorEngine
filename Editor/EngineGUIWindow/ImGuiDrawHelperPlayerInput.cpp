#include "ReflectionImGuiHelper.h"
#include "ExternUI.h"
#include "PlayerInput.h"
#include "InputActionManager.h"
#include "EditorPropertyRow.h"
#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */

namespace
{
	// 전용 드로어의 줄 배치 상태 (PHASE 21 W2-I4).
	editor::widgets::property_layout_state g_playerInputLayout{};
}

void ImGuiDrawHelperPlayerInput(PlayerInputComponent* playerInput)
{
	if (!playerInput) return;

	const editor::widgets::property_sheet sheet(g_playerInputLayout, { "Player Index", "Action Map" });
	ImGui::PushID(playerInput);
	ImGui::SetNextItemWidth(sheet.line("Player Index"));
	ImGui::InputInt("##PlayerIndex", &playerInput->controllerIndex);

	const std::string mapName = playerInput->m_actionMapName.empty()
		? std::string("None") : playerInput->m_actionMapName;
	ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
	const bool openMaps = ImGui::Button((mapName + "###ActionMap").c_str(), ImVec2(sheet.line("Action Map"), 0.f));
	ImGui::PopStyleVar();
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", mapName.c_str());
	if (openMaps)
	{
		ImGui::OpenPopup("selectMap");
	}

	if (ImGui::BeginPopup("selectMap"))
	{
		for (auto& actionMap : InputActionManagers->m_actionMaps)
		{
			ImGui::PushID(actionMap + 1);
			if (ImGui::MenuItem(actionMap->m_name.c_str()))
			{
				playerInput->SetActionMap(actionMap);
			}
			ImGui::PopID();
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}
