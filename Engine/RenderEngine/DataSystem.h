#pragma once

#include "Texture.h"
#include "AssetMetaRegistry.h"
#include "AssetJob.h"
#include "ClassProperty.h"
#include "AssetBundle.h"
#include "ShaderMetaHandle.h"
#include <iosfwd>

template <typename T>
using DataContainer = std::unordered_map<std::string, std::shared_ptr<T>>;

// Main system for storing runtime data
class ModelLoader;
class Model;
class Material;
struct ShaderMeta;
namespace YAML { class Node; }

enum class RuntimeAssetType
{
	Auto,
	CatalogOnly,
	Model,
	Material,
	Texture,
	UITexture,
	SpriteSheet,
	ShaderMeta,
};

enum class RuntimeAssetChangeKind
{
	CatalogUpsert,
	ContentReload,
	Removed,
};

// Editor 같은 authoring Host가 완전히 게시한 파일의 결과만 이 계약으로 넘긴다.
// Runtime은 source/meta 작성 방법을 알지 않고 catalog와 cache generation만 갱신한다.
struct RuntimeAssetChange
{
	RuntimeAssetChangeKind kind{ RuntimeAssetChangeKind::CatalogUpsert };
	RuntimeAssetType assetType{ RuntimeAssetType::Auto };
	FileGuid guid{};
	file::path path{};
};

class DataSystem : public Singleton<DataSystem>
{
public:
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

private:
    friend class Singleton<DataSystem>;

	DataSystem() = default;
	~DataSystem();
public:
	void Initialize();
    void Finalize();
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
	// M5-B1: standalone Material YAML의 단일 codec. scene embedded material은
	// typed reflection이 값을 복원한 뒤 같은 runtime finalize 규약을 공유한다.
	YAML::Node SerializeMaterialPayload(Material& material) const;
	bool DeserializeMaterialPayload(Material& material, const YAML::Node& node);
	// Model cache는 이 versioned envelope 안에 위 YAML payload를 넣는다. 기존
	// 무버전 binary record 판별은 probe 뒤 ModelLoader의 read-only 호환 경로가 맡는다.
	bool HasVersionedMaterialBinaryPayload(std::istream& input) const;
	bool SerializeMaterialBinaryPayload(Material& material, std::ostream& output) const;
	bool DeserializeMaterialBinaryPayload(Material& material, std::istream& input);
	void FinalizeMaterialRuntime(Material& material);
	Material* LoadMaterial(std::string_view name);
	// 소유권을 공유하는 조회. 컴포넌트처럼 참조를 보관하는 쪽은 이것을 써야
	// 캐시에서 제거되어도 사용 중인 머티리얼이 파괴되지 않는다.
	std::shared_ptr<Material> LoadMaterialShared(std::string_view name);
    Texture* LoadMaterialTexture(std::string_view filePath, bool isCompress = false);
	std::shared_ptr<Texture> LoadSharedMaterialTexture(std::string_view filePath, bool isCompress);
	Material* CreateMaterial();
	// Asset Metadata
	FileGuid GetFileGuid(const file::path& filepath) const;
	// catalog GUID를 정본으로 DataSystem 소유 cache slot을 얻는다. resolve가 반환한
	// shared snapshot은 호출자가 보유하는 동안 안전하지만 reload 뒤 옛 handle은
	// resolve되지 않는다. 값 복사 API는 기존 호출 호환 경계다.
	ShaderMetaHandle LoadShaderMetaHandle(FileGuid guid, std::string& outError);
	std::shared_ptr<const ShaderMeta> ResolveShaderMeta(ShaderMetaHandle handle) const;
	bool LoadShaderMetaGUID(FileGuid guid, ShaderMeta& outMeta,
		std::string& outError);

	// Authoring Host가 파일/meta 게시를 끝낸 뒤 전달하는 유일한 변경 경계다.
	// Player는 생산자를 설치하지 않고 startup catalog만 읽는다.
	void ApplyAssetChange(const RuntimeAssetChange& change);
	FileGuid GetFilenameToGuid(const std::string& filename) const;
	FileGuid GetStemToGuid(const std::string& stem) const;
	file::path GetFilePath(FileGuid fileguid) const;

	DataContainer<Model>		Models;
	DataContainer<Material>		Materials;
	DataContainer<Texture>		Textures;
	DataContainer<Texture>		UITextures;
	DataContainer<Texture>		SpriteSheets;
	std::unordered_map<int, std::unordered_set<std::string>> m_retainedAssets;

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

private:
	void AddModel(const file::path& filepath, const file::path& dir);
	void LoadAssetCatalog(const file::path& root);
	void RetireCachedAsset(RuntimeAssetType assetType, const file::path& path,
		FileGuid guid, bool remove);
	void InvalidateShaderMeta(FileGuid guid, bool remove);
	void SynchronizeLegacyMaterialProperties(Material& material) const;

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
	// 일부 legacy 소비자가 Texture/Model 내부 자료를 raw pointer로 보관한다.
	// reload에서 이전 cache generation을 즉시 파괴하지 않고 runtime 종료까지
	// 붙들어, 새 로드는 새 세대를 받되 기존 frame/component는 안전하게 마친다.
	std::mutex m_retiredAssetMutex;
	std::vector<std::shared_ptr<void>> m_retiredAssetGenerations;

	struct ShaderMetaCacheSlot
	{
		FileGuid guid{};
		std::uint32_t generation{ 1 };
		bool occupied{};
		std::shared_ptr<const ShaderMeta> value{};
	};
	mutable std::mutex m_shaderMetaMutex;
	std::unordered_map<FileGuid, std::uint32_t> m_shaderMetaSlotByGuid;
	std::vector<ShaderMetaCacheSlot> m_shaderMetaSlots;
	std::vector<std::uint32_t> m_shaderMetaFreeSlots;

};

static auto DataSystems = DataSystem::GetInstance();

