#include "Model.h"
#include "ShaderSystem.h"
#include "ModelLoader.h"
#include "Benchmark.hpp"
#include "PathFinder.h"
#include "ResourceAllocator.h"

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

        if (0 == assimpScene->mNumAnimations)
        {
            importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
        }

		ModelLoader loader = ModelLoader(assimpScene, path_.string());
        
		model = loader.LoadModel();
		model->path = path_;
		model->GenerateFileID();
	}

	return model;
}

void Model::GenerateFileID()
{
	m_fileID = path.string();
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

	return model;
}
