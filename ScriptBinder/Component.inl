#pragma once
#include "GameObject.h"

//TODO : 테스트 필요 : 작동하는지 여부가 궁금함.
template<typename T>
inline T* Component::GetComponent()
{
    if (m_pOwner)
    {
		//템플릿 함수 사용임을 명시해야함 + GetComponent<T>가 Component의 GetComponent<T>를 가리키지 않도록 접근자 설정
        return m_pOwner->template GameObject::GetComponent<T>();
    }
    return nullptr;
}