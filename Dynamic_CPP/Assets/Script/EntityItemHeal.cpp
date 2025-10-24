#include "EntityItemHeal.h"
#include "pch.h"
#include "RigidBodyComponent.h"
#include "EffectComponent.h"
void EntityItemHeal::Start()
{


	auto newEffect = SceneManagers->GetActiveScene()->CreateGameObject("effect", GameObjectType::Empty, GetOwner()->m_index);
	m_effect = newEffect->AddComponent<EffectComponent>();
	m_effect->m_effectTemplateName = "resourceView";
	m_effect->Apply();
	
}

void EntityItemHeal::OnTriggerEnter(const Collision& collision)
{
	//플레이어에 닿으면 피회복  얘가하거나 or 플레이어가 하거나
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

