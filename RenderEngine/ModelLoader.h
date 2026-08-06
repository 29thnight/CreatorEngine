#pragma once
#include <initializer_list>
#include "../Utility_Framework/Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Model.h"
#include "Scene.h"
#include "Skeleton.h"
#include "SkeletonLoader.h"

class ModelLoader
{
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
	// 캐시(DataSystems->Materials)와 모델이 함께 소유하므로 shared_ptr을 그대로 돌려준다.
	// 원시 포인터로 돌려주면 호출부가 별도 제어 블록을 만들어 이중 해제가 된다.
	std::shared_ptr<Material> GenerateMaterial(int index = -1);

	//Save To InHouse Format
	void ParseModel();
	void ParseNodes(std::ofstream& outfile);
	void ParseNode(std::ofstream& outfile, const ModelNode* node);
    void ParseMeshes(std::ofstream& outfile);
    void ParseMaterials(std::ofstream& outfile);
    void ParseSkeleton(std::ofstream& outfile);

    void LoadModelFromAsset();
    void LoadNodes(std::ifstream& infile, uint32_t size);
    void LoadNode(std::ifstream& infile, ModelNode*& node);
    void LoadMesh(std::ifstream& infile, uint32_t size);
    void LoadMaterial(std::ifstream& infile, uint32_t size);
    void LoadSkeleton(std::ifstream& infile);

	Model* LoadModel(bool isCreateMeshCollider = false);
	void GenerateSceneObjectHierarchy(ModelNode* node, bool isRoot, int parentIndex);
	void GenerateSkeletonToSceneObjectHierarchy(ModelNode* node, Bone* bone, bool isRoot, int parentIndex);

	GameObject* GenerateSceneObjectHierarchyObj(ModelNode* node, bool isRoot, int parentIndex);
	GameObject* GenerateSkeletonToSceneObjectHierarchyObj(ModelNode* node, Bone* bone, bool isRoot, int parentIndex);
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
    bool ExtractEmbeddedTextureToFile(const aiTexture* embedded, const file::path& destination) const;

	const aiScene* m_AIScene;
	LoadType m_loadType{ LoadType::UNKNOWN };
	std::string m_directory{};
	std::string m_metaDirectory{};
	FileGuid m_fileGuid{};

	Model* m_model{};
	Material* m_material{};
	Animator* m_animator{};
	Scene* m_scene{};
	Mathf::Matrix m_transform{ XMMatrixIdentity() };
	SkeletonLoader m_skeletonLoader;
	std::mutex m_modelMutex;

	// 하나의 임베디드 텍스처를 여러 머티리얼이 참조하는 일이 흔하므로(glTF는 이미지와
	// 머티리얼이 N:1) 모델 로드 범위에서 aiTexture 포인터를 키로 캐시한다.
	std::unordered_map<const aiTexture*, std::shared_ptr<Texture>> m_embeddedTextures{};

	std::vector<std::shared_ptr<GameObject>> m_gameObjects{};
	std::vector<std::string> m_cashedObjectName{};

	Mathf::Vector3 min{};
	Mathf::Vector3 max{};

	bool m_hasBones{ false };
	bool m_isInitialized{ false };
	bool m_isSkinnedMesh{ false };
	int m_numUVChannel{ 0 };
	int m_modelRootIndex{ 0 };
};