#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Skeleton.h"
#include "SkeletonLoader.h"

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
	ModelLoader(const aiScene* assimpScene);

	void ProcessMeshes();
	Mesh* GenerateMesh(aiMesh* mesh);
	void ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices);
	void GenerateSceneObjectHierarchy(aiNode* node, bool isRoot, int parentIndex);
};