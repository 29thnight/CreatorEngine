#pragma once
#include "GameObject.h"

//TODO : �׽�Ʈ �ʿ� : �۵��ϴ��� ���ΰ� �ñ���.
template<typename T>
inline T* Component::GetComponent()
{
    if (m_pOwner)
    {
		//���ø� �Լ� ������� ����ؾ��� + GetComponent<T>�� Component�� GetComponent<T>�� ����Ű�� �ʵ��� ������ ����
        return m_pOwner->template GameObject::GetComponent<T>();
    }
    return nullptr;
}