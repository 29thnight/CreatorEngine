#pragma once
#include "Core.Minimal.h"
#include "Camera.h"
#include "Light.h"
#include "SceneObject.h"
#include "AnimationJob.h"

class Scene
{
public:
	Scene();
	~Scene();
	Camera m_MainCamera;
	LightController m_LightController;
	std::vector<std::shared_ptr<SceneObject>> m_SceneObjects;

	std::shared_ptr<SceneObject> AddSceneObject(const std::shared_ptr<SceneObject>& sceneObject);
	std::shared_ptr<SceneObject> CreateSceneObject(const std::string_view& name, SceneObject::Index parentIndex = 0);
	std::shared_ptr<SceneObject> GetSceneObject(SceneObject::Index index);

	void SetBuffers(ID3D11Buffer* modelBuffer);

	void Update(float deltaSecond);
	void ShadowStage(Camera& camera);
	void UseModel();
	void UpdateModel(const Mathf::xMatrix& model);

	void EditorSceneObjectHierarchy();
	void EditorSceneObjectInspector();

	SceneObject* GetSelectSceneObject() { return m_selectedSceneObject; }
private:
	void UpdateModelRecursive(SceneObject::Index objIndex, Mathf::xMatrix model);
	void DrawSceneObject(const std::shared_ptr<SceneObject>& obj);
	
	AnimationJob m_animationJob{};
	SceneObject* m_selectedSceneObject = nullptr;
	ID3D11Buffer* m_ModelBuffer;
	bool m_isPlaying = false;
};