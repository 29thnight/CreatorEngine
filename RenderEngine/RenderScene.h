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
#include "UIRenderProxy.h"
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
class TerrainComponent;
class ImageComponent;
class TextComponent;
class ProxyCommand;
class RenderScene
{
public:
	using ProxyContainer		= std::vector<PrimitiveRenderProxy*>;
	using ProxyMap				= std::unordered_map<size_t, std::shared_ptr<PrimitiveRenderProxy>>;
	using UIProxyMap			= std::unordered_map<size_t, std::shared_ptr<UIRenderProxy>>;
	using AnimatorMap			= std::unordered_map<size_t, std::shared_ptr<Animator>>;
	using AnimationPalleteMap	= std::unordered_map<size_t, std::pair<bool, DirectX::XMMATRIX*>>;
	using RenderDataMap			= std::vector<std::shared_ptr<RenderPassData>>;
public:
	RenderScene() = default;
	~RenderScene();

	LightController* m_LightController{};
	static ShadowMapRenderDesc g_shadowMapDesc;

	void Initialize();
	void SetScene(Scene* scene) { m_currentScene = scene; }
	void Finalize();

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

	void RegisterAnimator(const std::shared_ptr<Animator>& animatorPtr);
	void UnregisterAnimator(const std::shared_ptr<Animator>& animatorPtr);

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

	void RegisterCommand(ImageComponent* imagePtr);
	bool InvaildCheckImage(ImageComponent* imagePtr);
	void UpdateCommand(ImageComponent* imagePtr);
	ProxyCommand MakeProxyCommand(ImageComponent* imagePtr);
	void UnregisterCommand(ImageComponent* imagePtr);

	void RegisterCommand(TextComponent* textPtr);
	bool InvaildCheckText(TextComponent* textPtr);
	void UpdateCommand(TextComponent* textPtr);
	ProxyCommand MakeProxyCommand(TextComponent* textPtr);
	void UnregisterCommand(TextComponent* textPtr);

	PrimitiveRenderProxy* FindProxy(size_t guid);
	UIRenderProxy* FindUIProxy(size_t guid);
	Scene* GetScene() { return m_currentScene; }

	void OnProxyDestroy();

	AnimatorMap& GetAnimatorMap() { return m_animatorMap; }

	static concurrent_queue<HashedGuid> RegisteredDestroyProxyGUIDs;
	static concurrent_queue<HashedGuid> RegisteredDestroyUIProxyGUIDs;

private:
	friend class HierarchyWindow;
	friend class InspectorWindow;
	friend class SceneViewWindow;
	friend class ProxyCommand;
	friend class AnimationJob;

	Scene*				m_currentScene{};
	AnimationJob		m_animationJob{};
	ProxyMap			m_proxyMap;
	UIProxyMap          m_uiProxyMap;
	AnimatorMap			m_animatorMap;
	AnimationPalleteMap m_palleteMap;
	RenderDataMap		m_renderDataMap{ 10, nullptr };
	ID3D11Buffer*		m_ModelBuffer{};
	std::atomic_flag	m_proxyMapFlag{};
	std::atomic_flag	m_uiProxyMapFlag{};
	bool				m_isPlaying = false;
};
