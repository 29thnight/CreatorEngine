#include "Weapon.h"
#include "pch.h"
void Weapon::Start()
{
}


void Weapon::Update(float tick)
{
}

void Weapon::SetEnabled(bool able)
{
	GetOwner()->SetEnabled(able);
	for (auto child : GetOwner()->m_childrenIndices)
	{
		auto childobj = GameObject::FindIndex(child);
		childobj->SetEnabled(able);
	}
}


void Weapon::OnTriggerEnter(const Collision& collision)
{
}
