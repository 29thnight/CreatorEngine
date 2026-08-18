#include "Component.h"
#include "GameObject.h"

void Component::SetOwner(GameObject* owner)
{
	m_pOwner = owner;
	// 널 소유자에 대한 방어. 예전에는 `&m_pOwner->m_transform`을 무조건 썼는데,
	// owner가 널이면 그 자리에서 널 역참조다. 지금 호출부에 SetOwner(nullptr)는
	// 없지만(전수 확인), 소유권 해제를 표현하려는 다음 사람이 자연스럽게 시도할
	// 모양이라 미리 막는다.
	m_pTransform = owner ? &owner->m_transform : nullptr;
}

Component& Component::GetComponent(HashedGuid typeof)
{
	if (!m_pOwner) throw std::null_exception("not set owner");

	// K2: m_componentIds(맵) 소멸 — GameObject::FindComponent(공개 창구, 마스크
	// 선판정 + 선형 탐색)로 수렴.
	if (Component* found = m_pOwner->FindComponent(typeof))
	{
		return *found;
	}

	throw std::null_exception("Component not found");
}