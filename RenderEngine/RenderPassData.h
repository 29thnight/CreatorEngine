#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Camera.h"
#include "Texture.h"
#include "concurrent_vector.h"

using namespace concurrency;
class Camera;
class PrimitiveRenderProxy;
class UIRenderProxy;

class RenderPassData
{
public:
	using ProxyContainer = concurrent_vector<PrimitiveRenderProxy*>;
	using UIProxyContainer = concurrent_vector<UIRenderProxy*>;
	using FrameProxyFindInstanceIDs = concurrent_vector<HashedGuid>;
	using FrameUIProxyIDs = concurrent_vector<HashedGuid>;
	static constexpr int STORE_FRAME_COUNT = 3;
	static constexpr int cascadeCount = 3;
public:
	Managed::UniquePtr<Texture> m_renderTarget;
	Managed::UniquePtr<Texture> m_depthStencil;
	Managed::UniquePtr<Texture> m_shadowMapTexture;
	Managed::UniquePtr<Texture> m_SSRPrevTexture;
	ID3D11DepthStencilView*		m_shadowMapDSVarr[cascadeCount]{};
	ID3D11ShaderResourceView*	sliceSRV[cascadeCount]{};
	FrameProxyFindInstanceIDs	m_findProxyVec[STORE_FRAME_COUNT];
	FrameProxyFindInstanceIDs	m_findShadowProxyVec[STORE_FRAME_COUNT];
	FrameUIProxyIDs				m_findUIProxyVec[STORE_FRAME_COUNT];
	ProxyContainer				m_deferredQueue;
	ProxyContainer				m_forwardQueue;
	ProxyContainer				m_terrainQueue;
	ProxyContainer				m_foliageQueue;
	ProxyContainer				m_shadowRenderQueue;
	UIProxyContainer			m_UIRenderQueue;
	ProxyContainer			    m_decalQueue;
	ProxyContainer              m_spriteRenderQueue;
	Camera						m_shadowCamera;
	//flags
	std::atomic_bool			m_isInitalized{ false };
	std::atomic_bool			m_isDestroy{ false };
	std::atomic<uint32>			m_index{ 0 };
	std::atomic<uint32>			m_frame{};

	// 프레임 밀봉된 카메라 행렬. 게임 스레드가 EndOfFrame에서 한 번 채우고,
	// 렌더 스레드는 이것만 읽는다(살아 있는 Camera를 다시 계산하지 않는다).
	Mathf::xMatrix				m_frameCalculatedView{};
	Mathf::xMatrix				m_frameCalculatedProjection{};

	// 그림자 캐스케이드 계산용 프레임 스크래치 (PHASE 3-2).
	//
	// 예전에는 이 둘이 Camera — 즉 게임 스레드가 소유한 객체 — 에 있었고,
	// 렌더 스레드가 매 프레임 clear() + push_back()으로 갈아엎었다.
	// 공유 객체의 std::vector를 렌더 스레드가 재할당하는 구조라, 게임 스레드가
	// 같은 카메라를 만지는 순간과 겹치면 그대로 힙 손상이다. 그림자 패스는
	// 두 경로(캐스케이드/일반)에서 같은 벡터를 갈아엎기까지 했다.
	//
	// 카메라별 렌더 측 저장소로 옮겨 소유자를 하나로 만든다. 이제 렌더 스레드는
	// 게임 카메라를 읽기만 한다.
	std::vector<float>			m_cascadeEnd;
	std::vector<ShadowInfo>		m_cascadeInfo;

	ComPtr<ID3D11Buffer>		m_ViewBuffer;
	ComPtr<ID3D11Buffer>		m_ProjBuffer;

	RenderPassData();
	~RenderPassData();

	void Initalize(uint32 index);

	void PushRenderQueue(PrimitiveRenderProxy* proxy);
	void SortRenderQueue();
	void ClearRenderQueue();

	void PushShadowRenderQueue(PrimitiveRenderProxy* proxy);
	void SortShadowRenderQueue();
	void ClearShadowRenderQueue();

	void PushUIRenderQueue(UIRenderProxy* proxy);
	void SortUIRenderQueue();
	void ClearUIRenderQueue();

	void PushCullData(const HashedGuid& instanceID);
	FrameProxyFindInstanceIDs& GetCullDataBuffer();
	void ClearCullDataBuffer();

	void PushShadowRenderData(const HashedGuid& instanceID);
	FrameProxyFindInstanceIDs& GetShadowRenderDataBuffer();
	void ClearShadowRenderDataBuffer();

	void PushUIRenderData(const HashedGuid& instanceID);
	FrameUIProxyIDs& GetUIRenderDataBuffer();
	void ClearUIRenderDataBuffer();

	void UpdateData(Camera* pCamera) {
		m_frameCalculatedView = pCamera->CalculateView();
		m_frameCalculatedProjection = pCamera->CalculateProjection();
	}

	void AddFrame()
	{
		m_frame.fetch_add(1, std::memory_order_relaxed);
	}

	void ClearRenderTarget();

	static bool VaildCheck(Camera* pCamera);
	static RenderPassData* GetData(Camera* pCamera);
};
#endif // !DYNAMICCPP_EXPORTS