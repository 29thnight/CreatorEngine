#pragma once
#include "Camera.h"
#include "../ScriptBinder/GameObject.h"
#include "AnimationJob.h"
#include "RenderCommand.h"
#include "RenderPassData.h"

class GameObject;
class Scene;
class LightController;
class HierarchyWindow;
class InspectorWindow;
class MeshRenderer;
class RenderScene
{
public:
	using ProxyContainer = std::vector<MeshRendererProxy*>;
	using ProxyMap		 = std::unordered_map<size_t, std::shared_ptr<MeshRendererProxy>>;
	using AnimatorMap	 = std::unordered_map<size_t, Animator*>;
	using RenderDataMap  = std::unordered_map<size_t, std::shared_ptr<RenderPassData>>;
public:
	RenderScene() = default;
	~RenderScene();

	LightController* m_LightController{};
	static ShadowMapRenderDesc g_shadowMapDesc;

	void Initialize();
	void SetScene(Scene* scene) { m_currentScene = scene; }

	void SetBuffers(ID3D11Buffer* modelBuffer);

	void Update(float deltaSecond);
	void ShadowStage(Camera& camera);
	void CreateShadowCommandList(ID3D11DeviceContext* deferredContext, Camera& camera);
	void UseModel();
	void UseModel(ID3D11DeviceContext* deferredContext);
	void UpdateModel(const Mathf::xMatrix& model);
	void UpdateModel(const Mathf::xMatrix& model, ID3D11DeviceContext* deferredContext);

	RenderPassData* AddRenderPassData(size_t cameraIndex);
	RenderPassData* GetRenderPassData(size_t cameraIndex);
	void RemoveRenderPassData(size_t cameraIndex);

	void RegisterAnimator(Animator* animatorPtr);
	void UnregisterAnimator(Animator* animatorPtr);

	void RegisterCommand(MeshRenderer* meshRendererPtr);
	void UpdateCommand(MeshRenderer* meshRendererPtr);
	void UnregisterCommand(MeshRenderer* meshRendererPtr);

	void PushShadowRenderQueue(MeshRendererProxy* proxy);
	ProxyContainer GetShadowRenderQueue();
	void ClearShadowRenderQueue();


	MeshRendererProxy* FindProxy(size_t guid);
	Scene* GetScene() { return m_currentScene; }

	void OnProxyDistroy();

	static std::queue<HashedGuid> RegisteredDistroyProxyGUIDs;

private:
	friend class HierarchyWindow;
	friend class InspectorWindow;
	friend class SceneViewWindow;

	Scene*			m_currentScene{};
	AnimationJob	m_animationJob{};
	ProxyMap		m_proxyMap;
	AnimatorMap		m_animatorMap;
	RenderDataMap   m_renderDataMap;
	ProxyContainer  m_shadowRenderQueue;
	ID3D11Buffer*	m_ModelBuffer{};
	std::mutex      m_shadowRenderMutex;
	std::atomic_flag m_proxyMapFlag{};
	bool			m_isPlaying = false;
};
