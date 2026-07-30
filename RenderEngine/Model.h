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
#include "ManagedHeapObject.h"

enum class ModelLoadType
{
    FormAsset,
	Form3DModel,
};

class ModelLoader;
class DataSystem;
class SceneRenderer;
class Model : public Managed::HeapObject,
	private Diagnostics::CountedResource<Diagnostics::EngineResource::Model>
{
public:
	Model();
    ~Model();

    static Model*					 LoadModelToScene(Model* model, Scene& Scene);
    static Model*					 LoadModel(std::string_view filePath);
	static Managed::SharedPtr<Model> LoadModelShared(std::string_view filePath);

    static GameObject*				 LoadModelToSceneObj(Model* model, Scene& Scene);

	// 원시 포인터 반환 (기존 호출부 호환용).
	// 반환값은 Model이 살아 있는 동안에만 유효하다. 오래 보관해야 한다면
	// 아래 *Shared 버전을 써서 수명을 함께 붙들어야 한다.
	Mesh*							 GetMesh(std::string_view name);
	Mesh*							 GetMesh(int index);
	Material*						 GetMaterial(std::string_view name);
	Material*						 GetMaterial(int index);
	Texture*						 GetTexture(std::string_view name);
	Texture*						 GetTexture(int index);

	// 소유권을 공유하는 반환. 컴포넌트·프록시처럼 참조를 보관하는 쪽은 이것을 쓴다.
	// 에셋이 언로드되어도 참조가 남아 있는 동안에는 파괴되지 않는다.
	std::shared_ptr<Mesh>			 GetMeshShared(std::string_view name);
	std::shared_ptr<Mesh>			 GetMeshShared(int index);
	std::shared_ptr<Material>		 GetMaterialShared(std::string_view name);
	std::shared_ptr<Material>		 GetMaterialShared(int index);
	std::shared_ptr<Texture>		 GetTextureShared(int index);

public:
    std::string						 name{};
	FileGuid						 guid{};
    file::path						 path{};
	file::file_time_type			 lastWriteTime{};
	ModelLoadType					 loadType{ ModelLoadType::Form3DModel };
									 
    AnimatorData*					 m_animator{};
    Skeleton*						 m_Skeleton{};
	bool							 m_hasBones{ false };
    int								 m_numTotalMeshes{};
	bool							 m_isMakeMeshCollider{ false };

private:
	friend class SceneRenderer;
    friend class ModelLoader;
	friend class DataSystem;

    std::vector<ModelNode*>			 m_nodes;

	// 소유권 공유 컨테이너.
	//
	//  - Mesh     : Model이 만들고 소유한다. 예전에는 소멸자에서 delete 했으나,
	//               MeshRenderer가 원시 포인터로 참조하고 있어 Model이 먼저 파괴되면
	//               댕글링이 됐다. shared_ptr로 두면 참조가 남아 있는 동안 유지된다.
	//  - Material / Texture : DataSystem이 만든 것을 참조만 하던 자리다. 공동 소유로
	//               승격해, 에셋 언로드가 사용 중인 리소스를 파괴하지 않게 한다.
	//               (12.2 보충 분석에서 지적한 "캐시가 유일 소유자" 문제의 해소)
    std::vector<std::shared_ptr<Mesh>>		m_Meshes;
    std::vector<std::shared_ptr<Material>>	m_Materials;
    std::vector<std::shared_ptr<Texture>>	m_Textures;
};
