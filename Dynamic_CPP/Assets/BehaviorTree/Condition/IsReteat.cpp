#include "IsReteat.h"
#include "pch.h"

bool IsReteat::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool hasIdentity = blackBoard.HasKey("Identity");
	if (!hasIdentity) return false; //Identity ������ bt ��ü �ƴ� ��������

	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "MonsterMage")
	{
		return false; // Identity�� ���ų� "MonsterMage"�� �ƴ� ��� false ��ȯ
	}
	
	bool hasTarget = blackBoard.HasKey("ClosedTarget");
	if (!hasTarget) return false; //Ÿ�� ������ ���ư� ���� ����� �÷��̾ ��� ���� ���տ����� �����.
	auto ClosedTarget = blackBoard.GetValueAsGameObject("ClosedTarget");

	auto selfTransform = m_owner->GetComponent<Transform>();
	auto TargetTransform = ClosedTarget->GetComponent<Transform>();

	Mathf::Vector3 Dir = TargetTransform->GetWorldPosition() - selfTransform->GetWorldPosition();
	float len = Dir.Length();
	std::cout << "len : " << len << "and";
	auto invokeRange = blackBoard.GetValueAsFloat("RetreatDistance");
	if (Dir.Length() < invokeRange)
	{
		return true; //���� ����� �÷��̾ �����ϸ� ����
	}
	
	return false; // ���� ���̸� false ��ȯ
}
