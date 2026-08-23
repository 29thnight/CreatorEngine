#include "ReflectionImGuiHelper.h"
#include "ExternUI.h"
#include "PlayerInput.h"
#include "InputActionManager.h"
#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */

void ImGuiDrawHelperPlayerInput(PlayerInputComponent* playerInput)
{
	if (playerInput)
	{

		ImGui::InputInt("Player Index", &playerInput->controllerIndex);

		ImGui::Text("Action Map");
		ImGui::SameLine();
		ImGui::PushID(playerInput);
		if (ImGui::Button(playerInput->m_actionMapName.c_str(), ImVec2(140, 0)))
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

}