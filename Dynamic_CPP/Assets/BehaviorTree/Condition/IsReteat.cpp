#include "IsReteat.h"
#include "pch.h"

bool IsReteat::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "Mage")
	{
		return false; // Identity가 없거나 "Mage"가 아닐 경우 false 반환
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
		return true; // 플레이어1 또는 플레이어2가 범위 내에 있으면 true 반환
	}

	return false; // 범위 밖이면 false 반환
}
