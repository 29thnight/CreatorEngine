#include "ModelLoader.h"
#include "PathFinder.h"
#include "DataSystem.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "assimp/material.h"
#include "assimp/Gltfmaterial.h"
#include "ReflectionYml.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <chrono>
#include <execution>
#include <iterator>
#include <sstream>

namespace
{
	constexpr std::uint32_t kMaxLegacyMaterialStringBytes = 1024u * 1024u;

	template <typename T>
	bool ReadLegacyValue(std::istream& input, T& value)
	{
		input.read(reinterpret_cast<char*>(&value), sizeof(value));
		return static_cast<bool>(input);
	}

	bool ReadLegacyString(std::istream& input, std::string& value)
	{
		std::uint32_t size{};
		if (!ReadLegacyValue(input, size) || size > kMaxLegacyMaterialStringBytes)
			return false;
		value.resize(size);
		if (size != 0) input.read(value.data(), size);
		return static_cast<bool>(input);
	}
}

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

ModelLoader::ModelLoader(std::string_view fileName)
{
}

ModelLoader::ModelLoader(const aiScene* assimpScene, std::string_view fileName) :
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
	else if (filepath.extension() == ".gltf" || filepath.extension() == ".glb")
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
	m_model = new Model;
	if(m_loadType == LoadType::ASSET)
	{
		m_model->loadType = ModelLoadType::FormAsset;
	}
	m_model->lastWriteTime = file::last_write_time(filepath);

	m_fileGuid = DataSystems->GetStemToGuid(filepath.stem().string());
	m_model->guid = m_fileGuid;
	m_model->path = filepath.string();
	m_model->name = filepath.stem().string();
    if(m_AIScene)
    {
        if (0 < m_AIScene->mNumAnimations)
        {
            m_model->m_animator = new AnimatorData();
        }
    }
}

size_t ModelLoader::CountNodes(aiNode* root)
{
	if (!root)
		return 0u;

	size_t count = 1u;
	for (uint32_t i = 0; i < root->mNumChildren; ++i)
		count += CountNodes(root->mChildren[i]);

	return count;
}

void ModelLoader::ProcessNodes()
{
	m_model->m_numTotalMeshes = m_AIScene->mNumMeshes;
	ProcessNode(m_AIScene->mRootNode, 0);
}

ModelNode* ModelLoader::ProcessNode(aiNode* node, int parentIndex)
{
	ModelNode* nodeObj = new ModelNode(node->mName.C_Str());
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
		auto count = CountNodes(m_AIScene->mRootNode);
		m_model->m_nodes.reserve(count);

		ProcessNodes();
		ProcessFlatMeshes();
		ProcessMaterials();
		if (m_model->m_hasBones)
		{
			Skeleton* skeleton = m_skeletonLoader.GenerateSkeleton(m_AIScene->mRootNode);
			m_model->m_Skeleton = skeleton;
			AnimatorData* animator = m_model->m_animator;
			animator->m_Motion = m_fileGuid;
			animator->m_Skeleton = skeleton;
		}
		// The binary .asset file is an Editor-owned import artifact. Runtime owns
		// only the in-memory serialization request; Player has no installed writer.
		RequestModelCacheWrite();
	}

	m_model->m_isMakeMeshCollider = isCreateMeshCollider;
	return m_model;
}

Mesh* ModelLoader::GenerateMesh(aiMesh* mesh)
{
	std::vector<Vertex> vertices;
	std::vector<uint32> indices;
	m_numUVChannel = mesh->GetNumUVChannels(); //테스트 해보고 어떻게 되는지 확인해보기
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

	std::string baseName = mesh->mName.C_Str();
	std::string uniqueName = baseName;
	int suffix = 1;

	while (true)
	{
		Mesh* mesh = m_model->GetMesh(uniqueName);
		if (nullptr != mesh)
		{
			bool isVertexDeff = mesh->m_vertices.size() != vertices.size();
			bool isIndexDeff = mesh->m_indices.size() != indices.size();
			if(isVertexDeff || isIndexDeff)
			{
				uniqueName = baseName + "(" + std::to_string(suffix++) + ")";
				break;
			}
		}
		else
		{
			break;
		}
	}

	Mesh* meshObj = new Mesh(uniqueName, vertices, indices);
	meshObj->m_materialIndex = mesh->mMaterialIndex;
	meshObj->m_modelName = m_model->name;
	//if(!m_model->m_hasBones)
	//{
	//	MeshOptimizer::Optimize(*meshObj, 1.05f);
	//	MeshOptimizer::GenerateShadowMesh(*meshObj);
	//}

	// Mesh는 meta::polymorphic 파생이라 operator delete가 커스텀 힙으로 라우팅된다.
	// 따라서 shared_ptr의 기본 deleter로 감싸도 해제 경로는 기존과 동일하다.
	m_model->m_Meshes.push_back(std::shared_ptr<Mesh>(meshObj));

	return meshObj;
}

void ModelLoader::ProcessMaterials()
{
	ResolveFileGuidFromMeta();

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

void ModelLoader::ResolveFileGuidFromMeta()
{
	MetaYml::Node modelFileNode = MetaYml::LoadFile(m_metaDirectory);
	m_fileGuid = modelFileNode["guid"].as<std::string>();
}

std::shared_ptr<Material> ModelLoader::GenerateMaterial(int index)
{
    std::string baseName{};
	if (index > -1)
	{
		baseName = m_AIScene->mMaterials[index]->GetName().C_Str();
	}

    if (baseName.empty())
    {
        // glTF의 material.name은 선택 항목이라 파일 전체가 무명인 경우가 흔하다
        // (스폰자 GLB는 25개 재질 전부 이름이 없다). 전부 "DefaultMaterial"로
        // 접으면 아래 중복 판정이 서로를 같은 재질로 보고 하나로 붕괴시킨다.
        // 파일 내 인덱스를 이름에 담아 애초에 겹치지 않게 한다.
        baseName = (index > -1)
            ? m_model->name + "_Mat" + std::to_string(index)
            : "DefaultMaterial";
    }

    std::string uniqueName = baseName;
    int suffix = 1;

    while (true)
    {
        // 이번 임포트에서 이미 내준 이름이면 같은 파일 안의 다른 재질이다.
        // 여기서 재사용하면 텍스처가 서로 다른 재질들이 하나로 접혀,
        // 대부분의 메시가 남의 재질(대개 검게 보이는 컷아웃)을 쓰게 된다.
        if (m_issuedMaterialNames.contains(uniqueName))
        {
            uniqueName = baseName + "(" + std::to_string(suffix++) + ")";
            continue;
        }

		std::shared_ptr<Material> cached =
			DataSystems->FindCachedMaterial(uniqueName);
		if (!cached)
		{
			break;
		}

		if (cached->m_fileGuid == m_fileGuid)
		{
            // 같은 파일을 다시 임포트하는 경우다. 재질을 인덱스 순서대로
            // 훑고 이름도 인덱스에서 나오므로, 각 인덱스가 제 짝을 다시 찾는다.
			m_issuedMaterialNames.insert(uniqueName);
			return cached;
        }

        // 다른 파일과의 이름 충돌 → 이름 뒤에 (숫자) 붙이기
        uniqueName = baseName + "(" + std::to_string(suffix++) + ")";
    }

    m_issuedMaterialNames.insert(uniqueName);

    auto material = std::make_shared<Material>();
    material->m_name = uniqueName;
	material->m_fileGuid = m_fileGuid;

	if (index > -1)
	{
		aiMaterial* mat = m_AIScene->mMaterials[index];

        Texture* normal = GenerateTexture(mat, aiTextureType_NORMALS);
        Texture* bump = GenerateTexture(mat, aiTextureType_HEIGHT);
        if (normal)
        {
            material->UseNormalMap(normal);
            material->m_normalTexName = normal->m_name;
        }
        else if (bump)
        {
            material->UseBumpMap(bump);
            material->m_normalTexName = bump->m_name;
        }

        Texture* ao = GenerateTexture(mat, aiTextureType_LIGHTMAP);
        if (ao)
        {
            material->UseAOMap(ao);
            material->m_AO_TexName = ao->m_name;
        }

        Texture* emissive = GenerateTexture(mat, aiTextureType_EMISSIVE);
        if (emissive)
        {
            material->UseEmissiveMap(emissive);
            material->m_EmissiveTexName = emissive->m_name;
        }

		if (m_loadType == LoadType::GLTF)
		{
            material->ConvertToLinearSpace(true);

            // glTF의 baseColorTexture가 어느 슬롯으로 들어오는지는 Assimp 버전을
            // 탄다. BASE_COLOR로 넣는 버전도 있고 DIFFUSE로만 넣는 버전도 있다.
            // 하나만 보면 '텍스처가 있는데 재질이 비어 있다'가 되고, 그 증상은
            // 화면에서 '검게 나온다'로만 드러나 원인이 멀다(실측으로 겪었다).
            Texture* albedo = GenerateTextureFromAny(mat,
                { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }, "baseColor");
            if (albedo)
            {
                material->UseBaseColorMap(albedo);
                material->m_baseColorTexName = albedo->m_name;
            }

            // 금속·거칠기도 같은 사정이다. glTF는 한 텍스처에 담지만 Assimp는
            // 버전에 따라 METALNESS/DIFFUSE_ROUGHNESS/UNKNOWN 중 하나로 준다.
            Texture* occlusionMetalRough = GenerateTextureFromAny(mat,
                { aiTextureType_METALNESS, aiTextureType_DIFFUSE_ROUGHNESS,
                  aiTextureType_UNKNOWN }, "metallicRoughness");
            if (occlusionMetalRough)
            {
                material->UseOccRoughMetalMap(occlusionMetalRough);
                material->m_ORM_TexName = occlusionMetalRough->m_name;
            }

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

            // OBJ/FBX 경로는 알파 채널을 보고 Transparent로 넘기는데 이쪽만 없었다.
            // glTF는 알파 처리 방식을 텍스처가 아니라 material.alphaMode로 선언하므로
            // 그 값을 봐야 한다. 안 보면 사슬·화분 같은 컷아웃 재질이 불투명으로
            // 디퍼드 큐에 실려 잘려야 할 부분이 검은 판으로 남는다.
            aiString alphaMode;
            if (mat->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS)
            {
                const std::string_view mode{ alphaMode.C_Str() };
                // 엔진의 렌더링 모드는 Opaque/Transparent 둘뿐이라 MASK도 함께 태운다.
                if ("MASK" == mode || "BLEND" == mode)
                {
                    material->m_renderingMode = MaterialRenderingMode::Transparent;
                }
            }
		}
		else
		{
            material->ConvertToLinearSpace(true);

            Texture* albedo = GenerateTexture(mat, aiTextureType_DIFFUSE);
            if (albedo)
            {
                material->UseBaseColorMap(albedo);
                material->m_baseColorTexName = albedo->m_name;
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

	material = DataSystems->RegisterImportedMaterial(material, baseName);
	if (material) m_issuedMaterialNames.insert(material->m_name);
	return material;
}

void ModelLoader::RequestModelCacheWrite()
{
	if (!AssetAuthoringPort::IsInstalled()) return;
	std::ostringstream output(std::ios::out | std::ios::binary);

    uint32_t nodeCount   = static_cast<uint32_t>(m_model->m_nodes.size());
    uint32_t meshCount   = static_cast<uint32_t>(m_model->m_Meshes.size());
    uint32_t materialCnt = static_cast<uint32_t>(m_model->m_Materials.size());

    output.write(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    output.write(reinterpret_cast<char*>(&meshCount), sizeof(meshCount));
    output.write(reinterpret_cast<char*>(&materialCnt), sizeof(materialCnt));

    bool hasSkeleton = m_model->m_hasBones && m_model->m_Skeleton;
    output.write(reinterpret_cast<char*>(&hasSkeleton), sizeof(hasSkeleton));

	if (hasSkeleton) SerializeSkeleton(output);
	SerializeNodes(output);
	SerializeMeshes(output);
	SerializeMaterials(output);
	if (!output.good())
	{
		Debug->LogWarning("모델 캐시 직렬화 실패: " + m_model->name);
		return;
	}

	const std::string payload = output.str();
	const file::path destination =
		PathFinder::Relative("Models\\") / (m_model->name + ".asset");
	const auto bytes = std::span<const std::byte>(
		reinterpret_cast<const std::byte*>(payload.data()), payload.size());
	if (!AssetAuthoringPort::WriteModelCache(destination, bytes))
		Debug->LogWarning("Editor model-cache write failed: " + destination.string());
}

void ModelLoader::SerializeNodes(std::ostream& output)
{
    for (const ModelNode* node : m_model->m_nodes)
    {
        SerializeNode(output, node);
    }
}

void ModelLoader::SerializeNode(std::ostream& output, const ModelNode* node)
{
    uint32_t nameSize = static_cast<uint32_t>(node->m_name.size());
    output.write(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
    output.write(node->m_name.data(), nameSize);

    output.write(reinterpret_cast<const char*>(&node->m_index), sizeof(node->m_index));
    output.write(reinterpret_cast<const char*>(&node->m_parentIndex), sizeof(node->m_parentIndex));
    output.write(reinterpret_cast<const char*>(&node->m_numMeshes), sizeof(node->m_numMeshes));
    output.write(reinterpret_cast<const char*>(&node->m_numChildren), sizeof(node->m_numChildren));
    output.write(reinterpret_cast<const char*>(&node->m_transform), sizeof(Mathf::Matrix));

    if (!node->m_meshes.empty())
        output.write(reinterpret_cast<const char*>(node->m_meshes.data()), node->m_meshes.size() * sizeof(uint32_t));
    if (!node->m_childrenIndex.empty())
        output.write(reinterpret_cast<const char*>(node->m_childrenIndex.data()), node->m_childrenIndex.size() * sizeof(uint32_t));
}

void ModelLoader::SerializeMeshes(std::ostream& output)
{
    for (const auto& mesh : m_model->m_Meshes)
    {
        uint32_t nameSize = static_cast<uint32_t>(mesh->m_name.size());
        output.write(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        output.write(mesh->m_name.data(), nameSize);
        output.write(reinterpret_cast<const char*>(&mesh->m_materialIndex), sizeof(mesh->m_materialIndex));

        uint32_t vertexCount = static_cast<uint32_t>(mesh->m_vertices.size());
        output.write(reinterpret_cast<char*>(&vertexCount), sizeof(vertexCount));
        if (vertexCount)
            output.write(reinterpret_cast<const char*>(mesh->m_vertices.data()), vertexCount * sizeof(Vertex));

        uint32_t indexCount = static_cast<uint32_t>(mesh->m_indices.size());
        output.write(reinterpret_cast<char*>(&indexCount), sizeof(indexCount));
        if (indexCount)
            output.write(reinterpret_cast<const char*>(mesh->m_indices.data()), indexCount * sizeof(uint32_t));

        output.write(reinterpret_cast<const char*>(&mesh->m_boundingBox), sizeof(DirectX::BoundingBox));
        output.write(reinterpret_cast<const char*>(&mesh->m_boundingSphere), sizeof(DirectX::BoundingSphere));
    }
}

void ModelLoader::SerializeMaterials(std::ostream& output)
{
    for (const auto& mat : m_model->m_Materials)
    {
		if (!mat || !DataSystems->SerializeMaterialBinaryPayload(*mat, output))
		{
			output.setstate(std::ios::failbit);
			return;
		}
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

void ModelLoader::SerializeSkeleton(std::ostream& output)
{
    Skeleton* skeleton = m_model->m_Skeleton;
	AnimatorData* animator = m_model->m_animator;
    if (!skeleton || !animator)
        return;

    SetParentIndexRecursive(skeleton->m_rootBone, -1);

    output.write(reinterpret_cast<char*>(&skeleton->m_rootTransform), sizeof(XMFLOAT4X4));
    output.write(reinterpret_cast<char*>(&skeleton->m_globalInverseTransform), sizeof(XMFLOAT4X4));

    uint32_t boneCount = static_cast<uint32_t>(skeleton->m_bones.size());
    output.write(reinterpret_cast<char*>(&boneCount), sizeof(boneCount));

    for (Bone* bone : skeleton->m_bones)
    {
        uint32_t nameSize = static_cast<uint32_t>(bone->m_name.size());
        output.write(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        output.write(bone->m_name.data(), nameSize);

        output.write(reinterpret_cast<char*>(&bone->m_index), sizeof(bone->m_index));
        output.write(reinterpret_cast<char*>(&bone->m_parentIndex), sizeof(bone->m_parentIndex));
        output.write(reinterpret_cast<char*>(&bone->m_offset), sizeof(XMFLOAT4X4));
    }

    uint32_t animCount = static_cast<uint32_t>(skeleton->m_animations.size());
    output.write(reinterpret_cast<char*>(&animCount), sizeof(animCount));

    for (const Animation& anim : skeleton->m_animations)
    {
        uint32_t animNameSize = static_cast<uint32_t>(anim.m_name.size());
        output.write(reinterpret_cast<char*>(&animNameSize), sizeof(animNameSize));
        output.write(anim.m_name.data(), animNameSize);

        output.write(reinterpret_cast<const char*>(&anim.m_duration), sizeof(anim.m_duration));
        output.write(reinterpret_cast<const char*>(&anim.m_ticksPerSecond), sizeof(anim.m_ticksPerSecond));
		output.write(reinterpret_cast<const char*>(&anim.m_totalKeyFrames), sizeof(anim.m_totalKeyFrames));
		output.write(reinterpret_cast<const char*>(&anim.m_isLoop), sizeof(anim.m_isLoop));

        uint32_t nodeAnimCount = static_cast<uint32_t>(anim.m_nodeAnimations.size());
        output.write(reinterpret_cast<char*>(&nodeAnimCount), sizeof(nodeAnimCount));

        for (const auto& [nodeName, nodeAnim] : anim.m_nodeAnimations)
        {
            uint32_t nodeNameSize = static_cast<uint32_t>(nodeName.size());
            output.write(reinterpret_cast<char*>(&nodeNameSize), sizeof(nodeNameSize));
            output.write(nodeName.data(), nodeNameSize);

            uint32_t posKeyCount = static_cast<uint32_t>(nodeAnim.m_positionKeys.size());
            output.write(reinterpret_cast<char*>(&posKeyCount), sizeof(posKeyCount));
            for (const auto& key : nodeAnim.m_positionKeys)
            {
                DirectX::XMFLOAT4 pos;
                XMStoreFloat4(&pos, key.m_position);
                output.write(reinterpret_cast<char*>(&pos), sizeof(pos));
                output.write(reinterpret_cast<const char*>(&key.m_time), sizeof(key.m_time));
            }

            uint32_t rotKeyCount = static_cast<uint32_t>(nodeAnim.m_rotationKeys.size());
            output.write(reinterpret_cast<char*>(&rotKeyCount), sizeof(rotKeyCount));
            for (const auto& key : nodeAnim.m_rotationKeys)
            {
                DirectX::XMFLOAT4 rot;
                XMStoreFloat4(&rot, key.m_rotation);
                output.write(reinterpret_cast<char*>(&rot), sizeof(rot));
                output.write(reinterpret_cast<const char*>(&key.m_time), sizeof(key.m_time));
            }

            uint32_t scaleKeyCount = static_cast<uint32_t>(nodeAnim.m_scaleKeys.size());
            output.write(reinterpret_cast<char*>(&scaleKeyCount), sizeof(scaleKeyCount));
            for (const auto& key : nodeAnim.m_scaleKeys)
            {
                output.write(reinterpret_cast<const char*>(&key.m_scale), sizeof(Mathf::Vector3));
                output.write(reinterpret_cast<const char*>(&key.m_time), sizeof(key.m_time));
            }
        }
    }

	// 16바이트를 통째로 적는다. Uuid16의 배치가 boost::uuids::uuid와 같아야
	// 이미 구워진 자산을 계속 읽을 수 있다 — Uuid.h의 static_assert가 지킨다.
	Uuid::Uuid16 guid = animator->m_Motion.m_guid;
	output.write(reinterpret_cast<const char*>(&guid), sizeof(Uuid::Uuid16));
}

const ModelLoader::CookedLoadBreakdown& ModelLoader::LastCookedLoadBreakdown() noexcept
{
    return MutableCookedLoadBreakdown();
}

ModelLoader::CookedLoadBreakdown& ModelLoader::MutableCookedLoadBreakdown() noexcept
{
    // 클래스 스코프 안에 둔다. 익명 namespace 에 두면 유니티 빌드에서 같은
    // namespace 의 다른 TU 와 합쳐질 수 있다.
    thread_local CookedLoadBreakdown breakdown{};
    return breakdown;
}

void ModelLoader::LoadModelFromAsset()
{
    using CookClock = std::chrono::steady_clock;
    const auto elapsedMs = [](CookClock::time_point since)
    {
        return std::chrono::duration<double, std::milli>(
            CookClock::now() - since).count();
    };

    CookedLoadBreakdown& breakdown = MutableCookedLoadBreakdown();
    breakdown = CookedLoadBreakdown{};   // 이전 로드의 잔재를 끌고 가지 않는다

    const auto entry = CookClock::now();

    file::path filepath = PathFinder::Relative("Models\\") / (m_model->name + ".asset");
    std::ifstream file(filepath, std::ios::binary);
    if (!file)
        return;                          // valid=false 로 남는다 — 실패를 0ms 로 읽으면 안 된다

    breakdown.openMs = elapsedMs(entry);

    uint32_t nodeCount{};
    uint32_t meshCount{};
    uint32_t materialCount{};

    file.read(reinterpret_cast<char*>(&nodeCount), sizeof(nodeCount));
    file.read(reinterpret_cast<char*>(&meshCount), sizeof(meshCount));
    file.read(reinterpret_cast<char*>(&materialCount), sizeof(materialCount));

    auto cursor = CookClock::now();
    LoadSkeleton(file);
    breakdown.skeletonMs = elapsedMs(cursor);   cursor = CookClock::now();
    LoadNodes(file, nodeCount);
    breakdown.nodesMs    = elapsedMs(cursor);   cursor = CookClock::now();
    LoadMesh(file, meshCount);
    breakdown.meshesMs   = elapsedMs(cursor);   cursor = CookClock::now();
    LoadMaterial(file, materialCount);
    breakdown.materialsMs = elapsedMs(cursor);

    breakdown.totalMs = elapsedMs(entry);
    breakdown.valid = true;
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

    node = new ModelNode(name);

    infile.read(reinterpret_cast<char*>(&node->m_index), sizeof(node->m_index));
    infile.read(reinterpret_cast<char*>(&node->m_parentIndex), sizeof(node->m_parentIndex));
    infile.read(reinterpret_cast<char*>(&node->m_numMeshes), sizeof(node->m_numMeshes));
    infile.read(reinterpret_cast<char*>(&node->m_numChildren), sizeof(node->m_numChildren));
    infile.read(reinterpret_cast<char*>(&node->m_transform), sizeof(XMFLOAT4X4));

    node->m_meshes.resize(node->m_numMeshes);
    node->m_childrenIndex.resize(node->m_numChildren);;
    if (node->m_numMeshes)
    {
        infile.read(reinterpret_cast<char*>(node->m_meshes.data()), node->m_numMeshes * sizeof(uint32_t));
    }
    if (node->m_numChildren)
    {
        infile.read(reinterpret_cast<char*>(node->m_childrenIndex.data()), node->m_numChildren * sizeof(uint32_t));
    }
}

void ModelLoader::LoadMesh(std::ifstream& infile, uint32_t size)
{
    //Benchmark asset;
    m_model->m_Meshes.reserve(size);
    for (uint32_t i = 0; i < size; ++i)
    {
        uint32_t nameSize{};
        infile.read(reinterpret_cast<char*>(&nameSize), sizeof(nameSize));
        std::string name;
        name.resize(nameSize);
        infile.read(name.data(), nameSize);

        auto* mesh = new Mesh();
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

        m_model->m_Meshes.push_back(std::shared_ptr<Mesh>(mesh));
    }
    //std::cout << "LoadMesh base : " << asset.GetElapsedTime() << std::endl;
}

void ModelLoader::LoadMaterial(std::ifstream& infile, uint32_t size)
{
    m_model->m_Materials.reserve(size);
	const bool versioned = size != 0
		&& DataSystems->HasVersionedMaterialBinaryPayload(infile);
    for (uint32_t i = 0; i < size; ++i)
    {
        auto mat = std::make_shared<Material>();
		if (versioned)
		{
			if (!DataSystems->DeserializeMaterialBinaryPayload(*mat, infile))
			{
				Debug->LogError("모델 material payload v1 복원 실패: "
					+ m_model->name + "[" + std::to_string(i) + "]");
				infile.setstate(std::ios::failbit);
				return;
			}
		}
		else
		{
			if (!ReadLegacyString(infile, mat->m_name)
				|| !ReadLegacyValue(infile, mat->m_materialInfo)
				|| !ReadLegacyValue(infile, mat->m_renderingMode)
				|| !ReadLegacyValue(infile, mat->m_fileGuid)
				|| !ReadLegacyString(infile, mat->m_baseColorTexName)
				|| !ReadLegacyString(infile, mat->m_normalTexName)
				|| !ReadLegacyString(infile, mat->m_ORM_TexName)
				|| !ReadLegacyString(infile, mat->m_AO_TexName)
				|| !ReadLegacyString(infile, mat->m_EmissiveTexName))
			{
				Debug->LogError("legacy 모델 material payload 복원 실패: "
					+ m_model->name + "[" + std::to_string(i) + "]");
				infile.setstate(std::ios::failbit);
				return;
			}
			DataSystems->FinalizeMaterialRuntime(*mat);
		}

		mat->ConvertToLinearSpace(true);
		mat = DataSystems->RegisterImportedMaterial(mat, mat->m_name);
		if (!mat)
		{
			infile.setstate(std::ios::failbit);
			return;
		}
		// 텍스처 유지는 재질 시간에 섞여 있지만 **공유 비용**이라 따로 센다.
		// 임포터를 바꿔도 그대로 남는 비용을 합쳐서 재면 비교가 무의미해진다.
		{
			const auto textureBegin = std::chrono::steady_clock::now();
			RetainMaterialTextures(*mat);
			MutableCookedLoadBreakdown().materialTextureMs +=
				std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - textureBegin).count();
		}
		m_model->m_Materials.push_back(std::move(mat));
    }
}

void ModelLoader::RetainMaterialTextures(Material& material)
{
	auto retain = [this](const std::string& name, Texture*& destination, bool compress)
	{
		if (name.empty()) return;
		std::shared_ptr<Texture> texture =
			DataSystems->LoadSharedMaterialTexture(name, compress);
		if (!texture) return;
		destination = texture.get();
		const bool alreadyRetained = std::ranges::any_of(m_model->m_Textures,
			[&texture](const std::shared_ptr<Texture>& candidate)
			{
				return candidate.get() == texture.get();
			});
		if (!alreadyRetained) m_model->m_Textures.push_back(std::move(texture));
	};

	retain(material.m_baseColorTexName, material.m_pBaseColor, true);
	retain(material.m_normalTexName, material.m_pNormal, false);
	retain(material.m_ORM_TexName, material.m_pOccRoughMetal, false);
	retain(material.m_AO_TexName, material.m_AOMap, false);
	retain(material.m_EmissiveTexName, material.m_pEmissive, false);
}

void ModelLoader::LoadSkeleton(std::ifstream& infile)
{
    //Benchmark asset;
    bool hasSkeleton{};
    infile.read(reinterpret_cast<char*>(&hasSkeleton), sizeof(hasSkeleton));
    if (!hasSkeleton)
        return;

    Skeleton* skeleton = new Skeleton();
    infile.read(reinterpret_cast<char*>(&skeleton->m_rootTransform), sizeof(XMFLOAT4X4));
    infile.read(reinterpret_cast<char*>(&skeleton->m_globalInverseTransform), sizeof(XMFLOAT4X4));

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

        Bone* bone = new Bone();
        bone->m_name = name;
        infile.read(reinterpret_cast<char*>(&bone->m_index), sizeof(bone->m_index));
        infile.read(reinterpret_cast<char*>(&bone->m_parentIndex), sizeof(bone->m_parentIndex));
        infile.read(reinterpret_cast<char*>(&bone->m_offset), sizeof(XMFLOAT4X4));
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

    uint32_t animCount{};
    infile.read(reinterpret_cast<char*>(&animCount), sizeof(animCount));
    skeleton->m_animations.reserve(animCount);

    for (uint32_t i = 0; i < animCount; ++i)
    {
        Animation anim{};

        uint32_t animNameSize{};
        infile.read(reinterpret_cast<char*>(&animNameSize), sizeof(animNameSize));
        anim.m_name.resize(animNameSize);
        infile.read(anim.m_name.data(), animNameSize);

        infile.read(reinterpret_cast<char*>(&anim.m_duration), sizeof(anim.m_duration));
        infile.read(reinterpret_cast<char*>(&anim.m_ticksPerSecond), sizeof(anim.m_ticksPerSecond));
		infile.read(reinterpret_cast<char*>(&anim.m_totalKeyFrames), sizeof(anim.m_totalKeyFrames));
        infile.read(reinterpret_cast<char*>(&anim.m_isLoop), sizeof(anim.m_isLoop));

        uint32_t nodeAnimCount{};
        infile.read(reinterpret_cast<char*>(&nodeAnimCount), sizeof(nodeAnimCount));

        for (uint32_t j = 0; j < nodeAnimCount; ++j)
        {
            uint32_t nodeNameSize{};
            infile.read(reinterpret_cast<char*>(&nodeNameSize), sizeof(nodeNameSize));
            std::string nodeName;
            nodeName.resize(nodeNameSize);
            infile.read(nodeName.data(), nodeNameSize);

            NodeAnimation nodeAnim{};
			nodeAnim.m_name = nodeName;

            uint32_t posKeyCount{};
            infile.read(reinterpret_cast<char*>(&posKeyCount), sizeof(posKeyCount));
            nodeAnim.m_positionKeys.reserve(posKeyCount);
            for (uint32_t k = 0; k < posKeyCount; ++k)
            {
                NodeAnimation::PositionKey key{};
                DirectX::XMFLOAT4 pos;
                infile.read(reinterpret_cast<char*>(&pos), sizeof(pos));
                key.m_position = XMLoadFloat4(&pos);
                infile.read(reinterpret_cast<char*>(&key.m_time), sizeof(key.m_time));
                nodeAnim.m_positionKeys.push_back(key);
            }

            uint32_t rotKeyCount{};
            infile.read(reinterpret_cast<char*>(&rotKeyCount), sizeof(rotKeyCount));
            nodeAnim.m_rotationKeys.reserve(rotKeyCount);
            for (uint32_t k = 0; k < rotKeyCount; ++k)
            {
                NodeAnimation::RotationKey key{};
                DirectX::XMFLOAT4 rot;
                infile.read(reinterpret_cast<char*>(&rot), sizeof(rot));
                key.m_rotation = XMLoadFloat4(&rot);
                infile.read(reinterpret_cast<char*>(&key.m_time), sizeof(key.m_time));
                nodeAnim.m_rotationKeys.push_back(key);
            }

            uint32_t scaleKeyCount{};
            infile.read(reinterpret_cast<char*>(&scaleKeyCount), sizeof(scaleKeyCount));
            nodeAnim.m_scaleKeys.reserve(scaleKeyCount);
            for (uint32_t k = 0; k < scaleKeyCount; ++k)
            {
                NodeAnimation::ScaleKey key{};
                infile.read(reinterpret_cast<char*>(&key.m_scale), sizeof(Mathf::Vector3));
                infile.read(reinterpret_cast<char*>(&key.m_time), sizeof(key.m_time));
                nodeAnim.m_scaleKeys.push_back(key);
            }

            anim.m_nodeAnimations.emplace(nodeName, std::move(nodeAnim));
        }

        skeleton->m_animations.push_back(std::move(anim));
    }

	Uuid::Uuid16 guid;
	infile.read(reinterpret_cast<char*>(&guid), sizeof(Uuid::Uuid16));

    m_model->m_Skeleton = skeleton;
    m_model->m_hasBones = true;
	
	m_model->m_animator = new AnimatorData();
	m_model->m_animator->m_Skeleton = skeleton;
	m_model->m_animator->m_Motion.m_guid = guid;

    //std::cout << "LoadSkeleton base : " << asset.GetElapsedTime() << std::endl;
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

Texture* ModelLoader::GenerateTextureFromAny(aiMaterial* material,
    std::initializer_list<aiTextureType> candidates, std::string_view label)
{
    if (nullptr == material) return nullptr;

    for (aiTextureType type : candidates)
    {
        if (0 == material->GetTextureCount(type)) continue;

        Texture* texture = GenerateTexture(material, type, 0, false);
        if (nullptr != texture)
        {
            Debug->LogDebug("[임포터] " + std::string(label) + " 슬롯 "
                + std::to_string(static_cast<int>(type)) + "에서 찾음: " + texture->m_name);
            return texture;
        }

        Debug->LogWarning("[임포터] " + std::string(label) + " 슬롯 "
            + std::to_string(static_cast<int>(type)) + "에 항목은 있으나 로드 실패");
    }

    Debug->LogDebug("[임포터] " + std::string(label) + " 텍스처 없음");
    return nullptr;
}

Texture* ModelLoader::GenerateTexture(aiMaterial* material, aiTextureType type, uint32 index, bool isCompress)
{
	if (0 == material->GetTextureCount(type))
	{
		return nullptr;
	}

	aiString str;
	material->GetTexture(type, index, &str);

	// GLB는 텍스처를 모델 파일 안에 담을 수 있고, 이때 머티리얼이 들고 있는 경로는
	// "*0" 같은 인덱스 참조라 디스크에서 절대 찾을 수 없다.
	// 임베디드가 있으면 씬이 들고 있는 바이트에서 바로 만들고, 없으면 기존 파일 경로 로직을 탄다.
	if (m_AIScene)
	{
		if (const aiTexture* embedded = m_AIScene->GetEmbeddedTexture(str.C_Str()))
		{
			return GenerateEmbeddedTexture(embedded, str.C_Str(), isCompress);
		}
	}

	return GenerateTexture(std::string_view(str.C_Str()), isCompress);
}

Texture* ModelLoader::GenerateEmbeddedTexture(const aiTexture* embedded, std::string_view reference, bool isCompress)
{
	if (nullptr == embedded)
	{
		return nullptr;
	}

	{
		std::unique_lock lock(m_modelMutex);
		auto cached = m_embeddedTextures.find(embedded);
		if (cached != m_embeddedTextures.end())
		{
			return cached->second.get();
		}
	}

	// 메모리에서 바로 텍스처를 만들지 않고 파일로 뽑아낸 뒤 일반 텍스처처럼 로드한다.
	//
	// 모델은 최초 임포트 때만 원본(.glb)을 파싱하고, 그 뒤로는 .asset 경로로 로드되면서
	// 머티리얼이 텍스처를 "이름"으로 찾는다. 메모리에만 존재하는 텍스처는 그 시점에
	// 찾을 수 없어 머티리얼이 검게 나온다. 파일로 남겨두면 임포트 이후의 모든 경로
	// (에셋 재로드·머티리얼 직렬화·에디터 브라우저)가 기존 로직 그대로 동작한다.
	const std::string fileName = MakeEmbeddedTextureFileName(embedded, reference);
	if (fileName.empty())
	{
		return nullptr;
	}

	const file::path destination = PathFinder::Relative("Materials\\") / fileName;

	if (!file::exists(destination))
	{
		const size_t payloadSize = embedded->mHeight == 0
			? static_cast<size_t>(embedded->mWidth)
			: static_cast<size_t>(embedded->mWidth) *
				static_cast<size_t>(embedded->mHeight) * sizeof(aiTexel);
		const auto payload = std::span<const std::byte>(
			reinterpret_cast<const std::byte*>(embedded->pcData), payloadSize);
		if (!AssetAuthoringPort::WriteEmbeddedTexture(destination, payload,
			embedded->mWidth, embedded->mHeight))
		{
			if (AssetAuthoringPort::IsInstalled())
				Debug->LogWarning("임베디드 텍스처 추출 실패: " + std::string(reference));
			else
				Debug->LogError("패키지에 임베디드 텍스처가 없다: " + destination.string());
			return nullptr;
		}

		Debug->LogDebug("임베디드 텍스처 추출: " + destination.filename().string() +
			(0 == embedded->mHeight
				? " [" + std::string(embedded->achFormatHint) + " " + std::to_string(embedded->mWidth) + "바이트]"
				: " [비압축 " + std::to_string(embedded->mWidth) + "x" + std::to_string(embedded->mHeight) + "]"));
	}

	// 이후는 디스크에 있는 여느 텍스처와 완전히 동일한 경로를 탄다.
	Texture* texture = GenerateTexture(fileName, isCompress);
	if (!texture)
	{
		return nullptr;
	}

	{
		std::unique_lock lock(m_modelMutex);
		// GenerateTexture가 이미 m_model->m_Textures에 넣었으므로 여기서는 캐시만 채운다.
		if (!m_model->m_Textures.empty())
		{
			m_embeddedTextures.emplace(embedded, m_model->m_Textures.back());
		}
	}

	return texture;
}

std::string ModelLoader::MakeEmbeddedTextureFileName(const aiTexture* embedded, std::string_view reference) const
{
	// 확장자는 Assimp가 주는 힌트를 쓴다("png", "jpg"...). 비압축이면 PNG로 저장한다.
	std::string extension = (0 == embedded->mHeight) ? std::string(embedded->achFormatHint) : std::string("png");
	if (extension.empty())
	{
		extension = "png";
	}

	// glTF의 image.name은 선택 항목이라 비어 있을 수 있다. 그때는 참조 인덱스로 만든다.
	std::string baseName;
	if (0 < embedded->mFilename.length)
	{
		baseName = file::path(embedded->mFilename.C_Str()).stem().string();
	}

	if (baseName.empty())
	{
		std::string label(reference);
		if (!label.empty() && '*' == label.front())
		{
			label.erase(0, 1);
		}
		baseName = m_model->name + "_embedded" + label;
	}

	// 파일명으로 쓸 수 없는 문자는 걷어낸다(머티리얼 이름이 그대로 들어오는 경우가 있다).
	for (char& character : baseName)
	{
		if (nullptr != std::strchr("\\/:*?\"<>|", character))
		{
			character = '_';
		}
	}

	return baseName.empty() ? std::string{} : baseName + "." + extension;
}

Texture* ModelLoader::GenerateTexture(std::string_view textureName, bool isCompress)
{
    if (textureName.empty())
        return nullptr;

    file::path path(textureName);
    auto texture = DataSystems->LoadSharedMaterialTexture(path.string(), isCompress);
    if (texture)
    {
		if (texture->m_name.empty())
        {
            texture->m_name = std::string(textureName);
        }

        {
            std::unique_lock lock(m_modelMutex);
            // 머티리얼과 같은 이유로 shared_ptr을 그대로 보관한다(공동 소유).
            m_model->m_Textures.push_back(texture);
        }
    }
    return texture.get();
}
