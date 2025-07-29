#include "ExternUI.h"
#include "ModuleBehavior.h"
#include "DataSystem.h"
#include "ReflectionImGuiHelper.h"

void ImGuiDrawHelperModuleBehavior(ModuleBehavior* moduleBehavior)
{
	if (moduleBehavior)
	{
		ImGui::Text("Script		 ");
		ImGui::SameLine();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1.1f, 5.1f));
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
		if (ImGui::Button(moduleBehavior->GetHashedName().ToString().c_str(), ImVec2(250, 0)))
		{
			FileGuid guid = DataSystems->GetStemToGuid(moduleBehavior->GetHashedName().ToString());
			file::path scriptFullPath = DataSystems->GetFilePath(guid);
			if (scriptFullPath.empty())
			{
				Debug->LogError("Script not found: " + moduleBehavior->GetHashedName().ToString());
				ImGui::PopStyleVar(2);
				return;
			}

			file::path slnPath = PathFinder::DynamicSolutionPath("Dynamic_CPP.sln");

			DataSystems->OpenSolutionAndFile(slnPath, scriptFullPath);
		}
		ImGui::PopStyleVar(2);

		const Meta::Type* type = Meta::Find(moduleBehavior->m_name.ToString());
		if (type)
		{
			Meta::DrawProperties(moduleBehavior, *type);
			Meta::DrawMethods(moduleBehavior, *type);
		}
		else
		{
			ImGui::Text("Script type not found: %s", moduleBehavior->m_name.ToString().c_str());
		}
	}
}
