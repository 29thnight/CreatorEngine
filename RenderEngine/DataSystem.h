#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../Utility_Framework/HLSLCompiler.h"
#include "Texture.h"
#include "ImGuiRegister.h"
#include "AssetMetaWather.h"
#include <DirectXTK/SpriteFont.h>
#include <DirectXTK/SpriteBatch.h>
#include "concurrent_queue.h"
#include "AssetJob.h"
#include "DLLAcrossSingleton.h"
#include "EngineSetting.h"

// Main system for storing runtime data
class ModelLoader;
class Model;
class Material;
class DataSystem : public DLLCore::Singleton<DataSystem>
{
public:
	enum class FileType
	{
		Unknown,
		Model,
		Texture,
		MaterialTexture,
		TerrainTexture,
		Shader,
		CppScript,
		CSharpScript,
		Prefab,
		Sound,
		HDR,
		VolumeProfile,
		End,
	};

	enum class TextureFileType
	{
		Texture,
		MaterialTexture,
		TerrainTexture,
		HDR,
	};

	enum class AssetType
	{
		Model,
		Material,
		Skeleton,
	};

private:
    friend class DLLCore::Singleton<DataSystem>;

	DataSystem() = default;
	~DataSystem();
public:
	void Initialize();
    void Finalize();
	void RenderForEditer();
	void MonitorFiles();
	//Resource Model
	void LoadModels();
	Model* LoadModelGUID(FileGuid guid);
	void LoadModel(std::string_view filePath);
	Model* LoadCashedModel(std::string_view filePath);
	//Resource Texture
	void LoadTextures();
	Texture* LoadTextureGUID(FileGuid guid);
	Texture* LoadTexture(std::string_view filePath);
	void CopyHDRTexture(std::string_view filePath);
	void CopyTexture(std::string_view filePath, const file::path& destination);
	void SelectTextureType();
	void CopyTextureSelectType(std::string_view filePath, TextureFileType type);
	//Resource Material
	void LoadMaterials();
	Material* LoadMaterialGUID(FileGuid guid);
    Texture* LoadMaterialTexture(std::string_view filePath, bool isCompress = false);
	Material* CreateMaterial();
	SpriteFont* LoadSFont(const std::wstring_view& filePath);
	// File Operations //파일 시스템에 접근이 가능하기 문에 보안상 이슈가 있을 가능성 있음
	void OpenFile(const file::path& filepath);
	void OpenExplorerSelectFile(const std::filesystem::path& filePath);
	void OpenSolutionAndFile(const file::path& slnPath, const file::path& filepath);
	// Asset Metadata
	FileGuid GetFileGuid(const file::path& filepath) const;
	FileGuid GetFilenameToGuid(const std::string& filename) const;
	FileGuid GetStemToGuid(const std::string& stem) const;
	file::path GetFilePath(FileGuid fileguid) const;

	file::path m_TargetTexturePath;
	concurrency::concurrent_queue<file::path> m_LoadTextureAssetQueue;
	// Contents Browser
	void OpenContentsBrowser();
	void CloseContentsBrowser();
	void ShowDirectoryTree(const file::path& directory);
	void SetContentsBrowserStyle(ContentsBrowserStyle style) { m_ContentsBrowserStyle = style; }
	ContentsBrowserStyle GetContentsBrowserStyle() const { return m_ContentsBrowserStyle; }
	void ShowCurrentDirectoryFiles();
	void ShowCurrentDirectoryFilesTile();
	void ShowCurrentDirectoryFilesTree(const file::path& directory);
	void DrawFileTile(ImTextureID iconTexture, const file::path& directory, const std::string& fileName, FileType& fileType, const ImVec2& tileSize = ImVec2(160, 160));

	ImFont* GetSmallFont() const { return smallFont; }
	ImFont* GetExtraSmallFont() const { return extraSmallFont; }

	AssetMetaWatcher* GetAssetMetaWatcher() const { return m_assetMetaWatcher.get(); }

	void ForceCreateYamlMetaFile(const file::path& filepath);
	void CreateVolumeProfile(const file::path& filepath);

	std::unordered_map<std::string, std::shared_ptr<Model>>	Models;
	std::unordered_map<std::string, std::shared_ptr<Material>> Materials;
	std::unordered_map<std::string, std::shared_ptr<Texture>> Textures;
	std::unordered_map<std::string, std::shared_ptr<SpriteFont>> SFonts;
	static ImGuiTextFilter filter;
	ContentsBrowserStyle m_ContentsBrowserStyle{ ContentsBrowserStyle::Tile };
	float tileSize = 160.0f;

	Material* m_trasfarMaterial{};

	std::string selectedFileName{};
	std::string selectedMetaFilePath{};
	std::optional<YAML::Node> selectedFileMetaNode{};

	//--------- Icon for ImGui
	Texture* TextureIcon{};
	Texture* ModelIcon{};
	Texture* AssetsIcon{};
	Texture* FolderIcon{};
	Texture* UnknownIcon{};
	Texture* ShaderIcon{};
	Texture* CodeIcon{};

	Texture* MainLightIcon{};
	Texture* PointLightIcon{};
	Texture* SpotLightIcon{};
	Texture* DirectionalLightIcon{};
	Texture* CameraIcon{};

	ImFont* smallFont{};
	ImFont* extraSmallFont{};

private:
	void AddModel(const file::path& filepath, const file::path& dir);

private:
	//--------- current file count
	uint32 currModelFileCount = 0;
	uint32 currShaderFileCount = 0;
	uint32 currTextureFileCount = 0;
	uint32 currMaterialFileCount = 0;
	//--------- Data Thread and Editor Payload
	std::thread m_DataThread{};
	file::path m_dragDropPath{};
	ImVec2 overlayPos{};

	file::path currentDirectory{};
	efsw::FileWatcher* m_watcher{};
	std::shared_ptr<AssetMetaRegistry> m_assetMetaRegistry{};
	std::shared_ptr<AssetMetaWatcher> m_assetMetaWatcher{};

	static std::atomic_bool m_isExecuteSolution;
};

static auto DataSystems = DataSystem::GetInstance();
#endif // !DYNAMICCPP_EXPORTS