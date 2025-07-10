#include "AsisFeed.h"
#include "pch.h"
#include "BoxColliderComponent.h"
#include "RigidBodyComponent.h"
void AsisFeed::Start()
{
	m_rigid = GetOwner()->GetComponent<RigidBodyComponent>();
	m_rigid->SetLockAngularX(true);
	m_rigid->SetLockAngularY(true);
	m_rigid->SetLockAngularZ(true);
	m_rigid->SetLockLinearX(true);
	m_rigid->SetLockLinearY(true);
	m_rigid->SetLockLinearZ(true);
	m_rigid->SetLinearDamping(10.0f);
	auto m_box = GetOwner()->GetComponent<BoxColliderComponent>();

	m_box->SetRestitution(0.01f);
	m_box->SetDynamicFriction(100.0f);
	
	std::cout << "asis start" << std::endl;
}

void AsisFeed::Update(float tick)
{
}

