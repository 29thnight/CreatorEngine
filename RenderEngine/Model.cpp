#include "Model.h"
#include "ShaderSystem.h"
#include "ModelLoader.h"
#include "Benchmark.hpp"
#include "PathFinder.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
#include "ReflectionYml.h"
#include <assimp/Exporter.hpp>

namespace anim
{
	constexpr uint32_t empty = 0;
}

Model::Model()
{
}

Model::~Model()
{
	// Mesh/Material/Texture는 shared_ptr가 관리하므로 수동 해제하지 않는다.
	// 다른 곳(컴포넌트·프록시)이 아직 참조 중이면 그쪽 수명이 끝날 때 해제된다.

	for (auto& node : m_nodes)
	{
		//delete node;
		delete node;
	}

	Memory::SafeDelete(m_animator);

	if (m_Skeleton)
	{
		delete m_Skeleton;
	}
}

namespace
{
    // .asset 캐시를 그대로 써도 되는가.
    //
    // 예전에는 존재 여부만 봤다. 그래서 원본(.glb/.fbx)을 다시 내보내도 캐시가
    // 있으면 그것을 읽었고, 바뀐 내용이 반영되지 않았다. 증상이 고약하다 —
    // 저작 도구에서 텍스처를 넣어 다시 뽑았는데 엔진에서는 재질이 계속 비어
    // 있고, 파일에는 텍스처가 멀쩡히 들어 있으니 임포터를 의심하게 된다.
    // 실제로 그 길로 한참 갔다.
    //
    // 원본이 캐시보다 새것이면 캐시를 버린다. 원본이 없으면(.asset만 배포된
    // 경우) 캐시가 유일한 진실이므로 그대로 쓴다.
    bool IsModelAssetUsable(const file::path& sourcePath, const file::path& assetPath)
    {
        std::error_code errorCode;
        if (!file::exists(assetPath, errorCode)) return false;
        if (!file::exists(sourcePath, errorCode)) return true;
        if (sourcePath == assetPath) return true;

        const auto assetTime = file::last_write_time(assetPath, errorCode);
        if (errorCode) return true;

        const auto sourceTime = file::last_write_time(sourcePath, errorCode);
        if (errorCode) return true;

        if (sourceTime <= assetTime) return true;

        Debug->LogWarning("[임포터] 원본이 캐시보다 새것이라 다시 임포트한다: "
            + sourcePath.filename().string());
        return false;
    }
}

Model* Model::LoadModel(std::string_view filePath)
{
	file::path path_ = filePath.data();
	Model* model{};
	try
	{
		file::path assetPath = filePath.data();
		assetPath = assetPath.replace_extension(".asset");
		if (IsModelAssetUsable(path_, assetPath))
		{
			Benchmark asset;
			ModelLoader loader = ModelLoader(nullptr, assetPath.string());
			model = loader.LoadModel();
			model->path = path_;
			model->m_numTotalMeshes = static_cast<int>(model->m_Meshes.size());

			std::cout << asset.GetElapsedTime() << " ms to load model from asset file: " << assetPath.string() << std::endl;

			return model;
		}
		else
		{
			Benchmark assimp;

			flag settings = aiProcess_LimitBoneWeights
				| aiProcessPreset_TargetRealtime_Fast
				| aiProcess_ConvertToLeftHanded
				| aiProcess_TransformUVCoords
				| aiProcess_GenBoundingBoxes;

			bool isCreateMeshCollider{ false };

			file::path metaPath = path_.string() + ".meta";
			if (file::exists(metaPath))
			{
				auto node = MetaYml::LoadFile(metaPath.string());
				if (node["ModelImporter"])
				{
					const MetaYml::Node& modelImporterNode = node["ModelImporter"];
					if (modelImporterNode)
					{
						if (modelImporterNode["OptimizeMeshes"] && modelImporterNode["OptimizeMeshes"].as<bool>())
						{
							settings |= aiProcess_OptimizeMeshes;
						}

						if (modelImporterNode["ImproveCacheLocality"] && modelImporterNode["ImproveCacheLocality"].as<bool>())
						{
							settings |= aiProcess_ImproveCacheLocality;
						}

						if (modelImporterNode["CreateMeshCollider"])
						{
							isCreateMeshCollider = modelImporterNode["CreateMeshCollider"].as<bool>();
						}
					}
				}
				else
				{
					settings |= aiProcess_OptimizeMeshes;
					settings |= aiProcess_ImproveCacheLocality;
				}
			}

			Assimp::Importer importer;
			Assimp::Exporter exproter;
			importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
			importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

			const aiScene* assimpScene = importer.ReadFile(filePath.data(), settings);
			if (nullptr == assimpScene)
			{
				throw std::exception("ModelLoader::Model file not found");
			}

			if (anim::empty == assimpScene->mNumAnimations)
			{
				importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
				importer.ApplyPostProcessing(aiProcess_GenBoundingBoxes);
			}
			ModelLoader loader = ModelLoader(assimpScene, path_.string());

			model = loader.LoadModel(isCreateMeshCollider);
			model->path = path_;
			model->m_numTotalMeshes = static_cast<int>(model->m_Meshes.size());

			std::cout << assimp.GetElapsedTime() << " ms to load model from assimp file: " << path_.string() << std::endl;

			return model;
		}
	}
	catch (const std::exception& e)
	{
		Debug->Log(e.what());
		return nullptr;
	}
}

Managed::SharedPtr<Model> Model::LoadModelShared(std::string_view filePath)
{
	file::path path_ = filePath.data();
	Managed::SharedPtr<Model> model{};
	try
	{
		file::path assetPath = filePath.data();
		assetPath = assetPath.replace_extension(".asset");
		if (IsModelAssetUsable(path_, assetPath))
		{
			//Benchmark asset;
			ModelLoader loader = ModelLoader(nullptr, assetPath.string());
			model = Managed::SharedPtr<Model>(loader.LoadModel());
			model->path = path_;
			model->m_numTotalMeshes = static_cast<int>(model->m_Meshes.size());

			//std::cout << asset.GetElapsedTime() << " ms to load model from asset file: " << assetPath.string() << std::endl;

			return model;
		}
		else
		{
			//Benchmark assimp;

			flag settings = aiProcess_LimitBoneWeights
				| aiProcessPreset_TargetRealtime_Fast
				| aiProcess_ConvertToLeftHanded
				| aiProcess_TransformUVCoords
				| aiProcess_GenBoundingBoxes;

			bool isCreateMeshCollider{ false };

			file::path metaPath = path_.string() + ".meta";
			if (file::exists(metaPath))
			{
				auto node = MetaYml::LoadFile(metaPath.string());
				if (node["ModelImporter"])
				{
					const MetaYml::Node& modelImporterNode = node["ModelImporter"];
					if (modelImporterNode)
					{
						if (modelImporterNode["OptimizeMeshes"] && modelImporterNode["OptimizeMeshes"].as<bool>())
						{
							settings |= aiProcess_OptimizeMeshes;
						}

						if (modelImporterNode["ImproveCacheLocality"] && modelImporterNode["ImproveCacheLocality"].as<bool>())
						{
							settings |= aiProcess_ImproveCacheLocality;
						}

						if (modelImporterNode["CreateMeshCollider"])
						{
							isCreateMeshCollider = modelImporterNode["CreateMeshCollider"].as<bool>();
						}
					}
				}
				else
				{
					settings |= aiProcess_OptimizeMeshes;
					settings |= aiProcess_ImproveCacheLocality;
				}
			}

			Assimp::Importer importer;
			Assimp::Exporter exproter;
			importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
			importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

			const aiScene* assimpScene = importer.ReadFile(filePath.data(), settings);
			if (nullptr == assimpScene)
			{
				throw std::exception("ModelLoader::Model file not found");
			}

			if (anim::empty == assimpScene->mNumAnimations)
			{
				importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
				importer.ApplyPostProcessing(aiProcess_GenBoundingBoxes);
			}
			ModelLoader loader = ModelLoader(assimpScene, path_.string());

			model = Managed::SharedPtr<Model>(loader.LoadModel(isCreateMeshCollider));
			model->path = path_;
			model->m_numTotalMeshes = static_cast<int>(model->m_Meshes.size());

			//std::cout << assimp.GetElapsedTime() << " ms to load model from assimp file: " << path_.string() << std::endl;

			return model;
		}
	}
	catch (const std::exception& e)
	{
		Debug->Log(e.what());
		return nullptr;
	}
}

// ── 소유권을 공유하는 조회 ──
// 참조를 보관하는 쪽(컴포넌트·프록시)은 이쪽을 써야 에셋 언로드에 안전하다.

std::shared_ptr<Mesh> Model::GetMeshShared(std::string_view name)
{
	std::string meshName = name.data();
	for (auto& mesh : m_Meshes)
	{
		if (mesh && mesh->GetName() == meshName)
		{
			return mesh;
		}
	}
	return nullptr;
}

std::shared_ptr<Mesh> Model::GetMeshShared(int index)
{
	if (index < 0 || index >= m_Meshes.size())
	{
		return nullptr;
	}
	return m_Meshes[index];
}

std::shared_ptr<Material> Model::GetMaterialShared(std::string_view name)
{
	std::string materialName = name.data();
	for (auto& material : m_Materials)
	{
		if (material && material->m_name == materialName)
		{
			return material;
		}
	}
	return nullptr;
}

std::shared_ptr<Material> Model::GetMaterialShared(int index)
{
	if (index < 0 || index >= m_Materials.size())
	{
		return nullptr;
	}
	return m_Materials[index];
}

std::shared_ptr<Texture> Model::GetTextureShared(int index)
{
	if (index < 0 || index >= m_Textures.size())
	{
		return nullptr;
	}
	return m_Textures[index];
}

// ── 원시 포인터 조회 (기존 호출부 호환) ──
// Model이 살아 있는 동안에만 유효하다.

Mesh* Model::GetMesh(std::string_view name)
{
	return GetMeshShared(name).get();
}

Mesh* Model::GetMesh(int index)
{
	return GetMeshShared(index).get();
}

Material* Model::GetMaterial(std::string_view name)
{
	return GetMaterialShared(name).get();
}

Material* Model::GetMaterial(int index)
{
	return GetMaterialShared(index).get();
}

Texture* Model::GetTexture(std::string_view name)
{
	std::string textureName = name.data();
	for (auto& texture : m_Textures)
	{
		if (texture && texture->m_name == textureName)
		{
			return texture.get();
		}
	}
	return nullptr;
}

Texture* Model::GetTexture(int index)
{
	return GetTextureShared(index).get();
}

