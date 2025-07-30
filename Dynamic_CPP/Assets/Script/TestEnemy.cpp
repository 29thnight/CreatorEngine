#include "TestEnemy.h"
#include "RigidBodyComponent.h"
#include "pch.h"

void TestEnemy::Start()
{
	Owner = GetOwner();
	auto rigid = Owner->GetComponent<RigidBodyComponent>();
	rigid->LockAngularXYZ();
	rigid->LockLinearXZ();
}

void TestEnemy::Update(float tick)
{
}

