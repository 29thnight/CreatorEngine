#include "IsMageAttack.h"
#include "pch.h"

bool IsMageAttack::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty()||Identity!="Mage")
	{
		return false; // Identity�� ���ų� "Mage"�� �ƴ� ��� false ��ȯ
	}
	
	float cooldown = blackBoard.GetValueAsFloat("AtkCooldown");
	if (cooldown > 0.0f)
	{
		return false; // ���� ��Ÿ���� ���������� false ��ȯ
	}

	float range = blackBoard.GetValueAsFloat("eTargetRange");

	auto target = blackBoard.GetValueAsGameObject("Target");
	if (!target)
	{
		return false; // Ÿ���� ������ false ��ȯ
	}

	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Transform* targetTransform = target->GetComponent<Transform>();

	Mathf::Vector3 dir = targetTransform->GetWorldPosition() - selfTransform->GetWorldPosition();
	
	if (dir.Length() < range)
	{
		return true; // Ÿ���� ���� ���� ������ true ��ȯ
	}

}
