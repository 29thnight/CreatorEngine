#include "Model.h"
#include "AssetSystem.h"
#include "ModelLoader.h"

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
    Assimp::Importer importer;
    const aiScene* assimpScene = importer.ReadFile(filePath.data(),
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_ConvertToLeftHanded
    );

    if (nullptr == assimpScene)
    {
        throw std::exception("ModelLoader::Model file not found");
    }

    ModelLoader loader = ModelLoader(assimpScene, path_.string());
    Model* model = loader.LoadModel();
	model->path = path_;

	return model;
}

Model* Model::LoadModelToScene(Model* model, Scene& Scene)
{
	ModelLoader loader = ModelLoader(model, &Scene);
	loader.GenerateSceneObjectHierarchy(model->m_node, true, model->m_SceneObject->m_index);

	return model;
}
