#pragma once
#include "Core.Minimal.h"
#include "TypeTrait.h"
#include <type_traits>
#ifndef DYNAMICCPP_EXPORTS

// 보유층(RenderScene)에 등록되는 모든 프록시의 공통 조각.
//
// 게임 스레드가 값 스냅샷으로 만들고 create delta에 실으면, 렌더 스레드가
// 프레임 경계에서 자기 저장소에 등록해 읽는다. 파괴도 GUID delta이며
// 프록시 자신은 RenderScene이나 회수 큐를 알지 않는다.
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

public:
	HashedGuid						m_instancedID{};
	Mathf::Vector3					m_worldPosition{ 0.0f, 0.0f, 0.0f };
	Mathf::xMatrix					m_worldMatrix{ XMMatrixIdentity() };
};
#endif // !DYNAMICCPP_EXPORTS
