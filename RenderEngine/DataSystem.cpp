#include "DataSystem.h"
#include "../MeshEditor.h"
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
	RemoveShaders();
}

void DataSystem::Initialize()
{
	/*m_DataThread = std::thread(&DataSystem::MonitorFiles, this);
	m_DataThread.detach();
	RenderForEditer();
	LoadShaders();
	LoadTextures();
	LoadMaterials();
	LoadBillboards();
	LoadModels();
	AddModel(PathFinder::Relative("Models\\plane\\plane.fbx"), 
		PathFinder::Relative("Models\\plane"));*/
}

void DataSystem::RenderForEditer()
{
	static std::string selectedModel{};

	ImGui::ContextRegister("Contents Browser", [&]()
	{
		ImGuiID dockspace_id = ImGui::GetID("Contents Browser Space");

		filter.Draw("Search", 180.0f);
		// DockSpace 영역 생성 (0.0f, 0.0f는 현재 창의 가용 영역 전체 사용)
		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));

	}, ImGuiWindowFlags_None);

	ImGui::ContextRegister("3D Models", [&]()
	{
		constexpr float tileSize = 128.0f;
		constexpr float padding = 10.0f;
		constexpr int tilesPerRow = 4;
		int count = 0;


		for (const auto& [key, model] : Models)
		{
			if (!filter.PassFilter(key.c_str()))
				continue;

			ImGui::BeginGroup();

			if (ImGui::ImageButton(key.c_str(), (ImTextureID)icon->m_pSRV, ImVec2(tileSize, tileSize)))
			{
				selectedModel = key;
				MeshEditorSystem->selectedModel = key;
			}

			if (ImGui::IsItemHovered()) 
			{
				selectedModel = key;
				MeshEditorSystem->selectedModel = key;
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
				if (Models[selectedModel] != dragDropModel)
				{
					dragDropModel = Models[selectedModel];
					ImGui::SetDragDropPayload("Model", &dragDropModel, sizeof(dragDropModel));
				}
				ImGui::EndDragDropSource();
			}

		}

	});

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

	ImGui::ContextRegister("Materials", [&]()
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
	});
}

void DataSystem::MonitorFiles()
{
	std::array<std::future<void>, 4> futures;

	while (true)
	{
		uint32 modelcount = 0;
		uint32 shadercount = 0;
		uint32 texturecount = 0;
		uint32 materialcount = 0;
		uint32 billboardcount = 0;
		auto shaderfunc = [&]() 
		{
			try
			{
				file::path shaderpath = PathFinder::Relative("Shaders\\");
				for (auto& dir : file::recursive_directory_iterator(shaderpath))
				{
					if (dir.is_directory())
						continue;

					if (dir.path().extension() == ".hlsl")
					{
						shadercount++;
					}
				}
			}
			catch (const file::filesystem_error& e)
			{
				Log::Warning("Could not load shaders" + std::string(e.what()));
			}
			catch (const std::exception& e)
			{
				Log::Warning("Error" + std::string(e.what()));
			}
		};

		auto modelfunc = [&]()
		{
			try
			{
				file::path modelpath = PathFinder::Relative("Models\\");
				for (auto& dir : file::recursive_directory_iterator(modelpath))
				{
					if (dir.is_directory())
						continue;
					if (dir.path().extension() == ".glb" || dir.path().extension() == ".gltf" || dir.path().extension() == ".fbx")
					{
						modelcount++;
					}
				}
			}
			catch (const file::filesystem_error& e)
			{
				Log::Warning("Could not load models" + std::string(e.what()));
			}
			catch (const std::exception& e)
			{
				Log::Warning("Error" + std::string(e.what()));
			}
		};

		auto texturefunc = [&]()
		{
			try
			{
				file::path texturepath = PathFinder::Relative("Textures\\");
				for (auto& dir : file::recursive_directory_iterator(texturepath))
				{
					if (dir.is_directory())
						continue;
					if (dir.path().extension() == ".png" || dir.path().extension() == ".jpg")
					{
						texturecount++;
					}
				}

				file::path materialpath = PathFinder::Relative("Material\\");
				for (auto& dir : file::recursive_directory_iterator(materialpath))
				{
					if (dir.is_directory())
						continue;
					if (dir.path().extension() == ".png" || dir.path().extension() == ".jpg")
					{
						materialcount++;
					}
				}
			}
			catch (const file::filesystem_error& e)
			{
				Log::Warning("Could not load textures" + std::string(e.what()));
			}
			catch (const std::exception& e)
			{
				Log::Warning("Error" + std::string(e.what()));
			}
		};

		auto billboardfunc = [&]()
		{
			try
			{
				file::path billboardpath = PathFinder::Relative("Billboards\\");
				for (auto& dir : file::recursive_directory_iterator(billboardpath))
				{
					if (dir.is_directory())
						continue;
					if (dir.path().extension() == ".png" || dir.path().extension() == ".jpg")
					{
						billboardcount++;
					}
				}
			}
			catch (const file::filesystem_error& e)
			{
				Log::Warning("Could not load billboards" + std::string(e.what()));
			}
			catch (const std::exception& e)
			{
				Log::Warning("Error" + std::string(e.what()));
			}
		};

		//Concurrency::parallel_invoke(shaderfunc, modelfunc, texturefunc, billboardfunc);
		futures[0] = std::async(std::launch::deferred, shaderfunc);
		futures[1] = std::async(std::launch::deferred, modelfunc);
		futures[2] = std::async(std::launch::deferred, texturefunc);
		futures[3] = std::async(std::launch::deferred, billboardfunc);
		//futures[4] = std::async(std::launch::deferred, materialfunc);

		for (auto& future : futures)
		{
			if (!future.valid())
				continue;

			future.get();
		}


		auto loadTexture = [&]()
		{
			if (texturecount != currTextureFileCount)
			{
				//LoadTextures();
				currTextureFileCount = texturecount;
			}
		};

		auto billboard = [&]()
		{
			if (billboardcount != currBillboardFileCount)
			{
				//LoadBillboards();
				currBillboardFileCount = billboardcount;
			}
		};

		auto loadMaterial = [&]()
		{
			if (materialcount != currMaterialFileCount)
			{
				//LoadMaterials();
				currMaterialFileCount = materialcount;
			}
		};

		auto loadShader = [&]()
		{
			if (shadercount != currShaderFileCount)
			{
				std::cout << "Shader count changed" << std::endl;
				//LoadShaders();
				currShaderFileCount = shadercount;
			}
		};

		futures[0] = std::async(std::launch::deferred, loadTexture);
		futures[1] = std::async(std::launch::deferred, billboard);
		futures[2] = std::async(std::launch::deferred, loadMaterial);
		futures[3] = std::async(std::launch::deferred, loadShader);

		try
		{

			for (auto& future : futures)
			{
				if (!future.valid())
					continue;

				future.get();
			}
		}
		catch (const std::exception& e)
		{
			Log::Warning("Error" + std::string(e.what()));
		}


		if (modelcount != currModelFileCount)
		{
			//LoadModels();
			currModelFileCount = modelcount;
		}

        std::this_thread::sleep_for(std::chrono::seconds(1));
	}
}

void DataSystem::LoadShaders()
{
	icon = Texture::LoadFormPath(PathFinder::Relative("Icon\\Model.png"));

	try
	{
		file::path shaderpath = PathFinder::Relative("Shaders\\");
		for (auto& dir : file::recursive_directory_iterator(shaderpath))
		{
			if (dir.is_directory())
				continue;

			if (dir.path().extension() == ".hlsl")
			{
				AddShaderFromPath(dir.path());
			}
		}
	}
	catch (const file::filesystem_error& e)
	{
		Log::Warning("Could not load shaders" + std::string(e.what()));
	}
	catch (const std::exception& e)
	{
		Log::Warning("Error" + std::string(e.what()));
	}
}

void DataSystem::LoadModels()
{
	file::path shaderpath = PathFinder::Relative("Models\\");
	//모델의 디렉토리별로 std::async를 사용하여 병렬로 로드
	//std::vector<std::future<void>> futures;

	//for (auto& dir : file::recursive_directory_iterator(shaderpath))
	//{
	//	if (dir.is_directory())
	//		continue;
	//	if (dir.path().extension() == ".glb" || dir.path().extension() == ".gltf" || dir.path().extension() == ".fbx")
	//	{
	//		futures.push_back(std::async(std::launch::async, &DataSystem::AddModel, this, dir.path(), dir.path().parent_path()));
	//	}
	//}

	//for (auto& future : futures)
	//{
	//	if (future.valid())
	//	{
	//		future.get();
	//		currModelFileCount++;
	//	}
	//}
	
	for (auto& dir : file::recursive_directory_iterator(shaderpath))
{
	if (dir.is_directory())
		continue;
	if (dir.path().extension() == ".glb" || dir.path().extension() == ".gltf" || dir.path().extension() == ".fbx")
	{
		AddModel(dir.path(), dir.path().parent_path());
	}
}
}

void DataSystem::LoadTextures()
{
	//file::path texturepath = PathFinder::Relative("Textures\\");
	//for (auto& dir : file::recursive_directory_iterator(texturepath))
	//{
	//	if (dir.is_directory())
	//		continue;
	//	if (dir.path().extension() == ".png" || 
	//		dir.path().extension() == ".jpg" || 
	//		dir.path().extension() == ".jpeg"||
	//		dir.path().extension() == ".dds" ||
	//		dir.path().extension() == ".tga"
	//		)
	//	{
	//		//textureLoader caching texture2D form file
	//		TextureLoader::LoadFromFile(dir.path(), sRGBSettings::FORCE_SRGB);
	//	}
	//}
}

void DataSystem::LoadMaterials()
{
	////TODO: Load materials from file
	//std::string key{};
	//file::path materialpath = PathFinder::Relative("Material\\");
	//for (auto& dir : file::recursive_directory_iterator(materialpath))
	//{
	//	if (dir.is_directory() && HasImageFile(dir))
	//	{
	//		key = dir.path().filename().string();
	//		Materials[key] = std::make_shared<Material>();
	//		Materials[key]->name = key;
	//		MaterialLoader::LoadFromPath(Materials[key], key, dir.path());
	//	}
	//}
}

void DataSystem::LoadBillboards()
{
	/*std::string key{};
	file::path billboardpath = PathFinder::Relative("Billboards\\");
	for (auto& dir : file::recursive_directory_iterator(billboardpath))
	{
		if (dir.is_directory())
		{
			key = dir.path().filename().string();
			continue;
		}
		if (dir.path().extension() == ".png" ||
			dir.path().extension() == ".jpg" ||
			dir.path().extension() == ".jpeg"||
			dir.path().extension() == ".dds" ||
			dir.path().extension() == ".tga"
			)
		{
			if (!Billboards[key])
			{
				Billboards[key] = std::make_shared<Billboard>();
				Billboards[key]->name = key;
				Billboards[key]->CreateBuffers();
			}
			Billboards[key]->AddTexture(TextureLoader::LoadFromFile(dir.path(), sRGBSettings::FORCE_SRGB).Get());
		}
	}*/
}

void DataSystem::AddShaderFromPath(const file::path& filepath)
{
	ComPtr<ID3DBlob> blob = HLSLCompiler::LoadFormFile(filepath.string());
	file::path filename = filepath.filename();
	std::string ext = filename.replace_extension().extension().string();
	filename.replace_extension();
	ext.erase(0, 1);

	AddShader(filename.string(), ext, blob);
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

void DataSystem::AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob)
{
	if (ext == "vs")
	{
		VertexShader vs = VertexShader(name, blob);
		vs.Compile();
		VertexShaders[name] = vs;
	}
	else if (ext == "hs")
	{
		HullShader hs = HullShader(name, blob);
		hs.Compile();
		HullShaders[name] = hs;
	}
	else if (ext == "ds")
	{
		DomainShader ds = DomainShader(name, blob);
		ds.Compile();
		DomainShaders[name] = ds;
	}
	else if (ext == "gs")
	{
		GeometryShader gs = GeometryShader(name, blob);
		gs.Compile();
		GeometryShaders[name] = gs;
	}
	else if (ext == "ps")
	{
		PixelShader ps = PixelShader(name, blob);
		ps.Compile();
		PixelShaders[name] = ps;
	}
	else if (ext == "cs")
	{
		ComputeShader cs = ComputeShader(name, blob);
		cs.Compile();
		ComputeShaders[name] = cs;
	}
	else
	{
		throw std::runtime_error("Unknown shader type");
	}
}

void DataSystem::RemoveShaders()
{
	VertexShaders.clear();
	HullShaders.clear();
	DomainShaders.clear();
	GeometryShaders.clear();
	PixelShaders.clear();
	ComputeShaders.clear();
}
