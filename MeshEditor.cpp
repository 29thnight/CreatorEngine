#include "MeshEditor.h"
#include "RenderEngine/DataSystem.h"
#include "InputManager.h"

void MeshEditor::ReadyToEdit()
{
    //if(2 < selectedModel.length() && AssetsSystem->Models[selectedModel])
    //{
    //    sceneRenderer->SetEditModel(AssetsSystem->Models[selectedModel]);
    //}
    //추후 추가 예정
	//else if (AssetsSystem->AnimatedModels[selectedModel])
	//{
	//	sceneRenderer->SetEditModel(AssetsSystem->AnimatedModels[selectedModel]);
	//}
	//else
	//{
	//	sceneRenderer->SetEditModel(nullptr);
	//}
}

void MeshEditor::CheckChangeMesh()
{
	if (_prevMeshId != _currMeshId)
	{
		_prevMeshId = _currMeshId;
	}

}

void MeshEditor::ShowMainUI()
{
	if(!show)
	{
		_prevMeshId = -1;
		_currMeshId = 0;
		selectedModel.clear();
		dragDropMaterial = nullptr;
		return;
	}

	ReadyToEdit();

	auto material = AssetsSystem2->GetPayloadMaterial();

	if (material && InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
	{
		dragDropMaterial = material;
		HandleConnection();
		AssetsSystem2->ClearPayloadMaterial();
	}

	//ImGui::Begin("ModelViewer");
	//ImGui::Image((ImTextureID)sceneRenderer->GetMeshEditorTarget()->GetSRV(),
	//	ImVec2(450, 560));
	//ImGui::End();

	/*ImGui::Begin("Meshs");
	if (!material)
	{
		ImGui::Text("Drag and Drop material");
	}
	if(2 < selectedModel.length() && AssetsSystem->Models[selectedModel])
	{
		int maxSize = AssetsSystem->Models[selectedModel]->meshes.size();
		if (ImGui::SmallButton("save"))
		{
			json j;
			file::path linkpath =
				AssetsSystem->Models[selectedModel]->path.parent_path() /
				std::string(AssetsSystem->Models[selectedModel]->name + ".meta");

			std::ofstream out(linkpath);
			for (int index = 0; index < maxSize; ++index)
			{
				j[AssetsSystem->Models[selectedModel]->meshes[index].name] =
					AssetsSystem->Models[selectedModel]->meshes[index].materialKey;
			}

			out << j.dump();
		}

		ImGui::SameLine();

		if (ImGui::SmallButton("close"))
		{
			show = false;
		}

		for (int index = 0; index < maxSize; ++index)
		{
			if (ImGui::Selectable(AssetsSystem->Models[selectedModel]->meshes[index].name.c_str()))
			{
				_currMeshId = index;
			}
		}
	}*/
	ImGui::End();

	CheckChangeMesh();
}

void MeshEditor::HandleConnection()
{
	/*if (2 < selectedModel.length() && AssetsSystem->Models[selectedModel])
	{
		AssetsSystem->Models[selectedModel]->meshes[_currMeshId].materialKey = dragDropMaterial->name;
		AssetsSystem->Models[selectedModel]->meshes[_currMeshId].material = dragDropMaterial;
	}*/
}
