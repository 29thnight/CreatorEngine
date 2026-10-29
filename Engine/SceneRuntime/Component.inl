#pragma once
#include "Entity.h"
#include "Component.h"

//TODO : 테스트 필요 : 작동하는지 여부가 궁금함.
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
//		auto component = m_pOwner->Entity::GetComponent<T>();
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

// Transform 조회는 SetOwner가 채운 m_pTransform 캐시를 직접 사용한다.
// 다른 타입은 소유 Entity의 GetComponent<T>()에 위임한다.
// SetOwner의 Transform 캐시 초기화 순서는 Component.cpp를 참고한다.
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
		return m_pOwner->Entity::GetComponent<T>();
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
		return m_pOwner->Entity::GetComponentDynamicCast<T>();
	}
}
