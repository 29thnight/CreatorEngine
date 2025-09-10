#include "pch.h"
#include "MageActtack.h"
#include "RigidBodyComponent.h"
#include "DebugLog.h"
NodeStatus MageActtack::Tick(float deltatime, BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");
	auto State = blackBoard.GetValueAsString("State");
	if (Identity.empty() || Identity != "Mage")
	{
		return NodeStatus::Failure; // Identity�� ���ų� "Mage"�� �ƴ� ��� ���� ��ȯ
	}

	float coolTime = blackBoard.GetValueAsFloat("eAtkCooldown");

	//���� ���� 
	auto target = blackBoard.GetValueAsGameObject("Target");
	if (!target)
	{
		return NodeStatus::Failure; // Ÿ���� ������ ���� ��ȯ
	}

	auto RigidBody = m_owner->GetComponent<RigidBodyComponent>();
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Transform* targetTransform = target->GetComponent<Transform>();
	Mathf::Vector3 dir = targetTransform->GetWorldPosition() - selfTransform->GetWorldPosition();

	float projectileSpeed = blackBoard.GetValueAsFloat("eProjectileSpeed");

	dir.Normalize();

	Mathf::Vector3 projectileVelocity = dir * projectileSpeed;

	if (State == "Attack")
	{
		//���� ���϶� 
		// ���ϸ��̼� ��� Ÿ�̹�(Ȥ�� ��� �ð�)�� ���缭 Projectile�� �����ϰ� �߻��ϴ� ������ �����ؾ� �մϴ�.
		// Projectile ���� �� �߻� 
		// todo:: 
		// autp Projectile = Instantiate(ProjectilePrefab, selfTransform->GetWorldPosition(), Quaternion::Identity);
		// projectile->GetComponent<RigidBodyComponent>()->SetLinearVelocity(projectileVelocity);
		// �߻�� Projectile�� Ÿ�� �������� �̵��ϰ� �˴ϴ�.
		// �÷��̾���� �浹ó���� projectile �����鿡�� ��ũ��Ʈ�� ó��
		// 
		// 
		//�߻����� 
		LOG("Mage Attack: Firing projectile");
		blackBoard.SetValueAsFloat("AtkCooldown", coolTime); // ���� ��Ÿ�� ����
		//��Ÿ�� ���� �Ǿ� �̰����� �ȵ��� 
		
		//���ϸ��̼� ���� ���ο� ���� 
	}
	else {
		State = "Attack"; // ���¸� �������� ����
		blackBoard.SetValueAsString("State", State); // ���¸� �����忡 ����
		//���⼭ ���� ��� ����
		
		return NodeStatus::Running; // ���� ����� ���۵Ǿ����� ��Ÿ���� ���� Running ���� ��ȯ

	}


	return NodeStatus::Success;
}
