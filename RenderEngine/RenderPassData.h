#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Camera.h"
#include "FrameCameraSnapshot.h"
#include "Texture.h"
#include "concurrent_vector.h"

using namespace concurrency;
class Camera;
class PrimitiveRenderProxy;
class UIRenderProxy;
class RHICommandContext;

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

	// 프레임 카메라 스냅샷 — 이중 버퍼 + 게시/래치 (PHASE 3-2).
	//
	// 게임 스레드가 EndOfFrame에서 뒷면을 채우고 게시하면, 렌더 스레드는 프레임
	// 시작에 게시된 면을 한 번 래치해 그 프레임 내내 같은 면만 읽는다.
	//
	// 예전에는 스냅샷이 한 벌뿐이라 게임 스레드가 제자리에서 갱신했다. 지금은
	// 배리어가 그 구간을 배타로 만들어 주지만, 배리어를 걷어내면 렌더가 절반만
	// 갱신된 스냅샷을 보게 된다. 이중 버퍼로 만들어 두면 그 부류가 성립하지 않는다.
	//
	// 래치를 프레임당 한 번만 하는 이유: 매 읽기마다 게시 인덱스를 다시 보면
	// 같은 프레임 안에서도 패스마다 다른 스냅샷을 볼 수 있다.
	FrameCameraSnapshot			m_snapshotBuffers[2];

	// 게임 스레드가 쓰고 렌더 스레드가 읽는다. release/acquire로 짝을 맞춘다.
	std::atomic<uint32>			m_publishedSnapshot{ 0 };

	// 렌더 스레드가 프레임 시작에 래치한 값. 즉시 경로도 읽으므로 atomic이되
	// 동기화 의미는 없다(relaxed).
	std::atomic<uint32>			m_latchedSnapshot{ 0 };

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

	// 프레임 렌더 입력을 밀봉해 게시한다. 게임 스레드에서만 부른다(EndOfFrame).
	//
	// 렌더가 읽고 있지 않은 뒷면을 채운 뒤 인덱스를 뒤집는다. 뒤집기가 release라
	// 그 앞의 쓰기가 전부 렌더 쪽에 보인다.
	void UpdateData(Camera* pCamera)
	{
		const uint32 backIndex = 1u - m_publishedSnapshot.load(std::memory_order_relaxed);
		FrameCameraSnapshot& snapshot = m_snapshotBuffers[backIndex];

		snapshot.view = pCamera->CalculateView();
		snapshot.projection = pCamera->CalculateProjection();
		snapshot.inverseView = XMMatrixInverse(nullptr, snapshot.view);
		snapshot.inverseProjection = XMMatrixInverse(nullptr, snapshot.projection);

		snapshot.eyePosition = pCamera->m_eyePosition;
		snapshot.forward = pCamera->m_forward;
		snapshot.right = pCamera->m_right;
		snapshot.up = pCamera->m_up;

		snapshot.fov = pCamera->m_fov;
		snapshot.nearPlane = pCamera->m_nearPlane;
		snapshot.farPlane = pCamera->m_farPlane;
		snapshot.isOrthographic = pCamera->m_isOrthographic;

		m_publishedSnapshot.store(backIndex, std::memory_order_release);
	}

	// 렌더 스레드가 프레임 시작에 한 번 부른다. 이후 이 프레임의 모든 읽기는
	// 같은 면을 본다 — 패스마다 다른 스냅샷을 보는 일이 없다.
	void LatchFrameSnapshot()
	{
		m_latchedSnapshot.store(m_publishedSnapshot.load(std::memory_order_acquire),
			std::memory_order_relaxed);
	}

	// 래치된 스냅샷. 렌더 경로는 카메라가 아니라 이것만 읽는다.
	const FrameCameraSnapshot& GetFrameSnapshot() const
	{
		return m_snapshotBuffers[m_latchedSnapshot.load(std::memory_order_relaxed)];
	}

	void AddFrame()
	{
		m_frame.fetch_add(1, std::memory_order_relaxed);
	}

	// 프레임 카메라 상수 버퍼를 갱신하고 VS 슬롯 1·2에 묶는다 (PHASE 3-2).
	//
	// 버퍼는 RenderPassData가 소유한다 — Initalize에서 카메라별로 만들어 둔 것이다.
	// 예전에는 Camera::DeferredUpdateBuffer가 카메라 소유 버퍼(m_ViewBuffer·
	// m_ProjBuffer)에 썼다. 렌더 스레드가 게임 소유 객체의 GPU 리소스를 갱신하는
	// 구조였고, 한 프레임에 열 몇 개 패스가 같은 버퍼를 각자의 deferred 컨텍스트에서
	// 기록했다. 값이 같아서 드러나지 않았을 뿐 소유자가 없는 상태였다.
	//
	// 값은 프레임 밀봉 스냅샷에서 가져온다. 살아 있는 카메라를 다시 계산하지 않는다.
	void BindFrameCameraBuffers(RHICommandContext& context) const;

	// 스크린 좌표를 월드 좌표로 되돌린다. 프레임 밀봉된 역행렬만 쓴다.
	//
	// Camera::ConvertScreenToWorld는 살아 있는 카메라에서 역행렬을 그 자리에서
	// 다시 구한다. 렌더 스레드(기즈모)가 그걸 부르면 게임 스레드가 카메라를
	// 움직이는 중일 때 같은 프레임 안에서도 기준이 흔들린다.
	Mathf::Vector4 ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth) const;

	void ClearRenderTarget();

	static bool VaildCheck(Camera* pCamera);
	static RenderPassData* GetData(Camera* pCamera);

	// 활성 렌더 씬. SceneManager 접근을 이 .cpp 한 곳에 가둔다 —
	// RenderEngine 하위 폴더(RHI/DX12 등)에서 SceneManager.h를 직접 include하면
	// 그쪽 전이 의존(AssetBundle.h 등)이 인용 경로 규칙에 걸려 풀리지 않는다.
	static class RenderScene* GetActiveRenderScene();
};
#endif // !DYNAMICCPP_EXPORTS