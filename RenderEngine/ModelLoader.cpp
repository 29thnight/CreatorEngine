#include "ModelLoader.h"
#include "Banchmark.hpp"
#include "PathFinder.h"

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
	m_model->name = filepath.filename().string();
	m_model->m_SceneObject = std::make_shared<SceneObject>(
		m_model->name, false, 0);
}

Model* ModelLoader::LoadModel()
{
	ProcessMeshes();
	if (m_model->m_hasBones)
	{
		Skeleton* skeleton = m_skeletonLoader.GenerateSkeleton(m_AIScene->mRootNode);
		m_model->m_Skeleton = skeleton;
		Animator* animator = m_model->m_SceneObject->m_animator = new Animator();
		animator->m_IsEnabled = true;
		animator->m_Skeleton = skeleton;
		m_animator = animator;
	}

	return m_model;
}

void ModelLoader::ProcessMeshes()
{
	ProcessMeshRecursive(m_AIScene->mRootNode);
	ConvertToAiNode(m_AIScene->mRootNode, 0);
	__debugbreak();
}

void ModelLoader::ProcessMeshRecursive(aiNode* node)
{

	for (uint32 i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* aimesh = m_AIScene->mMeshes[node->mMeshes[i]];
		GenerateMesh(aimesh);
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
		m_model->m_hasBones = true;
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
		auto rootObject = m_scene->CreateSceneObject(m_model->m_SceneObject->m_name, nextIndex);
		nextIndex = rootObject->m_index;
		
		if (m_model->m_hasBones)
		{
			Banchmark banch;	
			rootObject->m_animator = new Animator();
			rootObject->m_animator->m_IsEnabled = true;
			rootObject->m_animator->m_Skeleton = m_model->m_Skeleton;

			std::cout << "GenerateSceneObjectHierarchy new Animator : " << banch.GetElapsedTime() << std::endl;
		}
	}

	for (UINT i = 0; i < node->mNumMeshes; ++i)
	{
		Banchmark banch;
		std::shared_ptr<SceneObject> object = m_scene->CreateSceneObject(node->mName.C_Str(), nextIndex);

		unsigned int meshId = node->mMeshes[i];
		Mesh* mesh = m_model->m_Meshes[meshId];
		Material* material = m_model->m_Materials[meshId];
		MeshRenderer& meshRenderer = object->m_meshRenderer;
		meshRenderer.m_IsEnabled = true;
		meshRenderer.m_Mesh = mesh;
		meshRenderer.m_Material = material;
		object->m_transform.SetLocalMatrix(XMMatrixTranspose(XMMATRIX(&node->mTransformation.a1)));

		nextIndex = object->m_index;

		std::cout << "GenerateSceneObjectHierarchy new SceneObject : " << banch.GetElapsedTime() << std::endl;
	}

	for (UINT i = 0; i < node->mNumChildren; ++i)
	{
		GenerateSceneObjectHierarchy(node->mChildren[i], false, nextIndex);
	}
}

void ModelLoader::ConvertToAiNode(aiNode* node, int parentIndex)
{
	BinaryNode binaryNode;
	binaryNode.name = node->mName.C_Str();
	binaryNode.localTransform = XMMatrixTranspose(XMMATRIX(&node->mTransformation.a1));

	// 메쉬 포함하기
	for (uint32 i = 0; i < node->mNumMeshes; ++i)
	{
		uint32 meshIndex = node->mMeshes[i];

		Mesh* mesh = m_model->m_Meshes[meshIndex];

		binaryNode.meshes.push_back(mesh);
	}

	// 현재 노드 인덱스를 기록하고, 자식 정보를 연결
	uint32_t currentIndex = static_cast<uint32_t>(nodes.size());
	nodes.push_back(std::move(binaryNode));

	// 자식 순회
	for (uint32 i = 0; i < node->mNumChildren; ++i)
	{
		uint32_t childIndex = static_cast<uint32_t>(nodes.size());
		ConvertToAiNode(node->mChildren[i], currentIndex);
		nodes[currentIndex].children.push_back(childIndex);
	}
}

void ModelLoader::SaveModelBinary(const std::string_view& filePath)
{
	std::string modelPath = "Models\\";
	file::path file = PathFinder::Relative(modelPath + filePath.data());
	std::ofstream out(file, std::ios::binary);

	if (out.is_open())
	{
		for (const auto& node : nodes)
		{
			// 이름
			size_t nameLen = node.name.size();
			out.write(reinterpret_cast<const char*>(&nameLen), sizeof(size_t));
			out.write(node.name.c_str(), nameLen);

			// 트랜스폼
			out.write(reinterpret_cast<const char*>(&node.localTransform), sizeof(XMMATRIX));

			// 자식 노드 인덱스
			uint32_t childCount = static_cast<uint32_t>(node.children.size());
			out.write(reinterpret_cast<const char*>(&childCount), sizeof(uint32_t));
			out.write(reinterpret_cast<const char*>(node.children.data()), childCount * sizeof(uint32_t));

			// 포함된 메쉬
			uint32_t meshCount = static_cast<uint32_t>(node.meshes.size());
			out.write(reinterpret_cast<const char*>(&meshCount), sizeof(uint32_t));
			for (const auto& mesh : m_model->m_Meshes)
			{
				// 메쉬 이름
				size_t meshNameLen = mesh->m_name.size();
				out.write(reinterpret_cast<const char*>(&meshNameLen), sizeof(size_t));
				out.write(mesh->m_name.c_str(), meshNameLen);
				// 정점
				uint32_t vertexCount = static_cast<uint32_t>(mesh->m_vertices.size());
				out.write(reinterpret_cast<const char*>(&vertexCount), sizeof(uint32_t));
				out.write(reinterpret_cast<const char*>(mesh->m_vertices.data()), vertexCount * sizeof(Vertex));

				// 인덱스
				uint32_t indexCount = static_cast<uint32_t>(mesh->m_indices.size());
				out.write(reinterpret_cast<const char*>(&indexCount), sizeof(uint32_t));
				out.write(reinterpret_cast<const char*>(mesh->m_indices.data()), indexCount * sizeof(uint32_t));
			}
		}
	}

	out.close();
}

void ModelLoader::LoadModelBinary(const std::string_view& filePath)
{
	std::string modelPath = "Models\\";
	file::path file = PathFinder::Relative(modelPath + filePath.data());
	std::ifstream in(file, std::ios::binary);

	m_model = new Model();

	if (in.is_open())
	{
		while (!in.eof())
		{
			BinaryNode node;
			// 이름
			size_t nameLen;
			in.read(reinterpret_cast<char*>(&nameLen), sizeof(size_t));
			node.name.resize(nameLen);
			in.read(const_cast<char*>(node.name.c_str()), nameLen);
			// 트랜스폼
			in.read(reinterpret_cast<char*>(&node.localTransform), sizeof(XMMATRIX));
			// 자식 노드 인덱스
			uint32_t childCount;
			in.read(reinterpret_cast<char*>(&childCount), sizeof(uint32_t));
			node.children.resize(childCount);
			in.read(reinterpret_cast<char*>(node.children.data()), childCount * sizeof(uint32_t));
			// 포함된 메쉬
			uint32_t meshCount;
			in.read(reinterpret_cast<char*>(&meshCount), sizeof(uint32_t));
			for (uint32_t i = 0; i < meshCount; ++i)
			{
				Mesh* mesh = new Mesh();
				// 메쉬 이름
				size_t meshNameLen;
				in.read(reinterpret_cast<char*>(&meshNameLen), sizeof(size_t));
				mesh->m_name.resize(meshNameLen);
				in.read(const_cast<char*>(mesh->m_name.c_str()), meshNameLen);
				// 정점
				uint32_t vertexCount;
				in.read(reinterpret_cast<char*>(&vertexCount), sizeof(uint32_t));
				mesh->m_vertices.resize(vertexCount);
				in.read(reinterpret_cast<char*>(mesh->m_vertices.data()), vertexCount * sizeof(Vertex));
				// 인덱스
				uint32_t indexCount;
				in.read(reinterpret_cast<char*>(&indexCount), sizeof(uint32_t));
				mesh->m_indices.resize(indexCount);
				in.read(reinterpret_cast<char*>(mesh->m_indices.data()), indexCount * sizeof(uint32_t));
				m_model->m_Meshes.push_back(mesh);
			}
		}
	}

	in.close();
}
