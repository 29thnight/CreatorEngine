#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Skeleton.h"
#include "ObjectRenderers.h"
#include "Scene.h"
#include "SceneObject.h"
#include "Material.h"

class ModelLoader;
class DataSystem;
class Model
{
public:
    std::shared_ptr<SceneObject> m_SceneObject;
	Model();
    ~Model();
    Model(std::shared_ptr<SceneObject>& sceneObject);

    static Model* LoadModelToScene(Model* model, Scene& Scene);
    static Model* LoadModel(const std::string_view& filePath);

public:
    std::string	name{};
    file::path	path{};
    aiNode*     m_node{};

    Skeleton*   m_Skeleton{};
	bool m_hasBones{ false };

private:
    friend class ModelLoader;
	friend class DataSystem;


    std::vector<Mesh*>      m_Meshes;
    std::vector<Material*>  m_Materials;
    std::vector<Texture*>   m_Textures;
};
