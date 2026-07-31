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

	// 프레임 밀봉된 카메라 스냅샷 (PHASE 3-2).
	//
	// 게임 스레드가 EndOfFrame에서 한 번 채우고, 렌더 스레드는 이것만 읽는다.
	// 예전에는 패스들이 camera.CalculateView()나 camera.m_eyePosition을 그 자리에서
	// 다시 읽었다. 게임 스레드가 같은 카메라를 움직이는 중이면 값이 찢어지고,
	// 한 프레임 안에서도 패스마다 다른 카메라를 보게 된다. 밀봉해 두면 그런 부류가
	// 성립하지 않고, 락스텝(배리어 2-랑데뷰)을 풀 수 있는 전제가 된다.
	//
	// 역행렬까지 미리 담는 이유는 호출부가 XMMatrixInverse를 다시 부르지 않게 하기
	// 위해서다 — 같은 값을 여러 패스가 매 프레임 다시 구하고 있었다.
	Mathf::xMatrix				m_frameCalculatedView{};
	Mathf::xMatrix				m_frameCalculatedProjection{};
	Mathf::xMatrix				m_frameCalculatedInverseView{};
	Mathf::xMatrix				m_frameCalculatedInverseProjection{};

	Mathf::xVector				m_frameEyePosition{};
	Mathf::xVector				m_frameForward{};
	Mathf::xVector				m_frameRight{};
	Mathf::xVector				m_frameUp{};

	float						m_frameFov{};
	float						m_frameNearPlane{};
	float						m_frameFarPlane{};
	bool						m_frameIsOrthographic{ false };

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

	// 프레임 렌더 입력을 밀봉한다. 게임 스레드에서만 부른다(EndOfFrame).
	//
	// 여기서 담지 않은 카메라 값을 렌더 스레드가 읽고 있다면 그건 아직 이식되지
	// 않은 경로다 — 새로 필요해지면 여기에 추가하고 호출부를 스냅샷으로 돌린다.
	void UpdateData(Camera* pCamera) {
		m_frameCalculatedView = pCamera->CalculateView();
		m_frameCalculatedProjection = pCamera->CalculateProjection();
		m_frameCalculatedInverseView = XMMatrixInverse(nullptr, m_frameCalculatedView);
		m_frameCalculatedInverseProjection = XMMatrixInverse(nullptr, m_frameCalculatedProjection);

		m_frameEyePosition = pCamera->m_eyePosition;
		m_frameForward = pCamera->m_forward;
		m_frameRight = pCamera->m_right;
		m_frameUp = pCamera->m_up;

		m_frameFov = pCamera->m_fov;
		m_frameNearPlane = pCamera->m_nearPlane;
		m_frameFarPlane = pCamera->m_farPlane;
		m_frameIsOrthographic = pCamera->m_isOrthographic;
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