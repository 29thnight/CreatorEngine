#pragma once
#include "Core.Minimal.h"
#include "Mesh.h"
#include "Texture.h"
#include "Skeleton.h"
#include "RenderableComponents.h"
#include "Scene.h"
#include "GameObject.h"
#include "Material.h"
#include "AnimatorData.h"

class ModelLoader;
class DataSystem;
class SceneRenderer;
class Model
{
public:
	Model();
    ~Model();

    static Model* LoadModelToScene(Model* model, Scene& Scene);
    static Model* LoadModel(const std::string_view& filePath);

    static GameObject* LoadModelToSceneObj(Model* model, Scene& Scene);
	Mesh* GetMesh(const std::string_view& name);
	Mesh* GetMesh(int index);
	Material* GetMaterial(const std::string_view& name);
	Material* GetMaterial(int index);
	Texture* GetTexture(const std::string_view& name);
	Texture* GetTexture(int index);

public:
    std::string	name{};
    file::path	path{};

    AnimatorData*   m_animator{};
    Skeleton*       m_Skeleton{};
	bool            m_hasBones{ false };
    int             m_numTotalMeshes{};
	bool            m_isMakeMeshCollider{ false };

private:
	friend class SceneRenderer;
    friend class ModelLoader;
	friend class DataSystem;

    std::vector<ModelNode*> m_nodes;
    std::vector<Mesh*>      m_Meshes;
    std::vector<Material*>  m_Materials;
    std::vector<Texture*>   m_Textures;
};
