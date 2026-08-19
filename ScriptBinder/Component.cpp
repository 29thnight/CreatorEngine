#include "Component.h"
#include "GameObject.h"

void Component::SetOwner(Entity* owner)
{
	m_pOwner = owner;

	// ★ 캐시(Entity::m_pTransformComponent)를 읽으면 안 된다 — 순서가 어긋난다.
	//
	// S1-b로 값 멤버 m_transform이 사라지면서 이 줄을 `owner->GetComponent<Transform>()`
	// (= 캐시 반환 특수화)이나 `owner->Transform_()`로 바꾸고 싶어진다. 그런데
	// Entity 생성자는 `m_pTransformComponent = AddComponent<Transform>();`이고,
	// **그 AddComponent 안에서 불리는 것이 바로 이 SetOwner다** — 반환값이 대입되기
	// 전이라 그 시점의 캐시는 아직 널이다. 그대로 두면 모든 Transform 자신의
	// m_pTransform이 영구히 널로 남는다(적대적 검토가 잡은 지점).
	//
	// 그래서 캐시가 아니라 m_components를 직접 훑는 조회를 쓴다. 이 시점에 owner의
	// Transform이 실제로 들어 있는지는 경로마다 다르지만(템플릿 경로는 push_back 뒤,
	// Meta::Type 경로는 push_back 앞) — Transform 자신은 아래 Transform::SetOwner의
	// override가 self로 덮으므로 어느 쪽이든 옳게 끝난다. 다른 컴포넌트는 생성자가
	// Transform을 항상 먼저 붙여 두므로 여기서 찾힌다.
	//
	// 널 소유자 방어도 겸한다 — 예전 `&m_pOwner->m_transform`은 owner가 널이면
	// 그 자리에서 널 역참조였다(호출부는 현재 0곳이지만 다음 사람이 시도할 모양).
	m_pTransform = owner ? owner->GetComponentDynamicCast<Transform>() : nullptr;
}

Component& Component::GetComponent(HashedGuid typeof)
{
	if (!m_pOwner) throw std::null_exception("not set owner");

	// K2: m_componentIds(맵) 소멸 — Entity::FindComponent(공개 창구, 마스크
	// 선판정 + 선형 탐색)로 수렴.
	if (Component* found = m_pOwner->FindComponent(typeof))
	{
		return *found;
	}

	throw std::null_exception("Component not found");
}