#pragma once
#ifndef DYNAMICCPP_EXPORTS

#include "Texture.h"
#include "ImGuiRegister.h"
#include "AssetMetaRegistry.h"
#include <DirectXTK/SpriteBatch.h>
#include "AssetJob.h"
#include "ClassProperty.h"
#include "AssetBundle.h"

template <typename T>
using DataContainer = std::unordered_map<std::string, std::shared_ptr<T>>;

// Main system for storing runtime data
class ModelLoader;
class Model;
class Material;
class DataSystem : public Singleton<DataSystem>
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
		// ImTextureID는 backend descriptor라 영구 캐시할 수 없다. Texture의
		// 안정 신원만 보관하고 그리는 프레임에 EditorImGuiTexture로 해석한다.
		Texture* icon;
	};

private:
    friend class Singleton<DataSystem>;

	DataSystem() = default;
	~DataSystem();
public:
	void Initialize();
    void Finalize();
	void RenderForEditer();
	// Asset bundle operations
	void LoadAssetBundle(const AssetBundle& bundle);
	void RetainAssets(const AssetBundle& bundle);
	void ClearRetainedAssets();
	void UnloadUnusedAssets();
	//Resource Model
	Model* LoadModelGUID(FileGuid guid);
	void LoadModel(std::string_view filePath);
	Model* LoadCashedModel(std::string_view filePath);
	std::shared_ptr<Model> FindCachedModel(std::string_view name);
	std::vector<std::pair<std::string, std::shared_ptr<Model>>> SnapshotModels();
	//Resource Texture
	Texture* LoadTextureGUID(FileGuid guid);
	Texture* LoadTexture(std::string_view filePath, TextureFileType type = TextureFileType::Texture);
	std::shared_ptr<Texture> LoadSharedTexture(std::string_view filePath, TextureFileType type = TextureFileType::Texture);
	std::vector<std::pair<std::string, std::shared_ptr<Texture>>> SnapshotTextures();
	//Resource Material
	void InsertMaterial(std::shared_ptr<Material> material);
	std::shared_ptr<Material> FindCachedMaterial(std::string_view name);
	std::vector<std::pair<std::string, std::shared_ptr<Material>>> SnapshotMaterials();
	std::shared_ptr<Material> RegisterImportedMaterial(
		std::shared_ptr<Material> material, std::string_view baseName);
	Material* LoadMaterial(std::string_view name);
	// 소유권을 공유하는 조회. 컴포넌트처럼 참조를 보관하는 쪽은 이것을 써야
	// 캐시에서 제거되어도 사용 중인 머티리얼이 파괴되지 않는다.
	std::shared_ptr<Material> LoadMaterialShared(std::string_view name);
    Texture* LoadMaterialTexture(std::string_view filePath, bool isCompress = false);
	std::shared_ptr<Texture> LoadSharedMaterialTexture(std::string_view filePath, bool isCompress);
	Material* CreateMaterial();
	// Asset Metadata
	FileGuid GetFileGuid(const file::path& filepath) const;

	// Host authoring adapter가 meta 저장과 같은 transaction 안에서 등록한다.
	// runtime load 경로는 Initialize의 read-only catalog scan으로만 채운다.
	void RegisterFileGuid(const FileGuid& guid, const file::path& filepath);
	void UnregisterFilePath(const file::path& filepath);
	FileGuid GetFilenameToGuid(const std::string& filename) const;
	FileGuid GetStemToGuid(const std::string& stem) const;
	file::path GetFilePath(FileGuid fileguid) const;

	// ★ 콘텐츠 브라우저 API 열 하나가 여기 있었다 — 창째로
	//   EngineGUIWindow/ContentsBrowserWindow로 옮겼다 (PHASE 4-3 슬라이스 2).
	//   여는 방식은 ImGui::GetContext(...)로 이름만 알면 되므로 래퍼가 필요 없고,
	//   스타일은 EditorPreferences가 정본이라 사본을 들 이유도 없었다.

	ImFont* GetSmallFont() const { return smallFont; }
	ImFont* GetExtraSmallFont() const { return extraSmallFont; }

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
	void LoadAssetCatalog(const file::path& root);

private:
	//--------- current file count
	uint32 currModelFileCount = 0;
	uint32 currShaderFileCount = 0;
	uint32 currTextureFileCount = 0;
	uint32 currMaterialFileCount = 0;
	//--------- Data Thread and Editor Payload
	std::thread m_DataThread{};
	file::path m_dragDropPath{};
	std::shared_ptr<AssetMetaRegistry> m_assetMetaRegistry{};

};

static auto DataSystems = DataSystem::GetInstance();

#endif // !DYNAMICCPP_EXPORTS
