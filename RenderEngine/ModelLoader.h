#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Model.h"
#include "Scene.h"
#include "Skeleton.h"
#include "SkeletonLoader.h"
#include "ObjectRenderers.h"

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
	ModelLoader(Model* model, Scene* scene);
	ModelLoader(const aiScene* assimpScene, const std::string_view& fileName);
	
	Model* LoadModel();
	void ProcessMeshes();
	void ProcessMeshRecursive(aiNode* node);
	Mesh* GenerateMesh(aiMesh* mesh);
	void ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices);
	void GenerateSceneObjectHierarchy(aiNode* node, bool isRoot, int parentIndex);

	const aiScene* m_AIScene;
	LoadType m_loadType{ LoadType::UNKNOWN };
	std::string m_directory;

	Model* m_model;
	Scene* m_scene;
	Mathf::Matrix m_transform{ XMMatrixIdentity() };
	SkeletonLoader m_skeletonLoader;
	Animator* m_animator;

	bool m_isInitialized{ false };
};