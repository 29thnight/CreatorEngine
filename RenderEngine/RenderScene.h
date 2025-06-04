#pragma once
#include "Camera.h"
#include "Texture.h"
#include "../ScriptBinder/GameObject.h"
#include "AnimationJob.h"
#include "RenderCommand.h"

class GameObject;
class Scene;
class LightController;
class HierarchyWindow;
class InspectorWindow;
class MeshRenderer;
class RenderScene
{
public:
	RenderScene() = default;
	~RenderScene();

	LightController* m_LightController{};

	void Initialize();
	void SetScene(Scene* scene) { m_currentScene = scene; }

	void SetBuffers(ID3D11Buffer* modelBuffer);

	void Update(float deltaSecond);
	void ShadowStage(Camera& camera);
	void UseModel();
	void UseModel(ID3D11DeviceContext* deferredContext);
	void UpdateModel(const Mathf::xMatrix& model);
	void UpdateModel(const Mathf::xMatrix& model, ID3D11DeviceContext* deferredContext);

	void RegisterCommand(MeshRenderer* meshRendererPtr);
	void UnregisterCommand(MeshRenderer* meshRendererPtr);

	Scene* GetScene() { return m_currentScene; }

private:
	friend class HierarchyWindow;
	friend class InspectorWindow;
	friend class SceneViewWindow;

	Scene* m_currentScene{};
	AnimationJob m_animationJob{};
	std::unordered_map<HashedGuid, std::shared_ptr<RenderCommand>> m_proxyMap;
	ID3D11Buffer* m_ModelBuffer;
	bool m_isPlaying = false;
};
