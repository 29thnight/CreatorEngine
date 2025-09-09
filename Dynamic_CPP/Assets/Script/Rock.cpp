#include "Rock.h"
#include "pch.h"
#include "BoxColliderComponent.h"
#include "RigidBodyComponent.h"
void Rock::Start()
{
	auto m_box = GetOwner()->GetComponent<BoxColliderComponent>();

	m_box->SetRestitution(0.0f);
}

void Rock::Update(float tick)
{
}

