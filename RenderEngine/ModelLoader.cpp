#include "ModelLoader.h"
#include "Benchmark.hpp"
#include "PathFinder.h"
#include "DataSystem.h"
#include "ResourceAllocator.h"
#include "assimp/material.h"
#include "assimp/Gltfmaterial.h"
#include "ReflectionYml.h"
#include "SceneManager.h"
#include "meshoptimizer.h"
#include "RigidBodyComponent.h"
#include "MeshCollider.h"

//ThreadPool<std::function<void()>> ModelLoadPool{};

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
	if(m_AIScene)
	{
		if (0 < m_AIScene->mNumAnimations)
		{
			m_model->m_animator = new Animator();
		}
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

Model* ModelLoader::LoadModel(bool isCreateMeshCollider)
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
			animator->m_Motion = m_fileGuid;
			animator->SetEnabled(true);
			animator->m_Skeleton = skeleton;
		}
		ParseModel(); //not used in current implementation
	}

	m_model->m_isMakeMeshCollider = isCreateMeshCollider;
	return m_model;
}

Mesh* ModelLoader::GenerateMesh(aiMesh* mesh)
{
	std::vector<Vertex> vertices;
	std::vector<uint32> indices;
	m_numUVChannel = mesh->GetNumUVChannels(); //테스트 해보고 어떻게 되는지 확인해보기
	bool hasTexCoords = mesh->mTextureCoords[0] != nullptr;
	bool hasTexCoords1 = mesh->mTextureCoords[1] != nullptr;
    vertices.reserve(mesh->mNumVertices);
    indices.reserve(mesh->mNumFaces * 3);

	for (uint32 i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex = Vertex::ConvertToAiMesh(mesh, i);
		vertices.push_back(vertex);
	}

	for (uint32 i = 0; i < mesh->mNumFaces; i++)
	{
		const aiFace& face = mesh->mFaces[i];
		for (uint32 j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back((uint32)face.mIndices[j]);
		}
	}

	if(mesh->mNumBones > 0)
	{
		m_model->m_hasBones = true;
		ProcessBones(mesh, vertices);
	}

	Mesh* meshObj = AllocateResource<Mesh>(mesh->mName.C_Str(), vertices, indices);
	meshObj->m_materialIndex = mesh->mMaterialIndex;
	meshObj->m_modelName = m_model->name;
	//if(!m_model->m_hasBones)
	//{
	//	MeshOptimizer::Optimize(*meshObj, 1.05f);
	//	MeshOptimizer::GenerateShadowMesh(*meshObj);
	//}

	m_model->m_Meshes.push_back(meshObj);

	return meshObj;
}

void ModelLoader::ProcessMaterials()
{
	if (m_AIScene->mNumMaterials == 0)
	{
		m_model->m_Materials.push_back(GenerateMaterial());
	}
	else
	{
		for (UINT i = 0; i < m_AIScene->mNumMaterials; i++)
		{
			m_model->m_Materials.push_back(GenerateMaterial(i));
		}
	}
}

Material* ModelLoader::GenerateMaterial(int index)
{
	std::string baseName = "DefaultMaterial";
	if (index > -1)
	{
		baseName = m_AIScene->mMaterials[index]->GetName().C_Str();
	}

	MetaYml::Node modelFileNode = MetaYml::LoadFile(m_metaDirectory);
	m_fileGuid = modelFileNode["guid"].as<std::string>();
    std::string uniqueName = baseName;
    int suffix = 1;

    while (true)
    {
        auto iter = DataSystems->Materials.find(uniqueName);
        if (iter != DataSystems->Materials.end())
        {
            if (iter->second->m_fileGuid == m_fileGuid)
            {
                return iter->second.get();
            }
            else
            {
                // 이름 충돌 발생 → 이름 뒤에 (숫자) 붙이기
                uniqueName = baseName + "(" + std::to_string(suffix++) + ")";
            }
        }
        else
        {
            break;
        }
    }

    Material* material = AllocateResource<Material>();
    material->m_name = uniqueName;
	material->m_fileGuid = m_fileGuid;

	if (index > -1)
	{
		aiMaterial* mat = m_AIScene->mMaterials[index];

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
	DataSystems->Materials[material->m_name] = std::shared_ptr<Material>(material, deleter);

	return material;
}

void ModelLoader::ParseModel()
{
    file::path filepath = PathFinder::Relative("Models\\") / (m_model->name + ".asset");
    std::ofstream file(filepath, std::ios::binary);
    if (!file)
        return;

    uint32_t nodeCount   = static_cast<uint32_t>(m_model->m_nodes.size());
    uint32_t meshCount   = static_cast<uint32_t>(m_model->m_Meshes.size());
    uint32_t materialCnt = static_cast<uint32_t>(m_model->m_Materials.size());

    file.write(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    file.write(reinterpret_cast<char*>(&meshCount), sizeof(meshCount));
    file.write(reinterpret_cast<char*>(&materialCnt), sizeof(materialCnt));

    bool hasSkeleton = m_model->m_hasBones && m_model->m_Skeleton;
    file.write(reinterpret_cast<char*>(&hasSkeleton), sizeof(hasSkeleton));

    ParseNodes(file);
    ParseMeshes(file);
    ParseMaterials(file);
    if (hasSkeleton)
        ParseSkeleton(file);
}

void ModelLoader::ParseNodes(std::ofstream& outfile)
{
    for (const ModelNode* node : m_model->m_nodes)
    {
        ParseNode(outfile, node);
    }
}

void ModelLoader::ParseNode(std::ofstream& outfile, const ModelNode* node)
{
    uint32_t nameSize = static_cast<uint32_t>(node->m_name.size());
    outfile.write(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
    outfile.write(node->m_name.data(), nameSize);

    outfile.write(reinterpret_cast<const char*>(&node->m_index), sizeof(node->m_index));
    outfile.write(reinterpret_cast<const char*>(&node->m_parentIndex), sizeof(node->m_parentIndex));
    outfile.write(reinterpret_cast<const char*>(&node->m_numMeshes), sizeof(node->m_numMeshes));
    outfile.write(reinterpret_cast<const char*>(&node->m_numChildren), sizeof(node->m_numChildren));
    outfile.write(reinterpret_cast<const char*>(&node->m_transform), sizeof(Mathf::Matrix));

    if (!node->m_meshes.empty())
        outfile.write(reinterpret_cast<const char*>(node->m_meshes.data()), node->m_meshes.size() * sizeof(uint32_t));
    if (!node->m_childrenIndex.empty())
        outfile.write(reinterpret_cast<const char*>(node->m_childrenIndex.data()), node->m_childrenIndex.size() * sizeof(uint32_t));
}

void ModelLoader::ParseMeshes(std::ofstream& outfile)
{
    for (const Mesh* mesh : m_model->m_Meshes)
    {
        uint32_t nameSize = static_cast<uint32_t>(mesh->m_name.size());
        outfile.write(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        outfile.write(mesh->m_name.data(), nameSize);
        outfile.write(reinterpret_cast<const char*>(&mesh->m_materialIndex), sizeof(mesh->m_materialIndex));

        uint32_t vertexCount = static_cast<uint32_t>(mesh->m_vertices.size());
        outfile.write(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
        if (vertexCount)
            outfile.write(reinterpret_cast<const char*>(mesh->m_vertices.data()), vertexCount * sizeof(Vertex));

        uint32_t indexCount = static_cast<uint32_t>(mesh->m_indices.size());
        outfile.write(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));
        if (indexCount)
            outfile.write(reinterpret_cast<const char*>(mesh->m_indices.data()), indexCount * sizeof(uint32_t));

        outfile.write(reinterpret_cast<const char*>(&mesh->m_boundingBox), sizeof(DirectX::BoundingBox));
        outfile.write(reinterpret_cast<const char*>(&mesh->m_boundingSphere), sizeof(DirectX::BoundingSphere));
    }
}

void ModelLoader::ParseMaterials(std::ofstream& outfile)
{
    for (const Material* mat : m_model->m_Materials)
    {
        uint32_t nameSize = static_cast<uint32_t>(mat->m_name.size());
        outfile.write(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        outfile.write(mat->m_name.data(), nameSize);
        outfile.write(reinterpret_cast<const char*>(&mat->m_materialInfo), sizeof(MaterialInfomation));
        outfile.write(reinterpret_cast<const char*>(&mat->m_renderingMode), sizeof(mat->m_renderingMode));
        outfile.write(reinterpret_cast<const char*>(&mat->m_fileGuid), sizeof(FileGuid));

        auto writeTexName = [&](Texture* tex)
        {
            std::string tname = tex ? tex->m_name : std::string();
            uint32_t len = static_cast<uint32_t>(tname.size());
            outfile.write(reinterpret_cast<char*>(&len), sizeof(len));
            if(len) outfile.write(tname.data(), len);
        };

        writeTexName(mat->m_pBaseColor);
        writeTexName(mat->m_pNormal);
        writeTexName(mat->m_pOccRoughMetal);
        writeTexName(mat->m_AOMap);
        writeTexName(mat->m_pEmissive);
    }
}

void SetParentIndexRecursive(Bone* bone, int parent)
{
    bone->m_parentIndex = parent;
    for (Bone* child : bone->m_children)
    {
        SetParentIndexRecursive(child, bone->m_index);
    }
}

void ModelLoader::ParseSkeleton(std::ofstream& outfile)
{
    Skeleton* skeleton = m_model->m_Skeleton;
    if (!skeleton)
        return;

    SetParentIndexRecursive(skeleton->m_rootBone, -1);

    outfile.write(reinterpret_cast<char*>(&skeleton->m_rootTransform), sizeof(Mathf::Matrix));
    outfile.write(reinterpret_cast<char*>(&skeleton->m_globalInverseTransform), sizeof(Mathf::Matrix));

    uint32_t boneCount = static_cast<uint32_t>(skeleton->m_bones.size());
    outfile.write(reinterpret_cast<char*>(&boneCount), sizeof(boneCount));

    for (Bone* bone : skeleton->m_bones)
    {
        uint32_t nameSize = static_cast<uint32_t>(bone->m_name.size());
        outfile.write(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        outfile.write(bone->m_name.data(), nameSize);

        outfile.write(reinterpret_cast<char*>(&bone->m_index), sizeof(bone->m_index));
        outfile.write(reinterpret_cast<char*>(&bone->m_parentIndex), sizeof(bone->m_parentIndex));
        outfile.write(reinterpret_cast<char*>(&bone->m_offset), sizeof(Mathf::Matrix));
    }
}

void ModelLoader::LoadModelFromAsset()
{
    file::path filepath = PathFinder::Relative("Models\\") / (m_model->name + ".asset");
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        return;

    uint32_t nodeCount{};
    uint32_t meshCount{};
    uint32_t materialCount{};

    file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    file.read(reinterpret_cast<char*>(&meshCount), sizeof(meshCount));
    file.read(reinterpret_cast<char*>(&materialCount), sizeof(materialCount));

    LoadSkeleton(file);

    LoadNodes(file, nodeCount);
    LoadMesh(file, meshCount);
    LoadMaterial(file, materialCount);
}

void ModelLoader::LoadNodes(std::ifstream& infile, uint32_t size)
{
    m_model->m_nodes.reserve(size);
    for (uint32_t i = 0; i < size; ++i)
    {
        ModelNode* node{};
        LoadNode(infile, node);
        m_model->m_nodes.push_back(node);
    }
}

void ModelLoader::LoadNode(std::ifstream& infile, ModelNode*& node)
{
    uint32_t nameSize{};
    infile.read(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
    std::string name;
    name.resize(nameSize);
    infile.read(name.data(), nameSize);

    node = AllocateResource<ModelNode>(name);

    infile.read(reinterpret_cast<char*>(&node->m_index), sizeof(node->m_index));
    infile.read(reinterpret_cast<char*>(&node->m_parentIndex), sizeof(node->m_parentIndex));
    infile.read(reinterpret_cast<char*>(&node->m_numMeshes), sizeof(node->m_numMeshes));
    infile.read(reinterpret_cast<char*>(&node->m_numChildren), sizeof(node->m_numChildren));
    infile.read(reinterpret_cast<char*>(&node->m_transform), sizeof(Mathf::Matrix));

    node->m_meshes.resize(node->m_numMeshes);
    node->m_childrenIndex.resize(node->m_numChildren);
    if (node->m_numMeshes)
        infile.read(reinterpret_cast<char*>(node->m_meshes.data()), node->m_numMeshes * sizeof(uint32_t));
    if (node->m_numChildren)
        infile.read(reinterpret_cast<char*>(node->m_childrenIndex.data()), node->m_numChildren * sizeof(uint32_t));
}

void ModelLoader::LoadMesh(std::ifstream& infile, uint32_t size)
{
    m_model->m_Meshes.reserve(size);
    for (uint32_t i = 0; i < size; ++i)
    {
        uint32_t nameSize{};
        infile.read(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        std::string name;
        name.resize(nameSize);
        infile.read(name.data(), nameSize);

        auto* mesh = AllocateResource<Mesh>();
        mesh->m_name = name;
        infile.read(reinterpret_cast<char*>(&mesh->m_materialIndex), sizeof(mesh->m_materialIndex));

        uint32_t vertexCount{};
        infile.read(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
        mesh->m_vertices.resize(vertexCount);
        if (vertexCount)
            infile.read(reinterpret_cast<char*>(mesh->m_vertices.data()), vertexCount * sizeof(Vertex));

        uint32_t indexCount{};
        infile.read(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));
        mesh->m_indices.resize(indexCount);
        if (indexCount)
            infile.read(reinterpret_cast<char*>(mesh->m_indices.data()), indexCount * sizeof(uint32_t));

        infile.read(reinterpret_cast<char*>(&mesh->m_boundingBox), sizeof(DirectX::BoundingBox));
        infile.read(reinterpret_cast<char*>(&mesh->m_boundingSphere), sizeof(DirectX::BoundingSphere));

		mesh->AssetInit();

        m_model->m_Meshes.push_back(mesh);
    }
}

void ModelLoader::LoadMaterial(std::ifstream& infile, uint32_t size)
{
    m_model->m_Materials.reserve(size);
    for (uint32_t i = 0; i < size; ++i)
    {
        uint32_t nameSize{};
        infile.read(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        std::string name;
        name.resize(nameSize);
        infile.read(name.data(), nameSize);

        Material* mat = AllocateResource<Material>();
        mat->m_name = name;
        infile.read(reinterpret_cast<char*>(&mat->m_materialInfo), sizeof(MaterialInfomation));
        infile.read(reinterpret_cast<char*>(&mat->m_renderingMode), sizeof(mat->m_renderingMode));
        infile.read(reinterpret_cast<char*>(&mat->m_fileGuid), sizeof(FileGuid));

        auto readString = [&](std::string& outStr)
        {
            uint32_t len{};
            infile.read(reinterpret_cast<char*>(&len), sizeof(len));
            outStr.resize(len);
            if (len) infile.read(outStr.data(), len);
        };

        std::string baseColorName;
        std::string normalName;
        std::string ormName;
        std::string aoName;
        std::string emissiveName;

        readString(baseColorName);
        readString(normalName);
        readString(ormName);
        readString(aoName);
        readString(emissiveName);

        if (mat->m_materialInfo.m_useBaseColor)
        {
            if (Texture* tex = GenerateTexture(baseColorName))
                mat->UseBaseColorMap(tex);
        }
        if (mat->m_materialInfo.m_useNormalMap)
        {
            if (Texture* tex = GenerateTexture(normalName))
                mat->UseNormalMap(tex);
        }
        if (mat->m_materialInfo.m_useOccRoughMetal)
        {
            if (Texture* tex = GenerateTexture(ormName))
                mat->UseOccRoughMetalMap(tex);
        }
        if (mat->m_materialInfo.m_useAOMap)
        {
            if (Texture* tex = GenerateTexture(aoName))
                mat->UseAOMap(tex);
        }
        if (mat->m_materialInfo.m_useEmissive)
        {
            if (Texture* tex = GenerateTexture(emissiveName))
                mat->UseEmissiveMap(tex);
        }

        m_model->m_Materials.push_back(mat);
    }
}

void ModelLoader::LoadSkeleton(std::ifstream& infile)
{
    bool hasSkeleton{};
    infile.read(reinterpret_cast<char*>(&hasSkeleton), sizeof(hasSkeleton));
    if (!hasSkeleton)
        return;

    Skeleton* skeleton = AllocateResource<Skeleton>();
    infile.read(reinterpret_cast<char*>(&skeleton->m_rootTransform), sizeof(Mathf::Matrix));
    infile.read(reinterpret_cast<char*>(&skeleton->m_globalInverseTransform), sizeof(Mathf::Matrix));

    uint32_t boneCount{};
    infile.read(reinterpret_cast<char*>(&boneCount), sizeof(boneCount));
    skeleton->m_bones.reserve(boneCount);

    for (uint32_t i = 0; i < boneCount; ++i)
    {
        uint32_t nameSize{};
        infile.read(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        std::string name;
        name.resize(nameSize);
        infile.read(name.data(), nameSize);

        Bone* bone = AllocateResource<Bone>();
        bone->m_name = name;
        infile.read(reinterpret_cast<char*>(&bone->m_index), sizeof(bone->m_index));
        infile.read(reinterpret_cast<char*>(&bone->m_parentIndex), sizeof(bone->m_parentIndex));
        infile.read(reinterpret_cast<char*>(&bone->m_offset), sizeof(Mathf::Matrix));
        bone->m_localTransform = XMMatrixIdentity();
        bone->m_globalTransform = XMMatrixIdentity();

        skeleton->m_bones.push_back(bone);
    }

    for (Bone* bone : skeleton->m_bones)
    {
        if (bone->m_parentIndex >= 0 && bone->m_parentIndex < static_cast<int>(boneCount))
        {
            skeleton->m_bones[bone->m_parentIndex]->m_children.push_back(bone);
        }
        else
        {
            skeleton->m_rootBone = bone;
        }
    }

    m_model->m_Skeleton = skeleton;
    m_model->m_hasBones = true;
    if (m_model->m_animator)
    {
        m_model->m_animator->m_Skeleton = skeleton;
        m_model->m_animator->SetEnabled(true);
    }
}

void ModelLoader::ProcessBones(aiMesh* mesh, std::vector<Vertex>& vertices)
{
	for (uint32 i = 0; i < mesh->mNumBones; ++i)
	{
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
	static int modelSeparator = 0;
	int nextIndex = parentIndex;

	if (true == isRoot)
	{
		auto rootObject = m_scene->CreateGameObject(m_model->name, GameObjectType::Mesh, nextIndex);
		m_gameObjects.push_back(rootObject);
		nextIndex = rootObject->m_index;
		m_modelRootIndex = rootObject->m_index;

		if (m_model->m_hasBones)
		{
			m_animator = rootObject->AddComponent<Animator>();
			m_animator->SetEnabled(true);
			m_animator->m_Motion = m_model->m_animator->m_Motion;
			m_animator->m_Skeleton = m_model->m_Skeleton;
			m_isSkinnedMesh = true;
		}
		else
		{
			m_isSkinnedMesh = false;
		}

		if (1 == node->m_numMeshes && 0 == node->m_numChildren)
		{
			uint32 meshId = node->m_meshes[0];
			Mesh* mesh = m_model->m_Meshes[meshId];
			Material* material = m_model->m_Materials[mesh->m_materialIndex];
			MeshRenderer* meshRenderer = rootObject->AddComponent<MeshRenderer>();

			if (m_model->m_isMakeMeshCollider)
			{
				RigidBodyComponent* rigidbody = rootObject->AddComponent<RigidBodyComponent>();
				MeshColliderComponent* convexMesh = rootObject->AddComponent<MeshColliderComponent>();
				convexMesh->SetDensity(0);
				convexMesh->SetDynamicFriction(0);
				convexMesh->SetStaticFriction(0);
				convexMesh->SetRestitution(0);
			}

			meshRenderer->SetEnabled(true);
			meshRenderer->m_Mesh = mesh;
			meshRenderer->m_Material = material;
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
			rootObject->m_transform.SetLocalMatrix(node->m_transform);
			nextIndex = rootObject->m_index;
			
			return;
		}
	}

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		std::shared_ptr<GameObject> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);

		uint32 meshId			= node->m_meshes[i];
		Mesh* mesh				= m_model->m_Meshes[meshId];
		Material* material		= m_model->m_Materials[mesh->m_materialIndex];
		Mathf::Matrix transform = node->m_transform;

		SceneManagers->m_threadPool->Enqueue([=]
		{
			MeshRenderer* meshRenderer = object->AddComponent<MeshRenderer>();

			if (m_model->m_isMakeMeshCollider)
			{
				RigidBodyComponent* rigidbody = object->AddComponent<RigidBodyComponent>();
				MeshColliderComponent* convexMesh = object->AddComponent<MeshColliderComponent>();
				convexMesh->SetDensity(0);
				convexMesh->SetDynamicFriction(0);
				convexMesh->SetStaticFriction(0);
				convexMesh->SetRestitution(0);
			}

			meshRenderer->SetEnabled(true);
			meshRenderer->m_Mesh = mesh;
			meshRenderer->m_Material = material;
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
			object->m_transform.SetLocalMatrix(transform);
		});

		nextIndex = object->m_index;
		m_gameObjects.push_back(object);
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		std::shared_ptr<GameObject> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);
		m_gameObjects.push_back(object);
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
			boneObject = m_scene->CreateGameObject(bone->m_name, GameObjectType::Bone, nextIndex);
			m_gameObjects.push_back(boneObject);
		}
		else
		{
			boneObject->m_gameObjectType = GameObjectType::Bone;
		}
		nextIndex = boneObject->m_index;
		boneObject->m_rootIndex = m_modelRootIndex;
	}

	for (uint32 i = 0; i < bone->m_children.size(); ++i)
	{
		GenerateSkeletonToSceneObjectHierarchy(node, bone->m_children[i], false, nextIndex);
	}
}

GameObject* ModelLoader::GenerateSceneObjectHierarchyObj(ModelNode* node, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	std::shared_ptr<GameObject> rootObject;

	if (true == isRoot)
	{
		rootObject = m_scene->CreateGameObject(m_model->name, GameObjectType::Mesh, nextIndex);
		nextIndex = rootObject->m_index;
		m_modelRootIndex = rootObject->m_index;

		if (m_model->m_hasBones)
		{
			m_animator = rootObject->AddComponent<Animator>();
			m_animator->SetEnabled(true);
			m_animator->m_Motion = m_model->m_animator->m_Motion;
			m_animator->m_Skeleton = m_model->m_Skeleton;
			m_isSkinnedMesh = true;
		}
		else
		{
			m_isSkinnedMesh = false;
		}

		if (1 == node->m_numMeshes && 0 == node->m_numChildren)
		{
			uint32 meshId = node->m_meshes[0];
			Mesh* mesh = m_model->m_Meshes[meshId];
			Material* material = m_model->m_Materials[meshId];
			MeshRenderer* meshRenderer = rootObject->AddComponent<MeshRenderer>();

			if (m_model->m_isMakeMeshCollider)
			{
				RigidBodyComponent* rigidbody = rootObject->AddComponent<RigidBodyComponent>();
				MeshColliderComponent* convexMesh = rootObject->AddComponent<MeshColliderComponent>();
				convexMesh->SetDensity(0);
				convexMesh->SetDynamicFriction(0);
				convexMesh->SetStaticFriction(0);
				convexMesh->SetRestitution(0);
			}

			meshRenderer->SetEnabled(true);
			meshRenderer->m_Mesh = mesh;
			meshRenderer->m_Material = material;
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
			rootObject->m_transform.SetLocalMatrix(node->m_transform);

			nextIndex = rootObject->m_index;
			return rootObject.get();
		}
	}

	for (uint32 i = 0; i < node->m_numMeshes; ++i)
	{
		std::shared_ptr<GameObject> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);

		uint32 meshId = node->m_meshes[i];
		Mesh* mesh = m_model->m_Meshes[meshId];
		Material* material = m_model->m_Materials[mesh->m_materialIndex];
		Mathf::Matrix transform = node->m_transform;

		SceneManagers->m_threadPool->Enqueue([=]
		{
			MeshRenderer* meshRenderer = object->AddComponent<MeshRenderer>();

			if (m_model->m_isMakeMeshCollider)
			{
				RigidBodyComponent* rigidbody = object->AddComponent<RigidBodyComponent>();
				MeshColliderComponent* convexMesh = object->AddComponent<MeshColliderComponent>();
				convexMesh->SetDensity(0);
				convexMesh->SetDynamicFriction(0);
				convexMesh->SetStaticFriction(0);
				convexMesh->SetRestitution(0);
			}

			meshRenderer->SetEnabled(true);
			meshRenderer->m_Mesh = mesh;
			meshRenderer->m_Material = material;
			meshRenderer->m_isSkinnedMesh = m_isSkinnedMesh;
			object->m_transform.SetLocalMatrix(transform);
		});

		nextIndex = object->m_index;
		m_gameObjects.push_back(object);
	}

	if (false == isRoot && 0 == node->m_numMeshes)
	{
		std::shared_ptr<GameObject> object = m_scene->CreateGameObject(node->m_name, GameObjectType::Mesh, nextIndex);
		object->m_transform.SetLocalMatrix(node->m_transform);
		nextIndex = object->m_index;
	}

	for (uint32 i = 0; i < node->m_numChildren; ++i)
	{
		GenerateSceneObjectHierarchy(m_model->m_nodes[node->m_childrenIndex[i]], false, nextIndex);
	}

	SceneManagers->m_threadPool->NotifyAllAndWait();

	return rootObject.get();
}

GameObject* ModelLoader::GenerateSkeletonToSceneObjectHierarchyObj(ModelNode* node, Bone* bone, bool isRoot, int parentIndex)
{
	int nextIndex = parentIndex;
	std::shared_ptr<GameObject> rootObject;
	if (true == isRoot)
	{
		rootObject = m_scene->GetGameObject(m_modelRootIndex);
		nextIndex = rootObject->m_index;
	}
	else
	{
		std::shared_ptr<GameObject> boneObject{};
		boneObject = m_scene->GetGameObject(bone->m_name);
		if (nullptr == boneObject)
		{
			boneObject = m_scene->CreateGameObject(bone->m_name, GameObjectType::Bone, nextIndex);
		}
		else
		{
			boneObject->m_gameObjectType = GameObjectType::Bone;
		}
		nextIndex = boneObject->m_index;
		boneObject->m_rootIndex = m_modelRootIndex;
	}

	for (uint32 i = 0; i < bone->m_children.size(); ++i)
	{
		GenerateSkeletonToSceneObjectHierarchy(node, bone->m_children[i], false, nextIndex);
	}

	return rootObject.get();
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
        texture = GenerateTexture(textureName);
    }
    return texture;
}

Texture* ModelLoader::GenerateTexture(const std::string& textureName)
{
    if (textureName.empty())
            return nullptr;

    std::wstring stemp = std::wstring(textureName.begin(), textureName.end());
    file::path _path = stemp.c_str();

    Texture* texture = DataSystems->LoadMaterialTexture(_path.string());
    if (texture)
    {
            texture->m_name = textureName;
            m_model->m_Textures.push_back(texture);
    }
    return texture;
}
