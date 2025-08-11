#include "AtteckAction.h"
#include "pch.h"

NodeStatus AtteckAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	GameObject* target = blackBoard.GetValueAsGameObject("Target");
	Transform* targetTransform = target->GetComponent<Transform>();
	Mathf::Vector3 targetPos = targetTransform->GetWorldPosition();
	Mathf::Vector3 dir = targetPos - pos;
	dir.Normalize(); // Normalize direction vector

	CharacterControllerComponent* movement = m_owner->GetComponent<CharacterControllerComponent>();
	if (movement)
	{
		Mathf::Vector2 zeroVector(0.0f, 0.0f);
		//std::cout << "CharacterControllerComponent not found. Stopping movement." << std::endl;
		movement->Move(zeroVector); // Stop movement during attack
	}
	
	bool hasState = blackBoard.HasKey("State");
	bool hasAtkDelay = blackBoard.HasKey("AtkDelay");

	float attTime = 1.0f; // Example attack time, can be adjusted or retrieved from blackboard
	float atkDelay = 3.0f; // Example attack delay, attTime+delay ;
	float atkDelayTime;
	if (hasAtkDelay)
	{
		atkDelayTime = blackBoard.GetValueAsFloat("AtkDelay");
	}
	else
	{
		atkDelayTime = atkDelay; // Default attack delay time if not set in blackboard
	}
	
	static float elapsedTime = 0.0f;
	if (hasState)
	{
		std::string state = blackBoard.GetValueAsString("State");
		if (state == "Atteck")
		{
			atkDelayTime = -deltatime; // Decrease attack delay time
			blackBoard.SetValueAsFloat("AtkDelay", atkDelayTime); // Set attack delay
			if (elapsedTime > attTime)
			{
				elapsedTime = 0.0f; // Reset elapsed time after attack
				//animation->SetParameter("AtteckOn", false); // Reset attack parameter
				//std::cout << "AtteckAction executed!" << m_owner->GetHashedName().ToString() << std::endl;

				blackBoard.SetValueAsInt("AttackCount", 1); // Set Enemy attack count to 1
				std::cout << "Attack completed." << std::endl;
				blackBoard.SetValueAsString("State", "AtteckDelay");
				return NodeStatus::Success; // Attack action completed successfully
			}
			else
			{
				elapsedTime += deltatime; // Increment elapsed time
				//std::cout << "Attacking... Elapsed time: " << elapsedTime << std::endl;
				return NodeStatus::Running; // Continue running while attacking
			}
			//std::cout << "Atteck action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in attack state
		}
		else
		{
			//movement->Move(Mathf::Vector2(0.0f, 0.0f)); // Stop movement during attack
			blackBoard.SetValueAsString("State", "Atteck");
			std::cout << "Switching to Atteck state." << std::endl;
			blackBoard.SetValueAsVector3("AttackDirection", dir); // Set attack delay
			blackBoard.SetValueAsFloat("AtkDelay", atkDelay); // Set attack delay
			elapsedTime = 0.0f; // Reset elapsed time when switching to attack state
			// Here you can add any additional logic needed when switching to the attack state
			// For example, you might want to play an attack animation or sound
			return NodeStatus::Running; // Start the attack action
		}
	}

	// If the "AnimeState" key does not exist, we can set it to "Atteck" and start the attack action
	return NodeStatus::Success;
}

void AtteckAction::PerformSweepAttackTest(BlackBoard& blackBoard)
{
	std::cout << "=========================================" << std::endl;
	std::cout << "### " << m_owner->GetHashedName().ToString() << ": 캡슐 스윕 공격 테스트 시작 ###" << std::endl;

	Transform* selfTransform = m_owner->GetComponent<Transform>();

	// --- 1. 캐릭터와 무기 위치 설정 ---
	// 실제 게임에서는 애니메이션 소켓의 Transform을 사용해야 합니다.
	// 여기서는 캐릭터의 위치와 방향을 기반으로 가상의 공격 궤적을 만듭니다.
	DirectX::SimpleMath::Vector3 attackStartPosition = selfTransform->GetWorldPosition() + selfTransform->GetUp() * 1.5f + selfTransform->GetForward() * 1.0f;
	DirectX::SimpleMath::Vector3 attackEndPosition = selfTransform->GetWorldPosition() + selfTransform->GetUp() * 0.8f + selfTransform->GetForward() * 1.5f;

	// --- 2. 스윕 입력(SweepInput) 데이터 준비 ---
	SweepInput sweepInput;
	sweepInput.startPosition = attackStartPosition;
	sweepInput.startRotation = selfTransform->GetWorldQuaternion();
	sweepInput.direction = attackEndPosition - attackStartPosition;
	sweepInput.distance = sweepInput.direction.Length();
	sweepInput.direction.Normalize();

	// 블랙보드에서 타겟의 레이어를 가져오거나, 적 레이어를 하드코딩합니다.
	// 여기서는 2번 레이어를 'Enemy'라고 가정합니다.
	const int ENEMY_LAYER = 2;
	sweepInput.layerMask = (1 << ENEMY_LAYER);

	// --- 3. 칼의 판정 범위(Geometry) 정의 ---
	const float swordRadius = 0.15f;
	const float swordHalfHeight = 0.7f;

	// --- 4. PhysicX 모듈의 스윕 함수 호출 ---
	std::cout << "스윕 실행 (대상 레이어: " << ENEMY_LAYER << ")" << std::endl;
	SweepOutput sweepResult = PhysicsManagers->CapsuleSweep(sweepInput, swordRadius, swordHalfHeight);

	// --- 5. 결과 분석 및 출력 ---
	if (!sweepResult.touches.empty())
	{
		std::cout << "[성공] " << sweepResult.touches.size() << "개의 충돌을 감지했습니다!" << std::endl;
		for (const auto& hit : sweepResult.touches)
		{
			std::cout << "  - 충돌 객체 ID: " << hit.hitObjectID << std::endl;
			// 여기에 데미지 처리, 피격 이벤트 전송 등의 로직을 추가할 수 있습니다.
		}
	}
	else
	{
		std::cout << "[실패] 아무것도 감지되지 않았습니다." << std::endl;
	}
	std::cout << "=========================================" << std::endl;
}