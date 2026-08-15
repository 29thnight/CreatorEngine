#pragma once
#include "Camera.h"
#ifndef DYNAMICCPP_EXPORTS
#include "AnimationJob.h"
#else
class AnimationJob
{
};

class RenderPassData
{
};

class UIRenderProxy
{
};

// ID3D11Buffer·ID3D11DeviceContext 전방 선언이 여기 있었다 (E, 2026-08-09).
// m_ModelBuffer와 UseModel/UpdateModel 짝이 D4에서 사라진 뒤로 이 헤더에
// 그 두 타입을 쓰는 선언이 하나도 없다.
#endif // !DYNAMICCPP_EXPORTS

#include "PrimitiveRenderProxy.h"
#include "LightRenderProxy.h"
#include "UIRenderProxy.h"
#include "RenderPassData.h"
#include "ProxyCommandQueue.h"
#include "concurrent_unordered_map.h"

using namespace concurrency;

class GameObject;
class Scene;
class LightComponent;
class HierarchyWindow;
class InspectorWindow;
class MeshRenderer;
class FoliageComponent;
class TerrainComponent;
class SpriteRenderer;
class ImageComponent;
class TextComponent;
class ProxyCommand;
class SpriteSheetComponent;
class RenderScene
{
public:
	using ProxyContainer		= std::vector<PrimitiveRenderProxy*>;
	using ProxyMap				= std::unordered_map<size_t, std::shared_ptr<PrimitiveRenderProxy>>;
	using ProxySnapshot			= std::vector<std::shared_ptr<PrimitiveRenderProxy>>;
	// 광원은 프리미티브와 저장소를 나눈다. 소비자가 겹치지 않고(패스는 둘 중
	// 하나만 본다), 나눠 두면 순회에서 타입 태그로 거를 일이 없다.
	using LightProxyMap			= std::unordered_map<size_t, std::shared_ptr<LightRenderProxy>>;
	using LightProxySnapshot	= std::vector<std::shared_ptr<LightRenderProxy>>;
	using UIProxyMap			= std::unordered_map<size_t, std::shared_ptr<UIRenderProxy>>;
	using RenderDataMap			= std::vector<std::shared_ptr<RenderPassData>>;
public:
	RenderScene() = default;
	~RenderScene();

	static ShadowMapRenderDesc g_shadowMapDesc;

	void Initialize();
	void SetScene(Scene* scene, uint64_t sceneEpoch)
	{
		m_currentScene.store(scene, std::memory_order_release);
		m_sceneEpoch.store(sceneEpoch, std::memory_order_release);
	}
	uint64_t GetSceneEpoch() const
	{
		return m_sceneEpoch.load(std::memory_order_acquire);
	}
	// 렌더 소비 스레드 전용. frame packet의 epoch로 저장소를 전환한다.
	bool BeginProxyFrame(uint64_t sceneEpoch);
	void Finalize();



	RenderPassData* AddRenderPassData(size_t cameraIndex);
	RenderPassData* GetRenderPassData(size_t cameraIndex);
	void RemoveRenderPassData(size_t cameraIndex);
	void EraseRenderPassData();

	void RegisterAnimator(const std::shared_ptr<Animator>& animatorPtr);
	void UnregisterAnimator(const std::shared_ptr<Animator>& animatorPtr);

    void RegisterCommand(MeshRenderer* meshRendererPtr);
    void UpdateCommand(MeshRenderer* meshRendererPtr);
    void UnregisterCommand(MeshRenderer* meshRendererPtr);

	void RegisterCommand(TerrainComponent* terrainPtr);
	void UpdateCommand(TerrainComponent* terrainPtr);
	void UnregisterCommand(TerrainComponent* terrainPtr);

    void RegisterCommand(FoliageComponent* foliagePtr);
    void UpdateCommand(FoliageComponent* foliagePtr);
    void UnregisterCommand(FoliageComponent* foliagePtr);

	void RegisterCommand(SpriteRenderer* spriteRendererPtr);
	void UpdateCommand(SpriteRenderer* spriteRendererPtr);
	void UnregisterCommand(SpriteRenderer* spriteRendererPtr);

	void RegisterCommand(ImageComponent* imagePtr);
	void UpdateCommand(ImageComponent* imagePtr);
	void UnregisterCommand(ImageComponent* imagePtr);

	void RegisterCommand(TextComponent* textPtr);
	void UpdateCommand(TextComponent* textPtr);
	void UnregisterCommand(TextComponent* textPtr);

    void RegisterCommand(DecalComponent* decalPtr);
    void UpdateCommand(DecalComponent* decalPtr);
    void UnregisterCommand(DecalComponent* decalPtr);

	void RegisterCommand(SpriteSheetComponent* spriteSheetPtr);
	void UpdateCommand(SpriteSheetComponent* spriteSheetPtr);
	void UnregisterCommand(SpriteSheetComponent* spriteSheetPtr);

	void RegisterCommand(LightComponent* lightPtr);
	void UpdateCommand(LightComponent* lightPtr);
	void UnregisterCommand(LightComponent* lightPtr);

	// EnhancedRenderer는 DX11 RenderPassData의 카메라별 큐를 거치지 않는다.
	// 프레임 경계에서 프록시의 shared_ptr을 복사해 수명을 밀봉한 뒤, 필요한
	// 렌더 입력만 자체 드로우 목록으로 옮긴다.
	ProxySnapshot GetPrimitiveProxySnapshot();
	// 광원도 같은 규약이다 — 맵 순회는 락 안에서 shared_ptr 복사까지만 하고,
	// 값 읽기는 락 밖에서 한다.
	LightProxySnapshot GetLightProxySnapshot();
	Scene* GetScene() const { return m_currentScene.load(std::memory_order_acquire); }

	// 진단용 컨테이너 크기 스냅샷.
	// 씬을 오갈 때 이 값들이 회수되지 않고 계속 늘어나면 프록시 해제 누락이다.
	struct ResourceCounts
	{
		size_t proxies{ 0 };
		size_t lightProxies{ 0 };
		size_t uiProxies{ 0 };
		size_t animators{ 0 };
		size_t animationPalettes{ 0 };
		size_t renderPassDatas{ 0 };
	};
	ResourceCounts GetResourceCounts();

private:
	friend class HierarchyWindow;
	friend class InspectorWindow;
	friend class SceneViewWindow;
	friend class ProxyCommand;
	friend class AnimationJob;

	std::atomic<Scene*>	m_currentScene{};
	std::atomic_ullong	m_sceneEpoch{ 1 };
	uint64_t			m_consumedSceneEpoch{ 0 }; // 렌더 소비 스레드 전용
	AnimationJob		m_animationJob{};
	ProxyMap			m_proxyMap;
	LightProxyMap		m_lightProxyMap;
	UIProxyMap          m_uiProxyMap;
	RenderDataMap		m_renderDataMap{ 10, nullptr };
	std::atomic_flag	m_proxyMapFlag{};
	std::atomic_flag	m_lightProxyMapFlag{};
	std::atomic_flag	m_uiProxyMapFlag{};
	bool				m_isPlaying = false;
};
