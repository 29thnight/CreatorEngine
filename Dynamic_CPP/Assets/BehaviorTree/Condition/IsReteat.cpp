#include "IsReteat.h"
#include "pch.h"

bool IsReteat::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "Mage")
	{
		return false; // Identity�� ���ų� "Mage"�� �ƴ� ��� false ��ȯ
	}
	
	
	auto player1 = blackBoard.GetValueAsGameObject("Player1");
	auto player2 = blackBoard.GetValueAsGameObject("Player2");
	auto selfTransform = m_owner->GetComponent<Transform>();
	auto p1Transform = player1->GetComponent<Transform>();
	auto p2Transform = player2->GetComponent<Transform>();
	Mathf::Vector3 p1Dir = p1Transform->GetWorldPosition() - selfTransform->GetWorldPosition();
	Mathf::Vector3 p2Dir = p2Transform->GetWorldPosition() - selfTransform->GetWorldPosition();
	auto invokeRange = blackBoard.GetValueAsFloat("eDistanceRange");
	if (p1Dir.Length() < invokeRange || p2Dir.Length() < invokeRange)
	{
		return true; // �÷��̾�1 �Ǵ� �÷��̾�2�� ���� ���� ������ true ��ȯ
	}

	return false; // ���� ���̸� false ��ȯ
}
