#pragma once
#include "GameObject.h"
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
