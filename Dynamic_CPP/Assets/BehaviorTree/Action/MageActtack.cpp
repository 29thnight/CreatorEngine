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
		return NodeStatus::Failure; // Identity가 없거나 "Mage"가 아닐 경우 실패 반환
	}

	float coolTime = blackBoard.GetValueAsFloat("eAtkCooldown");

	//공격 엑션 
	auto target = blackBoard.GetValueAsGameObject("Target");
	if (!target)
	{
		return NodeStatus::Failure; // 타겟이 없으면 실패 반환
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
		//어텍 중일때 
		// 에니메이션 재생 타이밍(혹은 대기 시간)에 맞춰서 Projectile을 생성하고 발사하는 로직을 구현해야 합니다.
		// Projectile 생성 및 발사 
		// todo:: 
		// autp Projectile = Instantiate(ProjectilePrefab, selfTransform->GetWorldPosition(), Quaternion::Identity);
		// projectile->GetComponent<RigidBodyComponent>()->SetLinearVelocity(projectileVelocity);
		// 발사된 Projectile은 타겟 방향으로 이동하게 됩니다.
		// 플레이어와의 충돌처리는 projectile 프리펩에서 스크립트로 처리
		// 
		// 
		//발사이후 
		LOG("Mage Attack: Firing projectile");
		blackBoard.SetValueAsFloat("AtkCooldown", coolTime); // 공격 쿨타임 설정
		//쿨타임 설정 되어 이곳으로 안들어옴 
		
		//에니메이션 종료 여부에 따라 
	}
	else {
		State = "Attack"; // 상태를 어택으로 설정
		blackBoard.SetValueAsString("State", State); // 상태를 블랙보드에 저장
		//여기서 공격 모션 시작
		
		return NodeStatus::Running; // 공격 모션이 시작되었음을 나타내기 위해 Running 상태 반환

	}


	return NodeStatus::Success;
}
