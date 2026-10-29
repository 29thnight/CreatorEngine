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

// S1-b ?먮떒: ???섎뱶肄붾뵫??T=Transform ?뱀닔 遺꾧린???좎??쒕떎.
//
// Entity::GetComponent<Transform>() ?뱀닔??Entity.inl)媛 ?댁젣 罹먯떆
// ?ъ씤??m_pTransformComponent)瑜??뚮젮二쇰뒗 O(1) 寃쎈줈媛 ?먯쑝誘濡? ?ш린??
// m_pOwner->Entity::GetComponent<T>()濡??꾩엫?대룄 寃곌낵??媛숇떎 ???섏?留?
// this->m_pTransform 吏곸젒 ?묎렐?????④퀎 ??吏㏐퀬(媛꾩젒 ?몄텧 ?놁쓬), ??罹먯떆媛
// 媛숈? 媛믪쓣 媛뽯뒗?ㅻ뒗 遺덈??앹? ?대? Component::SetOwner ??怨?紐⑤뱺 而댄룷?뚰듃媛
// ?뚯쑀?먮? ?삳뒗 ?좎씪???듬줈)?먯꽌 ?몄슫?????뱀닔?붾? ?놁븿 ?댁쑀媛 ?녿떎.
//
// ?섏〈??二쇱쓽(?듯빀 ???꾩슂??諛곗꽑): this->m_pTransform? Component::SetOwner
// (Component.cpp ?????щ씪?댁뒪 ?뚯씪 諛?媛 梨꾩슫?? 洹??⑥닔??吏湲??ъ쟾??
// `owner->m_transform`??李몄“?섎뒗?? Entity::m_transform 媛?硫ㅻ쾭媛
// ?щ씪議뚯쑝誘濡?S1-b) 洹?以꾩씠 而댄뙆?쇰릺吏 ?딅뒗?? 怨좎퀜?????泥??쒗쁽?
// `owner ? owner->GetComponent<Transform>() : nullptr`(?먮뒗 owner->Transform_())??
// ??理쒖쥌 蹂닿퀬 李멸퀬.
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
