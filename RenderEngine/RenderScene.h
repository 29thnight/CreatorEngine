#pragma once
#include "Camera.h"
#include "../ScriptBinder/GameObject.h"
#ifndef DYNAMICCPP_EXPORTS
#include "AnimationJob.h"
#else
class AnimationJob
{
};

class RenderPassData
{
};

struct ID3D11Buffer;
struct ID3D11DeviceContext;
#endif // !DYNAMICCPP_EXPORTS

#include "MeshRendererProxy.h"
#include "RenderPassData.h"
#include "ProxyCommandQueue.h"
#include "concurrent_unordered_map.h"

using namespace concurrency;

class GameObject;
class Scene;
class LightController;
class HierarchyWindow;
class InspectorWindow;
class MeshRenderer;
class FoliageComponent;
class ProxyCommand;
class RenderScene
{
public:
	using ProxyContainer		= std::vector<PrimitiveRenderProxy*>;
	using ProxyMap				= std::unordered_map<size_t, std::shared_ptr<PrimitiveRenderProxy>>;
	using AnimatorMap			= std::unordered_map<size_t, Animator*>;
	using AnimationPalleteMap	= std::unordered_map<size_t, std::pair<bool, DirectX::XMMATRIX*>>;
	using RenderDataMap			= concurrent_unordered_map<size_t, std::shared_ptr<RenderPassData>>;
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
	void EraseRenderPassData();

	void RegisterAnimator(Animator* animatorPtr);
	void UnregisterAnimator(Animator* animatorPtr);

    void RegisterCommand(MeshRenderer* meshRendererPtr);
    bool InvaildCheckMeshRenderer(MeshRenderer* meshRendererPtr);
    void UpdateCommand(MeshRenderer* meshRendererPtr);
    ProxyCommand MakeProxyCommand(MeshRenderer* meshRendererPtr);
    void UnregisterCommand(MeshRenderer* meshRendererPtr);

	void RegisterCommand(TerrainComponent* terrainPtr);
	bool InvaildCheckTerrain(TerrainComponent* terrainPtr);
	void UpdateCommand(TerrainComponent* terrainPtr);
	ProxyCommand MakeProxyCommand(TerrainComponent* terrainPtr);
	void UnregisterCommand(TerrainComponent* terrainPtr);

    void RegisterCommand(FoliageComponent* foliagePtr);
    bool InvaildCheckFoliage(FoliageComponent* foliagePtr);
    void UpdateCommand(FoliageComponent* foliagePtr);
    ProxyCommand MakeProxyCommand(FoliageComponent* foliagePtr);
    void UnregisterCommand(FoliageComponent* foliagePtr);

	PrimitiveRenderProxy* FindProxy(size_t guid);
	Scene* GetScene() { return m_currentScene; }

	void OnProxyDestroy();

	static concurrent_queue<HashedGuid> RegisteredDestroyProxyGUIDs;

private:
	friend class HierarchyWindow;
	friend class InspectorWindow;
	friend class SceneViewWindow;
	friend class ProxyCommand;
	friend class AnimationJob;

	Scene*				m_currentScene{};
	AnimationJob		m_animationJob{};
	ProxyMap			m_proxyMap;
	AnimatorMap			m_animatorMap;
	AnimationPalleteMap m_palleteMap;
	RenderDataMap		m_renderDataMap;
	ID3D11Buffer*		m_ModelBuffer{};
	std::atomic_flag	m_proxyMapFlag{};
	bool				m_isPlaying = false;
};
