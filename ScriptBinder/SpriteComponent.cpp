#include "SpriteComponent.h"

SpriteComponent::SpriteComponent()
{
	m_orderID = Component::Order2Uint(ComponentOrder::MeshRenderer);
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<SpriteComponent>();
}
