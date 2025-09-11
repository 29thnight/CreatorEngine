#pragma once
#include "../Utility_Framework/Core.Minimal.h"
#include "DLLAcrossSingleton.h"
#include "GameObject.h"
#include <stop_token>
#include <DirectXTK/SpriteFont.h>

class Canvas;
class Texture;
class ImageComponent;
class TextComponent;
class SpriteSheetComponent;
class UIManager : public DLLCore::Singleton<UIManager>
{
public:
	friend class DLLCore::Singleton<UIManager>;
	Core::Delegate<void, Mathf::Vector2> m_clickEvent;
	std::shared_ptr<GameObject> MakeCanvas(std::string_view name = "Canvas");
	void AddCanvas(std::shared_ptr<GameObject> canvas);
	//�������̸� /�� ���� / ���ĵ���� �⺻0
	std::shared_ptr<GameObject> MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture,GameObject* canvas = nullptr,Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeImage(std::string_view name, const std::shared_ptr<Texture>& texture, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, Mathf::Vector2 Pos = { 960,540 },GameObject* canvas = nullptr);
	std::shared_ptr<GameObject> MakeButton(std::string_view name, const std::shared_ptr<Texture>& texture, std::function<void()> clickfun, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeText(std::string_view name, file::path FontName, GameObject* canvas = nullptr, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeText(std::string_view name, file::path FontName, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });

	std::shared_ptr<GameObject> MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, GameObject* canvas = nullptr, Mathf::Vector2 Pos = { 960,540 });
	std::shared_ptr<GameObject> MakeSpriteSheet(std::string_view name, const file::path& spriteSheetPath, std::string_view canvasname, Mathf::Vector2 Pos = { 960,540 });

	void DeleteCanvas(std::string canvasName);
	void CheckInput();

	GameObject* FindCanvasName(std::string_view name);
	GameObject* FindCanvasIndex(int index);
	void Update();
	
	void SortCanvas();
	void RegisterImageComponent(ImageComponent* image);
	void RegisterTextComponent(TextComponent* text);
	void RegisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet);
	void UnregisterImageComponent(ImageComponent* image);
	void UnregisterTextComponent(TextComponent* text);
	void UnregisterSpriteSheetComponent(SpriteSheetComponent* spriteSheet);

public:
	//ĵ���� ������Ʈ�� ����ִ°͸� ���Բ�
	std::vector<std::weak_ptr<GameObject>>	Canvases;
	std::unordered_map<std::string, std::weak_ptr<GameObject>> CanvasMap;
	std::vector<ImageComponent*>			Images;
	std::vector<TextComponent*>				Texts;
	std::vector<SpriteSheetComponent*>      SpriteSheets;
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

