#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "GameObject.h"
#include <stop_token>
class Canvas;
class Texture;
//�ʿ��Ѱ�
class UIManager : public Singleton<UIManager>
{

public:
	friend class Singleton;
	Core::Delegate<void, Mathf::Vector2> m_clickEvent;
	std::shared_ptr<GameObject> MakeCanvas(const std::string_view& name = "Canvas");

	//�������̸� / ���ϰ�� / ���ĵ���� �⺻0
	std::shared_ptr<GameObject> MakeImage(const std::string_view& name, Texture* texture, Mathf::Vector2 Pos = { 960,540 }, GameObject * canvas = nullptr);
	std::shared_ptr<GameObject> MakeButton(const std::string_view& name, Texture* texture, std::function<void()> clickfun, Mathf::Vector2 Pos = { 960,540 }, GameObject* canvas = nullptr);

	void CheckInput();
	//ĵ���� ������Ʈ�� ����ִ°͸� ���Բ�
	std::vector<GameObject*> Canvases;
	//���� ��ȣ�ۿ��� UI
	GameObject* CurCanvas = nullptr;
	GameObject* SelectUI = nullptr;
private:
	
	
	
};

static auto& UIManagers = UIManager::GetInstance();
//
//interface ICollision2D
//{
//	ICollision2D()
//	{	
//		m_clickEventHandle = UIManagers->m_clickEvent.AddLambda(
//			[this](Mathf::Vector2 _mousePos)
//			{
//				CheckClick(_mousePos);
//			});
//	}
//	virtual ~ICollision2D()
//	{
//		UIManagers->m_clickEvent.Remove(m_clickEventHandle);
//	}
//
//	virtual void CheckClick(Mathf::Vector2 _mousePos) = 0;
//	Core::DelegateHandle m_clickEventHandle{};
//};
//btn clilc -> collider�� Ȯ�� -> ����´�  ui�Լ� ����
//���� ���� canvas -> ���� ���ð�����  ui�� �ϳ� ���Բ�
//canvas���� ������?

