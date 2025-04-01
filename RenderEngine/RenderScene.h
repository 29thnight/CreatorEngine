#pragma once
#include "Camera.h"
#include "Texture.h"
#include "../ScriptBinder/GameObject.h"
#include "AnimationJob.h"

class GameObject;
class Scene;
class LightController;
class RenderScene
{
public:
	RenderScene();
	~RenderScene();

	Camera m_MainCamera;
	LightController* m_LightController;

	void Initialize();
	void SetScene(Scene* scene) { m_currentScene = scene; }

	void SetBuffers(ID3D11Buffer* modelBuffer);

	void Update(float deltaSecond);
	void ShadowStage(Camera& camera);
	void UseModel();
	void UseModel(ID3D11DeviceContext* deferredContext);
	void UpdateModel(const Mathf::xMatrix& model);
	void UpdateModel(const Mathf::xMatrix& model, ID3D11DeviceContext* deferredContext);

	void EditorSceneObjectHierarchy();
	void EditorSceneObjectInspector();

	Scene* GetScene() { return m_currentScene; }
	GameObject* GetSelectSceneObject() { return m_selectedSceneObject; }

private:
	void UpdateModelRecursive(GameObject::Index objIndex, Mathf::xMatrix model);
	void DrawSceneObject(const std::shared_ptr<GameObject>& obj);
	
	Scene* m_currentScene{};
	AnimationJob m_animationJob{};
	GameObject* m_selectedSceneObject = nullptr;
	ID3D11Buffer* m_ModelBuffer;
	bool m_isPlaying = false;

	std::thread animationJobThread{};
};