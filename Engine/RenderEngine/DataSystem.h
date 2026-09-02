#pragma once

#include "Texture.h"
#include "AuthoringNodeView.h" // D3-a-5b
#include "AssetMetaRegistry.h"
#include "AssetJob.h"
#include "ClassProperty.h"
#include "AssetBundle.h"
#include "ShaderMetaHandle.h"
#include <cstddef>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

template <typename T>
using DataContainer = std::unordered_map<std::string, std::shared_ptr<T>>;

// Main system for storing runtime data
class ModelLoader;
class Model;
class Material;
struct ShaderMeta;
namespace Authoring { class WriteNode; }
namespace experiment { class Model; } // I5-D1a 역브리지 입력
namespace experiment { struct Material; } // I5-D5c1 저작 원본 보관
namespace experiment::cooked { class CookedAssetCatalog; } // I7-C1
namespace experiment { struct AssetId; } // I7-C2
class Mesh; // I5-D34a 병행 바인딩 조회 입력
struct RHIExperimentVertexView; // I5-D34a 병행 바인딩 조회 출력

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
	// C4 이후 Model/Material의 장기 소비자는 shared_ptr를 보유한다. Texture 계열은
	// TerrainLayer와 legacy Material raw 별칭이 남아 있어 별도 이행 전까지 이전
	// cache generation을 runtime 종료까지 보존한다.
	static constexpr bool RequiresLegacyRetiredGeneration(
		RuntimeAssetType assetType) noexcept
	{
		return assetType == RuntimeAssetType::Texture
			|| assetType == RuntimeAssetType::UITexture
			|| assetType == RuntimeAssetType::SpriteSheet;
	}

	void Initialize();
    void Finalize();
	// Asset bundle operations
	void LoadAssetBundle(const AssetBundle& bundle);
	void RetainAssets(const AssetBundle& bundle);
	void ClearRetainedAssets();
	void UnloadUnusedAssets();
	//Resource Model
	Model* LoadModelGUID(FileGuid guid);
	// I5-D1a — experiment::Model → legacy ::Model 역브리지(전환기, I6 은퇴).
	// 렌더 소유가 아직 legacy인 동안 experiment 로드 결과를 기존 파이프에
	// 소비시키는 어댑터다(I5-M의 ConvertToLegacyMaterial과 같은 지위).
	// Model 컨테이너가 private+friend라 DataSystem 멤버로만 시공 가능하다 —
	// 정의는 ExperimentModelMigration.cpp(별도 TU).
	// I7-C1 — cooked catalog 기동. `<derivedRoot>/Derived/asset-manifest.cemf`를
	// 읽어 자산 GUID→cooked artifact 표를 세운다. 파일이 없으면 **무동작**이다
	// (에디터 작업 트리에는 Derived가 없다 — 마운트 실패가 아니라 미게시다).
	// 이것이 M2 resolver의 cooked 우선 해석과 모델 cookedPath의 유일한 출처다.
	bool MountCookedCatalog(const file::path& derivedRoot, std::string& outError);
	// 수명 안전: 호출자가 shared_ptr을 잡은 동안만 raw 포인터를 쓴다
	// (마운트가 렌더 중에 표를 갈아 끼워도 진행 중인 해석이 살아 있어야 한다).
	[[nodiscard]] std::shared_ptr<const experiment::cooked::CookedAssetCatalog>
		GetCookedCatalog() const;
	[[nodiscard]] std::size_t CookedCatalogEntryCount() const;
	[[nodiscard]] std::size_t CookedCatalogSourceAssetCount() const;
	// I7-C2 — 신선도 판정. cooked artifact가 소스보다 낡았으면 그 entry는 없는
	// 것으로 친다(빈 경로) — 모델은 source 디코더로, 텍스처는 source 폴백으로
	// 간다. 두 정책 모두 이미 서 있어서 여기서 경로만 끊으면 된다.
	//
	// 판정 기준은 **mtime**이다(아티팩트가 소스보다 오래되면 낡음). 아티팩트
	// 안에 소스 시각을 넣는 길은 막혀 있다 — ModelCookProducer가 결정적 cook을
	// 위해 일부러 지운다("같은 Assets tree를 어느 staging 경로에 놓아도 동일한
	// CEMC"). 내구적인 답은 소스 **내용 해시**를 CEMF에 싣는 것이고 그것은
	// 포맷 확장이라 별도 슬라이스다 — 그때까지 이 heuristic이 자리를 지킨다.
	[[nodiscard]] file::path ResolveCookedArtifact(
		const experiment::AssetId& assetId) const;
	// D5-d document cutover. Produced runtime assets use a fresh cooked artifact
	// when one exists. Editor may fall back to the authoring source; packaged
	// Player fails closed instead of silently reopening source YAML/bytes.
	[[nodiscard]] file::path ResolveCatalogAssetPath(FileGuid assetGuid) const;
	[[nodiscard]] std::size_t CookedCatalogStaleCount() const;

	[[nodiscard]] bool BuildLegacyModelFromExperiment(
		const experiment::Model& source, std::shared_ptr<Model>& outModel,
		std::string& outError);
	// I5-D1b — 로더 이중화의 experiment 쪽 절반: cooked→source 해석 디코더로
	// 로드해 역브리지로 내린다. 실패는 null(호출자가 Assimp 폴백). cookedPath는
	// cooked 게시 규약(pak)이 서기 전까지 빈 경로다(resolver가 Info로 계수).
	[[nodiscard]] std::shared_ptr<Model> LoadModelViaExperiment(
		FileGuid guid, const file::path& sourcePath);
	// I5-D34a — 병행 바인딩 조회: legacy Mesh 신원(m_hashingMesh)으로 experiment
	// packed 정점 뷰를 돌려준다. 스킨 레이아웃은 D34b 전까지 닫혀 있다(false).
	// RHI 메시 캐시에 함수로 주입되는 것이 소비자다 — 캐시는 이 클래스를 모른다.
	[[nodiscard]] bool TryGetExperimentVertexView(
		const Mesh& mesh, RHIExperimentVertexView& outView);
	// I5-D4b — 핸들 병행: 프록시 생성이 legacy Mesh 신원으로 experiment
	// (모델, 메시 인덱스) 페어를 얻어 렌더 사슬에 싣는다. A/B 스위치가 꺼져
	// 있으면 false(핸들 자체가 실리지 않아 대조군이 성립한다).
	[[nodiscard]] bool TryGetExperimentMeshBinding(const Mesh& mesh,
		std::shared_ptr<const experiment::Model>& outModel,
		std::uint32_t& outMeshIndex);
	// I5-D4b — 핸들→뷰 변환(순수, 스위치 무관). 신원 키(stableKey)까지 채워
	// 뷰가 IsHandleComplete가 되면 캐시의 핸들 진입점이 소비한다.
	[[nodiscard]] static bool BuildExperimentVertexView(
		const experiment::Model& model, std::uint32_t meshIndex,
		RHIExperimentVertexView& outView);
	// I5-D4c — 모델 GUID로 experiment 모델을 얻는다. MeshRenderer postLoad의
	// 이름→메시 해석이 experiment를 정본으로 삼는 창구다. A/B 스위치가 꺼져
	// 있거나 Assimp 폴백 모델이면 null(해석이 legacy로 간다).
	[[nodiscard]] std::shared_ptr<const experiment::Model>
		TryGetExperimentModel(FileGuid modelGuid);
	void LoadModel(std::string_view filePath);
	std::shared_ptr<Model> LoadCachedModelShared(std::string_view filePath);
	// 즉시 사용 legacy 호출부 호환. 참조를 보관하는 쪽은 위 shared API를 쓴다.
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
	bool SerializeMaterialPayload(Material& material,
		Authoring::WriteNode outNode) const;
	bool DeserializeMaterialPayload(Material& material, const Authoring::NodeView& node);
	// I5-D5c1 — 저작 원본 보관 창구. 새 정본(schema+shaderAssetId) 문서는
	// experiment::Material로 읽힌 뒤 legacy로 변환되고 **원본이 버려져 왔다** —
	// 그래서 sealing이 매 프레임 legacy를 experiment로 되돌린다(왕복). 이 창구는
	// 그 원본을 함께 돌려준다. legacy 표기 문서에는 원본이 없으므로
	// outAuthored는 채워지지 않는다(반환값은 그대로 성공).
	bool DeserializeMaterialPayload(Material& material,
		const Authoring::NodeView& node, experiment::Material* outAuthored);
	// I5-D5c1 — base 재질 자산의 저작 원본. 씬의 ref 표기가 base를 legacy로만
	// 로드해 왔다(LoadMaterialShared). 실패·legacy 표기 자산은 nullptr다.
	std::shared_ptr<const experiment::Material> LoadAuthoredMaterialShared(
		FileGuid assetGuid);
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
	// Watcher I/O thread는 cache/catalog를 직접 바꾸지 않고 이 큐에 게시한다.
	// Editor game thread가 프레임 경계에서 DrainQueuedAssetChanges를 호출해
	// generation 변경과 이후 load의 관측 순서를 직렬화한다.
	void QueueAssetChange(RuntimeAssetChange change);
	std::size_t DrainQueuedAssetChanges();
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

	// I5-D5c1 — base 재질 자산의 저작 원본 캐시(GUID 키). legacy Materials
	// 캐시와 별개다: 그쪽은 변환 산물이고 이쪽이 원본이다. 자체 뮤텍스를
	// 쓴다 — LoadAuthoredMaterialShared는 파일 파싱 중 legacy 캐시를 건드리지
	// 않으므로 m_materialMutex와 겹칠 이유가 없다.
	std::mutex m_authoredMaterialMutex;
	std::unordered_map<FileGuid,
		std::shared_ptr<const experiment::Material>> m_authoredMaterials;

	// I5-D34a — 병행 바인딩: legacy Mesh 신원 → {experiment 모델, 메시 인덱스}.
	// LoadModelViaExperiment 성공 시 채워지고, 조회는 렌더 캐시 주입 함수가
	// 한다. shared_ptr이 experiment 모델의 수명을 여기서 잡아 준다 — 역브리지가
	// legacy만 남기고 버리면 packed 정점의 출처가 사라지기 때문이다. 항목은
	// Models 캐시와 같은 성격의 영구 캐시다(브리지와 함께 I6에서 은퇴).
	struct ExperimentMeshBinding
	{
		std::shared_ptr<const experiment::Model> model;
		std::uint32_t meshIndex{ 0 };
	};
	std::unordered_map<HashedGuid, ExperimentMeshBinding> m_experimentMeshBindings;
	// I5-D4c — 모델 GUID(.meta 정본)→experiment 모델. 병행 바인딩과 같은
	// 지점에서 등록되고 같은 뮤텍스를 쓴다.
	std::unordered_map<FileGuid,
		std::shared_ptr<const experiment::Model>> m_experimentModels;
	std::mutex m_experimentMeshMutex;
	// I7-C1 — cooked catalog. immutable 표라 교체는 포인터 하나 바꾸기다.
	std::shared_ptr<const experiment::cooked::CookedAssetCatalog> m_cookedCatalog;
	mutable std::mutex m_cookedCatalogMutex;
	// I7-C2 — 마운트 때 한 번 판정한 stale 집합. 해석마다 stat을 두 번 하면
	// sealing이 매 프레임 그 값을 문다.
	std::unordered_set<FileGuid> m_cookedStaleAssets;

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
	// Texture 계열의 legacy raw 별칭만을 위한 한시적 보존 목록. Model/Material은
	// cache 분리 뒤 실제 shared consumer가 없으면 즉시 파괴된다.
	std::mutex m_retiredTextureMutex;
	std::vector<std::shared_ptr<void>> m_retiredTextureGenerations;

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
	std::mutex m_pendingAssetChangeMutex;
	std::vector<RuntimeAssetChange> m_pendingAssetChanges;

};

static auto DataSystems = DataSystem::GetInstance();

