#include "TestEnemy.h"
#include "pch.h"

#include "RigidBodyComponent.h"
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

