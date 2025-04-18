#pragma once
#include "../Utility_Framework/HLSLCompiler.h"
#include "ModelLoader.h"
#include "Texture.h"
//#include "Billboards.h"
#include "Model.h"
#include "ImGuiRegister.h"
#include "AssetMetaWather.h"
#include <DirectXTK/SpriteFont.h>
#include <DirectXTK/SpriteBatch.h>
// Main system for storing runtime data
class DataSystem : public Singleton<DataSystem>
{
public:
	enum class FileType
	{
		Unknown,
		Model,
		Texture,
		Shader,
		CppScript,
		CSharpScript,
		Sound,
	};
	//일단 이대로 진행
	enum class AssetType
	{
		Model,
		Material,
		Skeleton,
	};

	std::unordered_map<FileType, std::string> FileTypeString =
	{
		{ FileType::Model, "Model" },
		{ FileType::Texture, "Texture" },
		{ FileType::Shader, "Shader" },
		{ FileType::CppScript, "CppScript" },
		{ FileType::CSharpScript, "CSharpScript" },
		{ FileType::Sound, "Sound" }
	};

private:
    friend class Singleton;

	DataSystem() = default;
	~DataSystem();
public:
	void Initialize();
    void Finalize();
	void RenderForEditer();
	void MonitorFiles();
	void LoadModels();
	Model* LoadModelGUID(FileGuid guid);
	void LoadModel(const std::string_view& filePath);
	Model* LoadCashedModel(const std::string_view& filePath);
	void LoadTextures();
	void LoadMaterials();
	Texture* LoadTextureGUID(FileGuid guid);
	Texture* LoadTexture(const std::string_view& filePath);
    Texture* LoadMaterialTexture(const std::string_view& filePath);
	SpriteFont* LoadSFont(const std::wstring_view& filePath);
	void OpenFile(const file::path& filepath);

	void OpenContentsBrowser();
	void CloseContentsBrowser();
	void ShowDirectoryTree(const file::path& directory);
	void ShowCurrentDirectoryFiles();
	void DrawFileTile(ImTextureID iconTexture, const file::path& directory, const std::string& fileName, FileType& fileType, const ImVec2& tileSize = ImVec2(160, 160));

	ImFont* GetSmallFont() const { return smallFont; }
	ImFont* GetExtraSmallFont() const { return extraSmallFont; }

	std::unordered_map<std::string, std::shared_ptr<Model>>	Models;
	std::unordered_map<std::string, std::shared_ptr<Material>> Materials;
	std::unordered_map<std::string, std::shared_ptr<Texture>> Textures;
	std::unordered_map<std::string, std::shared_ptr<SpriteFont>> SFonts;
	static ImGuiTextFilter filter;
private:
	void AddModel(const file::path& filepath, const file::path& dir);

private:
	//--------- Icon for ImGui
	Texture* TextureIcon{};
	Texture* ModelIcon{};
	Texture* AssetsIcon{};
	Texture* FolderIcon{};
	Texture* UnknownIcon{};
	Texture* ShaderIcon{};
	Texture* CodeIcon{};
	ImFont* smallFont{};
	ImFont* extraSmallFont{};
	//--------- current file count
	uint32 currModelFileCount = 0;
	uint32 currShaderFileCount = 0;
	uint32 currTextureFileCount = 0;
	uint32 currMaterialFileCount = 0;
	//--------- Data Thread and Editor Payload
	std::thread m_DataThread{};
	file::path m_dragDropPath{};

	file::path currentDirectory{};
	efsw::FileWatcher* m_watcher{};
	std::shared_ptr<AssetMetaRegistry> m_assetMetaRegistry{};
	std::shared_ptr<AssetMetaWatcher> m_assetMetaWatcher{};
};

static inline auto& DataSystems = DataSystem::GetInstance();
