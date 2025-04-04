#pragma once
#include "../Utility_Framework/HLSLCompiler.h"
#include "ModelLoader.h"
#include "Texture.h"
//#include "Billboards.h"
#include "Model.h"
#include "ImGuiRegister.h"

// Main system for storing runtime data
class DataSystem : public Singleton<DataSystem>
{
private:
    friend class Singleton;

	DataSystem() = default;
	~DataSystem();
public:
	void Initialize();
	void RenderForEditer();
	void MonitorFiles();
	void LoadModels();
	void LoadTextures();
	void LoadMaterials();

	std::unordered_map<std::string, std::shared_ptr<Model>>	Models;
	std::unordered_map<std::string, std::shared_ptr<Material>> Materials;
	static ImGuiTextFilter filter;

private:
	void AddShaderFromPath(const file::path& filepath);
	void AddModel(const file::path& filepath, const file::path& dir);

private:
	//--------- Icon for ImGui
	Texture* icon{};
	//--------- current file count
	std::atomic<uint32> currModelFileCount = 0;
	uint32 currShaderFileCount = 0;
	uint32 currTextureFileCount = 0;
	uint32 currMaterialFileCount = 0;
	uint32 currBillboardFileCount = 0;
	//--------- Data Thread and Editor Payload
	std::thread m_DataThread{};
	file::path m_dragDropPath{};
};

static inline auto& DataSystems = DataSystem::GetInstance();
