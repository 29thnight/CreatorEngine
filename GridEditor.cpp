#include "GridEditor.h"
#include "RenderEngine/ImGuiRegister.h"

void GridEditor::ShowGridEditor()
{
	if (!show)
	{
		return;
	}

	ImGui::Text("GridEditor");

	ImGui::SetNextItemWidth(200);
	ImGui::InputInt("x", &x);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(200);
	ImGui::InputInt("y", &y);

	SetEditGrid();

	for (int j = 0; j < gridY; j++) {
		for (int i = 0; i < gridX; i++) {
			ImGui::PushID(gridX*j+i);
			if (ImGui::Button(std::to_string(_worldGrid[i][j]).c_str())) {
				bool flag = (bool)_worldGrid[i][j];
				_worldGrid[i][j] = !flag;
			}
			ImGui::PopID();
			ImGui::SameLine();
		}
		ImGui::Text("");
	}

	if(ImGui::Button("Save")) {
		_world->SetGrid(gridX, gridY,_worldGrid);
	}
		

}

void GridEditor::SetEditGrid()
{
	if ((x!= gridX||y!= gridY)&&(x!=0||y!=0))
	{
		gridX = x;
		gridY = y;

		_worldGrid.resize(gridX);
		for (int i = 0; i < gridX; i++)
		{
			_worldGrid[i].resize(gridY);
			for (int j = 0; j < gridY; j++)
			{
				_worldGrid[i][j] = 0;
			}
		}
	}
	
}
