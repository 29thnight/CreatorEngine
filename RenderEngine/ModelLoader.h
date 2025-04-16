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

	//Load From ASSIMP library
	void ProcessNodes();
	Node* ProcessNode(aiNode* node, int parentIndex);
	void ProcessFlatMeshes();
	void ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices);
	Mesh* GenerateMesh(aiMesh* mesh);
	void ProcessMaterials();
	Material* GenerateMaterial(aiMesh* mesh);

	//Save To InHouse Format
	void ParseModel();
	void ParseNodes(std::fstream& outfile);
	void ParseNode(std::fstream& outfile, Node* node);
	void ParseMeshes(std::fstream& outfile);
	void ParseMaterials(std::fstream& outfile);

	void LoadModelFromAsset();
	void LoadNodes(std::fstream& infile, uint32 size);
	void LoadNode(std::fstream& infile, Node* node);
	void LoadMesh(std::fstream& infile);
	void LoadMaterial(std::fstream& infile);

	Model* LoadModel();
	void GenerateSceneObjectHierarchy(Node* node, bool isRoot, int parentIndex);
	void GenerateSkeletonToSceneObjectHierarchy(Node* node, Bone* bone, bool isRoot, int parentIndex);
	Texture* GenerateTexture(aiMaterial* material, aiTextureType type, uint32 index = 0);

	std::shared_ptr<Assimp::Importer> m_importer{};
	const aiScene* m_AIScene;
	LoadType m_loadType{ LoadType::UNKNOWN };
	std::string m_directory;

	Model* m_model{};
	Material* m_material{};
	Animator* m_animator{};
	Scene* m_scene{};
	Mathf::Matrix m_transform{ XMMatrixIdentity() };
	SkeletonLoader m_skeletonLoader;

	Mathf::Vector3 min;
	Mathf::Vector3 max;

	bool m_hasBones{ false };
	bool m_isInitialized{ false };
	int m_modelRootIndex{ 0 };
};