#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "GameObject.h"
#include <stop_token>
#include <DirectXTK/SpriteFont.h>
class Canvas;
class Texture;

class UIManager : public Singleton<UIManager>
{

public:
	friend class Singleton;
	Core::Delegate<void, Mathf::Vector2> m_clickEvent;
	std::shared_ptr<GameObject> MakeCanvas(const std::string_view& name = "Canvas");

	//�������̸� / ���ϰ�� / ���ĵ���� �⺻0
	std::shared_ptr<GameObject> MakeImage(const std::string_view& name, Texture* texture,GameObject* canvas = nullptr,Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeImage(const std::string_view& name, Texture* texture, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeButton(const std::string_view& name, Texture* texture, std::function<void()> clickfun, GameObject* canvas = nullptr,Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeButton(const std::string_view& name, Texture* texture, std::function<void()> clickfun, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeText(const std::string_view& name, SpriteFont* Sfont, GameObject* canvas = nullptr, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeText(const std::string_view& name, SpriteFont* Sfont, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });



	void CheckInput();

	GameObject* FindCanvasName(std::string_view name);
	void Update();
	
	void SortCanvas();
	//ĵ���� ������Ʈ�� ����ִ°͸� ���Բ�
	std::vector<GameObject*> Canvases;
	//���� ĵ����
	//���� ��ȣ�ۿ��� UI
	GameObject* CurCanvas = nullptr;
	GameObject* SelectUI = nullptr;

	bool needSort = false;
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

