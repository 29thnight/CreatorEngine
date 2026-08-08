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

class UIRenderProxy
{
};

struct ID3D11Buffer;
struct ID3D11DeviceContext;
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
	using AnimatorMap			= std::unordered_map<size_t, std::shared_ptr<Animator>>;
	// 본 팔레트 버퍼는 소유권을 공유한다.
	//
	// 렌더 프록시가 이 버퍼를 캐싱하는데, 예전처럼 원시 포인터를 넘기면
	// UnregisterAnimator가 free한 뒤에도 프록시가 그 주소를 계속 들고 있어
	// 렌더 패스가 해제된 메모리를 읽었다(use-after-free).
	// shared_ptr로 두면 프록시가 참조하는 동안 버퍼가 살아남아 그 경합이 사라진다.
	using AnimationPalette		= std::shared_ptr<DirectX::XMMATRIX[]>;
	using AnimationPalleteMap	= std::unordered_map<size_t, std::pair<bool, AnimationPalette>>;
	using RenderDataMap			= std::vector<std::shared_ptr<RenderPassData>>;
public:
	RenderScene() = default;
	~RenderScene();

	static ShadowMapRenderDesc g_shadowMapDesc;

	void Initialize();
	void SetScene(Scene* scene) { m_currentScene = scene; }
	void Finalize();


	void Update(float deltaSecond);

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

	void RegisterCommand(SpriteRenderer* spriteRendererPtr);
	bool InvaildCheckSpriteRenderer(SpriteRenderer* spriteRendererPtr);
	void UpdateCommand(SpriteRenderer* spriteRendererPtr);
	ProxyCommand MakeProxyCommand(SpriteRenderer* spriteRendererPtr);
	void UnregisterCommand(SpriteRenderer* spriteRendererPtr);

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

    void RegisterCommand(DecalComponent* decalPtr);
    bool InvaildCheckDecal(DecalComponent* decalPtr);
    void UpdateCommand(DecalComponent* decalPtr);
    ProxyCommand MakeProxyCommand(DecalComponent* decalPtr);
    void UnregisterCommand(DecalComponent* decalPtr);

	void RegisterCommand(SpriteSheetComponent* spriteSheetPtr);
	bool InvaildCheckSpriteSheet(SpriteSheetComponent* spriteSheetPtr);
	void UpdateCommand(SpriteSheetComponent* spriteSheetPtr);
	ProxyCommand MakeProxyCommand(SpriteSheetComponent* spriteSheetPtr);
	void UnregisterCommand(SpriteSheetComponent* spriteSheetPtr);

	void RegisterCommand(LightComponent* lightPtr);
	bool InvaildCheckLight(LightComponent* lightPtr);
	void UpdateCommand(LightComponent* lightPtr);
	ProxyCommand MakeProxyCommand(LightComponent* lightPtr);
	void UnregisterCommand(LightComponent* lightPtr);

	PrimitiveRenderProxy* FindProxy(size_t guid);
	UIRenderProxy* FindUIProxy(size_t guid);
	// EnhancedRenderer는 DX11 RenderPassData의 카메라별 큐를 거치지 않는다.
	// 프레임 경계에서 프록시의 shared_ptr을 복사해 수명을 밀봉한 뒤, 필요한
	// 렌더 입력만 자체 드로우 목록으로 옮긴다.
	ProxySnapshot GetPrimitiveProxySnapshot();
	// 광원도 같은 규약이다 — 맵 순회는 락 안에서 shared_ptr 복사까지만 하고,
	// 값 읽기는 락 밖에서 한다.
	LightProxySnapshot GetLightProxySnapshot();
	Scene* GetScene() { return m_currentScene; }

	void OnProxyDestroy();

	AnimatorMap& GetAnimatorMap() { return m_animatorMap; }

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

	static concurrent_queue<HashedGuid> RegisteredDestroyProxyGUIDs;
	static concurrent_queue<HashedGuid> RegisteredDestroyLightProxyGUIDs;
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
	LightProxyMap		m_lightProxyMap;
	UIProxyMap          m_uiProxyMap;
	AnimatorMap			m_animatorMap;
	AnimationPalleteMap m_palleteMap;
	RenderDataMap		m_renderDataMap{ 10, nullptr };
	std::atomic_flag	m_proxyMapFlag{};
	std::atomic_flag	m_lightProxyMapFlag{};
	std::atomic_flag	m_uiProxyMapFlag{};
	bool				m_isPlaying = false;
};
