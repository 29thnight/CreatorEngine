#pragma once
#include "Camera.h"
#include "FrameCameraSnapshot.h"
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
	static constexpr int cascadeCount = 3;
public:
	// ── 남아 있는 이유: EffectSystem이 아직 DX11로 그린다 (PHASE 10) ──
	//
	// 파티클 모듈 셋(Billboard·Mesh·Trail GPU)이 이 둘을 OMSetRenderTargets에
	// 그대로 건다. 그 층이 DX12로 넘어가면 함께 정리된다.
	//
	// 그림자 맵·SSR 히스토리·뷰/투영 상수 버퍼도 여기 있었으나 읽는 곳이
	// 하나도 없어 T3에서 걷어냈다.
	ProxyContainer				m_deferredQueue;
	ProxyContainer				m_forwardQueue;
	ProxyContainer				m_terrainQueue;
	ProxyContainer				m_foliageQueue;
	UIProxyContainer			m_UIRenderQueue;
	ProxyContainer			    m_decalQueue;
	ProxyContainer              m_spriteRenderQueue;
	Camera						m_shadowCamera;
	//flags
	std::atomic_bool			m_isInitalized{ false };
	std::atomic_bool			m_isDestroy{ false };
	std::atomic<uint32>			m_index{ 0 };

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

	RenderPassData();
	~RenderPassData();

	void Initalize(uint32 index);

	void PushRenderQueue(PrimitiveRenderProxy* proxy);
	void SortRenderQueue();
	void ClearRenderQueue();

	void PushUIRenderQueue(UIRenderProxy* proxy);
	void SortUIRenderQueue();
	void ClearUIRenderQueue();

	// 컬링·그림자 ID 버퍼(PushCullData·PushShadowRenderData 계열)와
	// 그림자 큐를 걷었다 — 읽는 곳이 없었다(RenderSceneViewPlan ③).
	// UI instanceID 버퍼(PushUIRenderData·m_findUIProxyVec)도 같은 이유로 걷었다
	// (2026-08-20): 렌더 소비자가 없었고(라이브 UI는 RenderScene::UIProxySnapshot),
	// 회전(AddFrame)·클리어 호출자도 0이라 밀어 넣은 ID가 무한 축적될 뿐이었다.

	// 프레임 렌더 입력을 밀봉해 게시한다. 프레임 경계에서만 부른다.
	//
	// 렌더가 읽고 있지 않은 뒷면을 채운 뒤 인덱스를 뒤집는다. 뒤집기가 release라
	// 그 앞의 쓰기가 전부 읽는 쪽에 보인다.
	//
	// ── 왜 Camera*가 아니라 완성된 스냅샷을 받는가 ──
	//
	// 예전 서명은 UpdateData(Camera*)였고 이 안에서 CalculateView·
	// CalculateProjection을 직접 불렀다. 그것을 부르던 것이 DX11 렌더 루프였는데,
	// 그 루프가 은퇴하면서(ccca6964) 호출자가 0이 됐다 — 생산자만 사라지고
	// 소비자는 남았다. 그 뒤로 이 이중 버퍼는 한 번도 채워지지 않았고,
	// GetFrameSnapshot은 영행렬과 near=far=0을 계속 돌려주고 있었다.
	//
	// 지금 게시하는 것은 DX12 라이브 렌더러의 밀봉 지점이고, 거기에는 이미
	// 같은 값이 만들어져 있다. 완성된 스냅샷을 받으면 세 가지가 해결된다:
	//
	//   · 같은 계산을 두 번 하지 않는다
	//   · 살아 있는 Camera를 한 번 더 읽지 않는다 — 그 재읽기를 없애는 것이
	//     애초에 PHASE 3-2의 목적이었다
	//   · 라이브가 쓰는 값과 여기 실리는 값이 정의상 같다. 따로 계산하면
	//     둘이 어긋날 수 있고, 그 어긋남은 '가끔 기즈모만 한 프레임 늦다'
	//     같은 모습으로만 드러난다
	void PublishFrameSnapshot(const FrameCameraSnapshot& snapshot)
	{
		const uint32 backIndex = 1u - m_publishedSnapshot.load(std::memory_order_relaxed);
		m_snapshotBuffers[backIndex] = snapshot;
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

	// 스크린 좌표를 월드 좌표로 되돌린다. 프레임 밀봉된 역행렬만 쓴다.
	//
	// Camera::ConvertScreenToWorld는 살아 있는 카메라에서 역행렬을 그 자리에서
	// 다시 구한다. 렌더 스레드(기즈모)가 그걸 부르면 게임 스레드가 카메라를
	// 움직이는 중일 때 같은 프레임 안에서도 기준이 흔들린다.
	Mathf::Vector4 ConvertScreenToWorld(Mathf::Vector2 screenPosition, float depth) const;

	static bool VaildCheck(Camera* pCamera);
	static RenderPassData* GetData(Camera* pCamera);

	// 활성 렌더 씬. SceneManager 접근을 이 .cpp 한 곳에 가둔다 —
	// RenderEngine 하위 폴더(RHI/DX12 등)에서 SceneManager.h를 직접 include하면
	// 그쪽 전이 의존(AssetBundle.h 등)이 인용 경로 규칙에 걸려 풀리지 않는다.
	static class RenderScene* GetActiveRenderScene();
};
