#include "IsReteat.h"
#include "pch.h"

bool IsReteat::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool hasIdentity = blackBoard.HasKey("Identity");
	if (!hasIdentity) return false; //Identity 없으면 bt 객체 아님 돌려보냄

	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "MonsterMage")
	{
		return false; // Identity가 없거나 "MonsterMage"가 아닐 경우 false 반환
	}
	
	bool hasCooldown = blackBoard.HasKey("ReteatCooldown");
	if (hasCooldown)
	{
		float cooldown = blackBoard.GetValueAsFloat("ReteatCooldown");
		if (cooldown > 0.01f)
		{
			return false; // 후퇴 쿨타임이 남아있으면 false 반환
		}
		//0.01보다 작거나 아예 없다면 후퇴 진행
	}

	bool hasTarget = blackBoard.HasKey("ClosedTarget");
	if (!hasTarget) return false; //타겟 없으면 돌아가 가장 가까운 플레이어만 계산 하자 복합연산은 힘들다.
	auto ClosedTarget = blackBoard.GetValueAsGameObject("ClosedTarget");

	auto selfTransform = m_owner->GetComponent<Transform>();
	auto TargetTransform = ClosedTarget->GetComponent<Transform>();

	Mathf::Vector3 Dir = TargetTransform->GetWorldPosition() - selfTransform->GetWorldPosition();
	float len = Dir.Length();
	std::cout << "len : " << len << "and";
	float invokeRange = blackBoard.GetValueAsFloat("RetreatRange");
	if (Dir.Length() < invokeRange)
	{
		return true; //가장 가까운 플레이어가 근접하면 후퇴
	}
	
	return false; // 범위 밖이면 false 반환
}
