#pragma once
#include "GameObject.h"
#include "Component.h"

//TODO : �׽�Ʈ �ʿ� : �۵��ϴ��� ���ΰ� �ñ���.
//template<typename T>
//inline T& Component::GetComponent()
//{
//	if (!m_pOwner) throw std::null_exception("not set owner");
//
//	if constexpr (std::is_same_v<T, Transform>)
//	{
//		auto component = this->m_pTransform;
//		return *component;
//	}
//	else
//	{
//		auto component = m_pOwner->GameObject::GetComponent<T>();
//		if (component)
//		{
//			return *component;
//		}
//		else
//		{
//			throw std::null_exception("Component not found");
//		}
//	}
//}

// S1-b 판단: 이 하드코딩된 T=Transform 특수 분기는 유지한다.
//
// GameObject::GetComponent<Transform>() 특수화(GameObject.inl)가 이제 캐시
// 포인터(m_pTransformComponent)를 돌려주는 O(1) 경로가 됐으므로, 여기서
// m_pOwner->GameObject::GetComponent<T>()로 위임해도 결과는 같다 — 하지만
// this->m_pTransform 직접 접근이 한 단계 더 짧고(간접 호출 없음), 두 캐시가
// 같은 값을 갖는다는 불변식은 이미 Component::SetOwner 한 곳(모든 컴포넌트가
// 소유자를 얻는 유일한 통로)에서 세운다 — 특수화를 없앨 이유가 없다.
//
// 의존성 주의(통합 시 필요한 배선): this->m_pTransform은 Component::SetOwner
// (Component.cpp — 이 슬라이스 파일 밖)가 채운다. 그 함수는 지금 여전히
// `owner->m_transform`을 참조하는데, GameObject::m_transform 값 멤버가
// 사라졌으므로(S1-b) 그 줄이 컴파일되지 않는다. 고쳐야 할 대체 표현은
// `owner ? owner->GetComponent<Transform>() : nullptr`(또는 owner->Transform_())다
// — 최종 보고 참고.
template<typename T>
inline T* Component::GetComponent()
{
	if (!m_pOwner) return nullptr;
	if constexpr (std::is_same_v<T, Transform>)
	{
		return static_cast<T*>(this->m_pTransform);
	}
	else
	{
		return m_pOwner->GameObject::GetComponent<T>();
	}
}

template<typename T>
inline T* Component::GetComponentDynamicCast()
{
	if (!m_pOwner) return nullptr;
	if constexpr (std::is_same_v<T, Transform>)
	{
		return static_cast<T*>(this->m_pTransform);
	}
	else
	{
		return m_pOwner->GameObject::GetComponentDynamicCast<T>();
	}
}
