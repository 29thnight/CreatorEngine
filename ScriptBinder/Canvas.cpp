#include "Canvas.h"
#include "GameObject.h"
#include "ImageComponent.h"
#include "UIManager.h"
#include "TextComponent.h"
#include "UIButton.h"
Canvas::Canvas()
{
	m_orderID = Component::Order2Uint(ComponentOrder::MeshRenderer);
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<Canvas>();
}

void Canvas::AddUIObject(GameObject* obj)
{
	
	auto image = obj->GetComponent<ImageComponent>();
	if(image)
		image->SetCanvas(this);
	auto text = obj->GetComponent<TextComponent>();
	if (text)
		text->SetCanvas(this);
	auto btn = obj->GetComponent<UIButton>();
	if (btn)
		btn->SetCanvas(this);
	UIObjs.push_back(obj);
}


void Canvas::Update(float tick)
{
	if (PreCanvasOrder != CanvasOrder)
	{
		UIManagers->needSort = true;
		PreCanvasOrder = CanvasOrder;
	}
}
