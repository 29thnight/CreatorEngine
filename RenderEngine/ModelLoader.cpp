#include "ModelLoader.h"

ModelLoader::ModelLoader(Model* model, Scene* scene) :
	m_model(model),
	m_scene(scene)
{
}

ModelLoader::ModelLoader(const aiScene* assimpScene, const std::string_view& fileName) :
	m_AIScene(assimpScene),
	m_skeletonLoader(assimpScene)
{
	file::path filepath(fileName);
	m_directory = filepath.parent_path().string() + "\\";
	if (filepath.extension() == ".obj")
	{
		m_loadType = LoadType::OBJ;
	}
	else if (filepath.extension() == ".gltf")
	{
		m_loadType = LoadType::GLTF;
	}
	else if (filepath.extension() == ".fbx")
	{
		m_loadType = LoadType::FBX;
	}
	m_model = new Model();
	m_model->m_node = m_AIScene->mRootNode;
	m_model->m_SceneObject = std::make_shared<SceneObject>(
		filepath.filename().string(), false, 0);
}

Model* ModelLoader::LoadModel()
{
	ProcessMeshes();
	if (m_hasBones)
	{
		Skeleton* skeleton = m_skeletonLoader.GenerateSkeleton(m_AIScene->mRootNode);
		m_model->m_Skeleton = skeleton;
		Animator& animator = m_model->m_SceneObject->m_animator;
		animator.m_IsEnabled = true;
		animator.m_Skeleton = skeleton;
		m_animator = &animator;
	}

	return m_model;
}

void ModelLoader::ProcessMeshes()
{
	ProcessMeshRecursive(m_AIScene->mRootNode);
}

void ModelLoader::ProcessMeshRecursive(aiNode* node)
{
	//if (node->mNumMeshes == 0)
	//{
	//	m_transform = XMMatrixTranspose(XMMATRIX(&node->mTransformation.a1));
	//}

	for (uint32 i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = m_AIScene->mMeshes[node->mMeshes[i]];
		GenerateMesh(mesh);
	}

	for (uint32 i = 0; i < node->mNumChildren; i++)
	{
		ProcessMeshRecursive(node->mChildren[i]);
	}
}

Mesh* ModelLoader::GenerateMesh(aiMesh* mesh)
{
	std::vector<Vertex> vertices;
	std::vector<uint32> indices;
	bool hasTexCoords = mesh->mTextureCoords[0];

	for (uint32 i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;
		vertex.position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
		vertex.normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
		vertex.tangent = { mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z };
		vertex.bitangent = { mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z };
		if (hasTexCoords)
		{
			vertex.uv = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
		}
		vertices.push_back(vertex);
	}

	for (uint32 i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (uint32 j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	ProcessBones(mesh, vertices);
	Mesh* meshObj = new Mesh(mesh->mName.C_Str(), vertices, indices);
	m_model->m_Meshes.push_back(meshObj);
	m_model->m_Materials.push_back(new Material());

	return meshObj;
}

void ModelLoader::ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices)
{
	for (uint32 i = 0; i < mesh->mNumBones; ++i)
	{
		m_hasBones = true;
		aiBone* bone = mesh->mBones[i];
		int boneIndex = m_skeletonLoader.AddBone(bone);
		for (uint32 j = 0; j < bone->mNumWeights; ++j)
		{
			aiVertexWeight weight = bone->mWeights[j];
			uint32 vertexId = weight.mVertexId;
			float weightValue = weight.mWeight;

			for (uint32 k = 0; k < 4; ++k)
			{
				Vertex& vertex = vertices[vertexId];
				if (Mathf::GetFloatAtIndex(vertex.boneWeights, k) == 0.0f)
				{
					Mathf::SetFloatAtIndex(vertex.boneIndices, k, boneIndex);
					Mathf::SetFloatAtIndex(vertex.boneWeights, k, weightValue);
					break;
				}
			}
		}
	}
}

void ModelLoader::GenerateSceneObjectHierarchy(aiNode* node, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	if (true == isRoot)
	{
		m_model->m_SceneObject = m_scene->AddSceneObject(m_model->m_SceneObject);
		nextIndex = m_model->m_SceneObject->m_index;
	}

	for (UINT i = 0; i < node->mNumMeshes; ++i)
	{
		std::shared_ptr<SceneObject> object = isRoot ? m_model->m_SceneObject : m_scene->CreateSceneObject(node->mName.C_Str(), nextIndex);

		unsigned int meshId = node->mMeshes[i];
		Mesh* mesh = m_model->m_Meshes[meshId];
		Material* material = m_model->m_Materials[meshId];
		MeshRenderer& meshRenderer = object->m_meshRenderer;
		meshRenderer.m_IsEnabled = true;
		meshRenderer.m_Mesh = mesh;
		meshRenderer.m_Material = material;
		object->m_transform.SetLocalMatrix(XMMatrixTranspose(XMMATRIX(&node->mTransformation.a1)));

		if (m_hasBones)
		{
			meshRenderer.m_Animator = m_animator;
		}
		isRoot = false;
		nextIndex = object->m_index;
	}

	for (UINT i = 0; i < node->mNumChildren; ++i)
	{
		GenerateSceneObjectHierarchy(node->mChildren[i], false, nextIndex);
	}
}
