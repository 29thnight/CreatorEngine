#pragma once
#include <initializer_list>
#include <iosfwd>
#include <unordered_set>
#include <assimp/scene.h>
#include "../Utility_Framework/Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Model.h"
class Scene;
class Entity;
#include "Skeleton.h"
#include "SkeletonLoader.h"

class ModelLoader
{
public:
    // 쿠킹(.asset) 로드의 구간 계측.
    //
    // 왜 두는가: 쿠킹 로드가 어디에 시간을 쓰는지 몰라 최적화 방향을 추측으로
    // 정하고 있었다. 실제로 한 번 틀렸다 — 병목이 객체 구축이라고 보고 mmap
    // 설계까지 제안했는데, 파일 밖 측정에서 원인은 요소별 `ifstream::read`
    // 였고 mmap 은 ReadFile 보다 오히려 느렸다.
    //
    // ★ 텍스처 시간을 따로 낸다. 그것은 임포터를 바꿔도 그대로 남는 **공유
    //   비용**이라, 합쳐서 재면 이겼는지 졌는지를 가릴 수 없다.
    struct CookedLoadBreakdown final
    {
        double openMs{};             // ifstream 열기
        double skeletonMs{};         // 뼈 + 클립 + 키프레임
        double nodesMs{};            // 노드 트리
        double meshesMs{};           // 정점·인덱스(일괄 읽기)
        double materialsMs{};        // 재질 전체
        double materialTextureMs{};  // ↑ 중 RetainMaterialTextures (텍스처 I/O)
        double totalMs{};
        bool   valid{};
    };

    // 이 스레드에서 마지막으로 끝난 쿠킹 로드의 내역.
    // ★ 스레드마다 따로 둔다 — 모델 로드는 워커에서도 일어나므로 전역 하나면
    //   서로 덮어써 엉뚱한 자산의 내역을 읽게 된다.
    [[nodiscard]] static const CookedLoadBreakdown& LastCookedLoadBreakdown() noexcept;

private:
    [[nodiscard]] static CookedLoadBreakdown& MutableCookedLoadBreakdown() noexcept;

    enum class LoadType
    {
            UNKNOWN,
            OBJ,
            GLTF,
            FBX,
            ASSET
    };

private:
    friend class Model;
	ModelLoader();
	~ModelLoader();
	ModelLoader(Model* model, Scene* scene);
	ModelLoader(std::string_view fileName);
	ModelLoader(const aiScene* assimpScene, std::string_view fileName);

	size_t CountNodes(aiNode* root);

	void ProcessNodes();
	ModelNode* ProcessNode(aiNode* node, int parentIndex);
	void ProcessFlatMeshes();
	void ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices);
	Mesh* GenerateMesh(aiMesh* mesh);
	void ProcessMaterials();
	// 모델 .meta에서 파일 guid를 읽어 온다. 재질마다 읽으면 같은 YAML을 재질 수만큼
	// 재파싱하게 되므로 임포트당 한 번만 부른다.
	void ResolveFileGuidFromMeta();
	// 캐시(DataSystems->Materials)와 모델이 함께 소유하므로 shared_ptr을 그대로 돌려준다.
	// 원시 포인터로 돌려주면 호출부가 별도 제어 블록을 만들어 이중 해제가 된다.
	std::shared_ptr<Material> GenerateMaterial(int index = -1);

	//Save To InHouse Format
	void RequestModelCacheWrite();
	void SerializeNodes(std::ostream& output);
	void SerializeNode(std::ostream& output, const ModelNode* node);
    void SerializeMeshes(std::ostream& output);
    void SerializeMaterials(std::ostream& output);
    void SerializeSkeleton(std::ostream& output);

    void LoadModelFromAsset();
    void LoadNodes(std::ifstream& infile, uint32_t size);
    void LoadNode(std::ifstream& infile, ModelNode*& node);
    void LoadMesh(std::ifstream& infile, uint32_t size);
    void LoadMaterial(std::ifstream& infile, uint32_t size);
    void LoadSkeleton(std::ifstream& infile);
	void RetainMaterialTextures(Material& material);

	Model* LoadModel(bool isCreateMeshCollider = false);
	void GenerateSceneObjectHierarchy(ModelNode* node, bool isRoot, int parentIndex);
	void GenerateSkeletonToSceneObjectHierarchy(ModelNode* node, Bone* bone, bool isRoot, int parentIndex);

	Entity* GenerateSceneObjectHierarchyObj(ModelNode* node, bool isRoot, int parentIndex);
	Entity* GenerateSkeletonToSceneObjectHierarchyObj(ModelNode* node, Bone* bone, bool isRoot, int parentIndex);
    Texture* GenerateTexture(aiMaterial* material, aiTextureType type, uint32 index = 0, bool isCompress = false);

    /// 여러 슬롯을 순서대로 훑어 처음 잡히는 텍스처를 돌려준다.
    ///
    /// Assimp의 glTF 임포터가 baseColor를 BASE_COLOR에 넣는지 DIFFUSE에
    /// 넣는지는 버전을 탄다. 한 슬롯만 보면 파일에 텍스처가 있는데도 재질이
    /// 비어 있는 상태가 되고, 화면에서는 검게 나오는 것으로만 보인다.
    Texture* GenerateTextureFromAny(aiMaterial* material,
        std::initializer_list<aiTextureType> candidates, std::string_view label);
    Texture* GenerateTexture(std::string_view textureName, bool isCompress = false);
    // GLB/glTF처럼 텍스처 바이트가 모델 파일 안에 들어있는 경우를 처리한다.
    // 파일로 뽑아낸 뒤 일반 텍스처 경로에 태운다(이유는 구현부 주석 참고).
    Texture* GenerateEmbeddedTexture(const aiTexture* embedded, std::string_view reference, bool isCompress = false);
    std::string MakeEmbeddedTextureFileName(const aiTexture* embedded, std::string_view reference) const;

	const aiScene* m_AIScene;
	LoadType m_loadType{ LoadType::UNKNOWN };
	std::string m_directory{};
	std::string m_metaDirectory{};
	FileGuid m_fileGuid{};

	Model* m_model{};
	Material* m_material{};
	Animator* m_animator{};
	Scene* m_scene{};
	SkeletonLoader m_skeletonLoader;
	std::mutex m_modelMutex;

	// 하나의 임베디드 텍스처를 여러 머티리얼이 참조하는 일이 흔하므로(glTF는 이미지와
	// 머티리얼이 N:1) 모델 로드 범위에서 aiTexture 포인터를 키로 캐시한다.
	std::unordered_map<const aiTexture*, std::shared_ptr<Texture>> m_embeddedTextures{};

	// 이번 임포트에서 이미 발급한 머티리얼 이름. 파일 하나 안의 서로 다른 재질이
	// 같은 이름(특히 무명)으로 겹칠 때 이것들을 한 재질로 접지 않기 위한 표식이다.
	std::unordered_set<std::string> m_issuedMaterialNames{};

	std::vector<Entity*> m_gameObjects{};
	std::vector<std::string> m_cashedObjectName{};

	bool m_hasBones{ false };
	bool m_isInitialized{ false };
	bool m_isSkinnedMesh{ false };
	int m_numUVChannel{ 0 };
	int m_modelRootIndex{ 0 };
};
