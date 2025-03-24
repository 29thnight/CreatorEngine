#include "Model.h"
#include "AssetSystem.h"
#include "ModelLoader.h"
#include "Banchmark.hpp"
#include "PathFinder.h"

Model::Model()
{
}

Model::Model(std::shared_ptr<SceneObject>& sceneObject) :
	m_SceneObject(sceneObject)
{
}

Model::~Model()
{
	for (auto mesh : m_Meshes)
	{
		delete mesh;
	}

	for (auto material : m_Materials)
	{
		delete material;
	}

	for (auto texture : m_Textures)
	{
		delete texture;
	}

	if (m_Skeleton)
	{
		delete m_Skeleton;
	}
}

Model* Model::LoadModel(const std::string_view& filePath)
{
	file::path path_ = filePath.data();
	Model* model{};
	file::path test = PathFinder::Relative("Models\\").string() + path_.filename().string() + ".model";
	/*if (file::exists(test))
	{
		ModelLoader loader;
		loader.LoadModelBinary(path_.filename().string() + ".model");
		model = loader.m_model;
	}
	else*/
	{
		Assimp::Importer importer;
		importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
		importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

		const aiScene* assimpScene = importer.ReadFile(filePath.data(),
			aiProcess_LimitBoneWeights |
			aiProcessPreset_TargetRealtime_Quality |
			aiProcess_ConvertToLeftHanded
		);

		if (nullptr == assimpScene)
		{
			throw std::exception("ModelLoader::Model file not found");
		}

		ModelLoader loader = ModelLoader(assimpScene, path_.string());
		model = loader.LoadModel();
		model->path = path_;

		loader.SaveModelBinary(path_.filename().string() + ".model");
	}

	return model;
}

Model* Model::LoadModelToScene(Model* model, Scene& Scene)
{
	ModelLoader loader = ModelLoader(model, &Scene);
	file::path path_ = model->path;
	Assimp::Importer importer;
	importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
	importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS, 4);

	const aiScene* assimpScene;
	{
		Banchmark banch;
		assimpScene = importer.ReadFile(path_.string(),
			aiProcess_LimitBoneWeights |
			aiProcessPreset_TargetRealtime_Quality |
			aiProcess_ConvertToLeftHanded
		);

		std::cout << "LoadModelToScene : " << banch.GetElapsedTime() << std::endl;
	}
	loader.GenerateSceneObjectHierarchy(assimpScene->mRootNode, true, 0);

	return model;
}
