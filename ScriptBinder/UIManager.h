#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "GameObject.h"

class Canvas;

//�ʿ��Ѱ�
class UIManager : public Singleton<UIManager>
{

public:
	friend class Singleton;
	Core::Delegate<void, Mathf::Vector2> m_clickEvent;
	
private:
	std::vector<Canvas*> Canvases;
};

static auto& UIManagers = UIManager::GetInstance();

interface ICollision2D
{
	ICollision2D()
	{	
		m_clickEventHandle = UIManagers->m_clickEvent.AddLambda(
			[this](Mathf::Vector2 _mousePos)
			{
				CheckClick(_mousePos);
			});
	}
	virtual ~ICollision2D()
	{
		UIManagers->m_clickEvent.Remove(m_clickEventHandle);
	}

	virtual void CheckClick(Mathf::Vector2 _mousePos) = 0;
	Core::DelegateHandle m_clickEventHandle{};
};
//btn clilc -> collider�� Ȯ�� -> ����´�  ui�Լ� ����
//���� ���� canvas -> ���� ���ð�����  ui�� �ϳ� ���Բ�
//canvas���� ������?