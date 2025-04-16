#include "Canvas.h"
#include "GameObject.h"
#include "UIComponent.h"
#include "UIManager.h"
Canvas::Canvas()
{
	m_orderID = Component::Order2Uint(ComponentOrder::MeshRenderer);
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<Canvas>();
}

void Canvas::AddUIObject(GameObject* obj)
{

	obj->GetComponent<UIComponent>()->SetCanvas(this);
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
