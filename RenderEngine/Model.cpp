#include "Model.h"
#include "ShaderSystem.h"
#include "ModelLoader.h"
#include "Benchmark.hpp"
#include "PathFinder.h"
#include "ResourceAllocator.h"
#include "Mesh.h"
#include "Material.h"
#include "Texture.h"

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
		if (path_.extension() == ".asset")
		{
			ModelLoader loader = ModelLoader(nullptr, path_.string());
			model = loader.LoadModel();
		}
		else
		{
			Assimp::Importer importer;
			importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
			importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

			const aiScene* assimpScene = importer.ReadFile(filePath.data(),
				aiProcess_LimitBoneWeights
				| aiProcessPreset_TargetRealtime_Fast
				| aiProcess_ConvertToLeftHanded
				| aiProcess_TransformUVCoords
				| aiProcess_OptimizeMeshes
				| aiProcess_ImproveCacheLocality
			);

			if (nullptr == assimpScene)
			{
				throw std::exception("ModelLoader::Model file not found");
			}

			if (anim::empty == assimpScene->mNumAnimations)
			{
				importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
			}

			ModelLoader loader = ModelLoader(assimpScene, path_.string());

			model = loader.LoadModel();
			model->path = path_;

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
	Benchmark banch;
	loader.GenerateSceneObjectHierarchy(model->m_nodes[0], true, 0);
	if (model->m_hasBones)
	{
		loader.GenerateSkeletonToSceneObjectHierarchy(model->m_nodes[0], model->m_Skeleton->m_rootBone, true, 0);
	}

	Meta::MakeCustomChangeCommand([=] 
	{
	
	}, 
	[=] 
	{
	
	});


	return model;
}
