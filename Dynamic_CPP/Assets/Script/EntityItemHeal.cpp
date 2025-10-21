#include "EntityItemHeal.h"
#include "pch.h"
#include "RigidBodyComponent.h"
void EntityItemHeal::Start()
{
	
}

void EntityItemHeal::OnTriggerEnter(const Collision& collision)
{
	//�÷��̾ ������ ��ȸ��  �갡�ϰų� or �÷��̾ �ϰų�
	if (collision.otherObj->m_tag == "Ground")
	{
		//GetOwner()->GetComponent<RigidBodyComponent>()->SetIsTrigger(false);
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		rigid->SetLinearVelocity(Mathf::Vector3::Zero);
		rigid->SetAngularVelocity(Mathf::Vector3::Zero);
		rigid->UseGravity(false);
	}

}

void EntityItemHeal::Update(float tick)
{
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	if (abs(pos.y) <= 0.05f)
	{
		auto rigid = GetOwner()->GetComponent<RigidBodyComponent>();
		rigid->SetLinearVelocity(Mathf::Vector3::Zero);
		rigid->SetAngularVelocity(Mathf::Vector3::Zero);
		rigid->UseGravity(false);
	}
}

bool EntityItemHeal::CanHeal()
{
	return canHeal;
}

void EntityItemHeal::Use()
{
	canHeal = false;
	GetOwner()->Destroy();
}

int EntityItemHeal::GetHealAmount()
{
	return healAmount;
}

