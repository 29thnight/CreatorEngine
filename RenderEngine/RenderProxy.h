#pragma once
#include "Core.Minimal.h"
#include <type_traits>
#ifndef DYNAMICCPP_EXPORTS

// 보유층(RenderScene)에 등록되는 모든 프록시의 공통 조각.
//
// 게임 스레드가 만들어 등록하고, 렌더 스레드가 프레임 경계에서 스냅샷으로
// 읽는다. 파생 타입은 자기가 어느 저장소에 사는지 알고 있고(프리미티브 맵 ·
// 라이트 맵), DestroyProxy가 그쪽 회수 큐로 자신을 올린다.
//
// ★ 여기 있는 것은 "무엇이든 프록시라면 갖는 것"뿐이다 — 신원과 월드 변환.
//   그리는 것에만 있는 재질·메시나 라이트에만 있는 감쇠는 파생이 든다.
//   기준은 하나다: 라이트와 메시가 함께 답할 수 있는 질문인가.
class RenderProxy
{
public:
	RenderProxy() = default;
	virtual ~RenderProxy() = default;

	// 복사·이동은 컴파일러에게 맡긴다. 손으로 나열하던 시절 여섯 필드가
	// 빠져 있었고, 그 부류는 필드를 더할 때마다 다시 생긴다.
	RenderProxy(const RenderProxy&) = default;
	RenderProxy(RenderProxy&&) = default;
	RenderProxy& operator=(const RenderProxy&) = default;
	RenderProxy& operator=(RenderProxy&&) = default;

	// 자기 저장소의 회수 큐에 자신을 올린다. 여기서 맵을 직접 지우면
	// 렌더 스레드가 순회 중인 컨테이너를 건드린다 — 실제 제거는 프레임
	// 경계의 RenderScene::OnProxyDestroy가 한다.
	virtual void DestroyProxy() = 0;

	bool IsExpired() const { return m_isExpired; }

public:
	HashedGuid						m_instancedID{};
	Mathf::Vector3					m_worldPosition{ 0.0f, 0.0f, 0.0f };
	Mathf::xMatrix					m_worldMatrix{ XMMatrixIdentity() };

protected:
	bool							m_isExpired{ false };
};
#endif // !DYNAMICCPP_EXPORTS
