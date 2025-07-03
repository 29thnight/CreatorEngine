#include "Model.h"
#include "ShaderSystem.h"
#include "ModelLoader.h"
#include "Benchmark.hpp"
#include "PathFinder.h"
#include "ResourceAllocator.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"
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
	for (auto& mesh : m_Meshes)
	{
		//delete mesh;
        DeallocateResource(mesh);
	}

	if (m_Skeleton)
	{
        DeallocateResource(m_Skeleton);
	}
}

Model* Model::LoadModel(const std::string_view& filePath)
{
	file::path path_ = filePath.data();
	Model* model{};
	try
	{
		file::path assetPath = filePath.data();
		assetPath = assetPath.replace_extension(".asset");
		if (file::exists(assetPath))
		{
			Benchmark asset;
			ModelLoader loader = ModelLoader(nullptr, assetPath.string());
			model = loader.LoadModel();
			model->path = path_;

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


Mesh* Model::GetMesh(const std::string_view& name)
{
	std::string meshName = name.data();
	for (auto& mesh : m_Meshes)
	{
		if (mesh->GetName() == meshName)
		{
			return mesh;
		}
	}
}

Mesh* Model::GetMesh(int index)
{
	if (index < 0 || index >= m_Meshes.size())
	{
		return nullptr;
	}
	return m_Meshes[index];
}

Material* Model::GetMaterial(const std::string_view& name)
{
	std::string materialName = name.data();
	for (auto& material : m_Materials)
	{
		if (material->m_name == materialName)
		{
			return material;
		}
	}
}

Material* Model::GetMaterial(int index)
{
	if (index < 0 || index >= m_Materials.size())
	{
		return nullptr;
	}
	return m_Materials[index];
}

Texture* Model::GetTexture(const std::string_view& name)
{
	std::string textureName = name.data();
	for (auto& texture : m_Textures)
	{
		if (texture->m_name == textureName)
		{
			return texture;
		}
	}
}

Texture* Model::GetTexture(int index)
{
	if (index < 0 || index >= m_Textures.size())
	{
		return nullptr;
	}
	return m_Textures[index];
}

Model* Model::LoadModelToScene(Model* model, Scene& Scene)
{
    if (nullptr == model)
    {
        return nullptr;
    }

    ModelLoader loader = ModelLoader(model, &Scene);
    file::path path_ = model->path;

	loader.GenerateSceneObjectHierarchy(model->m_nodes[0], true, 0);
	if (model->m_hasBones)
	{
		loader.GenerateSkeletonToSceneObjectHierarchy(model->m_nodes[0], model->m_Skeleton->m_rootBone, true, 0);
	}

	SceneManagers->m_threadPool->NotifyAllAndWait();

	return model;
}


GameObject* Model::LoadModelToSceneObj(Model* model, Scene& Scene)
{
	if (nullptr == model)
	{
		return nullptr;
	}

    ModelLoader loader = ModelLoader(model, &Scene);
    file::path path_ = model->path;

	auto rootObj = loader.GenerateSceneObjectHierarchyObj(model->m_nodes[0], true, 0);
	if (model->m_hasBones)
	{
		rootObj = loader.GenerateSkeletonToSceneObjectHierarchyObj(model->m_nodes[0], model->m_Skeleton->m_rootBone, true, 0);
	}

	SceneManagers->m_threadPool->NotifyAllAndWait();

	return rootObj;
}