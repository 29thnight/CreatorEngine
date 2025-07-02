#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Model.h"
#include "Scene.h"
#include "Skeleton.h"
#include "SkeletonLoader.h"
#include "RenderableComponents.h"

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
	ModelLoader(const std::string_view& fileName);
	ModelLoader(const aiScene* assimpScene, const std::string_view& fileName);

	void ProcessNodes();
	ModelNode* ProcessNode(aiNode* node, int parentIndex);
	void ProcessFlatMeshes();
	void ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices);
	Mesh* GenerateMesh(aiMesh* mesh);
	void ProcessMaterials();
	Material* GenerateMaterial(int index = -1);

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
    Texture* GenerateTexture(aiMaterial* material, aiTextureType type, uint32 index = 0);
    Texture* GenerateTexture(const std::string& textureName);

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

	std::vector<std::shared_ptr<GameObject>> m_gameObjects{};

	Mathf::Vector3 min{};
	Mathf::Vector3 max{};

	bool m_hasBones{ false };
	bool m_isInitialized{ false };
	bool m_isSkinnedMesh{ false };
	int m_numUVChannel{ 0 };
	int m_modelRootIndex{ 0 };
};