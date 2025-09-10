#include "Canvas.h"
#include "GameObject.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "TextComponent.h"
#include "UIButton.h"
#include "SceneManager.h"
#include "RectTransformComponent.h"

Canvas::Canvas()
{
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<Canvas>();
}

void Canvas::OnDestroy()
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene != nullptr && m_pOwner->IsDestroyMark() && !UIManagers->CurCanvas.expired())
	{
		if (UIManagers->CurCanvas.lock() == m_pOwner->shared_from_this())
		{
			UIManagers->CurCanvas.reset();
		}
		UIManagers->DeleteCanvas(m_pOwner->ToString());
	}
}

void Canvas::AddUIObject(std::shared_ptr<GameObject> obj)
{
	auto image = obj->GetComponent<ImageComponent>();
	if (image)
	{
		image->SetCanvas(this);
		UIManagers->RegisterImageComponent(image);
		image->m_ownerCanvasName = m_pOwner->m_name.ToString();
	}
	auto text = obj->GetComponent<TextComponent>();
	if (text)
	{
		text->SetCanvas(this);
		UIManagers->RegisterTextComponent(text);
		text->m_ownerCanvasName = m_pOwner->m_name.ToString();
	}
	auto btn = obj->GetComponent<UIButton>();
	if (btn)
	{
		btn->SetCanvas(this);
		btn->m_ownerCanvasName = m_pOwner->m_name.ToString();
	}
	UIObjs.push_back(obj);
	//맨처음 추가되는 UI를 선택된 UI로 지정
	if (SelectUI.expired())
	{
		SelectUI = obj;
	}
}


void Canvas::Update(float tick)
{
	if (PreCanvasOrder != CanvasOrder)
	{
		UIManagers->needSort = true;
		PreCanvasOrder = CanvasOrder;
	}

	////이 부분 UI Manager 에서 통합으로 처리하자
	std::erase_if(UIObjs, [](const std::weak_ptr<GameObject>& obj) 
		{ return obj.expired() || (obj.lock() && obj.lock()->IsDestroyMark()); });
}
