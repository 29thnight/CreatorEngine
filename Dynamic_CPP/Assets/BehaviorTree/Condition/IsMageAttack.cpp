#include "IsMageAttack.h"
#include "pch.h"

bool IsMageAttack::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty()||Identity!="Mage")
	{
		return false; // Identity가 없거나 "Mage"가 아닐 경우 false 반환
	}
	
	float cooldown = blackBoard.GetValueAsFloat("AtkCooldown");
	if (cooldown > 0.0f)
	{
		return false; // 공격 쿨타임이 남아있으면 false 반환
	}

	float range = blackBoard.GetValueAsFloat("eTargetRange");

	auto target = blackBoard.GetValueAsGameObject("Target");
	if (!target)
	{
		return false; // 타겟이 없으면 false 반환
	}

	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Transform* targetTransform = target->GetComponent<Transform>();

	Mathf::Vector3 dir = targetTransform->GetWorldPosition() - selfTransform->GetWorldPosition();
	
	if (dir.Length() < range)
	{
		return true; // 타겟이 범위 내에 있으면 true 반환
	}

}
