#include "ModelLoader.h"
#include "Banchmark.hpp"
#include "PathFinder.h"

ModelLoader::ModelLoader()
{
}

ModelLoader::~ModelLoader()
{
}

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
	else if (filepath.extension() == ".asset")
	{
		m_loadType = LoadType::ASSET;
	}
	m_model = new Model();
	m_model->name = filepath.stem().string();
	m_model->m_animator = new Animator();
}

void ModelLoader::ProcessNodes()
{
	ProcessNode(m_AIScene->mRootNode, 0);
}

Node* ModelLoader::ProcessNode(aiNode* node, int parentIndex)
{
	Node* nodeObj = new Node(node->mName.C_Str());
	nodeObj->m_index = m_model->m_nodes.size();
	nodeObj->m_parentIndex = parentIndex;
	nodeObj->m_numMeshes = node->mNumMeshes;
	nodeObj->m_transform = DirectX::SimpleMath::Matrix(node->mTransformation[0]).Transpose();
	nodeObj->m_numChildren = node->mNumChildren;

	m_model->m_nodes.push_back(nodeObj);

	for (uint32 i = 0; i < node->mNumMeshes; i++)
	{
		nodeObj->m_meshes.push_back(node->mMeshes[i]);
	}

	for (uint32 i = 0; i < node->mNumChildren; i++)
	{
		Node* child = ProcessNode(node->mChildren[i], nodeObj->m_index);
		nodeObj->m_childrenIndex.push_back(child->m_index);
	}

	return nodeObj;
}

void ModelLoader::ProcessFlatMeshes()
{
	for (uint32 i = 0; i < m_AIScene->mNumMeshes; i++)
	{
		aiMesh* aimesh = m_AIScene->mMeshes[i];
		Mesh* meshObj = GenerateMesh(aimesh);
		
		Mathf::Vector3 meshMin = { aimesh->mAABB.mMin.x, aimesh->mAABB.mMin.y, aimesh->mAABB.mMin.z };
		Mathf::Vector3 meshMax = { aimesh->mAABB.mMax.x,  aimesh->mAABB.mMax.y,  aimesh->mAABB.mMax.z };

		min = Mathf::Vector3::Min(min, meshMin);
		max = Mathf::Vector3::Max(max, meshMax);

		DirectX::BoundingBox::CreateFromPoints(meshObj->m_boundingBox, min, max);
		DirectX::BoundingSphere::CreateFromBoundingBox(meshObj->m_boundingSphere, meshObj->m_boundingBox);
	}
}

Model* ModelLoader::LoadModel()
{
	ProcessNodes();
	ProcessFlatMeshes();
	if (m_model->m_hasBones)
	{
		Skeleton* skeleton = m_skeletonLoader.GenerateSkeleton(m_AIScene->mRootNode);
		m_model->m_Skeleton = skeleton;
		Animator* animator = m_model->m_animator;
		animator->m_IsEnabled = true;
		animator->m_Skeleton = skeleton;
		m_animator = animator;
	}

	return m_model;
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

void ModelLoader::ParseModel()
{
	std::fstream file;
	file.open(m_model->name + ".asset", std::ios::out | std::ios::binary);
	if (file.is_open())
	{
		ParseNodes(file);
		ParseMeshes(file);
		ParseMaterials(file);
		file.close();
	}
}

void ModelLoader::ParseNodes(std::fstream& outfile)
{
	for (uint32 i = 0; i < m_model->m_nodes.size(); i++)
	{
		Node* node = m_model->m_nodes[i];
		ParseNode(outfile, node);
	}
}

void ModelLoader::ParseNode(std::fstream& outfile, Node* node)
{
	outfile.write(reinterpret_cast<const char*>(&node->m_index), sizeof(node->m_index));
	outfile.write(reinterpret_cast<const char*>(&node->m_parentIndex), sizeof(node->m_parentIndex));
	outfile.write(reinterpret_cast<const char*>(&node->m_numMeshes), sizeof(node->m_numMeshes));
	outfile.write(reinterpret_cast<const char*>(&node->m_numChildren), sizeof(node->m_numChildren));
	outfile.write(reinterpret_cast<const char*>(&node->m_transform), sizeof(node->m_transform));
	for (uint32 i = 0; i < node->m_numMeshes; i++)
	{
		uint32 meshId = node->m_meshes[i];
		outfile.write(reinterpret_cast<const char*>(&meshId), sizeof(meshId));
	}
	for (uint32 i = 0; i < node->m_numChildren; i++)
	{
		uint32 childIndex = node->m_childrenIndex[i];
		outfile.write(reinterpret_cast<const char*>(&childIndex), sizeof(childIndex));
	}
}

void ModelLoader::ParseMeshes(std::fstream& outfile)
{
	for (uint32 i = 0; i < m_model->m_Meshes.size(); i++)
	{
		Mesh* mesh = m_model->m_Meshes[i];
		outfile.write(reinterpret_cast<char*>(&mesh->m_name), sizeof(mesh->m_name));
		outfile.write(reinterpret_cast<char*>(mesh->m_vertices.data()), mesh->m_vertices.size() * sizeof(Vertex));
		outfile.write(reinterpret_cast<char*>(mesh->m_indices.data()), mesh->m_indices.size() * sizeof(uint32));

	}
}

void ModelLoader::ParseMaterials(std::fstream& outfile)
{
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

void ModelLoader::GenerateSceneObjectHierarchy(Node* node, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	if (true == isRoot)
	{
		auto rootObject = m_scene->CreateSceneObject(m_model->name, nextIndex);
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

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		Banchmark banch;
		std::shared_ptr<SceneObject> object = m_scene->CreateSceneObject(node->m_name, nextIndex);

		uint32 meshId = node->m_meshes[i];
		Mesh* mesh = m_model->m_Meshes[meshId];
		Material* material = m_model->m_Materials[meshId];
		MeshRenderer& meshRenderer = object->m_meshRenderer;
		meshRenderer.m_IsEnabled = true;
		meshRenderer.m_Mesh = mesh;
		meshRenderer.m_Material = material;
		object->m_transform.SetLocalMatrix(node->m_transform);
		nextIndex = object->m_index;
		std::cout << "GenerateSceneObjectHierarchy new SceneObject : " << banch.GetElapsedTime() << std::endl;
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		std::shared_ptr<SceneObject> object = m_scene->CreateSceneObject(node->m_name, nextIndex);
		object->m_transform.SetLocalMatrix(node->m_transform);
		nextIndex = object->m_index;
	}

	for (uint32 i = 0; i < node->m_numChildren; ++i)
	{
		GenerateSceneObjectHierarchy(m_model->m_nodes[node->m_childrenIndex[i]], false, nextIndex);
	}
}
