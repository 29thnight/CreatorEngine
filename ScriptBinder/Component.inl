#pragma once
#include "GameObject.h"
#include "Component.h"

//TODO : Å×½ºÆ® ÇÊ¿ä : ÀÛµ¿ÇÏ´ÂÁö ¿©ºÎ°¡ ±Ã±İÇÔ.
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

// S1-b ?ë‹¨: ???˜ë“œì½”ë”©??T=Transform ?¹ìˆ˜ ë¶„ê¸°??? ì??œë‹¤.
//
// Entity::GetComponent<Transform>() ?¹ìˆ˜??Entity.inl)ê°€ ?´ì œ ìºì‹œ
// ?¬ì¸??m_pTransformComponent)ë¥??Œë ¤ì£¼ëŠ” O(1) ê²½ë¡œê°€ ?ìœ¼ë¯€ë¡? ?¬ê¸°??
// m_pOwner->Entity::GetComponent<T>()ë¡??„ì„?´ë„ ê²°ê³¼??ê°™ë‹¤ ???˜ì?ë§?
// this->m_pTransform ì§ì ‘ ?‘ê·¼?????¨ê³„ ??ì§§ê³ (ê°„ì ‘ ?¸ì¶œ ?†ìŒ), ??ìºì‹œê°€
// ê°™ì? ê°’ì„ ê°–ëŠ”?¤ëŠ” ë¶ˆë??ì? ?´ë? Component::SetOwner ??ê³?ëª¨ë“  ì»´í¬?ŒíŠ¸ê°€
// ?Œìœ ?ë? ?»ëŠ” ? ì¼???µë¡œ)?ì„œ ?¸ìš´?????¹ìˆ˜?”ë? ?†ì•¨ ?´ìœ ê°€ ?†ë‹¤.
//
// ?˜ì¡´??ì£¼ì˜(?µí•© ???„ìš”??ë°°ì„ ): this->m_pTransform?€ Component::SetOwner
// (Component.cpp ?????¬ë¼?´ìŠ¤ ?Œì¼ ë°?ê°€ ì±„ìš´?? ê·??¨ìˆ˜??ì§€ê¸??¬ì „??
// `owner->m_transform`??ì°¸ì¡°?˜ëŠ”?? Entity::m_transform ê°?ë©¤ë²„ê°€
// ?¬ë¼ì¡Œìœ¼ë¯€ë¡?S1-b) ê·?ì¤„ì´ ì»´íŒŒ?¼ë˜ì§€ ?ŠëŠ”?? ê³ ì³?????€ì²??œí˜„?€
// `owner ? owner->GetComponent<Transform>() : nullptr`(?ëŠ” owner->Transform_())??
// ??ìµœì¢… ë³´ê³  ì°¸ê³ .
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
