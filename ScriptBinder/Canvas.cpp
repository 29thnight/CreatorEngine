#include "Canvas.h"
#include "GameObject.h"
#include "UIComponent.h"
Canvas::Canvas()
{
	m_orderID = Component::Order2Uint(ComponentOrder::MeshRenderer);
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<Canvas>();
}

void Canvas::AddUIObject(GameObject* obj)
{
	UIObjs.push_back(obj);
}

void Canvas::Update(float tick)
{
	
}
