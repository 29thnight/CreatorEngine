#pragma once
#ifndef DYNAMICCPP_EXPORTS

#include "Texture.h"
#include "ImGuiRegister.h"
#include "AssetMetaWather.h"
#include <DirectXTK/SpriteBatch.h>
#include "concurrent_queue.h"
#include "AssetJob.h"
#include "DLLAcrossSingleton.h"
#include "EngineSetting.h"
#include "AssetBundle.h"

template <typename T>
using DataContainer = std::unordered_map<std::string, std::shared_ptr<T>>;

// Main system for storing runtime data
class ModelLoader;
class Model;
class Material;
class VolumeProfile;
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
		Font,
		End,
	};

	enum class TextureFileType
	{
		Texture,
		MaterialTexture,
		TerrainTexture,
		HDR,
		UITexture,
		SpriteSheet,
	};

	enum class AssetType
	{
		Model,
		Material,
		Skeleton,
	};

	struct FileTypeIcon {
		FileType type;
		ImTextureID icon;
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
	// Asset bundle operations
	void LoadAssetBundle(const AssetBundle& bundle);
	void RetainAssets(const AssetBundle& bundle);
	void ClearRetainedAssets();
	void UnloadUnusedAssets();
	//Resource Model
	void LoadModels();
	Model* LoadModelGUID(FileGuid guid);
	void LoadModel(std::string_view filePath);
	Model* LoadCashedModel(std::string_view filePath);
	//Resource Texture
	void LoadTextures();
	Texture* LoadTextureGUID(FileGuid guid);
	Texture* LoadTexture(std::string_view filePath, TextureFileType type = TextureFileType::Texture);
	std::shared_ptr<Texture> LoadSharedTexture(std::string_view filePath, TextureFileType type = TextureFileType::Texture);
	void CopyHDRTexture(std::string_view filePath);
	void CopyTexture(std::string_view filePath, const file::path& destination);
	void SelectTextureType();
	void CopyTextureSelectType(std::string_view filePath, TextureFileType type);
	//Resource Material
	void LoadMaterials();
	void InsertMaterial(std::shared_ptr<Material> material);
	void SaveMaterial(Material* material);
	Material* LoadMaterial(std::string_view name);
	// 소유권을 공유하는 조회. 컴포넌트처럼 참조를 보관하는 쪽은 이것을 써야
	// 캐시에서 제거되어도 사용 중인 머티리얼이 파괴되지 않는다.
	std::shared_ptr<Material> LoadMaterialShared(std::string_view name);
    Texture* LoadMaterialTexture(std::string_view filePath, bool isCompress = false);
	std::shared_ptr<Texture> LoadSharedMaterialTexture(std::string_view filePath, bool isCompress);
	Material* CreateMaterial();
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

	// OpenFile이 확장자를 보고 전용 편집기로 넘길 기회. true를 돌려주면
	// 그쪽이 처리한 것으로 보고 셸 실행으로 넘어가지 않는다.
	// (프리팹 → PrefabEditor가 그랬다)
	//
	// OpenFile은 콘텐츠 브라우저 말고도 App·MenuBar가 부른다. 어떤 확장자를
	// 어느 편집기로 보낼지는 에디터 정책이므로 이 이음매는 계속 남는다.
	using OpenFileOverride = std::function<bool(const file::path& filepath)>;
	void SetOpenFileOverride(OpenFileOverride handler)
	{
		m_openFileOverride = std::move(handler);
	}

	// ★ 콘텐츠 브라우저 API 열 하나가 여기 있었다 — 창째로
	//   EngineGUIWindow/ContentsBrowserWindow로 옮겼다 (PHASE 4-3 슬라이스 2).
	//   여는 방식은 ImGui::GetContext(...)로 이름만 알면 되므로 래퍼가 필요 없고,
	//   스타일은 EngineSetting이 정본이라 사본을 들 이유도 없었다.

	ImFont* GetSmallFont() const { return smallFont; }
	ImFont* GetExtraSmallFont() const { return extraSmallFont; }

	AssetMetaWatcher* GetAssetMetaWatcher() const { return m_assetMetaWatcher.get(); }

	void ForceCreateYamlMetaFile(const file::path& filepath);
	void CreateVolumeProfile(const file::path& filepath);
	void SaveExistVolumeProfile(FileGuid guid, VolumeProfile* volume);
	
	void AddSupportExtension(std::string_view ext);
	void RemoveSupportExtension(std::string_view ext);
	bool IsSupportExtension(std::string_view ext) const;

	DataContainer<Model>		Models;
	DataContainer<Material>		Materials;
	DataContainer<Texture>		Textures;
	DataContainer<Texture>		UITextures;
	DataContainer<Texture>		SpriteSheets;
	std::unordered_map<int, std::unordered_set<std::string>> m_retainedAssets;

	// 인스펙터 드래그앤드롭 전달용 임시 보관.
	// 전달 도중 캐시에서 제거되어도 안전하도록 공동 소유로 잡는다.
	std::shared_ptr<Material> m_trasfarMaterial{};
	std::string m_trasfarShader{};

	// 캐시별 보호 규약.
	//   m_modelMutex    : Models
	//   m_materialMutex : Materials
	//   m_textureMutex  : Textures / UITextures / SpriteSheets
	//   m_fontMutex     : SFonts
	// 이 맵들은 LoadAssetBundle이 스레드풀로 병렬 로딩하므로,
	// 조회·삽입 시 반드시 해당 뮤텍스를 잡아야 한다.
	std::mutex m_textureMutex;
	std::mutex m_materialMutex;
	std::mutex m_modelMutex;
	std::mutex m_fontMutex;

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

	std::unordered_map<std::string_view, FileTypeIcon> kExtensionMap{};

private:
	void AddModel(const file::path& filepath, const file::path& dir);

	// 에디터가 걸어 둔다. 비어 있으면 셸 실행으로 넘어간다 —
	// 플레이어 빌드에는 에디터가 없으므로 그것이 옳은 기본값이다.
	OpenFileOverride m_openFileOverride{};

private:
	//--------- current file count
	uint32 currModelFileCount = 0;
	uint32 currShaderFileCount = 0;
	uint32 currTextureFileCount = 0;
	uint32 currMaterialFileCount = 0;
	//--------- Data Thread and Editor Payload
	std::thread m_DataThread{};
	file::path m_dragDropPath{};
	efsw::FileWatcher* m_watcher{};
	std::shared_ptr<AssetMetaRegistry> m_assetMetaRegistry{};
	std::shared_ptr<AssetMetaWatcher>  m_assetMetaWatcher{};

	static std::atomic_bool m_isExecuteSolution;
};

static auto DataSystems = DataSystem::GetInstance();

#endif // !DYNAMICCPP_EXPORTS