#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Model.h"
#include "Scene.h"
#include "Skeleton.h"
#include "SkeletonLoader.h"
#include "ObjectRenderers.h"

struct BinaryNode
{
	std::string name;
	XMMATRIX localTransform;
	std::vector<Mesh*> meshes;     // 노드가 포함한 메쉬들
	std::vector<uint32_t> children;     // 자식 노드 인덱스들
};

class ModelLoader
{
	enum class LoadType
	{
		UNKNOWN,
		OBJ,
		GLTF,
		FBX
	};

private:
	friend class Model;
	ModelLoader() = default;
	ModelLoader(Model* model, Scene* scene);
	ModelLoader(const aiScene* assimpScene, const std::string_view& fileName);
	
	Model* LoadModel();
	void ProcessMeshes();
	void ProcessMeshRecursive(aiNode* node);
	Mesh* GenerateMesh(aiMesh* mesh);
	void ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices);
	void GenerateSceneObjectHierarchy(aiNode* node, bool isRoot, int parentIndex);

	void ConvertToAiNode(aiNode* node, int parentIndex);
	void SaveModelBinary(const std::string_view& filePath);
	void LoadModelBinary(const std::string_view& filePath);


	const aiScene* m_AIScene;
	LoadType m_loadType{ LoadType::UNKNOWN };
	std::string m_directory;

	Model* m_model;
	Scene* m_scene;
	Mathf::Matrix m_transform{ XMMatrixIdentity() };
	SkeletonLoader m_skeletonLoader;
	Animator* m_animator;

	std::vector<BinaryNode> nodes;

	bool m_isInitialized{ false };
};