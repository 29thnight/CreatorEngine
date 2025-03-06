#pragma once
#include "HLSLCompiler.h"
#include "Shader.h"
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
	void LoadShaders();
	void LoadModels();
	void LoadTextures();
	void LoadMaterials();
	void LoadBillboards();

	std::shared_ptr<Model> GetPayloadModel() { return dragDropModel; }
	void ClearPayloadModel() { dragDropModel = nullptr; }
	//std::shared_ptr<Billboard> GetPayloadBillboard() { return dragDropBillboard; }
	//void ClearPayloadBillboard() { dragDropBillboard = nullptr; }
	std::shared_ptr<Material> GetPayloadMaterial() { return dragDropMaterial; }
	void ClearPayloadMaterial() { dragDropMaterial = nullptr; }

	std::unordered_map<std::string, VertexShader>	VertexShaders;
	std::unordered_map<std::string, HullShader>		HullShaders;
	std::unordered_map<std::string, DomainShader>	DomainShaders;
	std::unordered_map<std::string, GeometryShader>	GeometryShaders;
	std::unordered_map<std::string, PixelShader>	PixelShaders;
	std::unordered_map<std::string, ComputeShader>	ComputeShaders;

	std::unordered_map<std::string, std::shared_ptr<Model>>		Models;
	std::unordered_map<std::string, std::shared_ptr<Material>>	Materials;
	//std::unordered_map<std::string, std::shared_ptr<Billboard>>	Billboards;
	static ImGuiTextFilter filter;

private:
	void AddShaderFromPath(const file::path& filepath);
	void AddModel(const file::path& filepath, const file::path& dir);
	void AddShader(const std::string& name, const std::string& ext, const ComPtr<ID3DBlob>& blob);
	void RemoveShaders();

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
	std::shared_ptr<Model> dragDropModel{};
	//std::shared_ptr<Billboard> dragDropBillboard{};
	std::shared_ptr<Material> dragDropMaterial{};
};

static inline auto& AssetsSystem = DataSystem::GetInstance();
