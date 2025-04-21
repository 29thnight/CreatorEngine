#include "ModelLoader.h"
#include "Benchmark.hpp"
#include "PathFinder.h"
#include "DataSystem.h"
#include "ResourceAllocator.h"
#include "assimp/material.h"
#include "assimp/Gltfmaterial.h"

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

ModelLoader::ModelLoader(const std::string_view& fileName)
{
}

ModelLoader::ModelLoader(const aiScene* assimpScene, const std::string_view& fileName) :
	m_AIScene(assimpScene),
	m_skeletonLoader(assimpScene)
{
	file::path filepath(fileName);
	m_directory = filepath.parent_path().string() + "\\";
	m_metaDirectory = filepath.string() + ".meta";
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
	m_model = AllocateResource<Model>();
	m_model->name = filepath.stem().string();
    if(0 < m_AIScene->mNumAnimations)
    {
        m_model->m_animator = new Animator();
    }
}

void ModelLoader::ProcessNodes()
{
	m_model->m_numTotalMeshes = m_AIScene->mNumMeshes;
	ProcessNode(m_AIScene->mRootNode, 0);
}

ModelNode* ModelLoader::ProcessNode(aiNode* node, int parentIndex)
{
	ModelNode* nodeObj = AllocateResource<ModelNode>(node->mName.C_Str());
	nodeObj->m_index = m_model->m_nodes.size();
	nodeObj->m_parentIndex = parentIndex;
	nodeObj->m_numMeshes = node->mNumMeshes;
	nodeObj->m_transform = XMMatrixTranspose(XMMATRIX(&node->mTransformation.a1));
	nodeObj->m_numChildren = node->mNumChildren;

	m_model->m_nodes.push_back(nodeObj);

	for (uint32 i = 0; i < node->mNumMeshes; i++)
	{
		nodeObj->m_meshes.push_back(node->mMeshes[i]);
	}

	for (uint32 i = 0; i < node->mNumChildren; i++)
	{
		ModelNode* child = ProcessNode(node->mChildren[i], nodeObj->m_index);
		nodeObj->m_childrenIndex.push_back(child->m_index);
	}

	return nodeObj;
}

void ModelLoader::ProcessFlatMeshes()
{
    m_model->m_Meshes.reserve(m_AIScene->mNumMeshes);

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
	if (m_loadType == LoadType::ASSET)
	{
		LoadModelFromAsset();
	}
	else
	{
		ProcessNodes();
		ProcessFlatMeshes();
		ProcessMaterials();
		if (m_model->m_hasBones)
		{
			Skeleton* skeleton = m_skeletonLoader.GenerateSkeleton(m_AIScene->mRootNode);
			m_model->m_Skeleton = skeleton;
			Animator* animator = m_model->m_animator;
			animator->SetEnabled(true);
			animator->m_Skeleton = skeleton;
		}
		ParseModel();
	}

	return m_model;
}

Mesh* ModelLoader::GenerateMesh(aiMesh* mesh)
{
	std::vector<Vertex> vertices;
	std::vector<uint32> indices;
	bool hasTexCoords = mesh->mTextureCoords[0];
    vertices.reserve(mesh->mNumVertices);
    indices.reserve(mesh->mNumFaces * 3); // Assuming each face is a triangle

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
			indices.push_back((uint32)face.mIndices[j]);
		}
	}

	ProcessBones(mesh, vertices);
	Mesh* meshObj = AllocateResource<Mesh>(mesh->mName.C_Str(), vertices, indices);
	meshObj->m_materialIndex = mesh->mMaterialIndex;
	m_model->m_Meshes.push_back(meshObj);

	return meshObj;
}

void ModelLoader::ProcessMaterials()
{
	for (UINT i = 0; i < m_AIScene->mNumMeshes; i++)
	{
		aiMesh* mesh = m_AIScene->mMeshes[i];
		m_model->m_Materials.push_back(GenerateMaterial(mesh));
	}
}

Material* ModelLoader::GenerateMaterial(aiMesh* mesh)
{
	Material* material = AllocateResource<Material>();
	material->m_name = mesh->mName.C_Str();

	MetaYml::Node modelFileNode = MetaYml::LoadFile(m_metaDirectory);
	material->m_fileGuid = modelFileNode["guid"].as<std::string>();

	if (mesh->mMaterialIndex >= 0)
	{
		aiMaterial* mat = m_AIScene->mMaterials[mesh->mMaterialIndex];

		Texture* normal = GenerateTexture(mat, aiTextureType_NORMALS);
		Texture* bump = GenerateTexture(mat, aiTextureType_HEIGHT);
		if (normal) material->UseNormalMap(normal);
		else if (bump) material->UseBumpMap(bump);

		Texture* ao = GenerateTexture(mat, aiTextureType_LIGHTMAP);
		if (ao) material->UseAOMap(ao);

		Texture* emissive = GenerateTexture(mat, aiTextureType_EMISSIVE);
		if (emissive) material->UseEmissiveMap(emissive);

		if (m_loadType == LoadType::GLTF)
		{
			material->ConvertToLinearSpace(true);
			Texture* albedo = GenerateTexture(mat, AI_MATKEY_BASE_COLOR_TEXTURE);
			if (albedo) material->UseBaseColorMap(albedo);

			Texture* occlusionMetalRough = GenerateTexture(mat, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE);
			if (occlusionMetalRough) material->UseOccRoughMetalMap(occlusionMetalRough);

			float metallic;
			if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
			{
				material->SetMetallic(metallic);
			}
			float roughness;
			if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
			{
				material->SetRoughness(roughness);
			}
		}
		else
		{
			Texture* albedo = GenerateTexture(mat, aiTextureType_DIFFUSE);
			if (albedo)
			{
				 material->UseBaseColorMap(albedo);
				 if (albedo->IsTextureAlpha())
				 {
					 material->m_renderingMode = MaterialRenderingMode::Transparent;
				 }
			}

			aiColor3D colour;
			aiReturn res = mat->Get(AI_MATKEY_COLOR_DIFFUSE, colour);
			if (res == aiReturn_SUCCESS)
				material->SetBaseColor(colour[0], colour[1], colour[2]);

			material->SetRoughness(0.9f);
			material->SetMetallic(0.0f);

			float shininess;
			res = mat->Get(AI_MATKEY_SHININESS, shininess);
			if (res == aiReturn_SUCCESS)
			{
				// convert shininess to roughness
				float roughness = sqrt(2.0f / (shininess + 2.0f));
				material->SetRoughness(roughness);
			}
		}
	}
	else
	{
		material->SetBaseColor(1, 0, 1);
	}

	auto deleter = [&](Material* mat)
	{
		if (mat)
		{
			DeallocateResource<Material>(mat);
		}
	};
	DataSystems->Materials[mesh->mName.C_Str()] = std::shared_ptr<Material>(material, deleter);

	return material;
}

void ModelLoader::ParseModel()
{
	std::fstream file;
	file::path filepath = PathFinder::Relative("Models\\").string() + m_model->name + ".asset";
	file.open(filepath, std::ios::out | std::ios::binary);
	if (file.is_open())
	{
		uint32 size = m_model->m_nodes.size();
		file.write(reinterpret_cast<char*>(&size), sizeof(uint32));
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
		ModelNode* node = m_model->m_nodes[i];
		ParseNode(outfile, node);
	}
}

void ModelLoader::ParseNode(std::fstream& outfile, ModelNode* node)
{
	size_t strSize = node->m_name.size();
	outfile.write(reinterpret_cast<char*>(&strSize), sizeof(size_t));
	outfile.write(reinterpret_cast<char*>(node->m_name.data()), strSize);
	outfile.write(reinterpret_cast<char*>(&node->m_index), sizeof(uint32));
	outfile.write(reinterpret_cast<char*>(&node->m_parentIndex), sizeof(uint32));
	outfile.write(reinterpret_cast<char*>(&node->m_numMeshes), sizeof(uint32));
	outfile.write(reinterpret_cast<char*>(&node->m_numChildren), sizeof(uint32));
	outfile.write(reinterpret_cast<char*>(&node->m_transform), sizeof(Mathf::Matrix));
	outfile.write(reinterpret_cast<char*>(node->m_meshes.data()), sizeof(uint32) * node->m_meshes.size());
	outfile.write(reinterpret_cast<char*>(node->m_childrenIndex.data()), sizeof(uint32) * node->m_childrenIndex.size());
}

void ModelLoader::ParseMeshes(std::fstream& outfile)
{
	for (uint32 i = 0; i < m_model->m_Meshes.size(); i++)
	{
		Mesh* mesh = m_model->m_Meshes[i];
		outfile.write(reinterpret_cast<char*>(mesh->m_name.data()), sizeof(mesh->m_name.size()));
		outfile.write(reinterpret_cast<char*>(mesh->m_vertices.data()), mesh->m_vertices.size() * sizeof(Vertex));
		outfile.write(reinterpret_cast<char*>(mesh->m_indices.data()), mesh->m_indices.size() * sizeof(uint32));
		//outfile.write(reinterpret_cast<char*>(&mesh->m_transform), sizeof(Mathf::Matrix));
		outfile.write(reinterpret_cast<char*>(&mesh->m_boundingBox), sizeof(DirectX::BoundingBox));
		outfile.write(reinterpret_cast<char*>(&mesh->m_boundingSphere), sizeof(DirectX::BoundingSphere));
	}
}

void ModelLoader::ParseMaterials(std::fstream& outfile)
{
}

void ModelLoader::LoadModelFromAsset()
{
	std::fstream file;
	file::path filepath = PathFinder::Relative("Models\\").string() + m_model->name + ".asset";
	file.open(filepath, std::ios::in | std::ios::binary);
	if (file.is_open())
	{
        uint32 size{};
		file.read(reinterpret_cast<char*>(&size), sizeof(uint32));
		m_model->m_nodes.resize(size);
		LoadNodes(file, size);
		LoadMesh(file);
		LoadMaterial(file);
		file.close();
	}
}

void ModelLoader::LoadNodes(std::fstream& infile, uint32 size)
{
	for (uint32 i = 0; i < size; i++)
	{
		LoadNode(infile, m_model->m_nodes[i]);
	}
}

void ModelLoader::LoadNode(std::fstream& infile, ModelNode* node)
{
    size_t size{};
	std::string name;
	infile.read(reinterpret_cast<char*>(&size), sizeof(size_t));
	infile.read(reinterpret_cast<char*>(name.data()), size);
	node = new ModelNode(name);

	infile.read(reinterpret_cast<char*>(&node->m_index), sizeof(uint32));
	infile.read(reinterpret_cast<char*>(&node->m_parentIndex), sizeof(uint32));
	infile.read(reinterpret_cast<char*>(&node->m_numMeshes), sizeof(uint32));
	infile.read(reinterpret_cast<char*>(&node->m_numChildren), sizeof(uint32));
	infile.read(reinterpret_cast<char*>(&node->m_transform), sizeof(Mathf::Matrix));
	infile.read(reinterpret_cast<char*>(node->m_meshes.data()), sizeof(uint32) * node->m_numMeshes);
	infile.read(reinterpret_cast<char*>(node->m_childrenIndex.data()), sizeof(uint32) * node->m_numChildren);
}

void ModelLoader::LoadMesh(std::fstream& infile)
{
	for (uint32 i = 0; i < m_model->m_nodes.size(); i++)
	{
		ModelNode* node = m_model->m_nodes[i];
		for (uint32 j = 0; j < node->m_numMeshes; j++)
		{
			Mesh* mesh = new Mesh();
			infile.read(reinterpret_cast<char*>(mesh->m_name.data()), sizeof(mesh->m_name.size()));
			infile.read(reinterpret_cast<char*>(mesh->m_vertices.data()), mesh->m_vertices.size() * sizeof(Vertex));
			infile.read(reinterpret_cast<char*>(mesh->m_indices.data()), mesh->m_indices.size() * sizeof(uint32));
			//infile.read(reinterpret_cast<char*>(&mesh->m_transform), sizeof(Mathf::Matrix));
			infile.read(reinterpret_cast<char*>(&mesh->m_boundingBox), sizeof(DirectX::BoundingBox));
			infile.read(reinterpret_cast<char*>(&mesh->m_boundingSphere), sizeof(DirectX::BoundingSphere));
			m_model->m_Meshes.push_back(mesh);
			//m_model->m_Materials.push_back(new Material());
		}
	}
}

void ModelLoader::LoadMaterial(std::fstream& infile)
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

void ModelLoader::GenerateSceneObjectHierarchy(ModelNode* node, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	if (true == isRoot)
	{
		auto rootObject = m_scene->CreateGameObject(m_model->name, GameObject::Type::Mesh, nextIndex);
		nextIndex = rootObject->m_index;
		m_modelRootIndex = rootObject->m_index;

		if (m_model->m_hasBones)
		{
			m_animator = rootObject->AddComponent<Animator>();
			m_animator->SetEnabled(true);
			m_animator->m_Skeleton = m_model->m_Skeleton;
		}

		if (1 == node->m_numMeshes && 0 == node->m_numChildren)
		{
			uint32 meshId = node->m_meshes[0];
			Mesh* mesh = m_model->m_Meshes[meshId];
			Material* material = m_model->m_Materials[meshId];
			MeshRenderer* meshRenderer = rootObject->AddComponent<MeshRenderer>();

			meshRenderer->SetEnabled(true);
			meshRenderer->m_Mesh = mesh;
			meshRenderer->m_Material = material;
			rootObject->m_transform.SetLocalMatrix(node->m_transform);
			nextIndex = rootObject->m_index;
			return;
		}
	}

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		std::shared_ptr<GameObject> object = m_scene->CreateGameObject(node->m_name, GameObject::Type::Mesh, nextIndex);

		uint32 meshId = node->m_meshes[i];
		Mesh* mesh = m_model->m_Meshes[meshId];
		Material* material = m_model->m_Materials[meshId];
		MeshRenderer* meshRenderer = object->AddComponent<MeshRenderer>();

		meshRenderer->SetEnabled(true);
		meshRenderer->m_Mesh = mesh;
		meshRenderer->m_Material = material;
		object->m_transform.SetLocalMatrix(node->m_transform);
		nextIndex = object->m_index;
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		std::shared_ptr<GameObject> object = m_scene->CreateGameObject(node->m_name, GameObject::Type::Mesh, nextIndex);
		object->m_transform.SetLocalMatrix(node->m_transform);
		nextIndex = object->m_index;
	}

	for (uint32 i = 0; i < node->m_numChildren; ++i)
	{
		GenerateSceneObjectHierarchy(m_model->m_nodes[node->m_childrenIndex[i]], false, nextIndex);
	}
}

void ModelLoader::GenerateSkeletonToSceneObjectHierarchy(ModelNode* node, Bone* bone, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	if (true == isRoot)
	{
		auto rootObject = m_scene->GetGameObject(m_modelRootIndex);
		nextIndex = rootObject->m_index;
	}
	else
	{
		std::shared_ptr<GameObject> boneObject{};
		boneObject = m_scene->GetGameObject(bone->m_name);
		if (nullptr == boneObject)
		{
			boneObject = m_scene->CreateGameObject(bone->m_name, GameObject::Type::Bone, nextIndex);
		}
		else
		{
			boneObject->m_gameObjectType = GameObject::Type::Bone;
		}
		nextIndex = boneObject->m_index;
		boneObject->m_rootIndex = m_modelRootIndex;
	}

	for (uint32 i = 0; i < bone->m_children.size(); ++i)
	{
		GenerateSkeletonToSceneObjectHierarchy(node, bone->m_children[i], false, nextIndex);
	}
}

Texture* ModelLoader::GenerateTexture(aiMaterial* material, aiTextureType type, uint32 index)
{
	bool hasTex = material->GetTextureCount(type) > 0;
	Texture* texture = nullptr;
	if (hasTex)
	{
		aiString str;
		material->GetTexture(type, index, &str);
		std::string textureName = str.C_Str();
		std::wstring stemp = std::wstring(textureName.begin(), textureName.end());
		file::path _path = stemp.c_str();
		auto it = DataSystems->Textures.find(textureName);
		if (it != DataSystems->Textures.end())
		{
			texture = it->second.get();
		}
		else
		{
			texture = DataSystems->LoadMaterialTexture(_path.string());
			if(nullptr != texture)
			{
				texture->m_name = textureName;
				m_model->m_Textures.push_back(texture);
			}
		}
	}
	return texture;
}
