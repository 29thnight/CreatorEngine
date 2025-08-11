#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "DLLAcrossSingleton.h"
#include "GameObject.h"
#include <stop_token>
#include <DirectXTK/SpriteFont.h>

class Canvas;
class Texture;
class UIManager : public DLLCore::Singleton<UIManager>
{

public:
	friend class DLLCore::Singleton<UIManager>;
	Core::Delegate<void, Mathf::Vector2> m_clickEvent;
	std::shared_ptr<GameObject> MakeCanvas(std::string_view name = "Canvas");

	//�������̸� /�� ���� / ���ĵ���� �⺻0
	std::shared_ptr<GameObject> MakeImage(std::string_view name, Texture* texture,GameObject* canvas = nullptr,Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeImage(std::string_view name, Texture* texture, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeButton(std::string_view name, Texture* texture, std::function<void()> clickfun, Mathf::Vector2 Pos = { 960,540 },GameObject* canvas = nullptr);
	std::shared_ptr<GameObject> MakeButton(std::string_view name, Texture* texture, std::function<void()> clickfun, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeText(std::string_view name, SpriteFont* Sfont, GameObject* canvas = nullptr, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeText(std::string_view name, SpriteFont* Sfont, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });

	void DeleteCanvas(std::string canvasName);
	void CheckInput();

	GameObject* FindCanvasName(std::string_view name);
	void Update();
	
	void SortCanvas();
	//ĵ���� ������Ʈ�� ����ִ°͸� ���Բ�
	std::vector<std::weak_ptr<GameObject>> Canvases;
	//���� ĵ����
	//���� ��ȣ�ۿ��� UI
	std::weak_ptr<GameObject> CurCanvas;
	GameObject* SelectUI = nullptr;

	bool needSort = false;
};

static auto UIManagers = UIManager::GetInstance();

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

