#include "DataSystem.h"
//#include "MaterialLoader.h"
#include "Model.h"	
#include <ppltasks.h>
#include <ppl.h>
#include "Banchmark.hpp"
#include <future>

ImGuiTextFilter DataSystem::filter;

bool HasImageFile(const file::path& directory)
{
	for (const auto& entry : file::directory_iterator(directory))
	{
		if (entry.is_regular_file()) // 파일인지 확인
		{
			std::string ext = entry.path().extension().string();
			if (ext == ".png" || ext == ".jpg")
			{
				return true; // 하나라도 있으면 바로 true 반환
			}
		}
	}
	return false; // 이미지 파일 없음
}

DataSystem::~DataSystem()
{
}

void DataSystem::Initialize()
{
}

void DataSystem::RenderForEditer()
{
	//static std::string selectedModel{};

	//ImGui::ContextRegister("Contents Browser", [&]()
	//{
	//	ImGuiID dockspace_id = ImGui::GetID("Contents Browser Space");

	//	filter.Draw("Search", 180.0f);
	//	// DockSpace 영역 생성 (0.0f, 0.0f는 현재 창의 가용 영역 전체 사용)
	//	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));

	//}, ImGuiWindowFlags_None);

	//ImGui::ContextRegister("3D Models", [&]()
	//{
	//	constexpr float tileSize = 128.0f;
	//	constexpr float padding = 10.0f;
	//	constexpr int tilesPerRow = 4;
	//	int count = 0;


	//	for (const auto& [key, model] : Models)
	//	{
	//		if (!filter.PassFilter(key.c_str()))
	//			continue;

	//		ImGui::BeginGroup();

	//		if (ImGui::ImageButton(key.c_str(), (ImTextureID)icon->m_pSRV, ImVec2(tileSize, tileSize)))
	//		{
	//			selectedModel = key;
	//			MeshEditorSystem->selectedModel = key;
	//		}

	//		if (ImGui::IsItemHovered()) 
	//		{
	//			selectedModel = key;
	//			MeshEditorSystem->selectedModel = key;
	//		}

	//		if (selectedModel == key)
	//		{
	//			ImGui::TextWrapped("[Selected]");
	//		}

	//		ImGui::TextWrapped("%s", key.c_str());

	//		ImGui::EndGroup();

	//		count++;
	//		if (count % tilesPerRow != 0)
	//		{
	//			ImGui::SameLine(0.0f, padding);
	//		}

	//		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
	//		{
	//			ImGui::Text("Drag to Scene : %s", selectedModel.c_str());
	//			if (Models[selectedModel] != dragDropModel)
	//			{
	//				dragDropModel = Models[selectedModel];
	//				ImGui::SetDragDropPayload("Model", &dragDropModel, sizeof(dragDropModel));
	//			}
	//			ImGui::EndDragDropSource();
	//		}

	//	}

	//});

	/*ImGui::ContextRegister("Billboards", [&]() 
	{
		constexpr float tileSize = 64.0f;
		constexpr float padding = 10.0f;
		constexpr int tilesPerRow = 4;
		int count = 0;

		for (const auto& [key, billboard] : Billboards)
		{
			if (!filter.PassFilter(key.c_str()))
				continue;

			ImGui::BeginGroup();
			if (ImGui::ImageButton(key.c_str(), (ImTextureID)icon.Get(), ImVec2(tileSize, tileSize)))
			{
				selectedModel = key;
			}
			if (ImGui::IsItemHovered())
			{
				selectedModel = key;
			}
			if (selectedModel == key)
			{
				ImGui::TextWrapped("[Selected]");
			}
			ImGui::TextWrapped("%s", key.c_str());
			ImGui::EndGroup();
			count++;
			if (count % tilesPerRow != 0)
			{
				ImGui::SameLine(0.0f, padding);
			}
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				ImGui::Text("Drag to Scene : %s", selectedModel.c_str());
				if (Billboards[selectedModel] != dragDropBillboard)
				{
					dragDropBillboard = Billboards[selectedModel];
					ImGui::SetDragDropPayload("Billboard", &dragDropBillboard, sizeof(dragDropBillboard));
				}
				ImGui::EndDragDropSource();
			}
		}
	});*/

	/*ImGui::ContextRegister("Materials", [&]()
	{
		constexpr float tileSize = 128.0f;
		constexpr float padding = 10.0f;
		constexpr int tilesPerRow = 4;
		int count = 0;

		for (const auto& [key, material] : Materials)
		{
			if (!filter.PassFilter(key.c_str()))
				continue;

			ImGui::BeginGroup();
			if (ImGui::ImageButton(key.c_str(), (ImTextureID)icon->m_pSRV, ImVec2(tileSize, tileSize)))
			{
				selectedModel = key;
			}
			if (ImGui::IsItemHovered())
			{
				selectedModel = key;
			}
			if (selectedModel == key)
			{
				ImGui::TextWrapped("[Selected]");
			}
			ImGui::TextWrapped("%s", key.c_str());
			ImGui::EndGroup();

			count++;
			if (count % tilesPerRow != 0)
			{
				ImGui::SameLine(0.0f, padding);
			}

			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
			{
				ImGui::Text("Drag to Scene : %s", selectedModel.c_str());
				if (Materials[selectedModel] != dragDropMaterial)
				{
					dragDropMaterial = Materials[selectedModel];
					ImGui::SetDragDropPayload("Material", &dragDropMaterial, sizeof(dragDropMaterial));
				}
				ImGui::EndDragDropSource();
			}
		}
	});*/
}

void DataSystem::MonitorFiles()
{
}

void DataSystem::LoadModels()
{
	file::path shaderpath = PathFinder::Relative("Models\\");
}

void DataSystem::LoadTextures()
{
}

void DataSystem::LoadMaterials()
{
}

void DataSystem::AddModel(const file::path& filepath, const file::path& dir)
{
	std::string name = file::path(filepath.filename()).replace_extension().string();

	if(Models[name])
	{
		return;
	}

	std::shared_ptr<Model> model;

	//ModelLoader::LoadFromFile(filepath, dir, &model, &animmodel);
	if (model)
	{
		Models[model->name] = model;
	}
	//if (animmodel)
	//{
	//	AnimatedModels[animmodel->name] = animmodel;
	//}
}